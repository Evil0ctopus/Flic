from __future__ import annotations

import json
import socket
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

from .config import COMFY_API_URL


class ComfyApiError(RuntimeError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


def _request_json(url: str, method: str = "GET", payload: dict[str, Any] | None = None, timeout: float = 30.0) -> dict[str, Any]:
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = urllib.request.Request(url=url, method=method, data=data, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            try:
                return json.loads(raw)
            except json.JSONDecodeError as exc:
                raise ComfyApiError("COMFY_ERROR", f"Invalid JSON response from ComfyUI endpoint: {url}") from exc
    except socket.timeout as exc:
        raise ComfyApiError("COMFY_TIMEOUT", f"ComfyUI request timed out: {url}") from exc
    except urllib.error.URLError as exc:
        if isinstance(exc.reason, socket.timeout):
            raise ComfyApiError("COMFY_TIMEOUT", f"ComfyUI request timed out: {url}") from exc
        raise ComfyApiError(
            "COMFY_DOWN",
            f"Cannot reach ComfyUI at {COMFY_API_URL}. Start ComfyUI and verify port 8188 is open.",
        ) from exc
    except Exception as exc:
        raise ComfyApiError("COMFY_ERROR", f"Unexpected ComfyUI error on {url}: {type(exc).__name__}") from exc


def check_server(api_url: str = COMFY_API_URL, timeout: float = 3.0) -> bool:
    try:
        _request_json(f"{api_url}/system_stats", timeout=timeout)
        return True
    except ComfyApiError:
        return False


def queue_prompt(workflow: dict[str, Any], api_url: str = COMFY_API_URL) -> str:
    response = _request_json(f"{api_url}/prompt", method="POST", payload={"prompt": workflow})
    prompt_id = response.get("prompt_id")
    if not prompt_id:
        raise ComfyApiError("COMFY_ERROR", "ComfyUI response missing prompt_id")
    return str(prompt_id)


def wait_for_result(prompt_id: str, api_url: str = COMFY_API_URL, timeout_seconds: float = 240.0, poll_seconds: float = 1.0) -> dict[str, Any]:
    start = time.time()
    while (time.time() - start) < timeout_seconds:
        history = _request_json(f"{api_url}/history/{prompt_id}")
        if prompt_id in history:
            return history[prompt_id]
        time.sleep(poll_seconds)
    raise ComfyApiError("COMFY_TIMEOUT", f"Timed out waiting for ComfyUI result for prompt {prompt_id}")


def _extract_first_image_meta(history_entry: dict[str, Any]) -> dict[str, Any]:
    outputs = history_entry.get("outputs", {})
    for node_output in outputs.values():
        images = node_output.get("images", [])
        if images:
            return images[0]
    raise ComfyApiError("COMFY_ERROR", "ComfyUI history has no image outputs")


def _download_image(image_meta: dict[str, Any], api_url: str = COMFY_API_URL) -> bytes:
    query = urllib.parse.urlencode(
        {
            "filename": image_meta.get("filename", ""),
            "subfolder": image_meta.get("subfolder", ""),
            "type": image_meta.get("type", "output"),
        }
    )
    url = f"{api_url}/view?{query}"
    try:
        with urllib.request.urlopen(url, timeout=60) as resp:
            return resp.read()
    except socket.timeout as exc:
        raise ComfyApiError("COMFY_TIMEOUT", "Timed out downloading generated image from ComfyUI") from exc
    except urllib.error.URLError as exc:
        raise ComfyApiError("COMFY_DOWN", "ComfyUI became unreachable while downloading output image") from exc
    except Exception as exc:
        raise ComfyApiError("COMFY_ERROR", f"Unexpected image download error: {type(exc).__name__}") from exc


def run_workflow(workflow: dict[str, Any], api_url: str = COMFY_API_URL, timeout_seconds: float = 240.0) -> bytes:
    prompt_id = queue_prompt(workflow=workflow, api_url=api_url)
    history_entry = wait_for_result(prompt_id=prompt_id, api_url=api_url, timeout_seconds=timeout_seconds)
    image_meta = _extract_first_image_meta(history_entry)
    return _download_image(image_meta=image_meta, api_url=api_url)
