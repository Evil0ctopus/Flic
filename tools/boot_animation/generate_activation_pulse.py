from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import shutil
import subprocess
import sys
import time
from io import BytesIO
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw

if __package__ in (None, ""):
    sys.path.append(str(Path(__file__).resolve().parents[2]))
    from tools.animation_builder.comfy_api import ComfyApiError, check_server, run_workflow  # type: ignore
    from tools.animation_builder.config import COMFY_API_URL  # type: ignore
    from tools.boot_animation.sd_boot_utils import detect_sd_card, ensure_boot_folder, purge_boot_animation  # type: ignore
else:
    from ..animation_builder.comfy_api import ComfyApiError, check_server, run_workflow
    from ..animation_builder.config import COMFY_API_URL
    from .sd_boot_utils import detect_sd_card, ensure_boot_folder, purge_boot_animation

WORKFLOW_PATH = Path("tools/boot_animation/comfyui_activation_pulse.json")
OUTPUT_DIR = Path("output/boot")
FRAME_SIZE = (320, 240)
FRAME_COUNT_DEFAULT = 76
SEED_DEFAULT = 424242
CHECKPOINT_DIR_CANDIDATES = [
    Path("C:/Flic/ComfyUI/models/checkpoints"),
    Path("C:/Flic/ComfyUI/ComfyUI_windows_portable/ComfyUI/models/checkpoints"),
]

POWER_CYAN = (76, 195, 255)
ICE_BLUE = (174, 232, 255)
BRIGHT_CYAN = (130, 220, 255)


def _find_node_by_title(workflow: dict[str, Any], title: str) -> dict[str, Any]:
    for node in workflow.values():
        if not isinstance(node, dict):
            continue
        meta = node.get("_meta", {})
        if isinstance(meta, dict) and meta.get("title") == title:
            return node
    raise KeyError(f"Workflow node with title '{title}' was not found")


def _load_workflow_spec(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError("Workflow file must be a JSON object")
    if "prompt_template" not in payload or not isinstance(payload["prompt_template"], dict):
        raise ValueError("Workflow missing prompt_template object")

    frame_size = payload.get("frame_size")
    if frame_size != [FRAME_SIZE[0], FRAME_SIZE[1]]:
        raise ValueError(f"Workflow frame_size must be {[FRAME_SIZE[0], FRAME_SIZE[1]]}")

    frame_count = int(payload.get("frame_count", FRAME_COUNT_DEFAULT))
    if frame_count < 70 or frame_count > 90:
        raise ValueError(f"Frame count out of contract range (70-90): {frame_count}")

    return payload


def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def _clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def _smoothstep(t: float) -> float:
    t = _clamp(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _stage_progress(frame_index: int, start: int, end: int) -> float:
    if frame_index <= start:
        return 0.0
    if frame_index >= end:
        return 1.0
    span = max(1, end - start)
    return (frame_index - start) / float(span)


def _discover_checkpoint_name() -> str | None:
    checkpoints: list[Path] = []
    for directory in CHECKPOINT_DIR_CANDIDATES:
        if not directory.exists():
            continue
        checkpoints.extend(
            path
            for path in directory.iterdir()
            if path.is_file() and path.suffix.lower() in {".safetensors", ".ckpt"}
        )
    checkpoints.sort()
    if not checkpoints:
        return None
    return checkpoints[0].name


def _build_frame_prompt(frame_index: int, total_frames: int) -> str:
    if frame_index <= 10:
        stage = (
            "pure black background, centered classic power icon, 140px circle diameter, 8px stroke, "
            "12px top gap, 8px vertical stem 40px height, soft cyan #4CC3FF glow radius 12-16"
        )
    elif frame_index <= 25:
        stage = (
            "button press inward by 3-4px, main ripple plus faint echo, ripple thickness 6px, "
            "ripple radius 0-200px, pure black background"
        )
    elif frame_index <= 45:
        stage = (
            "glow intensification cyan to bright cyan to white, glow radius 16-60, 1px chromatic aberration, soft bloom"
        )
    elif frame_index <= 60:
        stage = "radial expansion, bright white center with soft cyan edges, by frame 60 full-screen white"
    else:
        stage = "white to black fade with centered idle face reveal and soft cyan glow radius 12-20"

    return (
        "Activation Pulse boot animation frame, deterministic, strict geometry, no random objects, no particles, "
        f"stage: {stage}, frame {frame_index + 1} of {total_frames}"
    )


def _mix_rgb(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    t = _clamp(t, 0.0, 1.0)
    return (
        int(round(_lerp(a[0], b[0], t))),
        int(round(_lerp(a[1], b[1], t))),
        int(round(_lerp(a[2], b[2], t))),
    )


def _draw_ring(draw: ImageDraw.ImageDraw, cx: int, cy: int, radius: int, stroke: int, color: tuple[int, int, int]) -> None:
    for w in range(stroke):
        r = radius - w
        if r <= 0:
            continue
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=color, width=1)


def _draw_power_icon(
    draw: ImageDraw.ImageDraw,
    cx: int,
    cy: int,
    radius: int,
    stroke: int,
    stem_width: int,
    stem_height: int,
    top_gap_px: int,
    color: tuple[int, int, int],
) -> None:
    _draw_ring(draw, cx, cy, radius, stroke, color)

    # Create a top opening in the circle ring.
    gap_half = top_gap_px // 2
    gap_top = cy - radius - 2
    gap_bottom = gap_top + stroke + 4
    draw.rectangle((cx - gap_half, gap_top, cx + gap_half, gap_bottom), fill=(0, 0, 0))

    stem_top = cy - radius + top_gap_px
    stem_bottom = stem_top + stem_height
    stem_half = stem_width // 2
    draw.rectangle((cx - stem_half, stem_top, cx + stem_half, stem_bottom), fill=color)


def _draw_icon_glow(
    draw: ImageDraw.ImageDraw,
    cx: int,
    cy: int,
    radius: int,
    stem_height: int,
    glow_radius: int,
    color_a: tuple[int, int, int],
    color_b: tuple[int, int, int],
    intensity: float,
) -> None:
    layers = max(1, glow_radius)
    for d in range(layers, 0, -1):
        t = 1.0 - (d / float(layers))
        shade = _mix_rgb(color_a, color_b, t)
        scale = intensity * (1.0 - t) * 0.7
        c = (
            int(round(shade[0] * scale)),
            int(round(shade[1] * scale)),
            int(round(shade[2] * scale)),
        )
        r = radius + d
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=c, width=1)

        stem_top = cy - radius + 12 - d
        stem_bottom = stem_top + stem_height + (2 * d)
        draw.line((cx, stem_top, cx, stem_bottom), fill=c, width=1)


def _draw_radial_fill(canvas: Image.Image, cx: int, cy: int, radius: int, edge_color: tuple[int, int, int]) -> None:
    draw = ImageDraw.Draw(canvas, "RGB")
    if radius <= 0:
        return
    for r in range(radius, 0, -1):
        t = 1.0 - (r / float(radius))
        color = _mix_rgb(edge_color, (255, 255, 255), t)
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=color)



def _draw_happy_face(canvas: Image.Image, progress: float, frame_index: int) -> Image.Image:
    """Draw a simple happy face with fade-in effect during fade-out stage."""
    face = Image.new("RGB", FRAME_SIZE, (0, 0, 0))
    draw = ImageDraw.Draw(face, "RGB")
    
    # Fade in eyes and mouth based on progress.
    eye_alpha = _clamp(progress - 0.1, 0.0, 1.0)
    smile_alpha = _clamp(progress - 0.3, 0.0, 1.0)
    blink = 0.85 + 0.15 * math.sin(frame_index * 0.6)
    pupil_shift = int(round(1.5 * math.sin(frame_index * 0.5)))
    smile_bob = int(round(2.0 * math.sin(frame_index * 0.4)))
    
    if eye_alpha > 0.01:
        eye_color = (
            int(round(220 * eye_alpha * blink)),
            int(round(245 * eye_alpha * blink)),
            int(round(255 * eye_alpha * blink)),
        )
        # Left eye
        draw.ellipse((100, 100, 130, 130), fill=eye_color)
        draw.ellipse((108 + pupil_shift, 110, 118 + pupil_shift, 118), fill=(30, 40, 48))
        # Right eye
        draw.ellipse((190, 100, 220, 130), fill=eye_color)
        draw.ellipse((198 + pupil_shift, 110, 208 + pupil_shift, 118), fill=(30, 40, 48))
    
    if smile_alpha > 0.01:
        smile_color = (
            int(round(120 * smile_alpha)),
            int(round(180 * smile_alpha)),
            int(round(220 * smile_alpha)),
        )
        # Simple smile arc
        for offset in range(4):
            draw.arc((105, 125 + smile_bob, 215, 175 + smile_bob), 0, 180, fill=smile_color, width=2 + offset)
    
    return Image.blend(canvas, face, 0.8)


def _add_idle_glow(canvas: Image.Image, progress: float) -> Image.Image:
    glow = Image.new("RGB", FRAME_SIZE, (0, 0, 0))
    draw = ImageDraw.Draw(glow, "RGB")
    radius = int(round(_lerp(12.0, 20.0, progress)))
    points = [(102, 110), (218, 110)]
    for px, py in points:
        for d in range(radius, 0, -1):
            t = 1.0 - (d / float(radius))
            c = _mix_rgb(POWER_CYAN, ICE_BLUE, t)
            dim = 0.15 * (1.0 - t) * progress
            shade = (
                int(round(c[0] * dim)),
                int(round(c[1] * dim)),
                int(round(c[2] * dim)),
            )
            draw.ellipse((px - d, py - d, px + d, py + d), outline=shade, width=1)
    return Image.blend(canvas, glow, 0.6)


def _render_base_with_comfy(workflow_spec: dict[str, Any], frame_index: int, total_frames: int, api_url: str) -> Image.Image:
    workflow = copy.deepcopy(workflow_spec["prompt_template"])

    style = _find_node_by_title(workflow, "STYLE_PROMPT")
    negative = _find_node_by_title(workflow, "NEG_PROMPT")
    sampler = _find_node_by_title(workflow, "SAMPLER")
    checkpoint = _find_node_by_title(workflow, "CHECKPOINT")
    saver = _find_node_by_title(workflow, "IMAGE")

    checkpoint_name = _discover_checkpoint_name()
    if checkpoint_name is None:
        raise RuntimeError("No .safetensors or .ckpt checkpoint found for Comfy workflow")

    style.setdefault("inputs", {})["text"] = _build_frame_prompt(frame_index, total_frames)
    negative.setdefault("inputs", {})["text"] = (
        "text, watermark, logo, letters, words, humans, faces, characters, creatures, extra symbols, "
        "busy background, UI panels, clutter, asymmetry, warped circle, artifacts, blur, noise, low quality"
    )
    sampler.setdefault("inputs", {})["seed"] = int(workflow_spec.get("seed", SEED_DEFAULT)) + frame_index
    checkpoint.setdefault("inputs", {})["ckpt_name"] = checkpoint_name
    saver.setdefault("inputs", {})["filename_prefix"] = f"output/boot/frame_{frame_index:03d}"

    png_bytes = run_workflow(workflow=workflow, api_url=api_url, timeout_seconds=240.0)
    with Image.open(BytesIO(png_bytes)) as image:
        return image.convert("RGB").resize(FRAME_SIZE, Image.Resampling.LANCZOS)


def _deterministic_base(frame_index: int) -> Image.Image:
    width, height = FRAME_SIZE
    base = Image.new("RGB", FRAME_SIZE, (2, 4, 10))
    draw = ImageDraw.Draw(base, "RGB")

    for y in range(height):
        band = int(round(_lerp(2.0, 12.0, y / float(height - 1))))
        draw.line((0, y, width, y), fill=(band, band + 2, band + 8))

    # Deterministic vertical micro-texture bands.
    for x in range(0, width, 7):
        shade = 10 + ((x + frame_index * 5) % 10)
        draw.line((x, 0, x, height), fill=(shade, shade + 1, shade + 4))

    return base


def _compose_activation_pulse(base_rgb: Image.Image, frame_index: int, total_frames: int, idle_rgb: Image.Image) -> Image.Image:
    _ = base_rgb
    width, height = FRAME_SIZE
    cx, cy = width // 2, height // 2

    canvas = Image.new("RGB", FRAME_SIZE, (0, 0, 0))
    draw = ImageDraw.Draw(canvas, "RGB")

    circle_radius = 70
    stroke = 8
    stem_width = 8
    stem_height = 40
    top_gap = 12

    if frame_index <= 10:
        p = _smoothstep(_stage_progress(frame_index, 0, 10))
        icon_color = _mix_rgb((0, 0, 0), POWER_CYAN, 0.40 * p)
        glow_r = int(round(_lerp(12.0, 16.0, p)))
        _draw_icon_glow(draw, cx, cy, circle_radius, stem_height, glow_r, POWER_CYAN, ICE_BLUE, 0.5 * p)
        _draw_power_icon(draw, cx, cy, circle_radius, stroke, stem_width, stem_height, top_gap, icon_color)

    elif frame_index <= 25:
        p = _smoothstep(_stage_progress(frame_index, 10, 25))
        press = int(round(_lerp(0.0, 4.0, p)))
        icon_radius = circle_radius - press
        icon_color = _mix_rgb(POWER_CYAN, BRIGHT_CYAN, p)
        _draw_icon_glow(draw, cx, cy, icon_radius, stem_height - 2, 16, POWER_CYAN, ICE_BLUE, 0.65)
        _draw_power_icon(draw, cx, cy, icon_radius, stroke, stem_width, stem_height - 2, top_gap, icon_color)

        ripple_r = int(round(_lerp(0.0, 200.0, p)))
        ripple_alpha = _clamp(1.0 - p, 0.0, 1.0)
        ripple_color = (
            int(round(POWER_CYAN[0] * ripple_alpha)),
            int(round(POWER_CYAN[1] * ripple_alpha)),
            int(round(POWER_CYAN[2] * ripple_alpha)),
        )
        if ripple_r > 0:
            draw.ellipse((cx - ripple_r, cy - ripple_r, cx + ripple_r, cy + ripple_r), outline=ripple_color, width=6)
        echo_r = ripple_r - 26
        if echo_r > 0:
            echo_color = (
                int(round(ripple_color[0] * 0.35)),
                int(round(ripple_color[1] * 0.35)),
                int(round(ripple_color[2] * 0.35)),
            )
            draw.ellipse((cx - echo_r, cy - echo_r, cx + echo_r, cy + echo_r), outline=echo_color, width=6)

    elif frame_index <= 45:
        p = _smoothstep(_stage_progress(frame_index, 25, 45))
        icon_color = _mix_rgb(POWER_CYAN, (255, 255, 255), p)
        glow_r = int(round(_lerp(16.0, 60.0, p)))
        _draw_icon_glow(draw, cx, cy, circle_radius, stem_height, glow_r, POWER_CYAN, (255, 255, 255), 0.95)

        # Subtle 1px chromatic aberration at stronger glow levels.
        if p > 0.2:
            r = circle_radius
            draw.ellipse((cx - r - 1, cy - r, cx + r - 1, cy + r), outline=(90, 160, 255), width=1)
            draw.ellipse((cx - r + 1, cy - r, cx + r + 1, cy + r), outline=(220, 240, 255), width=1)

        _draw_power_icon(draw, cx, cy, circle_radius, stroke, stem_width, stem_height, top_gap, icon_color)

    elif frame_index < 60:
        p = _smoothstep(_stage_progress(frame_index, 45, 60))
        max_radius = int(round(_lerp(70.0, math.hypot(width, height), p)))
        _draw_radial_fill(canvas, cx, cy, max_radius, POWER_CYAN)

    if frame_index == 60:
        return Image.new("RGB", FRAME_SIZE, (255, 255, 255))

    if frame_index >= 61:
        # Use linear frame progress here to guarantee distinct output per frame.
        t = _clamp((frame_index - 60) / 15.0, 0.0, 1.0)
        p = t
        shade = int(round(_lerp(255.0, 0.0, t)))
        bg = Image.new("RGB", FRAME_SIZE, (shade, shade, shade))
        with_glow = _add_idle_glow(bg, p)
        with_face = _draw_happy_face(with_glow, p, frame_index)
        return with_face

    return canvas


def _idle_face_image() -> Image.Image:
    candidates = [
        Path("D:/Flic/animations/face/default/idle/frame_000.png"),
        Path("D:/Flic/animations/face/default/neutral/frame_000.png"),
        Path("build/animations/output/idle/final/frame_000.png"),
    ]
    for candidate in candidates:
        if candidate.exists():
            with Image.open(candidate) as src:
                return src.convert("RGB").resize(FRAME_SIZE, Image.Resampling.LANCZOS)

    fallback = Image.new("RGB", FRAME_SIZE, (0, 0, 0))
    draw = ImageDraw.Draw(fallback, "RGB")
    draw.ellipse((86, 90, 126, 130), fill=(220, 245, 255))
    draw.ellipse((194, 90, 234, 130), fill=(220, 245, 255))
    draw.ellipse((100, 104, 110, 114), fill=(30, 40, 48))
    draw.ellipse((208, 104, 218, 114), fill=(30, 40, 48))
    draw.line((130, 164, 190, 164), fill=(120, 180, 220), width=4)
    return fallback


def _save_rgb_png(image: Image.Image, path: Path, frame_index: int) -> None:
    _ = frame_index
    rgb = image.convert("RGB")
    path.parent.mkdir(parents=True, exist_ok=True)
    rgb.save(path, format="PNG", optimize=True)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _validate_png(path: Path) -> None:
    with Image.open(path) as image:
        image.verify()
    with Image.open(path) as image:
        if image.size != FRAME_SIZE:
            raise ValueError(f"Invalid frame size for {path.name}: {image.size}")
        if image.mode != "RGB":
            raise ValueError(f"Invalid frame mode for {path.name}: {image.mode}")
        image.load()


def validate_generated_frames(output_dir: Path, expected_count: int) -> dict[str, Any]:
    expected_names = [f"frame_{index:03d}.png" for index in range(expected_count)]
    frames = sorted(path for path in output_dir.glob("frame_*.png") if path.is_file())

    actual_names = [path.name for path in frames]
    if actual_names != expected_names:
        raise ValueError(
            "Frame naming/count mismatch. "
            f"Expected {expected_names[0]}..{expected_names[-1]} ({expected_count} files), got {len(actual_names)} files."
        )

    for frame in frames:
        _validate_png(frame)

    return {
        "count": len(frames),
        "first": frames[0] if frames else None,
        "last": frames[-1] if frames else None,
    }


def sync_activation_pulse_to_sd(sd_boot_root: Path | None = None, source_dir: Path | None = None) -> dict[str, Any]:
    boot_root = ensure_boot_folder(sd_boot_root)
    source = OUTPUT_DIR if source_dir is None else Path(source_dir)

    if not source.exists():
        raise FileNotFoundError(f"Source folder not found: {source}")

    source_frames = sorted(path for path in source.glob("frame_*.png") if path.is_file())
    if not source_frames:
        raise ValueError("No source PNG frames to sync")

    # Ensure boot folder contains only the new set.
    for stale in list(boot_root.iterdir()):
        if stale.is_dir():
            shutil.rmtree(stale)
        else:
            stale.unlink()

    expected_names: list[str] = []
    hashes: set[str] = set()

    for index, frame in enumerate(source_frames):
        target_name = f"frame_{index:03d}.png"
        expected_names.append(target_name)
        destination = boot_root / target_name
        shutil.copy2(frame, destination)
        _validate_png(destination)

        frame_hash = _sha256(destination)
        if frame_hash in hashes:
            raise ValueError(f"Duplicate frame content detected after sync: {destination.name}")
        hashes.add(frame_hash)

    on_sd = sorted(path.name for path in boot_root.iterdir() if path.is_file())
    if on_sd != expected_names:
        raise ValueError("SD boot folder contains unexpected files or ordering mismatch")

    return {
        "sd_boot_root": boot_root,
        "frame_count": len(expected_names),
        "duplicates": 0,
        "extra_files": 0,
    }


def validate_boot_install(sd_boot_root: Path | None = None, expected_count: int = FRAME_COUNT_DEFAULT) -> dict[str, Any]:
    boot_root = ensure_boot_folder(sd_boot_root)
    expected_names = [f"frame_{index:03d}.png" for index in range(expected_count)]
    actual_files = sorted(path for path in boot_root.iterdir() if path.is_file())
    actual_names = [path.name for path in actual_files]

    if actual_names != expected_names:
        raise RuntimeError(
            "Boot folder contains unexpected files. "
            f"Expected exactly {expected_count} zero-padded frames and nothing else."
        )

    for frame in actual_files:
        _validate_png(frame)

    return {
        "boot_root": boot_root,
        "frame_count": len(actual_files),
        "status": "ok",
    }


def _start_comfy_background() -> None:
    starter = Path("tools/animation_builder/vscode_start_comfy.py")
    kwargs: dict[str, object] = {
        "stdout": subprocess.DEVNULL,
        "stderr": subprocess.DEVNULL,
        "stdin": subprocess.DEVNULL,
        "close_fds": True,
    }
    if sys.platform.startswith("win"):
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS
    subprocess.Popen([sys.executable, str(starter)], **kwargs)


def _wait_for_comfy(api_url: str, timeout_seconds: float, poll_seconds: float = 2.0) -> bool:
    if timeout_seconds <= 0:
        return check_server(api_url=api_url)

    start = time.time()
    while (time.time() - start) < timeout_seconds:
        if check_server(api_url=api_url):
            return True
        time.sleep(poll_seconds)
    return False


def generate_activation_pulse(
    workflow_path: Path = WORKFLOW_PATH,
    output_dir: Path = OUTPUT_DIR,
    api_url: str = COMFY_API_URL,
    wait_for_comfy: float = 120.0,
    strict_comfy: bool = True,
) -> dict[str, Any]:
    workflow_spec = _load_workflow_spec(workflow_path)
    frame_count = int(workflow_spec.get("frame_count", FRAME_COUNT_DEFAULT))

    if output_dir.exists():
        for stale in output_dir.glob("*.png"):
            stale.unlink()
    output_dir.mkdir(parents=True, exist_ok=True)

    comfy_ready = check_server(api_url=api_url)
    if not comfy_ready:
        _start_comfy_background()
        comfy_ready = _wait_for_comfy(api_url=api_url, timeout_seconds=wait_for_comfy)

    if strict_comfy and not comfy_ready:
        raise RuntimeError(f"ComfyUI not reachable at {api_url} in strict mode")

    idle_face = _idle_face_image()

    rendered_with_comfy = 0
    rendered_fallback = 0
    for frame_index in range(frame_count):
        if comfy_ready:
            try:
                base = _render_base_with_comfy(workflow_spec, frame_index, frame_count, api_url=api_url)
                rendered_with_comfy += 1
            except (ComfyApiError, KeyError, ValueError) as exc:
                if strict_comfy:
                    raise RuntimeError(f"Strict Comfy mode failed on frame {frame_index:03d}: {exc}") from exc
                base = _deterministic_base(frame_index)
                rendered_fallback += 1
        else:
            if strict_comfy:
                raise RuntimeError("Strict Comfy mode requested, but ComfyUI is unavailable")
            base = _deterministic_base(frame_index)
            rendered_fallback += 1

        final = _compose_activation_pulse(base, frame_index, frame_count, idle_face)
        _save_rgb_png(final, output_dir / f"frame_{frame_index:03d}.png", frame_index)

    validation = validate_generated_frames(output_dir, frame_count)
    return {
        "frame_count": frame_count,
        "rendered_with_comfy": rendered_with_comfy,
        "rendered_fallback": rendered_fallback,
        "validation": validation,
    }


def run_pipeline(args: argparse.Namespace) -> int:
    sd_root = None if args.sd_root is None else Path(args.sd_root)
    resolved_boot_root = ensure_boot_folder(sd_root)
    purge_report = purge_boot_animation(sd_root)

    generation = generate_activation_pulse(
        workflow_path=Path(args.workflow),
        output_dir=Path(args.output),
        api_url=args.api,
        wait_for_comfy=float(args.wait_for_comfy),
        strict_comfy=bool(args.strict_comfy),
    )

    sync_report = sync_activation_pulse_to_sd(sd_root, Path(args.output))
    install_validation = validate_boot_install(sd_root, expected_count=generation["frame_count"])

    print("SUMMARY")
    print(f"Detected SD root: {resolved_boot_root.parent.parent}")
    print(f"Boot folder: {purge_report['boot_root']}")
    print(
        "Purge removed -> "
        f"files={purge_report['removed_files']} hidden={purge_report['removed_hidden']} dirs={purge_report['removed_dirs']}"
    )
    print(f"Workflow: {Path(args.workflow)}")
    print(f"Generated frames: {generation['frame_count']}")
    print(f"Comfy-rendered frames: {generation['rendered_with_comfy']}")
    print(f"Fallback-rendered frames: {generation['rendered_fallback']}")
    print(f"Installed frames: {sync_report['frame_count']}")
    print(f"Install validation: {install_validation['status']}")
    print("SUCCESS: Activation Pulse boot animation purged, regenerated, installed, and validated.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Purge, generate, install, and validate Activation Pulse boot animation")
    parser.add_argument("--workflow", default=str(WORKFLOW_PATH))
    parser.add_argument("--output", default=str(OUTPUT_DIR))
    parser.add_argument("--api", default=COMFY_API_URL)
    parser.add_argument("--wait-for-comfy", type=float, default=120.0)
    parser.add_argument("--sd-root", default=None, help="Drive root (D:/) or Flic root (D:/Flic)")
    parser.set_defaults(strict_comfy=True)
    parser.add_argument("--strict-comfy", dest="strict_comfy", action="store_true")
    parser.add_argument("--allow-fallback", dest="strict_comfy", action="store_false")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return run_pipeline(args)
    except Exception as exc:
        print(f"ERROR: {type(exc).__name__}: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
