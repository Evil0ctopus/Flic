from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import wave
import zlib
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable

REQUIRED_SD_DIRS = [
    "Flic",
    "Flic/boot",
    "Flic/animations",
    "Flic/animations/face",
    "Flic/animations/face/default",
    "Flic/sounds",
    "Flic/config",
]

ORPHAN_DIR_NAMES = {"temp", "cache", "final", "old", "backup"}
HIDDEN_JUNK_FILES = {".ds_store", "thumbs.db", "desktop.ini"}
KNOWN_FLIC_ROOT_NAMES = {
    "boot",
    "animations",
    "sounds",
    "config",
    "voices",
    "memory",
    "logs",
}

REQUIRED_FACE_JSONS = [
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


@dataclass
class RecoveryContext:
    sd_root: Path
    flic_root: Path


def _log(message: str) -> None:
    print(f"[recovery] {message}")


def _drive_type(root: Path) -> int:
    if os.name != "nt":
        return 0
    return int(ctypes.windll.kernel32.GetDriveTypeW(str(root)))


def _filesystem_name(root: Path) -> str | None:
    if os.name != "nt":
        return None

    fs_name = ctypes.create_unicode_buffer(261)
    serial = ctypes.c_uint(0)
    max_component = ctypes.c_uint(0)
    flags = ctypes.c_uint(0)
    ok = ctypes.windll.kernel32.GetVolumeInformationW(
        ctypes.c_wchar_p(str(root)),
        None,
        0,
        ctypes.byref(serial),
        ctypes.byref(max_component),
        ctypes.byref(flags),
        fs_name,
        ctypes.sizeof(fs_name),
    )
    if ok == 0:
        return None
    return fs_name.value


def _candidate_roots() -> list[Path]:
    roots: list[Path] = []
    for letter in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        root = Path(f"{letter}:/")
        if not root.exists():
            continue
        if _drive_type(root) in (2, 3):
            roots.append(root)
    return roots


def detect_sd_card(sd_root_override: str | None = None) -> Path:
    """Detect SD card drive letter dynamically on Windows (e.g. D:/)."""
    _log("Detecting SD card root...")
    if sd_root_override:
        root = Path(sd_root_override)
        _log(f"Using explicit SD root override: {root}")
        return root

    env_root = os.environ.get("FLIC_SD_ROOT")
    if env_root:
        root = Path(env_root)
        _log(f"Using FLIC_SD_ROOT override: {root}")
        return root

    candidates = _candidate_roots()
    if not candidates:
        raise FileNotFoundError("No mounted candidate drives found.")

    system_drive = os.environ.get("SystemDrive", "C:")
    system_drive_letter = Path(system_drive).drive.upper().replace(":", "")

    def drive_letter(path: Path) -> str:
        return path.drive.upper().replace(":", "")

    removable_first = sorted(candidates, key=lambda p: 0 if _drive_type(p) == 2 else 1)
    removable = [root for root in removable_first if _drive_type(root) == 2]
    non_system = [root for root in removable_first if drive_letter(root) != system_drive_letter]
    removable_non_system = [root for root in removable if drive_letter(root) != system_drive_letter]

    for root in removable_non_system:
        if (root / "Flic").exists():
            _log(f"Detected SD root by existing Flic folder: {root}")
            return root

    if len(removable_non_system) == 1:
        _log(f"Detected SD root by single removable drive: {removable_non_system[0]}")
        return removable_non_system[0]

    for root in non_system:
        if (root / "Flic").exists():
            _log(f"Detected SD root by existing Flic folder (non-system): {root}")
            return root

    raise FileNotFoundError(
        "Unable to detect a non-system SD card automatically. "
        "Set FLIC_SD_ROOT or pass --sd-root (example: D:/)."
    )


def ensure_sd_fat32(sd_root: Path) -> None:
    """Confirm SD card filesystem is FAT32 and abort safely if not."""
    fs = _filesystem_name(sd_root)
    _log(f"Filesystem check for {sd_root}: {fs or 'unknown'}")
    if fs is None:
        raise RuntimeError(
            "Unable to read filesystem type. Re-seat SD card and retry."
        )

    if fs.upper() != "FAT32":
        print()
        print("ERROR: SD card is not FAT32.")
        print(f"Detected filesystem: {fs}")
        print("Reformat the SD card as FAT32, then re-run recovery.")
        print("Windows quick steps: File Explorer -> right-click drive -> Format -> FAT32 -> Start")
        print()
        raise RuntimeError("SD filesystem is not FAT32")


def ensure_flic_structure(sd_root: Path) -> RecoveryContext:
    """Ensure required /Flic folder structure exists."""
    flic_root = sd_root / "Flic"
    _log(f"Ensuring Flic directory structure under {flic_root}")
    for rel in REQUIRED_SD_DIRS:
        path = sd_root / rel
        path.mkdir(parents=True, exist_ok=True)
        _log(f"Ensured directory: {path}")
    return RecoveryContext(sd_root=sd_root, flic_root=flic_root)


def _iter_tree(path: Path) -> Iterable[Path]:
    for p in sorted(path.rglob("*"), key=lambda x: (str(x).count(os.sep), str(x).lower())):
        yield p


def _format_size(size: int) -> str:
    units = ["B", "KB", "MB", "GB"]
    value = float(size)
    for unit in units:
        if value < 1024.0 or unit == units[-1]:
            return f"{value:.1f}{unit}"
        value /= 1024.0
    return f"{size}B"


def _print_tree(path: Path) -> None:
    _log(f"Tree for {path}")
    if not path.exists():
        print("  (missing)")
        return
    print(f"  {path.name}/")
    for p in _iter_tree(path):
        rel = p.relative_to(path)
        depth = len(rel.parts)
        indent = "  " + "  " * depth
        suffix = "/" if p.is_dir() else ""
        size = 0 if p.is_dir() else p.stat().st_size
        print(f"{indent}{p.name}{suffix}  [{_format_size(size)}]")


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        while True:
            chunk = fh.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def sweep_and_clean_sd(ctx: RecoveryContext, archive_unknown: bool = True) -> dict[str, int]:
    """Enumerate and clean orphan/temp/junk files under /Flic, optionally archiving unknown entries."""
    _log("Starting SD sweep and cleanup...")
    _print_tree(ctx.flic_root)

    removed_junk = 0
    removed_dirs = 0
    archived_unknown = 0

    backup_dir = None
    if archive_unknown:
        stamp = datetime.now().strftime("%Y%m%d_%H%M")
        backup_dir = ctx.flic_root / f"_recovery_backup_{stamp}"

    # Remove hidden junk files and orphan folders recursively.
    for p in sorted(ctx.flic_root.rglob("*"), key=lambda x: len(x.parts), reverse=True):
        if p.is_file() and p.name.lower() in HIDDEN_JUNK_FILES:
            p.unlink(missing_ok=True)
            removed_junk += 1
            _log(f"Removed hidden junk file: {p}")
            continue

        if p.is_dir() and p.name.lower() in ORPHAN_DIR_NAMES:
            shutil.rmtree(p, ignore_errors=True)
            removed_dirs += 1
            _log(f"Removed orphan directory: {p}")

    # Archive unknown top-level Flic entries.
    if backup_dir is not None:
        for child in sorted(ctx.flic_root.iterdir(), key=lambda p: p.name.lower()):
            if child.name.startswith("_recovery_backup_"):
                continue
            if child.name.lower() not in KNOWN_FLIC_ROOT_NAMES:
                backup_dir.mkdir(parents=True, exist_ok=True)
                dst = backup_dir / child.name
                if dst.exists():
                    if dst.is_dir():
                        shutil.rmtree(dst, ignore_errors=True)
                    else:
                        dst.unlink(missing_ok=True)
                shutil.move(str(child), str(dst))
                archived_unknown += 1
                _log(f"Archived unknown entry: {child} -> {dst}")

    _log("Sweep and cleanup complete.")
    return {
        "removed_junk": removed_junk,
        "removed_dirs": removed_dirs,
        "archived_unknown": archived_unknown,
    }


def _png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    head = struct.pack(">I", len(data)) + chunk_type + data
    crc = zlib.crc32(chunk_type)
    crc = zlib.crc32(data, crc)
    return head + struct.pack(">I", crc & 0xFFFFFFFF)


def _write_rgb_png(path: Path, width: int, height: int, rgb: tuple[int, int, int]) -> None:
    raw = bytearray()
    row = bytes([rgb[0], rgb[1], rgb[2]]) * width
    for _ in range(height):
        raw.append(0)
        raw.extend(row)
    data = zlib.compress(bytes(raw), 9)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # RGB, no alpha
    png = b"\x89PNG\r\n\x1a\n" + _png_chunk(b"IHDR", ihdr) + _png_chunk(b"IDAT", data) + _png_chunk(b"IEND", b"")
    path.write_bytes(png)


def _write_default_boot_wav(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    sample_rate = 22050
    duration_s = 0.3
    freq = 880.0
    total = int(sample_rate * duration_s)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        for i in range(total):
            amp = 14000
            value = int(amp * __import__("math").sin(2.0 * __import__("math").pi * freq * i / sample_rate))
            w.writeframes(struct.pack("<h", value))


def _minimal_face_json(name: str) -> dict[str, Any]:
    pixels = [
        {"x": 104, "y": 110, "color": "#AEE6FF"},
        {"x": 105, "y": 110, "color": "#AEE6FF"},
        {"x": 106, "y": 110, "color": "#AEE6FF"},
        {"x": 132, "y": 110, "color": "#AEE6FF"},
        {"x": 133, "y": 110, "color": "#AEE6FF"},
        {"x": 134, "y": 110, "color": "#AEE6FF"},
    ]
    return {
        "name": name.replace(".json", ""),
        "fps": 12,
        "frames": [
            {"duration": 80, "pixels": pixels},
            {"duration": 80, "pixels": pixels},
        ],
    }


def _copy_if_exists(srcs: list[Path], dst: Path) -> bool:
    for src in srcs:
        if src.exists() and src.is_file():
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            return True
    return False


def _ensure_reference_assets(repo_root: Path) -> dict[str, int]:
    assets_root = repo_root / "assets"
    boot_dir = assets_root / "boot"
    faces_dir = assets_root / "faces"
    sounds_dir = assets_root / "sounds"
    config_dir = assets_root / "config"

    for d in (boot_dir, faces_dir, sounds_dir, config_dir):
        d.mkdir(parents=True, exist_ok=True)

    created = 0

    # Boot defaults: create deterministic 320x240 RGB PNG frames if missing.
    existing_boot_png = sorted(boot_dir.glob("frame_*.png"))
    if not existing_boot_png:
        for i in range(12):
            t = i / 11.0
            c = int(8 + (t * 70))
            _write_rgb_png(boot_dir / f"frame_{i:03d}.png", 320, 240, (0, c, min(255, c + 80)))
        created += 12
        _log("Created default boot frames in assets/boot")

    # Faces defaults.
    for name in REQUIRED_FACE_JSONS:
        dst = faces_dir / name
        if dst.exists():
            continue
        copied = _copy_if_exists(
            [
                repo_root / "ai" / "animations" / name,
                repo_root / name,
            ],
            dst,
        )
        if not copied:
            dst.write_text(json.dumps(_minimal_face_json(name), indent=2), encoding="utf-8")
        created += 1

    for alias, source in ALIASES.items():
        alias_path = faces_dir / alias
        if not alias_path.exists():
            shutil.copy2(faces_dir / source, alias_path)
            created += 1

    # Sounds defaults.
    if not any(p.suffix.lower() in {".wav", ".mp3"} for p in sounds_dir.glob("*")):
        _write_default_boot_wav(sounds_dir / "boot_chime.wav")
        created += 1
        _log("Created default boot_chime.wav in assets/sounds")

    # Config defaults.
    if not any(config_dir.glob("*.json")):
        (config_dir / "settings.json").write_text(
            json.dumps(
                {
                    "face_style": "default",
                    "boot_sound": "boot_chime.wav",
                    "animations_root": "/Flic/animations/face/default",
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        created += 1

    return {"created_assets": created}


def _read_png_header(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()[:33]
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError(f"Invalid PNG signature: {path}")
    if data[12:16] != b"IHDR":
        raise RuntimeError(f"Invalid PNG header chunk: {path}")
    width = int.from_bytes(data[16:20], "big")
    height = int.from_bytes(data[20:24], "big")
    color_type = data[25]
    return width, height, color_type


def _validate_boot_frames(boot_dir: Path) -> dict[str, Any]:
    frames = sorted(boot_dir.glob("frame_*.png"), key=lambda p: p.name)
    if not frames:
        raise RuntimeError(f"No boot frames found in {boot_dir}")

    expected = [f"frame_{i:03d}.png" for i in range(len(frames))]
    names = [p.name for p in frames]
    if names != expected:
        raise RuntimeError("Boot frame names are not sequential zero-padded from frame_000.png")

    for frame in frames:
        width, height, color_type = _read_png_header(frame)
        if (width, height) != (320, 240):
            raise RuntimeError(f"Boot frame must be 320x240: {frame} has {width}x{height}")
        if color_type in (4, 6):
            raise RuntimeError(f"Boot frame has alpha channel (not allowed): {frame}")

    return {"boot_frames": len(frames)}


def _validate_faces(face_dir: Path) -> dict[str, Any]:
    for name in REQUIRED_FACE_JSONS:
        path = face_dir / name
        if not path.exists():
            raise RuntimeError(f"Missing required face JSON: {path}")

    for alias, source in ALIASES.items():
        alias_hash = _sha256(face_dir / alias)
        source_hash = _sha256(face_dir / source)
        if alias_hash != source_hash:
            raise RuntimeError(f"Alias hash mismatch: {alias} != {source}")

    return {"faces": len(REQUIRED_FACE_JSONS) + len(ALIASES)}


def _validate_sounds(sounds_dir: Path) -> dict[str, Any]:
    count = 0
    for path in sorted(sounds_dir.glob("*")):
        if path.is_file() and path.suffix.lower() in {".wav", ".mp3"}:
            _ = path.read_bytes()[:32]
            count += 1
    if count == 0:
        raise RuntimeError(f"No readable sound files found in {sounds_dir}")
    return {"sounds": count}


def install_reference_assets_to_sd(ctx: RecoveryContext, repo_root: Path | None = None) -> dict[str, Any]:
    """Install canonical local assets into SD and validate them."""
    _log("Installing reference assets to SD...")
    root = Path.cwd() if repo_root is None else Path(repo_root)
    created = _ensure_reference_assets(root)

    assets_root = root / "assets"
    boot_src = assets_root / "boot"
    faces_src = assets_root / "faces"
    sounds_src = assets_root / "sounds"
    config_src = assets_root / "config"

    boot_dst = ctx.flic_root / "boot"
    faces_dst = ctx.flic_root / "animations" / "face" / "default"
    sounds_dst = ctx.flic_root / "sounds"
    config_dst = ctx.flic_root / "config"

    for src, dst in (
        (boot_src, boot_dst),
        (faces_src, faces_dst),
        (sounds_src, sounds_dst),
        (config_src, config_dst),
    ):
        dst.mkdir(parents=True, exist_ok=True)
        for file in sorted(src.glob("*")):
            if file.is_file():
                shutil.copy2(file, dst / file.name)
                _log(f"Installed: {file} -> {dst / file.name}")

    boot_stats = _validate_boot_frames(boot_dst)
    face_stats = _validate_faces(faces_dst)
    sound_stats = _validate_sounds(sounds_dst)

    summary = {
        **created,
        **boot_stats,
        **face_stats,
        **sound_stats,
        "boot_dst": str(boot_dst),
        "faces_dst": str(faces_dst),
        "sounds_dst": str(sounds_dst),
        "config_dst": str(config_dst),
    }

    _log("Install complete.")
    _log(json.dumps(summary, indent=2))
    return summary


def purge_local_face_outputs(repo_root: Path | None = None) -> dict[str, int]:
    """Optional helper to clean local generated face output directories."""
    root = Path.cwd() if repo_root is None else Path(repo_root)
    removed_files = 0
    removed_dirs = 0

    for folder in [
        root / "build" / "animations" / "output",
        root / "output",
        root / "ComfyUI" / "output" / "face",
    ]:
        if not folder.exists() or not folder.is_dir():
            continue
        for child in sorted(folder.iterdir(), key=lambda p: p.name.lower()):
            if child.is_dir():
                shutil.rmtree(child, ignore_errors=True)
                removed_dirs += 1
            else:
                child.unlink(missing_ok=True)
                removed_files += 1

    return {"removed_files": removed_files, "removed_dirs": removed_dirs}


def _run_full(sd_root_override: str | None, repo_root: Path | None) -> int:
    sd_root = detect_sd_card(sd_root_override)
    ensure_sd_fat32(sd_root)
    ctx = ensure_flic_structure(sd_root)
    sweep = sweep_and_clean_sd(ctx, archive_unknown=True)
    install = install_reference_assets_to_sd(ctx, repo_root=repo_root)

    print()
    print("RECOVERY COMPLETE")
    print(f"SD root: {sd_root}")
    print(f"Cleanup: {sweep}")
    print(f"Install: boot={install['boot_frames']} faces={install['faces']} sounds={install['sounds']}")
    print("Next steps:")
    print("- Flash firmware")
    print("- Reboot device")
    print("- Open Serial Monitor and watch logs")
    print()
    return 0


def _run_quick(sd_root_override: str | None, repo_root: Path | None) -> int:
    sd_root = detect_sd_card(sd_root_override)
    ensure_sd_fat32(sd_root)
    ctx = ensure_flic_structure(sd_root)
    install = install_reference_assets_to_sd(ctx, repo_root=repo_root)

    print()
    print("QUICK REINSTALL COMPLETE")
    print(f"SD root: {sd_root}")
    print(f"Install: boot={install['boot_frames']} faces={install['faces']} sounds={install['sounds']}")
    print("Next steps:")
    print("- Flash firmware")
    print("- Reboot device")
    print("- Open Serial Monitor and watch logs")
    print()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Flic SD health, recovery, and asset reinstall pipeline")
    parser.add_argument("--mode", choices=["full", "quick"], default="full")
    parser.add_argument("--sd-root", default=None, help="Optional SD root override like D:/")
    parser.add_argument("--repo-root", default=None, help="Optional repository root")
    parser.add_argument("--purge-local-face-outputs", action="store_true")
    args = parser.parse_args()

    repo_root = Path(args.repo_root) if args.repo_root else None

    try:
        if args.purge_local_face_outputs:
            report = purge_local_face_outputs(repo_root=repo_root)
            _log(f"Local face outputs purged: {report}")

        if args.mode == "quick":
            return _run_quick(args.sd_root, repo_root)
        return _run_full(args.sd_root, repo_root)
    except Exception as exc:
        print(f"ERROR: {type(exc).__name__}: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
