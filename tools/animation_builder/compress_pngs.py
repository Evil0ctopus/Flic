from __future__ import annotations

import shutil
import subprocess
from pathlib import Path
from typing import Iterable

from .config import MAX_PNG_SIZE_BYTES


def _png_files(folder: Path) -> Iterable[Path]:
    return sorted(p for p in folder.glob("*.png") if p.is_file())


def compress_folder_pngs(folder: Path, max_size_bytes: int = MAX_PNG_SIZE_BYTES) -> None:
    folder = Path(folder)
    pngquant = shutil.which("pngquant")

    for png in _png_files(folder):
        if pngquant:
            subprocess.run(
                [pngquant, "--force", "--skip-if-larger", "--ext", ".png", "--quality", "55-95", str(png)],
                check=False,
                capture_output=True,
                text=True,
            )

        if png.stat().st_size > max_size_bytes:
            raise ValueError(f"PNG too large after compression: {png} ({png.stat().st_size} bytes)")
