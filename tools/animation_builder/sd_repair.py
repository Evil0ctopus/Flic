from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import shutil
import string
import sys
from pathlib import Path
from typing import Any

from PIL import Image

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.emotion_suite import EMOTION_SUITE, OUTPUT_ROOT, clean_output_root, suite_output_folder  # type: ignore
    from tools.animation_builder.generate_animation import COMFY_API_URL, generate_all  # type: ignore
else:
    from .emotion_suite import EMOTION_SUITE, OUTPUT_ROOT, clean_output_root, suite_output_folder
    from .generate_animation import COMFY_API_URL, generate_all

EXPECTED_METADATA = {"emotions": EMOTION_SUITE}
ALLOWED_ROOT_FILES = {"metadata.json"}
COMPATIBILITY_METADATA_NAME = "animation_metadata.json"
REPAIR_SEED = 1337
EXPECTED_FRAME_SIZE = (240, 240)


def _drive_type(root: Path) -> int:
    if os.name != "nt":
        return 0
    drive_type = ctypes.windll.kernel32.GetDriveTypeW(str(root))
    return int(drive_type)


def _candidate_drive_roots() -> list[tuple[Path, int]]:
    roots: list[tuple[Path, int]] = []
    for drive_letter in string.ascii_uppercase:
        root = Path(f"{drive_letter}:/")
        if not root.exists():
            continue
        drive_type = _drive_type(root)
        if drive_type in (2, 3):
            roots.append((root, drive_type))
    return roots


def _normalize_sd_root(sd_root: Path | str) -> Path:
    root = Path(sd_root)
    if root.name.lower() == "default" and root.parent.name.lower() == "face":
        return root
    if root.name.lower() == "face":
        return root / "default"
    return root / "Flic" / "animations" / "face" / "default"


def detect_sd_card() -> Path:
    env_root = os.environ.get("FLIC_SD_ROOT") or os.environ.get("FLIC_SD_DRIVE")
    if env_root:
        return _normalize_sd_root(env_root)

    candidates = _candidate_drive_roots()
    preferred_candidates = sorted(candidates, key=lambda item: 0 if item[1] == 2 else 1)

    for drive_root, _drive_type in preferred_candidates:
        default_root = drive_root / "Flic" / "animations" / "face" / "default"
        if default_root.exists():
            return default_root

    for drive_root, _drive_type in preferred_candidates:
        face_root = drive_root / "Flic" / "animations" / "face"
        if face_root.exists():
            return face_root / "default"

    removable_candidates = [drive_root for drive_root, drive_type in candidates if drive_type == 2]
    if len(removable_candidates) == 1:
        return removable_candidates[0] / "Flic" / "animations" / "face" / "default"

    if len(candidates) == 1:
        return candidates[0][0] / "Flic" / "animations" / "face" / "default"

    raise FileNotFoundError("Unable to detect the SD card containing Flic/animations/face")


def validate_sd_structure(sd_default_root: Path | None = None) -> Path:
    default_root = detect_sd_card() if sd_default_root is None else Path(sd_default_root)
    default_root.mkdir(parents=True, exist_ok=True)
    default_root.parent.mkdir(parents=True, exist_ok=True)
    return default_root


def _sorted_pngs(folder: Path) -> list[Path]:
    return sorted(path for path in folder.glob("frame_*.png") if path.is_file())


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _json_payload(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None


def _validate_png(path: Path) -> tuple[bool, str]:
    try:
        with Image.open(path) as image:
            image.verify()
        with Image.open(path) as image:
            if image.size != EXPECTED_FRAME_SIZE:
                return False, f"expected {EXPECTED_FRAME_SIZE[0]}x{EXPECTED_FRAME_SIZE[1]}, found {image.size[0]}x{image.size[1]}"
            image.load()
        return True, "ok"
    except Exception as exc:
        return False, str(exc)


def scan_for_corrupted_pngs(sd_default_root: Path) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []
    for png in sd_default_root.rglob("*.png"):
        if not png.is_file():
            continue
        ok, detail = _validate_png(png)
        if not ok:
            issues.append({"category": "corrupted_png", "path": str(png), "detail": detail})
    return issues


def scan_for_wrong_resolution(sd_default_root: Path) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []
    for png in sd_default_root.rglob("*.png"):
        if not png.is_file():
            continue
        try:
            with Image.open(png) as image:
                if image.size != EXPECTED_FRAME_SIZE:
                    issues.append({
                        "category": "wrong_resolution",
                        "path": str(png),
                        "detail": f"expected {EXPECTED_FRAME_SIZE[0]}x{EXPECTED_FRAME_SIZE[1]}, found {image.size[0]}x{image.size[1]}",
                    })
        except Exception as exc:
            issues.append({"category": "wrong_resolution", "path": str(png), "detail": str(exc)})
    return issues


def scan_for_stray_folders(sd_default_root: Path) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []
    allowed_root_dirs = set(EMOTION_SUITE)
    for child in sd_default_root.iterdir():
        if child.is_dir() and child.name not in allowed_root_dirs:
            issues.append({"category": "stray_folder", "path": str(child), "detail": "unexpected folder at animation root"})

    for path in sd_default_root.rglob("*"):
        if not path.is_dir():
            continue
        if path.name in {"left", "right", "final"}:
            issues.append({"category": "stray_folder", "path": str(path), "detail": "legacy compositor folder"})
    return issues


def scan_for_boot_frames(sd_default_root: Path) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []
    for png in sd_default_root.rglob("*.png"):
        if not png.is_file():
            continue
        lower_parts = [part.lower() for part in png.parts]
        lower_name = png.name.lower()
        if any("boot" in part for part in lower_parts) or "boot" in lower_name or "splash" in lower_name:
            issues.append({"category": "boot_frame", "path": str(png), "detail": "boot/splash frame should not be on the rebuilt SD card"})
    return issues


def scan_for_hidden_files(sd_default_root: Path) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []
    hidden_names = {"thumbs.db", "desktop.ini"}
    for path in sd_default_root.rglob("*"):
        if path.name.startswith(".") or path.name.lower() in hidden_names:
            issues.append({"category": "hidden_file", "path": str(path), "detail": "hidden file should not be present"})
    return issues


def scan_metadata(sd_default_root: Path) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []
    default_metadata = sd_default_root / "metadata.json"
    compatibility_metadata = sd_default_root.parent / COMPATIBILITY_METADATA_NAME

    for path in (default_metadata, compatibility_metadata):
        payload = _json_payload(path)
        if payload is None:
            issues.append({"category": "metadata_missing_or_invalid", "path": str(path), "detail": "missing or unreadable JSON"})
            continue
        if payload != EXPECTED_METADATA:
            issues.append({"category": "metadata_mismatch", "path": str(path), "detail": json.dumps(payload, sort_keys=True)})

    return issues


def run_diagnostics(sd_default_root: Path) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []
    issues.extend(scan_for_corrupted_pngs(sd_default_root))
    issues.extend(scan_for_wrong_resolution(sd_default_root))
    issues.extend(scan_for_stray_folders(sd_default_root))
    issues.extend(scan_for_boot_frames(sd_default_root))
    issues.extend(scan_for_hidden_files(sd_default_root))
    issues.extend(scan_metadata(sd_default_root))
    return issues


def purge_sd_card(sd_default_root: Path) -> None:
    if not sd_default_root.exists():
        sd_default_root.mkdir(parents=True, exist_ok=True)
        return

    for item in list(sd_default_root.iterdir()):
        if item.is_dir():
            shutil.rmtree(item)
        else:
            item.unlink()


def regenerate_emotion_suite(seed: int = REPAIR_SEED) -> list[Path]:
    clean_output_root()
    return generate_all(api_url=COMFY_API_URL, seed=seed, strict_comfy=False, force_local_renderer=True)


def _copy_emotion_frames(sd_default_root: Path, emotion: str) -> list[Path]:
    src_dir = suite_output_folder(emotion)
    final_src = src_dir / "final"
    if not final_src.is_dir():
        raise FileNotFoundError(f"Missing final source folder: {final_src}")

    final_frames = _sorted_pngs(final_src)
    if not final_frames:
        raise FileNotFoundError(f"No PNG frames found in {final_src}")

    dst_dir = sd_default_root / emotion
    temp_dir = sd_default_root / f".{emotion}.tmp"
    if dst_dir.exists():
        shutil.rmtree(dst_dir)
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    temp_dir.mkdir(parents=True, exist_ok=True)

    copied: list[Path] = []
    for frame in final_frames:
        destination = temp_dir / frame.name
        shutil.copy2(frame, destination)
        ok, detail = _validate_png(destination)
        if not ok:
            shutil.rmtree(temp_dir)
            raise ValueError(f"Invalid frame copied for {emotion}: {destination} ({detail})")
        copied.append(destination)

    temp_dir.rename(dst_dir)
    return copied


def write_clean_metadata(sd_default_root: Path) -> list[Path]:
    payload = json.dumps(EXPECTED_METADATA, indent=2) + "\n"
    default_metadata = sd_default_root / "metadata.json"
    compatibility_metadata = sd_default_root.parent / COMPATIBILITY_METADATA_NAME
    default_metadata.write_text(payload, encoding="utf-8")
    compatibility_metadata.write_text(payload, encoding="utf-8")
    return [default_metadata, compatibility_metadata]


def sync_clean_frames_to_sd(sd_default_root: Path) -> dict[str, Any]:
    installed: dict[str, int] = {}
    for emotion in EMOTION_SUITE:
        copied = _copy_emotion_frames(sd_default_root, emotion)
        installed[emotion] = len(copied)
        print(f"[OK] synced {emotion}: {len(copied)} final frames")
    metadata_paths = write_clean_metadata(sd_default_root)
    for path in metadata_paths:
        print(f"[OK] wrote metadata -> {path}")
    return {"installed": installed, "metadata_paths": metadata_paths}


def validate_final_sd_state(sd_default_root: Path) -> list[dict[str, str]]:
    issues: list[dict[str, str]] = []

    for emotion in EMOTION_SUITE:
        emotion_dir = sd_default_root / emotion
        if not emotion_dir.is_dir():
            issues.append({"category": "missing_emotion_folder", "path": str(emotion_dir), "detail": "emotion folder missing"})
            continue

        frames = _sorted_pngs(emotion_dir)
        if not frames:
            issues.append({"category": "missing_frames", "path": str(emotion_dir), "detail": "no final frames found"})
            continue

        for frame in frames:
            ok, detail = _validate_png(frame)
            if not ok:
                issues.append({"category": "invalid_final_frame", "path": str(frame), "detail": detail})

    allowed_root_entries = set(EMOTION_SUITE) | ALLOWED_ROOT_FILES
    for child in sd_default_root.iterdir():
        if child.name not in allowed_root_entries:
            issues.append({"category": "unexpected_root_entry", "path": str(child), "detail": "unexpected file or folder under /Flic/animations/face/default"})

    issues.extend(scan_metadata(sd_default_root))
    issues.extend(scan_for_hidden_files(sd_default_root))
    issues.extend(scan_for_stray_folders(sd_default_root))
    issues.extend(scan_for_boot_frames(sd_default_root))
    return issues


def repair_sd_card() -> dict[str, Any]:
    sd_default_root = validate_sd_structure()
    pre_issues = run_diagnostics(sd_default_root)

    print(f"[INFO] SD target: {sd_default_root}")
    if pre_issues:
        print(f"[INFO] Pre-repair diagnostics found {len(pre_issues)} issue(s)")
        for issue in pre_issues:
            print(f"[WARN] {issue['category']}: {issue['path']} :: {issue['detail']}")
    else:
        print("[INFO] Pre-repair diagnostics found no issues")

    print("[INFO] Purging existing default animation assets")
    purge_sd_card(sd_default_root)

    print("[INFO] Regenerating full emotion suite locally")
    local_outputs = regenerate_emotion_suite()
    print(f"[INFO] Generated {len(local_outputs)} emotion folders in {OUTPUT_ROOT}")

    print("[INFO] Syncing clean final frames to SD")
    sync_report = sync_clean_frames_to_sd(sd_default_root)

    final_issues = validate_final_sd_state(sd_default_root)
    if final_issues:
        print(f"[ERROR] Final validation found {len(final_issues)} issue(s)")
        for issue in final_issues:
            print(f"[ERROR] {issue['category']}: {issue['path']} :: {issue['detail']}")
    else:
        print("[OK] Final validation passed")

    return {
        "sd_default_root": sd_default_root,
        "pre_issues": pre_issues,
        "final_issues": final_issues,
        "sync_report": sync_report,
        "local_outputs": local_outputs,
    }


def main() -> int:
    try:
        report = repair_sd_card()
    except Exception as exc:
        print(f"ERROR: {type(exc).__name__}: {exc}")
        return 2

    if report["final_issues"]:
        print("DONE with warnings. The repair completed, but validation reported remaining problems.")
        return 1

    print("SUCCESS: SD card repaired and rebuilt with a clean 12-emotion animation set.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())