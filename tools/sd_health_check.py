from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.system_recovery.sd_health import detect_sd_card, ensure_flic_structure, ensure_sd_fat32


REQUIRED_DIRS = [
    "Flic",
    "Flic/boot",
    "Flic/animations/face/default",
    "Flic/sounds",
    "Flic/config",
]


def _print_tree(root: Path) -> None:
    print(f"[sd-health] Tree for {root}")
    if not root.exists():
        print("  (missing)")
        return

    print(f"  {root.name}/")
    entries = sorted(root.rglob("*"), key=lambda p: (len(p.relative_to(root).parts), str(p).lower()))
    for entry in entries:
        rel = entry.relative_to(root)
        indent = "  " + "  " * len(rel.parts)
        suffix = "/" if entry.is_dir() else ""
        print(f"{indent}{entry.name}{suffix}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Flic SD health check")
    parser.add_argument("--sd-root", default=None, help="Optional SD root override like D:/")
    args = parser.parse_args()

    try:
        sd_root = detect_sd_card(args.sd_root)
    except Exception as exc:
        print(f"ERROR: unable to detect SD card: {exc}")
        return 2

    if not sd_root.exists():
        print(f"ERROR: SD root is not present: {sd_root}")
        return 2

    try:
        ensure_sd_fat32(sd_root)
    except Exception:
        print("Reformat this card as FAT32 using SD Card Formatter, then rerun.")
        return 2

    ctx = ensure_flic_structure(sd_root)

    missing = []
    for rel in REQUIRED_DIRS:
        path = sd_root / rel
        if not path.exists() or not path.is_dir():
            missing.append(str(path))

    _print_tree(ctx.flic_root)

    if missing:
        print("ERROR: Required folders are missing after structure check:")
        for path in missing:
            print(f" - {path}")
        return 2

    print("[sd-health] SD card is healthy and folder structure is ready.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
