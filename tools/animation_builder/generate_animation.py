from __future__ import annotations

import argparse
import copy
import json
import random
import shutil
import sys
import time
from io import BytesIO
from pathlib import Path
from typing import Any

from PIL import Image, ImageEnhance

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.comfy_api import ComfyApiError, check_server, run_workflow  # type: ignore
    from tools.animation_builder.compress_pngs import compress_folder_pngs  # type: ignore
    from tools.animation_builder.config import (  # type: ignore
        COMFY_API_URL,
        WORKFLOW_TEMPLATE_PATH,
    )
    from tools.animation_builder.eyelid_morph import openness_to_params  # type: ignore
    from tools.animation_builder.emotion_suite import (  # type: ignore
        DEFAULT_NEGATIVE_PROMPT,
        DEFAULT_STYLE_PROMPT,
        EMOTION_SUITE,
        OUTPUT_ROOT,
        build_negative_prompt,
        build_style_prompt,
        clean_output_root,
        load_emotion_spec,
        render_fallback_frame,
        suite_output_folder,
    )
    from tools.animation_builder.environment_state import default_checkpoint_name, has_primary_checkpoint  # type: ignore
    from tools.animation_builder.face_compositor import assemble_emotion_final_frames  # type: ignore
    from tools.animation_builder.mirror_utils import mirror_frame  # type: ignore
    from tools.animation_builder.frame_interpolator import interpolate_sequence  # type: ignore
    from tools.animation_builder.glow_engine import frame_glow_modifier  # type: ignore
    from tools.animation_builder.validate_pngs import validate_folder_pngs  # type: ignore
else:
    from .comfy_api import ComfyApiError, check_server, run_workflow
    from .compress_pngs import compress_folder_pngs
    from .config import (
        COMFY_API_URL,
        WORKFLOW_TEMPLATE_PATH,
    )
    from .eyelid_morph import openness_to_params
    from .emotion_suite import (
        DEFAULT_NEGATIVE_PROMPT,
        DEFAULT_STYLE_PROMPT,
        EMOTION_SUITE,
        OUTPUT_ROOT,
        build_negative_prompt,
        build_style_prompt,
        clean_output_root,
        load_emotion_spec,
        render_fallback_frame,
        suite_output_folder,
    )
    from .environment_state import default_checkpoint_name, has_primary_checkpoint
    from .face_compositor import assemble_emotion_final_frames
    from .mirror_utils import mirror_frame
    from .frame_interpolator import interpolate_sequence
    from .glow_engine import frame_glow_modifier
    from .validate_pngs import validate_folder_pngs


def _load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _ensure_rgba_png(png_bytes: bytes) -> bytes:
    with Image.open(BytesIO(png_bytes)) as image:
        rgba = image.convert("RGBA")
        buffer = BytesIO()
        rgba.save(buffer, format="PNG", optimize=True)
        return buffer.getvalue()


def _write_eye_frames(frame_path: Path, png_bytes: bytes) -> None:
    frame_path.write_bytes(png_bytes)
    right_path = frame_path.parent.parent / "right" / frame_path.name
    with Image.open(BytesIO(png_bytes)) as image:
        mirrored = mirror_frame(image.convert("RGBA"))
        out = BytesIO()
        mirrored.save(out, format="PNG", optimize=True)
        right_path.write_bytes(out.getvalue())


def _postprocess_model_frame(png_bytes: bytes, open_value: float, glow_scale: float) -> bytes:
    with Image.open(BytesIO(png_bytes)) as image:
        base = image.convert("RGBA")

        closedness = max(0.0, min(1.0, 1.0 - float(open_value)))
        if closedness > 0:
            overlay = Image.new("RGBA", base.size, (0, 0, 0, 0))
            band_h = int(base.height * 0.26 * closedness)
            if band_h > 0:
                for y in range(band_h):
                    alpha = int(180 * (1.0 - (y / max(band_h, 1))))
                    for x in range(base.width):
                        overlay.putpixel((x, y), (0, 0, 0, alpha))
                        overlay.putpixel((x, base.height - 1 - y), (0, 0, 0, alpha))
                base = Image.alpha_composite(base, overlay)

        glow = max(0.75, min(1.35, float(glow_scale)))
        if abs(glow - 1.0) > 1e-3:
            base = ImageEnhance.Brightness(base).enhance(glow)

        buffer = BytesIO()
        base.save(buffer, format="PNG", optimize=True)
        return buffer.getvalue()


def _find_node_by_label(workflow: dict[str, Any], label: str) -> dict[str, Any]:
    for node in workflow.values():
        title = (node.get("_meta", {}) or {}).get("title") or node.get("label")
        if str(title) == label:
            return node
    raise KeyError(f"Workflow node with label '{label}' not found")


def _inject_frame_params(workflow: dict[str, Any], animation_name: str, frame_idx: int, total_frames: int, open_value: float, seed: int) -> None:
    eyelid_strength, iris_scale, pupil_visibility = openness_to_params(open_value)
    spec = load_emotion_spec(animation_name)

    style_node = _find_node_by_label(workflow, "STYLE_PROMPT")
    neg_node = _find_node_by_label(workflow, "NEG_PROMPT")
    sampler_node = _find_node_by_label(workflow, "SAMPLER")
    checkpoint_node = _find_node_by_label(workflow, "CHECKPOINT")

    style_text = build_style_prompt(animation_name, spec, frame_idx, total_frames, open_value)

    style_node.setdefault("inputs", {})["text"] = style_text
    neg_node.setdefault("inputs", {})["text"] = build_negative_prompt(spec, emotion_name=animation_name)

    sampler_inputs = sampler_node.setdefault("inputs", {})
    sampler_inputs["seed"] = int(seed)

    # Fold the frame state into prompt hints so each frame remains distinct.
    style_node.setdefault("inputs", {})["text"] += (
        f", eyelid_strength={eyelid_strength:.3f}, iris_scale={iris_scale:.3f}, "
        f"pupil_visibility={pupil_visibility:.3f}, glow={float(spec.get('glow_bias', 1.0)):.3f}"
    )

    checkpoint_name = default_checkpoint_name()
    if checkpoint_name is None:
        raise ComfyApiError("COMFY_DOWN", f"No checkpoint file found in C:/Flic/ComfyUI/models/checkpoints")
    checkpoint_node.setdefault("inputs", {})["ckpt_name"] = checkpoint_name


def generate_animation(
    animation_name: str,
    api_url: str = COMFY_API_URL,
    seed: int | None = None,
    strict_comfy: bool = False,
    force_local_renderer: bool = False,
) -> Path:
    spec = load_emotion_spec(animation_name)
    workflow_template = _load_json(WORKFLOW_TEMPLATE_PATH)

    open_values = list(spec.get("open_values", []))
    target_frames = int(spec.get("frame_count", len(open_values)))
    if open_values and target_frames > 0 and len(open_values) != target_frames:
        open_values = interpolate_sequence(open_values, target_frames)
    if not open_values:
        raise ValueError(f"Spec '{animation_name}' has no open_values")

    base_seed = int(seed if seed is not None else spec.get("seed", random.randint(1, 2**31 - 1)))
    out_folder = suite_output_folder(animation_name)
    if out_folder.exists():
        for stale in list(out_folder.iterdir()):
            if stale.is_dir():
                shutil.rmtree(stale)
            else:
                stale.unlink()
    left_folder = out_folder / "left"
    right_folder = out_folder / "right"
    left_folder.mkdir(parents=True, exist_ok=True)
    right_folder.mkdir(parents=True, exist_ok=True)

    use_model = has_primary_checkpoint() and not force_local_renderer

    if not use_model:
        total = len(open_values)
        for idx, open_value in enumerate(open_values):
            frame_path = left_folder / f"frame_{idx:03d}.png"
            png_bytes = render_fallback_frame(animation_name, spec, idx, total, float(open_value))
            _write_eye_frames(frame_path, png_bytes)
    else:
        comfy_available = check_server(api_url=api_url)
        if not comfy_available and strict_comfy:
            raise ComfyApiError(
                "COMFY_DOWN",
                f"ComfyUI is not reachable at {api_url}. Start ComfyUI, then retry generation.",
            )

        total = len(open_values)
        try:
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

                timing = spec.get("timing", {}) if isinstance(spec.get("timing"), dict) else {}
                glow_curve = timing.get("glow_curve") if isinstance(timing.get("glow_curve"), list) else []
                if glow_curve and len(glow_curve) != total:
                    glow_curve = interpolate_sequence(glow_curve, total)
                glow_value = float(glow_curve[idx]) if glow_curve else frame_glow_modifier(idx, total)

                png_bytes = _ensure_rgba_png(run_workflow(workflow=workflow, api_url=api_url))
                png_bytes = _postprocess_model_frame(png_bytes, float(open_value), glow_value)
                frame_path = left_folder / f"frame_{idx:03d}.png"
                _write_eye_frames(frame_path, png_bytes)
        except ComfyApiError as exc:
            if strict_comfy:
                raise
            total = len(open_values)
            for idx, open_value in enumerate(open_values):
                frame_path = left_folder / f"frame_{idx:03d}.png"
                png_bytes = render_fallback_frame(animation_name, spec, idx, total, float(open_value))
                _write_eye_frames(frame_path, png_bytes)

    left_frames = sorted(left_folder.glob("frame_*.png"))
    right_frames = sorted(right_folder.glob("frame_*.png"))
    if len(left_frames) != len(right_frames):
        raise ValueError(f"Mismatched left/right frame counts for {animation_name}: {len(left_frames)} vs {len(right_frames)}")

    compress_folder_pngs(left_folder)
    compress_folder_pngs(right_folder)
    validate_folder_pngs(left_folder)
    validate_folder_pngs(right_folder)

    final_folder = assemble_emotion_final_frames(animation_name, spec, out_folder)
    compress_folder_pngs(final_folder)
    validate_folder_pngs(final_folder)

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


def generate_all(
    api_url: str = COMFY_API_URL,
    seed: int | None = None,
    strict_comfy: bool = False,
    force_local_renderer: bool = False,
) -> list[Path]:
    outputs: list[Path] = []
    for emotion_name in EMOTION_SUITE:
        outputs.append(
            generate_animation(
                emotion_name,
                api_url=api_url,
                seed=seed,
                strict_comfy=strict_comfy,
                force_local_renderer=force_local_renderer,
            )
        )
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
    parser.add_argument("--strict-comfy", action="store_true", help="fail instead of using local fallback")
    args = parser.parse_args()

    if not wait_for_comfy(api_url=args.api, timeout_seconds=args.wait_for_comfy):
        if args.strict_comfy:
            raise ComfyApiError(
                "COMFY_DOWN",
                f"ComfyUI is not reachable at {args.api}. Start ComfyUI, then retry generation.",
            )

    if args.animation == "all":
        clean_output_root()

    if args.animation == "all":
        paths = generate_all(api_url=args.api, seed=args.seed, strict_comfy=args.strict_comfy)
        for p in paths:
            print(f"Generated: {p}")
    else:
        p = generate_animation(args.animation, api_url=args.api, seed=args.seed, strict_comfy=args.strict_comfy)
        print(f"Generated: {p}")


if __name__ == "__main__":
    main()
