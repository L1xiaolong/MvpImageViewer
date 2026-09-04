#!/usr/bin/env python3
"""Generate MVP Image Viewer app icon assets from a single logo source.

The script is intentionally conservative: it never invents artwork and only derives platform
assets from the logo file supplied by the user. SVG sources are rasterized with PyQt5 when
available. PNG/JPEG resizing, macOS `.icns`, and Windows `.ico` generation use Pillow.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


MACOS_ICNS_SIZES = [16, 32, 64, 128, 256, 512, 1024]
WINDOWS_ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise RuntimeError(f"Required tool not found: {name}")
    return path


def generate_resized_png(source: Path, size: int, output: Path) -> None:
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("Icon PNG resizing requires Pillow: python3 -m pip install Pillow") from exc

    output.parent.mkdir(parents=True, exist_ok=True)
    with Image.open(source) as image:
        image = image.convert("RGBA")
        image = image.resize((size, size), Image.Resampling.LANCZOS)
        image.save(output, format="PNG")


def render_svg_to_png(source: Path, output: Path, size: int = 1024) -> None:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    try:
        from PyQt5.QtCore import QRectF
        from PyQt5.QtGui import QColor, QGuiApplication, QImage, QPainter
        from PyQt5.QtSvg import QSvgRenderer
    except ImportError as exc:
        raise RuntimeError(
            "SVG icon generation requires PyQt5 in this environment. "
            "Provide a 1024x1024 PNG instead or install PyQt5."
        ) from exc

    app = QGuiApplication.instance() or QGuiApplication([])
    renderer = QSvgRenderer(str(source))
    if not renderer.isValid():
        raise RuntimeError(f"Invalid SVG logo source: {source}")

    image = QImage(size, size, QImage.Format_ARGB32)
    image.fill(QColor(0, 0, 0, 0))
    painter = QPainter(image)
    renderer.render(painter, QRectF(0, 0, size, size))
    painter.end()

    output.parent.mkdir(parents=True, exist_ok=True)
    if not image.save(str(output), "PNG"):
        raise RuntimeError(f"Failed to render SVG logo to PNG: {output}")
    # Keep the QGuiApplication alive until rendering completes; the local binding may be used by Qt.
    _ = app


def prepare_raster_source(source: Path, output_root: Path) -> Path:
    if source.suffix.lower() != ".svg":
        return source

    generated_source = output_root / "brand" / "app_icon.png"
    render_svg_to_png(source, generated_source)
    return generated_source


def generate_macos_icns(source: Path, output_root: Path) -> None:
    try:
        from PIL import Image, ImageDraw
    except ImportError as exc:
        raise RuntimeError("macOS .icns generation requires Pillow: python3 -m pip install Pillow") from exc

    macos_dir = output_root / "icons" / "macos"
    macos_dir.mkdir(parents=True, exist_ok=True)
    with Image.open(source) as image:
        logo = image.convert("RGBA")
        canvas_size = 1024
        canvas = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(canvas)

        # macOS artwork deliberately includes its own rounded white tile and
        # optical padding. Windows continues to use the transparent source.
        tile_bounds = (88, 88, 936, 936)
        draw.rounded_rectangle(
            tile_bounds,
            radius=190,
            fill=(255, 255, 255, 255),
            outline=(229, 233, 241, 255),
            width=8,
        )

        logo.thumbnail((512, 512), Image.Resampling.LANCZOS)
        logo_position = (
            (canvas_size - logo.width) // 2,
            (canvas_size - logo.height) // 2,
        )
        canvas.alpha_composite(logo, logo_position)
        canvas.save(
            macos_dir / "ISPImageViewer.icns",
            format="ICNS",
            sizes=[(size, size) for size in MACOS_ICNS_SIZES],
        )


def generate_runtime_png(source: Path, output_root: Path) -> None:
    brand_dir = output_root / "brand"
    brand_dir.mkdir(parents=True, exist_ok=True)
    if source.resolve() == (brand_dir / "app_icon.png").resolve():
        return
    generate_resized_png(source, 1024, brand_dir / "app_icon.png")


def generate_windows_ico(source: Path, output_root: Path) -> None:
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError(
            "Windows .ico generation requires Pillow: python3 -m pip install Pillow"
        ) from exc

    windows_dir = output_root / "icons" / "windows"
    windows_dir.mkdir(parents=True, exist_ok=True)
    image = Image.open(source).convert("RGBA")
    images = [image.resize((size, size), Image.Resampling.LANCZOS) for size in WINDOWS_ICO_SIZES]
    images[-1].save(
        windows_dir / "ISPImageViewer.ico",
        format="ICO",
        sizes=[(size, size) for size in WINDOWS_ICO_SIZES],
        append_images=images[:-1],
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate MVP Image Viewer app icon assets.")
    parser.add_argument("source", type=Path, help="Logo source image, preferably a square PNG.")
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "assets",
        help="Asset root directory. Defaults to <repo>/assets.",
    )
    parser.add_argument(
        "--require-ico",
        action="store_true",
        help="Fail if Windows .ico generation cannot be completed.",
    )
    args = parser.parse_args()

    source = args.source.expanduser().resolve()
    if not source.exists():
        print(f"Logo source does not exist: {source}", file=sys.stderr)
        return 2

    output_root = args.output_root.expanduser().resolve()
    raster_source = prepare_raster_source(source, output_root)
    generate_runtime_png(raster_source, output_root)
    generate_macos_icns(raster_source, output_root)

    try:
        generate_windows_ico(raster_source, output_root)
    except RuntimeError as exc:
        if args.require_ico:
            raise
        print(f"Warning: {exc}", file=sys.stderr)
        print("Continuing after generating runtime PNG and macOS .icns.", file=sys.stderr)

    print(f"Generated app icon assets under: {output_root}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"Command failed: {' '.join(exc.cmd)}", file=sys.stderr)
        raise SystemExit(exc.returncode)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
