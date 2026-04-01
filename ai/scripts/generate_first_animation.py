#!/usr/bin/env python3
"""Generate Flic's first safe animation JSON.

This script is intentionally small and easy to extend. It only emits data in the
approved animation format and never writes executable content.
"""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANIMATIONS_DIR = ROOT / "animations"
OUTPUT_FILE = ANIMATIONS_DIR / "flic_first_animation.json"
MILESTONE_STATE_FILE = ROOT / "memory" / "milestone_state.json"
FIRST_ANIMATION_FLAG_FILE = ROOT / "memory" / "first_animation_created.flag"


def directory_is_empty(path: Path) -> bool:
    if not path.exists():
        return True
    return not any(path.iterdir())


def directory_has_only_placeholder(path: Path) -> bool:
    if not path.exists():
        return False

    entries = [entry.name for entry in path.iterdir() if entry.is_file()]
    return entries == ["placeholder.json"]


def build_first_animation() -> dict:
    return {
        "name": "flic_first_animation",
        "fps": 10,
        "frames": [
            {
                "duration": 100,
                "pixels": [
                    {"x": 120, "y": 120, "color": "#202020"},
                    {"x": 121, "y": 120, "color": "#404040"},
                ],
            },
            {
                "duration": 100,
                "pixels": [
                    {"x": 120, "y": 120, "color": "#404040"},
                    {"x": 121, "y": 120, "color": "#808080"},
                    {"x": 120, "y": 121, "color": "#404040"},
                ],
            },
            {
                "duration": 120,
                "pixels": [
                    {"x": 119, "y": 119, "color": "#808080"},
                    {"x": 120, "y": 120, "color": "#C0C0C0"},
                    {"x": 121, "y": 121, "color": "#808080"},
                ],
            },
            {
                "duration": 120,
                "pixels": [
                    {"x": 118, "y": 118, "color": "#C0C0C0"},
                    {"x": 120, "y": 120, "color": "#FFFFFF"},
                    {"x": 122, "y": 122, "color": "#C0C0C0"},
                ],
            },
        ],
    }


def main() -> int:
    ANIMATIONS_DIR.mkdir(parents=True, exist_ok=True)
    if not directory_is_empty(ANIMATIONS_DIR) and not directory_has_only_placeholder(ANIMATIONS_DIR):
        return 0

    payload = build_first_animation()
    OUTPUT_FILE.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    MILESTONE_STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
    MILESTONE_STATE_FILE.write_text(
        json.dumps({"unlocked": [{"id": "first_animation", "trigger": "animation_created"}]}, indent=2)
        + "\n",
        encoding="utf-8",
    )
    FIRST_ANIMATION_FLAG_FILE.write_text("created\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
