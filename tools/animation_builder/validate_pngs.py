from __future__ import annotations

import struct
from pathlib import Path
from typing import Iterable

from .config import IMAGE_SIZE, MAX_PNG_SIZE_BYTES


PNG_SIG = b"\x89PNG\r\n\x1a\n"


def _png_files(folder: Path) -> Iterable[Path]:
    return sorted(p for p in folder.glob("*.png") if p.is_file())


def _read_ihdr(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()[:64]
    if len(data) < 33 or not data.startswith(PNG_SIG):
        raise ValueError(f"Invalid PNG signature: {path}")
    if data[12:16] != b"IHDR":
        raise ValueError(f"Missing IHDR: {path}")
    width = struct.unpack(">I", data[16:20])[0]
    height = struct.unpack(">I", data[20:24])[0]
    color_type = data[25]
    return width, height, color_type


def validate_folder_pngs(folder: Path, max_size_bytes: int = MAX_PNG_SIZE_BYTES) -> None:
    folder = Path(folder)
    for png in _png_files(folder):
        if png.stat().st_size > max_size_bytes:
            raise ValueError(f"File exceeds max size: {png}")

        width, height, color_type = _read_ihdr(png)
        if (width, height) != IMAGE_SIZE:
            raise ValueError(f"Invalid dimensions for {png.name}: {(width, height)} != {IMAGE_SIZE}")
        if color_type != 6:
            raise ValueError(f"Invalid color type for {png.name}: {color_type} (need RGBA=6)")
