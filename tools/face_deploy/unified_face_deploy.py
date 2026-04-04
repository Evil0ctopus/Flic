from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
from typing import Any

from PIL import Image

if __package__ in (None, ""):
    import sys

    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.boot_animation.sd_boot_utils import detect_sd_card as _detect_sd_card  # type: ignore
else:
    from ..boot_animation.sd_boot_utils import detect_sd_card as _detect_sd_card

CORE_UNIFIED = [
    "idle_breathing.json",
    "blink.json",
    "emotion_calm.json",
    "emotion_curious.json",
    "emotion_happy.json",
    "emotion_sleepy.json",
    "emotion_surprised.json",
]

ALIASES = {
    "happy_wiggle.json": "emotion_happy.json",
    "sleepy_fade.json": "emotion_sleepy.json",
    "surprise.json": "emotion_surprised.json",
    "thinking_loop.json": "emotion_curious.json",
}

UNIFIED_SET = sorted(CORE_UNIFIED + list(ALIASES.keys()))
HIDDEN_FILES = {".ds_store", "thumbs.db", "desktop.ini"}
STRAY_FOLDERS = {"left", "right", "temp", "cache", "final"}


def _hex_to_rgba(color_text: str) -> tuple[int, int, int, int]:
    text = color_text.strip()
    if text.startswith("#"):
        text = text[1:]
    if len(text) != 6:
        return (255, 255, 255, 255)
    try:
        return (int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16), 255)
    except ValueError:
        return (255, 255, 255, 255)


def _materialize_idle_png_frames(src_json: Path, face_default_root: Path) -> int:
    payload = json.loads(src_json.read_text(encoding="utf-8"))
    frames = payload.get("frames", [])
    if not isinstance(frames, list) or not frames:
        raise RuntimeError(f"Idle JSON has no frames: {src_json}")

    idle_dir = face_default_root / "idle"
    if idle_dir.exists() and idle_dir.is_dir():
        for stale in idle_dir.glob("frame_*.png"):
            stale.unlink(missing_ok=True)
    idle_dir.mkdir(parents=True, exist_ok=True)

    written = 0
    for idx, frame in enumerate(frames):
        pixels = frame.get("pixels", []) if isinstance(frame, dict) else []
        image = Image.new("RGBA", (240, 240), (0, 0, 0, 0))
        if isinstance(pixels, list):
            for pixel in pixels:
                if not isinstance(pixel, dict):
                    continue
                x = int(pixel.get("x", -1))
                y = int(pixel.get("y", -1))
                if x < 0 or y < 0 or x >= 240 or y >= 240:
                    continue
                color = _hex_to_rgba(str(pixel.get("color", "#FFFFFF")))
                image.putpixel((x, y), color)

        out_path = idle_dir / f"frame_{idx:03d}.png"
        image.save(out_path, format="PNG", optimize=True)
        written += 1

    if written == 0:
        raise RuntimeError("No idle PNG frames were materialized")
    return written


def detect_sd_card() -> Path:
    """Detect SD card root drive dynamically (e.g. D:/)."""
    root = _detect_sd_card()
    return Path(root)


def ensure_flic_structure(sd_root: Path | None = None) -> dict[str, Path]:
    """Ensure the required Flic folders exist without touching /Flic/boot content."""
    root = detect_sd_card() if sd_root is None else Path(sd_root)
    flic_root = root / "Flic"
    animations_root = flic_root / "animations"
    face_root = animations_root / "face"
    face_default_root = face_root / "default"
    boot_root = flic_root / "boot"

    for folder in (flic_root, animations_root, face_root, face_default_root, boot_root):
        folder.mkdir(parents=True, exist_ok=True)

    return {
        "sd_root": root,
        "flic_root": flic_root,
        "animations_root": animations_root,
        "face_root": face_root,
        "face_default_root": face_default_root,
        "boot_root": boot_root,
    }


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def purge_sd_faces(sd_root: Path | None = None) -> dict[str, Any]:
    """Delete all face JSONs and stale folders under face paths, preserving /Flic/boot."""
    paths = ensure_flic_structure(sd_root)
    animations_root = paths["animations_root"]
    face_root = paths["face_root"]
    face_default_root = paths["face_default_root"]

    removed_json = 0
    removed_hidden = 0
    removed_other = 0
    removed_dirs = 0

    # Remove JSON and hidden files from /Flic/animations recursively.
    for file_path in sorted(animations_root.rglob("*"), key=lambda p: str(p).lower()):
        if not file_path.is_file():
            continue
        lname = file_path.name.lower()
        if file_path.suffix.lower() == ".json":
            file_path.unlink(missing_ok=True)
            removed_json += 1
            continue
        if lname in HIDDEN_FILES or lname.startswith("."):
            file_path.unlink(missing_ok=True)
            removed_hidden += 1
            continue

    # Remove stray folders from /Flic/animations recursively.
    for dir_path in sorted(animations_root.rglob("*"), key=lambda p: len(str(p)), reverse=True):
        if not dir_path.is_dir():
            continue
        if dir_path.name.lower() in STRAY_FOLDERS:
            shutil.rmtree(dir_path, ignore_errors=True)
            removed_dirs += 1

    # Purge all legacy subfolders under /Flic/animations/face then recreate a clean default path.
    if face_root.exists() and face_root.is_dir():
        for child in sorted(face_root.iterdir(), key=lambda p: p.name.lower()):
            if child.is_dir():
                shutil.rmtree(child, ignore_errors=True)
                removed_dirs += 1
            else:
                child.unlink(missing_ok=True)
                removed_other += 1

    face_default_root.mkdir(parents=True, exist_ok=True)

    leftovers: list[str] = []
    for child in sorted(face_default_root.iterdir(), key=lambda p: p.name.lower()):
        leftovers.append(str(child))

    if leftovers:
        raise RuntimeError("Face folders are not empty after purge: " + ", ".join(leftovers))

    return {
        "removed_json": removed_json,
        "removed_hidden": removed_hidden,
        "removed_other": removed_other,
        "removed_dirs": removed_dirs,
        "face_root": paths["face_root"],
        "face_default_root": paths["face_default_root"],
    }


def purge_local_face_outputs(repo_root: Path | None = None) -> dict[str, Any]:
    """Delete local face-only outputs and stale face JSONs, preserving boot outputs."""
    root = Path.cwd() if repo_root is None else Path(repo_root)

    removed_files = 0
    removed_dirs = 0

    # Comfy face outputs only (preserve boot outputs)
    candidate_output_dirs = [
        root / "ComfyUI" / "output" / "face",
        Path("C:/Flic/ComfyUI/output/face"),
        root / "output" / "face",
        root / "build" / "animations" / "face",
    ]

    seen = set()
    for folder in candidate_output_dirs:
        key = str(folder).lower()
        if key in seen:
            continue
        seen.add(key)
        if not folder.exists() or not folder.is_dir():
            continue
        for child in sorted(folder.iterdir(), key=lambda p: p.name.lower()):
            if child.is_dir():
                shutil.rmtree(child, ignore_errors=True)
                removed_dirs += 1
            else:
                child.unlink(missing_ok=True)
                removed_files += 1

    # Cached face PNG leftovers only.
    for folder in [root / "output", root / "build"]:
        if not folder.exists() or not folder.is_dir():
            continue
        for path in folder.rglob("*"):
            if not path.is_file():
                continue
            lpath = str(path).replace("\\", "/").lower()
            if "/boot/" in lpath:
                continue
            if "/face/" in lpath and path.suffix.lower() in {".png", ".jpg", ".jpeg", ".json"}:
                path.unlink(missing_ok=True)
                removed_files += 1

    # Remove old repo face JSONs not in unified set.
    anim_dir = root / "ai" / "animations"
    removed_repo_json = 0
    if anim_dir.exists() and anim_dir.is_dir():
        for json_file in sorted(anim_dir.glob("*.json"), key=lambda p: p.name.lower()):
            if json_file.name.lower() not in {name.lower() for name in UNIFIED_SET}:
                json_file.unlink(missing_ok=True)
                removed_repo_json += 1

    return {
        "removed_files": removed_files,
        "removed_dirs": removed_dirs,
        "removed_repo_json": removed_repo_json,
    }


def install_new_unified_faces(sd_root: Path | None = None, repo_root: Path | None = None) -> dict[str, Any]:
    """Install only new unified face set and aliases under /Flic/animations/face/default/."""
    root = Path.cwd() if repo_root is None else Path(repo_root)
    paths = ensure_flic_structure(sd_root)
    target = paths["face_default_root"]
    src_dir = root / "ai" / "animations"

    if not src_dir.exists():
        raise FileNotFoundError(f"Missing local animation folder: {src_dir}")

    installed: list[str] = []

    for file_name in CORE_UNIFIED:
        src = src_dir / file_name
        if not src.exists():
            raise FileNotFoundError(f"Missing core unified face JSON: {src}")
        dst = target / file_name
        shutil.copy2(src, dst)
        installed.append(file_name)

    for alias_name, source_name in sorted(ALIASES.items()):
        src = src_dir / source_name
        if not src.exists():
            raise FileNotFoundError(f"Missing alias source JSON: {src}")
        dst = target / alias_name
        shutil.copy2(src, dst)
        installed.append(alias_name)

    # Validation: existence + hash match
    validations: list[str] = []
    for file_name in sorted(installed):
        dst = target / file_name
        if not dst.exists():
            raise RuntimeError(f"Install validation failed; missing on SD: {dst}")

    for core_name in CORE_UNIFIED:
        src = src_dir / core_name
        dst = target / core_name
        if _sha256(src) != _sha256(dst):
            raise RuntimeError(f"Hash mismatch for core unified file: {core_name}")
        validations.append(f"core ok: {core_name}")

    for alias_name, source_name in sorted(ALIASES.items()):
        alias_path = target / alias_name
        source_path = target / source_name
        if _sha256(alias_path) != _sha256(source_path):
            raise RuntimeError(f"Alias mismatch: {alias_name} != {source_name}")
        validations.append(f"alias ok: {alias_name} -> {source_name}")

    # Compatibility bridge: FaceEngine still expects PNG frames in /face/default/idle.
    idle_json = src_dir / "idle_breathing.json"
    idle_png_count = _materialize_idle_png_frames(idle_json, target)
    validations.append(f"idle png frames: {idle_png_count}")

    return {
        "target": target,
        "installed": sorted(installed),
        "validations": validations,
    }


def verify_boot_animation_integrity(sd_root: Path | None = None, repo_root: Path | None = None) -> dict[str, Any]:
    """Verify boot frames remain intact and final frame visually aligns with unified face geometry."""
    paths = ensure_flic_structure(sd_root)
    root = Path.cwd() if repo_root is None else Path(repo_root)
    boot_root = paths["boot_root"]

    frame_files = sorted(boot_root.glob("frame_*.png"), key=lambda p: p.name)
    if not frame_files:
        raise RuntimeError(f"No boot frames found in {boot_root}")

    for frame in frame_files:
        with Image.open(frame) as img:
            if img.size != (320, 240):
                raise RuntimeError(f"Boot frame size mismatch ({frame.name}): {img.size}, expected (320, 240)")

    # Visual compatibility check: sample unified idle reference points in final boot frame.
    unified_idle = root / "ai" / "animations" / "idle_breathing.json"
    if not unified_idle.exists():
        raise FileNotFoundError(f"Missing local unified idle JSON: {unified_idle}")

    payload = json.loads(unified_idle.read_text(encoding="utf-8"))
    frames = payload.get("frames", [])
    if not frames:
        raise RuntimeError("Unified idle JSON has no frames")

    points = frames[0].get("pixels", [])
    if not points:
        raise RuntimeError("Unified idle first frame has no pixels")

    sample_points = points[:: max(1, len(points) // 32)]
    final_frame = frame_files[-1]
    lit = 0
    with Image.open(final_frame) as img:
        rgb = img.convert("RGB")
        for pt in sample_points:
            x = int(pt["x"])
            y = int(pt["y"])
            if x < 0 or y < 0 or x >= rgb.width or y >= rgb.height:
                continue
            r, g, b = rgb.getpixel((x, y))
            luma = (0.2126 * r) + (0.7152 * g) + (0.0722 * b)
            if luma >= 12.0:
                lit += 1

    ratio = lit / float(max(1, len(sample_points)))
    if ratio < 0.45:
        raise RuntimeError(
            "Final boot frame does not visually match unified face set enough "
            f"(lit ratio {ratio:.2f}, expected >= 0.45)"
        )

    return {
        "boot_root": boot_root,
        "frame_count": len(frame_files),
        "final_frame": final_frame.name,
        "face_match_ratio": round(ratio, 3),
    }


def run_pipeline(sd_root: Path | None = None, repo_root: Path | None = None) -> dict[str, Any]:
    detect = detect_sd_card() if sd_root is None else Path(sd_root)
    ensure = ensure_flic_structure(detect)
    purge_sd = purge_sd_faces(detect)
    purge_local = purge_local_face_outputs(repo_root)
    install = install_new_unified_faces(detect, repo_root)
    boot = verify_boot_animation_integrity(detect, repo_root)

    summary = {
        "sd_root": str(detect),
        "face_target": str(ensure["face_default_root"]),
        "purge_sd": purge_sd,
        "purge_local": purge_local,
        "install": install,
        "boot_verify": boot,
    }
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description="Purge old faces, install unified face set, and verify runtime inputs.")
    parser.add_argument("--sd-root", type=Path, default=None, help="Optional SD root override (drive root or /Flic root)")
    parser.add_argument("--repo-root", type=Path, default=Path.cwd(), help="Optional repository root path")
    args = parser.parse_args()

    summary = run_pipeline(sd_root=args.sd_root, repo_root=args.repo_root)

    print("SUMMARY")
    print(f"SD root: {summary['sd_root']}")
    print(f"Face target: {summary['face_target']}")

    purge_sd = summary["purge_sd"]
    print(
        "SD face purge removed -> "
        f"json={purge_sd['removed_json']} hidden={purge_sd['removed_hidden']} "
        f"other={purge_sd['removed_other']} dirs={purge_sd['removed_dirs']}"
    )

    purge_local = summary["purge_local"]
    print(
        "Local face purge removed -> "
        f"files={purge_local['removed_files']} dirs={purge_local['removed_dirs']} "
        f"repo_json={purge_local['removed_repo_json']}"
    )

    print(f"Installed unified face files: {len(summary['install']['installed'])}")
    print(f"Boot frames verified: {summary['boot_verify']['frame_count']} @ 320x240")
    print(
        "Final boot frame unified-face match ratio: "
        f"{summary['boot_verify']['face_match_ratio']:.3f}"
    )

    print("NEXT")
    print("1) Reboot device.")
    print("2) Open serial monitor.")
    print("3) Confirm runtime prints show exact JSON file path from /Flic/animations/face/default/.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
