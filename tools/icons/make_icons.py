#!/usr/bin/env python3
"""Packs the PNGs from vox_render_icon into app/icons/voxwright.ico (Windows)
and app/icons/voxwright.icns (macOS).

    build/dev/tools/vox_render_icon app/icons/app.svg build/icons
    python3 tools/icons/make_icons.py build/icons app/icons

Needs Pillow. The generated files are committed, so building the app does
not need either tool.
"""

import sys
from pathlib import Path

from PIL import Image

ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
ICNS_SIZES = [16, 32, 64, 128, 256, 512, 1024]


def load(folder: Path, size: int) -> Image.Image:
    return Image.open(folder / f"icon_{size}.png").convert("RGBA")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    pngs = Path(sys.argv[1])
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)

    largest = load(pngs, ICO_SIZES[-1])
    largest.save(out / "voxwright.ico", format="ICO", sizes=[(s, s) for s in ICO_SIZES],
                 append_images=[load(pngs, s) for s in ICO_SIZES[:-1]])

    biggest = load(pngs, ICNS_SIZES[-1])
    biggest.save(out / "voxwright.icns", format="ICNS",
                 append_images=[load(pngs, s) for s in ICNS_SIZES[:-1]])
    print(out / "voxwright.ico")
    print(out / "voxwright.icns")
    return 0


if __name__ == "__main__":
    sys.exit(main())
