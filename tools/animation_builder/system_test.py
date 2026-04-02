from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.comfy_api import ComfyApiError  # type: ignore
    from tools.animation_builder.comfy_healthcheck import healthcheck  # type: ignore
    from tools.animation_builder.config import BUILD_ROOT_PATH, COMFY_API_URL, IMAGE_SIZE, MAX_PNG_SIZE_BYTES  # type: ignore
    from tools.animation_builder.generate_animation import generate_animation  # type: ignore
else:
    from .comfy_api import ComfyApiError
    from .comfy_healthcheck import healthcheck
    from .config import BUILD_ROOT_PATH, COMFY_API_URL, IMAGE_SIZE, MAX_PNG_SIZE_BYTES
    from .generate_animation import generate_animation


PNG_SIG = b"\x89PNG\r\n\x1a\n"


def _png_dimensions(path: Path) -> tuple[int, int]:
    data = path.read_bytes()[:64]
    if len(data) < 33 or not data.startswith(PNG_SIG) or data[12:16] != b"IHDR":
        raise ValueError(f"Invalid PNG header: {path}")
    width = struct.unpack(">I", data[16:20])[0]
    height = struct.unpack(">I", data[20:24])[0]
    return width, height


def main() -> None:
    parser = argparse.ArgumentParser(description="End-to-end animation builder test")
    parser.add_argument("--api", default=COMFY_API_URL)
    parser.add_argument("--seed", type=int, default=12345)
    parser.add_argument("--animation", default="blink")
    args = parser.parse_args()

    try:
        if not healthcheck(api_url=args.api, attempts=1, delay_seconds=0):
            print("SYSTEM_FAIL")
            print("Reason: COMFY_DOWN")
            raise SystemExit(1)

        out = generate_animation(args.animation, api_url=args.api, seed=args.seed)

        pngs = sorted(out.glob("*.png"))
        if not pngs:
            raise ValueError(f"No PNG frames generated in {out}")

        for png in pngs:
            if png.stat().st_size > MAX_PNG_SIZE_BYTES:
                raise ValueError(f"PNG too large: {png.name}")
            if _png_dimensions(png) != IMAGE_SIZE:
                raise ValueError(f"PNG dimensions mismatch: {png.name}")

        print("SYSTEM_OK")
        print(f"Output: {out}")
        print(f"Build root: {BUILD_ROOT_PATH}")
    except ComfyApiError as exc:
        print("SYSTEM_FAIL")
        print(f"Reason: {exc.code}: {exc.message}")
        raise SystemExit(2)
    except Exception as exc:
        print("SYSTEM_FAIL")
        print(f"Reason: {type(exc).__name__}: {exc}")
        raise SystemExit(3)


if __name__ == "__main__":
    main()
