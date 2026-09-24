# Third-party notices

## Lucide and Feather icons

The icons listed below are included in `assets/icons/ui/` and
`assets/icons/ui-dark/` and embedded in the application. Their Lucide names
are recorded from the SVG class attributes in this repository. Local changes
include filenames and dark-theme stroke colors and widths.

- Upstream: https://github.com/lucide-icons/lucide
- License source: https://raw.githubusercontent.com/lucide-icons/lucide/main/LICENSE
- License text retrieved: 2026-09-09. The original imported icon version is not recorded.
- Full copyright and license notices: [licenses/Lucide-LICENSE](licenses/Lucide-LICENSE).

Lucide uses the ISC license; its Feather-derived icons also retain the MIT
notice for Cole Bemis. The complete upstream notice is retained, including
its list of Feather-derived icons. These notices apply to both light and
dark variants and must accompany redistributed copies.

| Local filename (both variants) | Lucide name |
| --- | --- |
| `actual-size.svg` | `scan-eye` |
| `back.svg` | `chevron-left` |
| `close.svg` | `x` |
| `compare.svg` | `columns-2` |
| `cover.svg` | `arrow-left-from-line` |
| `exif.svg` | `spell-check` |
| `fit.svg` | `fullscreen` |
| `folder-open.svg` | `folder-open` |
| `folder-pane-plus.svg` | `folder-input` |
| `folder-plus.svg` | `folder-plus` |
| `folder.svg` | `folder` |
| `forward.svg` | `chevron-right` |
| `gallery.svg` | `layout-panel-left` |
| `grid.svg` | `layout-grid` |
| `histogram.svg` | `chart-column-big` |
| `image-resize.svg` | `image-upscale` |
| `info.svg` | `circle-alert` |
| `list.svg` | `layout-list` |
| `pixel-probe.svg` | `inspection-panel` |
| `rotate-ccw-square.svg` | `rotate-ccw-square` |
| `rotate-cw-square.svg` | `rotate-cw-square` |
| `screenshot.svg` | `crop` |
| `search.svg` | `search` |
| `settings.svg` | `settings-2` |
| `side-by-side.svg` | `square-centerline-dashed-horizontal` |
| `sort.svg` | `arrow-down-wide-narrow` |
| `split-vertical.svg` | `flip-horizontal-2` |

## Other runtime dependencies

Qt, Exiv2, LibRaw, and any enabled LittleCMS or transitive runtime dependencies
retain their respective licenses. The icon notices above are not a complete
runtime license inventory. Packaged builds additionally contain
`RUNTIME_DEPENDENCIES.json`, `RUNTIME_DEPENDENCIES.md`, and the collected
evidence under `licenses/runtime/`. This records the actual deployed files
and available notices, not a complete compatibility or source-delivery approval.
Distributors must supply the notices and source
materials required by the actual libraries included in their packages.

Exiv2 is GPL-2.0-or-later; bundling it requires a compatible distribution
of the combined application, independently of the project's own license.

## Diagnostic archives and crash capture

- miniz 3.0.2 is statically linked for ZIP export. Source: https://github.com/richgel999/miniz/tree/3.0.2 . Full upstream license: `third_party/miniz/LICENSE`, distributed as `licenses/miniz-LICENSE`.
- macOS embeds Crashpad revision `4b8fc2b536e04407032cb6eba687cfe68cb5966a` and its pinned mini_chromium dependency. Source: https://chromium.googlesource.com/crashpad/crashpad/ . These components are built by `scripts/build_crashpad_macos.sh`; their upstream licenses are copied into the application resource licenses directory at packaging time. Crash reporting is local only; network uploads are disabled.
- `ispview_crash_handler.exe` is this project's Windows helper using the Windows DbgHelp API.
