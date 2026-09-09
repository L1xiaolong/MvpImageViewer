# Runtime inventory and license collection

Packaging requires Python 3.9+ and runs `scripts/collect_runtime_licenses.py`
after deployment and pruning, before producing an installer. A nonzero exit
blocks packaging; `RUNTIME_DEPENDENCIES.md` and `.json` explain the findings.
The collector does not download files, guess ownership from DLL names, or
grant permission to redistribute vendor components.

## What is collected

- Every deployed DLL/EXE/shared library (including macOS framework binaries),
  QML/JavaScript file, translation, resource archive and font is inventoried.
  Only the project's own application executable is excluded. OS libraries
  that are not copied into the package are outside this inventory.
- Each identified file has a SHA-256, component version and provenance.
- MSYS2 files must match the installed package database **and the installed
  file's bytes**. Package-owned license/copyright notices and available SBOMs
  are copied into `licenses/runtime/`, including notices outside the standard
  `share/licenses` directory (for example ICU's versioned license directory).
- Package-level license expressions are preserved as metadata. They can
  cover tools or documentation absent from the installed application; they
  are not a conclusion that every DLL uses every listed license.
- Unknown files, changed hashes, missing or empty notices, and stale catalog
  entries are blocking findings. A successful collection is labeled
  `collected`, not a full legal-compliance approval.

The JSON lists every copied evidence file and its SHA-256. The two reports
and all evidence are shipped at the Windows installation root or under
`Contents/Resources` on macOS. macOS hashes describe the relocated files
**before signing**, since signing may change binary bytes.

## Inspect an existing MSYS2 Windows package

Run from the repository root with the Python executable on PATH:

```powershell
python scripts/collect_runtime_licenses.py dist/MVPImageViewer-windows-x64 `
  --msys-root 'D:/Program Files/msys64' `
  --output build/runtime-license-audit-windows
```

Use the root of the MSYS2 installation, not its `ucrt64` subdirectory. The
output argument keeps an existing package unchanged. Local Windows packaging
derives this root from `MSYS2_UCRT64`; the GitHub workflow uses its MSYS2 root.

If the installed packages have been upgraded since the application was
packaged, restore the matching packages or rebuild. Do not assign the new
package version to old DLLs just because their filenames match.

## Vendor SDKs, macOS and custom builds

For binaries not owned by MSYS2 (including a Microsoft redistributable, Qt
official SDK, Homebrew or a custom Qt build), supply a reviewed JSON catalog.
This is an evidence input, not an exception that bypasses hash or notice
checks. Run the collector without a catalog first to obtain file paths and
hashes; it will report unresolved files and exit nonzero.

```json
{
  "schema_version": 1,
  "components": [
    {
      "id": "vendor-component",
      "version": "EXACT-BUILD-VERSION",
      "homepage": "https://vendor.example/component",
      "declared_license": "LicenseRef-Vendor-Component",
      "source_reference": "https://vendor.example/exact-sdk-or-source-release",
      "notices": ["notices/vendor/LICENSE.txt"],
      "files": [
        {
          "path": "vendor.dll",
          "sha256": "REPLACE_WITH_64_LOWERCASE_HEX_DIGITS"
        }
      ]
    }
  ]
}
```

This example is intentionally incomplete and cannot pass the collector.
Notice paths are relative to the catalog directory and must stay within it;
deployed file paths are relative to the package root. Every actual runtime
file supplied by that component must have an exact path and hash. Include
upstream copyright/third-party attribution files along with license texts.
Use the actual license identifier and terms for the SDK obtained; a URL alone
is not a license text. MSYS2 matching fills files not supplied by the catalog.

```powershell
$env:ISPVIEW_RUNTIME_CATALOG = 'packaging/runtime/windows.json'
.\build_windows.ps1 -Toolchain msys2 -Mode package
```

```sh
ISPVIEW_RUNTIME_CATALOG=packaging/runtime/macos.json ./build_macos.sh package
```

For macOS use the post-relocation, pre-signing hashes from the first packaging
attempt's diagnostic report. The temporary bundle is removed on failure, but
the reports survive in `build/runtime-license-audit-macos/`. Establish the
component versions and notices from the actual SDK/build records, not merely
the reported filenames. SDK updates require reviewing and refreshing the
catalog. No macOS SDK catalog is fabricated or supplied by this repository.

For GitHub releases, repository variables `ISPVIEW_RUNTIME_CATALOG_MACOS` and
`ISPVIEW_RUNTIME_CATALOG_WINDOWS` can point to reviewed catalogs checked out
with the release commit. Without the necessary catalog, that platform's
packaging job intentionally stops. Diagnostics are retained as separate
workflow artifacts; they are not uploaded as public release installers.

## Remaining source and licensing review

The collector establishes deployed-file provenance and gathers available
notices; it does not establish the complete obligations for each binary.
In particular:

- Inspect Qt's SBOM/source attribution data for code statically included in
  the Qt libraries; copying generic license texts is not a substitute for
  the relevant copyright/attribution notices.
- Pin and retain matching source archives, package recipes and patches for
  libraries that require corresponding source. The generated MSYS2 recipe
  link is a discovery link to a moving branch, not a source offer or an
  immutable source delivery mechanism.
- Custom Qt builds also require the actual source changes and build records,
  including the AGL patch and helper-template changes performed by
  `scripts/build_qt_no_icu_macos.sh`.
- Document the required library replacement/relinking and installation
  information, and review any GPL-only modules or optional Exiv2-enabled build.
- Obtain the redistribution terms and notice requirements of proprietary SDK
  components from their actual distribution. Do not label them open source
  or remove rendering dependencies solely to make this check pass.

Reference: https://www.qt.io/development/open-source-lgpl-obligations

## Tests

```sh
python3 -m unittest discover -s tests -p test_runtime_licenses.py
```

These tests exercise provenance mismatches, unknown nested runtime files,
missing notices, vendor catalogs, stale entries, traversal rejection, and
macOS framework detection without a Qt SDK. The release validation job runs
them before packaging. They do not replace an actual macOS packaging test.
