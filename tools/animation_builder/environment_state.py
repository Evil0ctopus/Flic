from __future__ import annotations

from pathlib import Path

FIXED_COMFY_DIR = Path("C:/Flic/ComfyUI")
CHECKPOINTS_DIR = FIXED_COMFY_DIR / "models" / "checkpoints"
PORTABLE_CHECKPOINTS_DIR = FIXED_COMFY_DIR / "ComfyUI_windows_portable" / "ComfyUI" / "models" / "checkpoints"
PORTABLE_PYTHON = FIXED_COMFY_DIR / "ComfyUI_windows_portable" / "python_embeded" / "python.exe"
SD_EXPORT_ROOT = Path("D:/Flic/animations/face/default")
SD_FALLBACK_ROOT = Path("build/animations/sd_export")
FALLBACK_RENDERER_PATH = Path(__file__).resolve().parent / "generate_animation.py"
FIRST_TIME_STATE_PATH = Path(__file__).resolve().parent / ".first_time_complete"


def checkpoint_files() -> list[Path]:
    files: list[Path] = []
    for directory in (CHECKPOINTS_DIR, PORTABLE_CHECKPOINTS_DIR):
        if not directory.exists():
            continue
        for path in directory.iterdir():
            if path.is_file() and path.suffix.lower() in {".safetensors", ".ckpt"}:
                files.append(path)
    return sorted(files)


def primary_checkpoint_files() -> list[Path]:
    return checkpoint_files()


def default_checkpoint_name() -> str | None:
    files = primary_checkpoint_files()
    if not files:
        return None
    return files[0].name


def has_primary_checkpoint() -> bool:
    return len(primary_checkpoint_files()) > 0


def has_checkpoint() -> bool:
    return len(checkpoint_files()) > 0


def entrypoint_files() -> list[Path]:
    return [
        FIXED_COMFY_DIR / "run_cpu.bat",
        FIXED_COMFY_DIR / "run_nvidia_gpu.bat",
        FIXED_COMFY_DIR / "ComfyUI_windows_portable" / "run_cpu.bat",
        FIXED_COMFY_DIR / "ComfyUI_windows_portable" / "run_nvidia_gpu.bat",
        FIXED_COMFY_DIR / "main.py",
    ]


def has_embedded_python() -> bool:
    return PORTABLE_PYTHON.exists()


def sd_mount_exists() -> bool:
    return Path("D:/").exists()


def sd_export_target(animation_name: str) -> Path:
    if sd_mount_exists():
        return SD_EXPORT_ROOT / animation_name
    return SD_FALLBACK_ROOT / animation_name


def builder_script_paths() -> list[Path]:
    root = Path(__file__).resolve().parent
    return [
        root / "vscode_start_comfy.py",
        root / "run_when_comfy.py",
        root / "generate_animation.py",
        root / "verify_installation.py",
        root / "first_time_setup.py",
        root / "export_to_sd.py",
    ]
