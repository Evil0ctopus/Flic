from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.comfy_api import ComfyApiError, check_server  # type: ignore
    from tools.animation_builder.config import COMFY_API_URL  # type: ignore
    from tools.animation_builder.environment_state import has_checkpoint  # type: ignore
    from tools.animation_builder.generate_animation import generate_all, generate_animation  # type: ignore
else:
    from .comfy_api import ComfyApiError, check_server
    from .config import COMFY_API_URL
    from .environment_state import has_checkpoint
    from .generate_animation import generate_all, generate_animation


def main() -> None:
    parser = argparse.ArgumentParser(description="Wait for ComfyUI then generate Flic animations")
    parser.add_argument("--animation", default="blink", help="animation name to generate")
    parser.add_argument("--all", action="store_true", help="generate all animation specs")
    parser.add_argument("--api", default=COMFY_API_URL)
    parser.add_argument("--poll", type=float, default=2.0)
    parser.add_argument("--max-wait", type=float, default=0.0, help="seconds to wait (0 = wait forever)")
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--strict-comfy", action="store_true", help="fail if Comfy workflow/custom nodes are unavailable")
    args = parser.parse_args()

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

    checkpoint_present = has_checkpoint()
    if not checkpoint_present:
        print("No checkpoint found — running node-only fallback pipeline.")
        if args.all:
            paths = generate_all(api_url=args.api, seed=args.seed, strict_comfy=args.strict_comfy, force_local_renderer=True)
            for p in paths:
                print(f"Generated: {p}")
        else:
            p = generate_animation(
                args.animation,
                api_url=args.api,
                seed=args.seed,
                strict_comfy=args.strict_comfy,
                force_local_renderer=True,
            )
            print(f"Generated: {p}")
        return

    print("Checkpoint detected — enabling model-based generation.")

    if not check_server(api_url=args.api):
        _start_comfy_background()

    print(f"Waiting for ComfyUI at {args.api}...")
    start = time.time()
    comfy_ready = False
    while not check_server(api_url=args.api):
        if args.max_wait > 0 and (time.time() - start) >= args.max_wait:
            if args.strict_comfy:
                print("COMFY_DOWN")
                raise SystemExit(1)
            break
        time.sleep(max(0.5, args.poll))
    else:
        comfy_ready = True

    if comfy_ready:
        print("COMFY_READY")
        print("ComfyUI reachable. Starting generation...")
    else:
        print("ComfyUI did not become ready in time. Starting fallback-capable generation...")
    try:
        if args.all:
            paths = generate_all(api_url=args.api, seed=args.seed, strict_comfy=args.strict_comfy)
            for p in paths:
                print(f"Generated: {p}")
        else:
            p = generate_animation(
                args.animation,
                api_url=args.api,
                seed=args.seed,
                strict_comfy=args.strict_comfy,
            )
            print(f"Generated: {p}")
    except ComfyApiError as exc:
        print(f"{exc.code}: {exc.message}")
        raise SystemExit(2)
    except Exception as exc:
        print(f"GENERATION_FAILED: {type(exc).__name__}: {exc}")
        raise SystemExit(3)


if __name__ == "__main__":
    main()
