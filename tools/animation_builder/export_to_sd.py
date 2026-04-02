from __future__ import annotations

import shutil
from pathlib import Path

from .config import SD_MOUNT_PATH_OBJ


def export_animation_to_sd(animation_name: str, source_folder: Path, sd_root: Path = SD_MOUNT_PATH_OBJ) -> Path:
    source_folder = Path(source_folder)
    target = Path(sd_root) / animation_name
    target.mkdir(parents=True, exist_ok=True)

    for png in sorted(source_folder.glob("*.png")):
        shutil.copy2(png, target / png.name)

    return target
