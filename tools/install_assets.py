from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.system_recovery.sd_health import (
    detect_sd_card,
    ensure_flic_structure,
    ensure_sd_fat32,
    install_reference_assets_to_sd,
)


REQUIRED_FACE_JSONS = [
    "idle_breathing.json",
    "blink.json",
    "emotion_calm.json",
    "emotion_curious.json",
    "emotion_happy.json",
    "emotion_sleepy.json",
    "emotion_surprised.json",
]

ALIAS_PAIRS = {
    "happy_wiggle.json": "emotion_happy.json",
    "sleepy_fade.json": "emotion_sleepy.json",
    "surprise.json": "emotion_surprised.json",
    "thinking_loop.json": "emotion_curious.json",
}


def main() -> int:
    parser = argparse.ArgumentParser(description="Install canonical Flic assets to SD")
    parser.add_argument("--sd-root", default=None, help="Optional SD root override like D:/")
    parser.add_argument("--repo-root", default=None, help="Optional repository root")
    args = parser.parse_args()

    repo_root = Path(args.repo_root) if args.repo_root else Path.cwd()

    try:
        sd_root = detect_sd_card(args.sd_root)
        ensure_sd_fat32(sd_root)
        ctx = ensure_flic_structure(sd_root)
        summary = install_reference_assets_to_sd(ctx, repo_root=repo_root)
    except Exception as exc:
        print(f"ERROR: {type(exc).__name__}: {exc}")
        return 2

    print("[install-assets] Install summary:")
    print(json.dumps(summary, indent=2))
    print("[install-assets] Required face files:")
    for name in REQUIRED_FACE_JSONS:
        print(f" - {name}")
    print("[install-assets] Required alias duplicates:")
    for alias, source in ALIAS_PAIRS.items():
        print(f" - {alias} -> {source}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
