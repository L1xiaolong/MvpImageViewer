# Brand assets

This directory contains the user-approved logo source and generated runtime icon.

Source asset:

- `logo.svg`

Generated runtime asset expected by the application:

- `app_icon.png`

When `app_icon.png` exists, CMake embeds it into Qt resources and `main.cpp` uses it as the
application/window icon on Windows and other non-macOS platforms. On macOS, the application
keeps the white rounded-tile artwork embedded in `ISPImageViewer.icns` both before and after
launch.

To regenerate app icons from the SVG source:

```bash
python3 tools/generate_app_icons.py assets/brand/logo.svg
```
