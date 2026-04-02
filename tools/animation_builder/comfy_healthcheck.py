from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.comfy_api import check_server  # type: ignore
    from tools.animation_builder.config import COMFY_API_URL  # type: ignore
else:
    from .comfy_api import check_server
    from .config import COMFY_API_URL


def healthcheck(api_url: str = COMFY_API_URL, attempts: int = 1, delay_seconds: float = 1.0) -> bool:
    for _ in range(max(1, attempts)):
        if check_server(api_url=api_url):
            return True
        time.sleep(max(0.0, delay_seconds))
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description="ComfyUI health check")
    parser.add_argument("--api", default=COMFY_API_URL, help="ComfyUI API URL")
    parser.add_argument("--attempts", type=int, default=1)
    parser.add_argument("--delay", type=float, default=1.0)
    args = parser.parse_args()

    ok = healthcheck(api_url=args.api, attempts=args.attempts, delay_seconds=args.delay)
    if ok:
        print("COMFY_READY")
        raise SystemExit(0)

    print("COMFY_DOWN")
    raise SystemExit(1)


if __name__ == "__main__":
    main()
