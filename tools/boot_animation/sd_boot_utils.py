from __future__ import annotations

import ctypes
import os
import shutil
import string
from pathlib import Path
from typing import Any

_HIDDEN_FILENAMES = {"thumbs.db", "desktop.ini", ".ds_store"}


def _drive_type(root: Path) -> int:
    if os.name != "nt":
        return 0
    return int(ctypes.windll.kernel32.GetDriveTypeW(str(root)))


def _candidate_roots() -> list[Path]:
    roots: list[Path] = []
    for letter in string.ascii_uppercase:
        root = Path(f"{letter}:/")
        if not root.exists():
            continue
        dtype = _drive_type(root)
        if dtype in (2, 3):
            roots.append(root)
    return roots


def _normalize_sd_root(root: Path) -> Path:
    name = root.name.lower()
    parent_name = root.parent.name.lower() if root.parent != root else ""
    if name == "boot" and parent_name == "flic":
        return root.parent.parent
    if name == "flic":
        return root.parent
    return root


def detect_sd_card() -> Path:
    env_root = os.environ.get("FLIC_SD_ROOT") or os.environ.get("FLIC_SD_DRIVE")
    if env_root:
        return _normalize_sd_root(Path(env_root))

    roots = _candidate_roots()

    # Prefer removable media first to avoid accidentally selecting a local drive
    # that happens to contain a C:/Flic development folder.
    ordered = sorted(roots, key=lambda path: 0 if _drive_type(path) == 2 else 1)

    for root in ordered:
        if (root / "Flic" / "boot").exists():
            return root

    for root in ordered:
        if (root / "Flic").exists():
            return root

    removable = [root for root in roots if _drive_type(root) == 2]
    if len(removable) == 1:
        return removable[0]

    if len(roots) == 1:
        return roots[0]

    raise FileNotFoundError("Unable to auto-detect SD card root containing /Flic")


def ensure_boot_folder(sd_root: Path | None = None) -> Path:
    root = detect_sd_card() if sd_root is None else _normalize_sd_root(Path(sd_root))
    # Accept either drive root (D:/) or direct Flic root (D:/Flic).
    flic_root = root / "Flic" if root.name.lower() != "flic" else root
    boot_root = flic_root / "boot"
    boot_root.mkdir(parents=True, exist_ok=True)
    return boot_root


def purge_boot_animation(sd_root: Path | None = None) -> dict[str, Any]:
    boot_root = ensure_boot_folder(sd_root)

    removed_files = 0
    removed_hidden = 0
    removed_dirs = 0

    for child in list(boot_root.iterdir()):
        if child.is_dir():
            shutil.rmtree(child, ignore_errors=True)
            removed_dirs += 1
            continue

        name = child.name.lower()
        child.unlink(missing_ok=True)
        if name.startswith(".") or name in _HIDDEN_FILENAMES:
            removed_hidden += 1
        else:
            removed_files += 1

    if any(boot_root.iterdir()):
        raise RuntimeError(f"Boot folder is not empty after purge: {boot_root}")

    return {
        "boot_root": boot_root,
        "removed_files": removed_files,
        "removed_hidden": removed_hidden,
        "removed_dirs": removed_dirs,
    }
