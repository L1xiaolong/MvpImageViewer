# Qt Design Studio visual workspace

Open `MVPImageViewerDesign.qmlproject`. `DesignPreview.ui.qml` is now only a composition root: it
loads the real production `BrowsePage.qml` with mock data and contains no separately drawn UI.
All editable UI sources live under `../src/qml`. There is no design-to-runtime synchronization
step and no preview-only layout that can drift away from the application.

## What can be edited visually

Select an item in the Navigator or directly on the canvas, then edit it in **Properties**:

- `../src/qml/Mvp/TopToolbar.ui.qml`: visually editable workbench toolbar
- `../src/qml/Mvp/FolderNavigator.qml`: production folder navigator
- `../src/qml/Mvp/ThumbnailTile.qml`: production grid/list tile
- `../src/qml/Pages/BrowsePage.qml`: production page composition, inspector and status rail
- `gridModeButton`, `listModeButton`, `compactModeButton`: display modes
- `thumbnailSlider`: thumbnail size control
- `sortButton`: sort selector
- `folderOperations`: folder menu and new-folder action
- `fileSearch`: file search field
- `compareButton`: compare action
- `folderNavigator`: in-app folder browser
- `thumbnailGrid`: image workspace
- `inspectorRail`: right-side information rail
- `statusBar`: selection and view status

Use **Zoom screen to fit all content** in the 2D toolbar to see the full 1440 × 900 frame.
**Run App** launches `App.qml`, which hosts `DesignPreview.ui.qml`; that preview renders the real
production `BrowsePage.qml` with `MockBrowseController.qml` and exercises production interactions.

## Production mapping

Qt Design Studio cannot reliably expand the production `BrowsePage.qml` in its 2D editor because
that file contains runtime controller calls, menus, shortcuts, and interaction handlers. Shared
visual components are therefore edited directly as `.ui.qml` files, while `BrowsePage.qml` wires
their signals to the runtime controller:

- `../src/qml/Mvp/TopToolbar.ui.qml`: toolbar layout and visual states, shared by design and runtime
- `../src/qml/Pages/BrowsePage.qml`: page layout and toolbar-controller wiring
- `../src/qml/Mvp/Theme.qml`: colors, typography, radii, and sizes
- `../src/qml/Mvp/ThumbnailTile.qml`: image tile behavior
- `../src/qml/Mvp/FolderNavigator.qml`: folder navigation behavior
- `../src/qml/Mvp/AppIconButton.qml`: reusable icon buttons

## Editing contract

1. Never add page visuals to `design/DesignPreview.ui.qml`; it is only a mock-data host.
2. Make every visual change in `src/qml`. Qt Design Studio lists that directory in the same project.
3. For toolbar work, open `TopToolbar.ui.qml` directly in the 2D workspace.
4. Use **Run App** to preview the real production page with mock data.
5. Rebuild the compiled `.app` before checking the packaged application.

Saved changes under `src/qml` are therefore production changes by definition. Do not manually edit
`*.qtds` files; they are Qt Design Studio session metadata.

## Settings preview and UI regression coverage

`SettingsPreview.qml` hosts the production `SettingsCard.qml`; it contains no duplicate page
visuals. `MockSettingsController.qml` and `MockDiagnosticsController.qml` supply deterministic
update and diagnostic states. The automated tests replace settings with the real C++
`AppSettings` backed by a temporary INI store for persistence, translation, shortcut capture,
conflict, reset, and cancellation checks. Destructive actions use the mock controller only.

```sh
cmake -S . -B build/macos-preset-release -DMVPVIEW_BUILD_TESTS=ON
cmake --build build/macos-preset-release --target settings_ui_regression -j 8
ctest --test-dir build/macos-preset-release -R settings_ui_regression --output-on-failure
```

The test exports five light and five dark PNGs, plus shortcut conflict, update download,
and diagnostic restart states, to `build/macos-preset-release/settings-ui/`. Long-page exports
expand the production popup vertically just for the image; layout checks use the normal
scrolling popup at 980 × 640 and 1440 × 900. Offline update and diagnostic fixtures never
contact a release server, export real user data, or delete application data.
