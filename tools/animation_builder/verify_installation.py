from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.environment_state import (  # type: ignore
        FALLBACK_RENDERER_PATH,
        FIXED_COMFY_DIR,
        PORTABLE_PYTHON,
        builder_script_paths,
        checkpoint_files,
        entrypoint_files,
        has_embedded_python,
        sd_mount_exists,
    )
else:
    from .environment_state import (
        FALLBACK_RENDERER_PATH,
        FIXED_COMFY_DIR,
        PORTABLE_PYTHON,
        builder_script_paths,
        checkpoint_files,
        has_embedded_python,
        sd_mount_exists,
    )


def _print_status(prefix: str, message: str) -> None:
    print(f"[{prefix}] {message}")


def _embedded_python_version() -> str:
    if not has_embedded_python():
        return ""

    result = subprocess.run(
        [str(PORTABLE_PYTHON), "-c", "import torch; print(torch.__version__)"] ,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return ""
    return result.stdout.strip()


def _check_custom_nodes() -> list[str]:
    if not has_embedded_python():
        return ["Embedded Python missing; custom-node import check skipped."]

    script = r"""
import importlib.util
import json
from pathlib import Path

root = Path(r"C:\Flic\ComfyUI\custom_nodes")
errors = []

if root.exists():
    for path in sorted(root.rglob("*.py")):
        if path.name.startswith("__"):
            continue
        if path.suffix.lower() != ".py":
            continue
        spec = importlib.util.spec_from_file_location(f"custom_{path.stem}", path)
        if spec is None or spec.loader is None:
            continue
        module = importlib.util.module_from_spec(spec)
        try:
            spec.loader.exec_module(module)
        except Exception as exc:
            errors.append(f"{path.name}: {type(exc).__name__}: {exc}")

print(json.dumps(errors))
"""
    result = subprocess.run([str(PORTABLE_PYTHON), "-c", script], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        return [f"Custom-node import check failed to run: {result.stderr.strip() or 'unknown error'}"]

    try:
        errors = json.loads(result.stdout.strip() or "[]")
    except json.JSONDecodeError:
        return ["Custom-node import check returned invalid JSON."]

    return list(errors)


def main() -> int:
    exit_code = 0

    if FIXED_COMFY_DIR.exists():
        _print_status("OK", f"ComfyUI root found: {FIXED_COMFY_DIR}")
    else:
        _print_status("ERROR", f"ComfyUI root missing: {FIXED_COMFY_DIR}")
        exit_code = 1

    entrypoint_candidates = {
        "run_cpu.bat": [FIXED_COMFY_DIR / "run_cpu.bat", FIXED_COMFY_DIR / "ComfyUI_windows_portable" / "run_cpu.bat"],
        "run_nvidia_gpu.bat": [FIXED_COMFY_DIR / "run_nvidia_gpu.bat", FIXED_COMFY_DIR / "ComfyUI_windows_portable" / "run_nvidia_gpu.bat"],
        "main.py": [FIXED_COMFY_DIR / "main.py"],
    }
    missing_entrypoints = [name for name, candidates in entrypoint_candidates.items() if not any(path.exists() for path in candidates)]
    if missing_entrypoints:
        _print_status("ERROR", f"Missing entrypoints: {', '.join(missing_entrypoints)}")
        exit_code = 1
    else:
        _print_status("OK", "Entry points present: run_cpu.bat, run_nvidia_gpu.bat, main.py")

    if has_embedded_python():
        _print_status("OK", f"Embedded Python found: {PORTABLE_PYTHON}")
        torch_version = _embedded_python_version()
        if torch_version:
            _print_status("OK", f"Torch version: {torch_version}")
        else:
            _print_status("WARN", "Torch version could not be read from embedded Python")
    else:
        _print_status("ERROR", f"Embedded Python missing: {PORTABLE_PYTHON}")
        exit_code = 1

    custom_node_errors = _check_custom_nodes()
    if custom_node_errors and custom_node_errors[0].startswith("Embedded Python missing"):
        _print_status("WARN", custom_node_errors[0])
    elif custom_node_errors:
        for error in custom_node_errors:
            _print_status("WARN", f"Custom node import error: {error}")
    else:
        _print_status("OK", "Custom nodes load without import errors")

    checkpoints = checkpoint_files()
    if checkpoints:
        _print_status("OK", f"Checkpoint detected: {checkpoints[0].name}")
    else:
        _print_status("WARN", "No checkpoint found — fallback only")

    if sd_mount_exists():
        _print_status("OK", "SD card detected — enabling D:\\ export.")
    else:
        _print_status("OK", "SD card not mounted — silent fallback enabled")

    if FALLBACK_RENDERER_PATH.exists():
        _print_status("OK", f"Fallback renderer path exists: {FALLBACK_RENDERER_PATH}")
    else:
        _print_status("ERROR", f"Fallback renderer path missing: {FALLBACK_RENDERER_PATH}")
        exit_code = 1

    missing_scripts = [path.name for path in builder_script_paths() if not path.exists()]
    if missing_scripts:
        _print_status("ERROR", f"Missing animation builder scripts: {', '.join(missing_scripts)}")
        exit_code = 1
    else:
        _print_status("OK", "All animation builder scripts present")

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())