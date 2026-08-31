#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./scripts/release.sh VERSION

Updates the project version, creates a release commit and matching vVERSION tag
on main, then atomically pushes both to GitHub. The tag triggers the GitHub
Release workflow.

Example:
  ./scripts/release.sh 0.2.3
EOF
}

if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
    usage
    exit 0
fi

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 2
fi

version="$1"
if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "VERSION must use MAJOR.MINOR.PATCH form, for example 0.2.3." >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/.." && pwd)"
cd "$repo_dir"

for required_command in git perl; do
    command -v "$required_command" >/dev/null 2>&1 || {
        echo "Required command is missing: $required_command" >&2
        exit 2
    }
done

if [[ -n "$(git status --porcelain)" ]]; then
    echo "Working tree is not clean. Commit or stash changes before releasing." >&2
    exit 2
fi

branch="$(git branch --show-current)"
if [[ "$branch" != "main" ]]; then
    echo "Releases must be created from main; current branch is $branch." >&2
    exit 2
fi

if ! git remote get-url origin >/dev/null 2>&1; then
    echo "Git remote 'origin' is not configured." >&2
    exit 2
fi

echo "Refreshing origin/main and release tags"
git fetch --prune --tags origin
if [[ "$(git rev-parse HEAD)" != "$(git rev-parse origin/main)" ]]; then
    if git merge-base --is-ancestor HEAD origin/main; then
        git merge --ff-only origin/main
    elif git merge-base --is-ancestor origin/main HEAD; then
        echo "Local main is ahead of origin/main; those commits will be included in the release."
    else
        echo "Local main and origin/main have diverged." >&2
        echo "Reconcile the branches before creating a release." >&2
        exit 2
    fi
fi

tag="v$version"
if git ls-remote --exit-code --tags --refs origin "refs/tags/$tag" >/dev/null 2>&1; then
    echo "Tag $tag already exists on origin." >&2
    exit 2
fi

# If a previous atomic push failed, allow the exact local release commit/tag
# to be pushed again without rewriting history or creating another commit.
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    tag_commit="$(git rev-list -n 1 "$tag")"
    project_version="$(sed -nE \
        's/^project\(ISPImageViewer VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)$/\1/p' \
        CMakeLists.txt)"
    if [[ "$tag_commit" == "$(git rev-parse HEAD)" && \
          "$project_version" == "$version" && \
          "$(git log -1 --pretty=%s)" == "chore: release $tag" ]]; then
        echo "Retrying atomic push for existing local release $tag"
        git push --atomic origin main "$tag"
        echo "Published $tag. Track the package build at GitHub Actions → Release."
        exit 0
    fi
    echo "Local tag $tag already exists but is not a retryable release tag." >&2
    exit 2
fi

current_version="$(sed -nE \
    's/^project\(ISPImageViewer VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)$/\1/p' \
    CMakeLists.txt)"
if [[ -z "$current_version" ]]; then
    echo "Could not read the project version from CMakeLists.txt." >&2
    exit 1
fi
if [[ "$current_version" == "$version" ]]; then
    echo "Project version is already $version; choose a new release version." >&2
    exit 2
fi

ISPVIEW_RELEASE_VERSION="$version" perl -0pi -e \
    's/(project\(ISPImageViewer VERSION )[^ ]+( LANGUAGES CXX\))/$1$ENV{ISPVIEW_RELEASE_VERSION}$2/' \
    CMakeLists.txt

if ! grep -Fq "project(ISPImageViewer VERSION $version LANGUAGES CXX)" CMakeLists.txt; then
    echo "Could not update the project version in CMakeLists.txt." >&2
    exit 1
fi

git diff --check
if [[ "$(git diff --name-only)" != "CMakeLists.txt" ]]; then
    echo "Version update unexpectedly modified files other than CMakeLists.txt." >&2
    git diff --name-only >&2
    exit 1
fi
git add CMakeLists.txt
git commit -m "chore: release $tag"
git tag -a "$tag" -m "Release $tag"

echo "Atomically publishing main and $tag"
if ! git push --atomic origin main "$tag"; then
    echo "Atomic push failed; neither main nor $tag was updated on GitHub." >&2
    echo "Fix the remote issue and rerun this same command to retry." >&2
    exit 1
fi

echo "Published $tag. Track the package build at GitHub Actions → Release."
