from __future__ import annotations

import argparse
import copy
import json
import random
import sys
import time
from pathlib import Path
from typing import Any

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.comfy_api import ComfyApiError, check_server, run_workflow  # type: ignore
    from tools.animation_builder.compress_pngs import compress_folder_pngs  # type: ignore
    from tools.animation_builder.config import (  # type: ignore
        BUILD_ROOT_PATH,
        COMFY_API_URL,
        DEFAULT_NEGATIVE_PROMPT,
        DEFAULT_STYLE_PROMPT,
        SPECS_ROOT,
        WORKFLOW_TEMPLATE_PATH,
    )
    from tools.animation_builder.eyelid_morph import openness_to_params  # type: ignore
    from tools.animation_builder.export_to_sd import export_animation_to_sd  # type: ignore
    from tools.animation_builder.glow_engine import frame_glow_modifier  # type: ignore
    from tools.animation_builder.mirror_frames import mirror_animation_frames  # type: ignore
    from tools.animation_builder.validate_pngs import validate_folder_pngs  # type: ignore
else:
    from .comfy_api import ComfyApiError, check_server, run_workflow
    from .compress_pngs import compress_folder_pngs
    from .config import (
        BUILD_ROOT_PATH,
        COMFY_API_URL,
        DEFAULT_NEGATIVE_PROMPT,
        DEFAULT_STYLE_PROMPT,
        SPECS_ROOT,
        WORKFLOW_TEMPLATE_PATH,
    )
    from .eyelid_morph import openness_to_params
    from .export_to_sd import export_animation_to_sd
    from .glow_engine import frame_glow_modifier
    from .mirror_frames import mirror_animation_frames
    from .validate_pngs import validate_folder_pngs


def _load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _find_node_by_label(workflow: dict[str, Any], label: str) -> dict[str, Any]:
    for node in workflow.values():
        title = (node.get("_meta", {}) or {}).get("title") or node.get("label")
        if str(title) == label:
            return node
    raise KeyError(f"Workflow node with label '{label}' not found")


def _inject_frame_params(workflow: dict[str, Any], animation_name: str, frame_idx: int, total_frames: int, open_value: float, seed: int) -> None:
    eyelid_strength, iris_scale, pupil_visibility = openness_to_params(open_value)
    glow = frame_glow_modifier(frame_idx, total_frames)

    style_node = _find_node_by_label(workflow, "STYLE_PROMPT")
    neg_node = _find_node_by_label(workflow, "NEG_PROMPT")
    eyelid_node = _find_node_by_label(workflow, "EYELID_NODE")
    seed_node = _find_node_by_label(workflow, "SEED")

    style_text = (
        f"{DEFAULT_STYLE_PROMPT}, animation={animation_name}, "
        f"frame={frame_idx}, eyelid_strength={eyelid_strength:.3f}, "
        f"iris_scale={iris_scale:.3f}, pupil_visibility={pupil_visibility:.3f}, "
        f"glow={glow:.3f}"
    )

    style_node.setdefault("inputs", {})["text"] = style_text
    neg_node.setdefault("inputs", {})["text"] = DEFAULT_NEGATIVE_PROMPT

    eyelid_inputs = eyelid_node.setdefault("inputs", {})
    eyelid_inputs["eyelid_strength"] = float(eyelid_strength)
    eyelid_inputs["iris_scale"] = float(iris_scale)
    eyelid_inputs["pupil_visibility"] = float(pupil_visibility)
    eyelid_inputs["glow"] = float(glow)

    seed_node.setdefault("inputs", {})["seed"] = int(seed)


def generate_animation(animation_name: str, api_url: str = COMFY_API_URL, seed: int | None = None) -> Path:
    if not check_server(api_url=api_url):
        raise ComfyApiError(
            "COMFY_DOWN",
            f"ComfyUI is not reachable at {api_url}. Start ComfyUI, then retry generation.",
        )

    spec_path = SPECS_ROOT / f"{animation_name}.json"
    if not spec_path.exists():
        raise FileNotFoundError(f"Spec not found: {spec_path}")

    spec = _load_json(spec_path)
    workflow_template = _load_json(WORKFLOW_TEMPLATE_PATH)

    open_values = spec.get("open_values", [])
    if not open_values:
        raise ValueError(f"Spec '{animation_name}' has no open_values")

    base_seed = int(seed if seed is not None else spec.get("seed", random.randint(1, 2**31 - 1)))
    out_folder = BUILD_ROOT_PATH / animation_name
    out_folder.mkdir(parents=True, exist_ok=True)

    total = len(open_values)
    for idx, open_value in enumerate(open_values):
        workflow = copy.deepcopy(workflow_template)
        _inject_frame_params(
            workflow=workflow,
            animation_name=animation_name,
            frame_idx=idx,
            total_frames=total,
            open_value=float(open_value),
            seed=base_seed + idx,
        )

        png_bytes = run_workflow(workflow=workflow, api_url=api_url)
        frame_path = out_folder / f"{idx:03d}.png"
        frame_path.write_bytes(png_bytes)

    compress_folder_pngs(out_folder)
    validate_folder_pngs(out_folder)
    right_folder = mirror_animation_frames(out_folder)

    try:
        export_animation_to_sd(animation_name, out_folder)
        export_animation_to_sd(f"{animation_name}_right", right_folder)
    except OSError as exc:
        print(f"WARN: SD export skipped for '{animation_name}': {exc}")

    return out_folder


def wait_for_comfy(api_url: str, timeout_seconds: float, poll_seconds: float = 2.0) -> bool:
    if timeout_seconds <= 0:
        return check_server(api_url=api_url)

    start = time.time()
    while (time.time() - start) < timeout_seconds:
        if check_server(api_url=api_url):
            return True
        time.sleep(poll_seconds)
    return False


def generate_all(api_url: str = COMFY_API_URL, seed: int | None = None) -> list[Path]:
    outputs: list[Path] = []
    for spec_file in sorted(SPECS_ROOT.glob("*.json")):
        outputs.append(generate_animation(spec_file.stem, api_url=api_url, seed=seed))
    return outputs


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate Flic animation frames with ComfyUI Flux")
    parser.add_argument("animation", nargs="?", default="all", help="animation name or 'all'")
    parser.add_argument("--api", default=COMFY_API_URL, help="ComfyUI API base URL")
    parser.add_argument("--seed", type=int, default=None, help="optional deterministic base seed")
    parser.add_argument(
        "--wait-for-comfy",
        type=float,
        default=0,
        help="seconds to wait for ComfyUI before failing (0 = no wait)",
    )
    args = parser.parse_args()

    if not wait_for_comfy(api_url=args.api, timeout_seconds=args.wait_for_comfy):
        raise ComfyApiError(
            "COMFY_DOWN",
            f"ComfyUI is not reachable at {args.api}. Start ComfyUI, then retry generation.",
        )

    if args.animation == "all":
        paths = generate_all(api_url=args.api, seed=args.seed)
        for p in paths:
            print(f"Generated: {p}")
    else:
        p = generate_animation(args.animation, api_url=args.api, seed=args.seed)
        print(f"Generated: {p}")


if __name__ == "__main__":
    main()
