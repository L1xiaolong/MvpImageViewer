# Application UI icons

The 49 SVG filenames in `ui/` and matching `ui-dark/` variants are project-authored
artwork under the project MIT license. The approved design is recorded in
`design/icon-proposal/`. The brand logo is managed separately in `assets/brand/`.

## Geometry and sizes

- Shared `viewBox="0 0 20 20"`; primary stroke width 1.5, caption stroke 1.4.
- Toolbar and folder navigation: 16 px; regular icon buttons: 20 px;
  window caption glyphs: 12 px. Existing QML controls own layout and hit targets.
- Rounded line ends and joins, small corner radii, transparent backgrounds.
- Light and dark variants preserve paths, weights, opacity and bounds exactly.

## Theme colors

| Purpose | Light | Dark |
| --- | --- | --- |
| Foreground | `#303036` | `#DDDDE2` |
| Windows folders | `#A9771E` | `#D7B06B` |
| macOS folders | `#3D7FAD` | `#7AB5DF` |
| Close caption on red hover | `#FFFFFF` | `#FFFFFF` |

SVG paint colors are explicit because QML `Image` does not inherit the surrounding
control's text color. Preserve the colored folders' 0.16 fill opacity. Hover,
selection, focus and disabled appearance are applied by the existing QML controls.

Edit `ui/`, then regenerate the dark set from the project root:

```sh
node tools/generate_dark_icons.mjs
```

Use the existing filenames so CMake resources and QML references remain aligned.
Do not reintroduce an icon library or copy its path data when adding new artwork.

Gallery controls reuse `actual-size.svg` and `fit.svg`; `fullscreen.svg` uses two
diagonally opposed expansion arrows. `smooth-display.svg` depicts a continuous
edge inside an image frame. Both use the same 16 px button rendering as the toolbar.
