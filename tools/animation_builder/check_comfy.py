from __future__ import annotations

from .comfy_api import check_server
from .config import COMFY_API_URL


if __name__ == "__main__":
    ok = check_server(COMFY_API_URL)
    print("COMFY_OK" if ok else "COMFY_DOWN")
