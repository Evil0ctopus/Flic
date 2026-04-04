from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.comfy_api import check_server  # type: ignore
    from tools.animation_builder.config import COMFY_API_URL  # type: ignore
    from tools.animation_builder.environment_state import has_primary_checkpoint  # type: ignore
    from tools.animation_builder.emotion_suite import EMOTION_SUITE, OUTPUT_ROOT, clean_output_root, suite_output_folder  # type: ignore
    from tools.animation_builder.generate_animation import generate_all  # type: ignore
    from tools.animation_builder.sd_repair import detect_sd_card, validate_sd_structure  # type: ignore
    from tools.animation_builder.sync_face_frames_to_sd import sync_suite_to_sd  # type: ignore
else:
    from .comfy_api import check_server
    from .config import COMFY_API_URL
    from .environment_state import has_primary_checkpoint
    from .emotion_suite import EMOTION_SUITE, OUTPUT_ROOT, clean_output_root, suite_output_folder
    from .generate_animation import generate_all
    from .sd_repair import detect_sd_card, validate_sd_structure
    from .sync_face_frames_to_sd import sync_suite_to_sd


def _start_comfy_background() -> None:
    starter = Path(__file__).resolve().parent / "vscode_start_comfy.py"
    kwargs: dict[str, object] = {
        "stdout": subprocess.DEVNULL,
        "stderr": subprocess.DEVNULL,
        "stdin": subprocess.DEVNULL,
        "close_fds": True,
    }
    if sys.platform.startswith("win"):
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS
    subprocess.Popen([sys.executable, str(starter)], **kwargs)


def _wait_for_comfy(api_url: str, timeout_seconds: float, poll_seconds: float = 2.0) -> bool:
    if timeout_seconds <= 0:
        return check_server(api_url=api_url)

    start = time.time()
    while (time.time() - start) < timeout_seconds:
        if check_server(api_url=api_url):
            return True
        time.sleep(poll_seconds)
    return False


def _validate_local_outputs() -> list[str]:
    missing: list[str] = []
    for emotion in EMOTION_SUITE:
        folder = suite_output_folder(emotion)
        if not folder.exists() or not folder.is_dir():
            missing.append(emotion)
            continue
        left = folder / "left"
        right = folder / "right"
        final = folder / "final"
        if not left.is_dir() or not right.is_dir() or not final.is_dir():
            missing.append(emotion)
            continue
        left_pngs = sorted(left.glob("frame_*.png"))
        right_pngs = sorted(right.glob("frame_*.png"))
        final_pngs = sorted(final.glob("frame_*.png"))
        if (
            not left_pngs
            or not right_pngs
            or not final_pngs
            or len(left_pngs) != len(right_pngs)
            or len(final_pngs) != len(left_pngs)
        ):
            missing.append(emotion)
    return missing


def _validate_sd_outputs() -> list[str]:
    missing: list[str] = []
    sd_default_root = validate_sd_structure(detect_sd_card())
    for emotion in EMOTION_SUITE:
        folder = sd_default_root / emotion
        if not folder.exists() or not folder.is_dir():
            missing.append(emotion)
            continue
        pngs = sorted(folder.glob("frame_*.png"))
        if not pngs:
            missing.append(emotion)
    return missing


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate the full emotion suite and sync it to SD")
    parser.add_argument("--api", default=COMFY_API_URL)
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--strict-comfy", action="store_true")
    parser.add_argument("--wait-for-comfy", type=float, default=90.0)
    args = parser.parse_args()

    checkpoint_present = has_primary_checkpoint()
    comfy_ready = check_server(api_url=args.api)
    strict_effective = bool(args.strict_comfy)
    if checkpoint_present and not comfy_ready:
        _start_comfy_background()
        comfy_ready = _wait_for_comfy(api_url=args.api, timeout_seconds=args.wait_for_comfy)

    if not checkpoint_present:
        strict_effective = False
        comfy_ready = False

    if not comfy_ready and checkpoint_present and args.strict_comfy:
        print("ERROR: ComfyUI did not become reachable in time.")
        return 2

    clean_output_root()
    outputs = generate_all(api_url=args.api, seed=args.seed, strict_comfy=strict_effective, force_local_renderer=not comfy_ready)

    local_missing = _validate_local_outputs()
    if local_missing:
        print(f"ERROR: missing local emotion outputs: {', '.join(local_missing)}")
        return 3

    sync_report = sync_suite_to_sd()
    sync_errors = sync_report.get("errors", [])
    if sync_errors:
        print("ERROR: SD sync completed with missing emotion folders.")
        return 4

    sd_missing = _validate_sd_outputs()
    if sd_missing:
        print(f"ERROR: missing SD emotion folders: {', '.join(sd_missing)}")
        return 5

    print("SUMMARY")
    print(f"Generated emotions: {', '.join(EMOTION_SUITE)}")
    print(f"Local output root: {OUTPUT_ROOT}")
    sd_root = sync_report.get("sd_default_root")
    if sd_root is None:
        sd_root = validate_sd_structure(detect_sd_card())
    print(f"SD output root: {sd_root}")
    print(f"Frames generated: {len(outputs)} folders")
    print(f"Metadata files: {', '.join(str(path) for path in sync_report['metadata_paths'])}")
    print("SUCCESS: full emotion suite generated, cleaned, synced, and validated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
