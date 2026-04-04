from __future__ import annotations

import subprocess
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.environment_state import has_checkpoint  # type: ignore
else:
    from .environment_state import has_checkpoint

FIXED_COMFY_DIR = Path("C:/Flic/ComfyUI")


def _looks_like_comfy_root(folder: Path) -> bool:
    return (
        (folder / "run_nvidia_gpu.bat").exists()
        or (folder / "run_cpu.bat").exists()
        or (folder / "ComfyUI_windows_portable" / "run_nvidia_gpu.bat").exists()
        or (folder / "ComfyUI_windows_portable" / "run_cpu.bat").exists()
        or (folder / "main.py").exists()
    )


def main() -> int:
    comfy_root = FIXED_COMFY_DIR

    if has_checkpoint():
        print("Checkpoint detected — enabling model-based generation.")
    else:
        print("No checkpoint found — running node-only fallback pipeline.")

    if not comfy_root.exists() or not comfy_root.is_dir():
        print(f"[ComfyUI] ERROR: required folder not found: {comfy_root}")
        print("[ComfyUI] Install ComfyUI at C:/Flic/ComfyUI and rerun the task.")
        return 2

    if not _looks_like_comfy_root(comfy_root):
        print(f"[ComfyUI] ERROR: no startup entrypoint in {comfy_root}")
        print("[ComfyUI] Expected run_nvidia_gpu.bat, run_cpu.bat, or main.py")
        return 3

    nvidia_bat = comfy_root / "run_nvidia_gpu.bat"
    cpu_bat = comfy_root / "run_cpu.bat"
    portable_nvidia_bat = comfy_root / "ComfyUI_windows_portable" / "run_nvidia_gpu.bat"
    portable_cpu_bat = comfy_root / "ComfyUI_windows_portable" / "run_cpu.bat"
    main_py = comfy_root / "main.py"

    print(f"[ComfyUI] Using folder: {comfy_root}")

    if nvidia_bat.exists():
        print("[ComfyUI] Launch mode: standalone NVIDIA (run_nvidia_gpu.bat)")
        return subprocess.call(["cmd", "/c", str(nvidia_bat)], cwd=str(comfy_root))

    if portable_nvidia_bat.exists():
        print("[ComfyUI] Launch mode: portable NVIDIA (ComfyUI_windows_portable/run_nvidia_gpu.bat)")
        return subprocess.call(["cmd", "/c", str(portable_nvidia_bat)], cwd=str(portable_nvidia_bat.parent))

    if cpu_bat.exists():
        print("[ComfyUI] Launch mode: standalone CPU (run_cpu.bat)")
        return subprocess.call(["cmd", "/c", str(cpu_bat)], cwd=str(comfy_root))

    if portable_cpu_bat.exists():
        print("[ComfyUI] Launch mode: portable CPU (ComfyUI_windows_portable/run_cpu.bat)")
        return subprocess.call(["cmd", "/c", str(portable_cpu_bat)], cwd=str(portable_cpu_bat.parent))

    if main_py.exists():
        print("[ComfyUI] Launch mode: Python fallback CPU (main.py --cpu)")
        return subprocess.call(
            [
                sys.executable,
                "main.py",
                "--listen",
                "127.0.0.1",
                "--port",
                "8188",
                "--cpu",
            ],
            cwd=str(comfy_root),
        )

    print("[ComfyUI] No startup entrypoint found (run_nvidia_gpu.bat/run_cpu.bat/main.py).")
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
