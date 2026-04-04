from __future__ import annotations

import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.sd_repair import detect_sd_card, sync_clean_frames_to_sd, validate_sd_structure  # type: ignore
else:
    from .sd_repair import detect_sd_card, sync_clean_frames_to_sd, validate_sd_structure


def sync_suite_to_sd() -> dict[str, object]:
    sd_default_root = validate_sd_structure(detect_sd_card())
    return sync_clean_frames_to_sd(sd_default_root)


def main() -> int:
    try:
        report = sync_suite_to_sd()
    except Exception as exc:
        print(f"ERROR: {type(exc).__name__}: {exc}")
        return 2

    installed = report["installed"]
    print(f"SUCCESS: synced emotions -> {', '.join(installed.keys())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
