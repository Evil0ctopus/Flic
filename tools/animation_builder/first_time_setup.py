from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from pathlib import Path

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.environment_state import FIRST_TIME_STATE_PATH, checkpoint_files, sd_mount_exists  # type: ignore
    from tools.animation_builder.generate_animation import generate_animation  # type: ignore
    from tools.animation_builder.verify_installation import main as verify_installation_main  # type: ignore
else:
    from .environment_state import FIRST_TIME_STATE_PATH, checkpoint_files, sd_mount_exists
    from .generate_animation import generate_animation
    from .verify_installation import main as verify_installation_main


def _prompt_yes_no(question: str, default: bool = False) -> bool:
    suffix = "[Y/n]" if default else "[y/N]"
    answer = input(f"{question} {suffix} ").strip().lower()
    if not answer:
        return default
    return answer in {"y", "yes"}


def _write_state() -> None:
    FIRST_TIME_STATE_PATH.write_text(
        json.dumps(
            {
                "completed_at": datetime.now(timezone.utc).isoformat(),
                "checkpoint_present": len(checkpoint_files()) > 0,
                "sd_mount_present": sd_mount_exists(),
            },
            indent=2,
        ),
        encoding="utf-8",
    )


def main() -> int:
    if FIRST_TIME_STATE_PATH.exists():
        print("First-time setup already completed.")
        return 0

    print("=== Flic First-Time Setup Wizard ===")
    print("This wizard helps you finish the ComfyUI side of the Flic animation pipeline.")
    print()

    if _prompt_yes_no("Review checkpoint setup instructions now?", default=True):
        print("Checkpoint location: C:/Flic/ComfyUI/models/checkpoints/")
        print("Drop a .safetensors or .ckpt file there to enable model-based generation.")
        print()

    if _prompt_yes_no("Test the installation verifier now?", default=True):
        print()
        verify_installation_main()
        print()

    if _prompt_yes_no("Run a sample animation now?", default=True):
        print()
        output = generate_animation(
            "blink",
            strict_comfy=False,
            force_local_renderer=len(checkpoint_files()) == 0,
        )
        print(f"Sample animation generated at: {output}")
        print()

    if _prompt_yes_no("Enable SD export instructions?", default=False):
        if sd_mount_exists():
            print("SD card detected — enabling D:\\ export.")
        else:
            print("D:\\ is not mounted. Flic will use the silent local export fallback until it appears.")
        print()

    _write_state()
    print(f"First-time setup complete. State saved to {FIRST_TIME_STATE_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())