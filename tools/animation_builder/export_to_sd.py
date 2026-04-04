from __future__ import annotations

import shutil
from pathlib import Path

from .environment_state import SD_EXPORT_ROOT, sd_export_target

_SD_EXPORT_ANNOUNCED = False


def export_animation_to_sd(animation_name: str, source_folder: Path) -> Path:
    source_folder = Path(source_folder)
    target = sd_export_target(animation_name)

    global _SD_EXPORT_ANNOUNCED
    if target.is_relative_to(SD_EXPORT_ROOT) and not _SD_EXPORT_ANNOUNCED:
        print("SD card detected — enabling D:\\ export.")
        _SD_EXPORT_ANNOUNCED = True

    target.mkdir(parents=True, exist_ok=True)

    for png in sorted(source_folder.glob("*.png")):
        shutil.copy2(png, target / png.name)

    return target
