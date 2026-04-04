from __future__ import annotations

from pathlib import Path
from typing import Any

from PIL import Image, ImageChops, ImageColor


def _clamp_int(value: int, low: int, high: int) -> int:
    return max(low, min(high, value))


def _to_rgba(color: str, alpha: int) -> tuple[int, int, int, int]:
    red, green, blue = ImageColor.getrgb(color)
    return red, green, blue, _clamp_int(alpha, 0, 255)


def _sorted_frames(folder: Path) -> list[Path]:
    return sorted(p for p in folder.glob("frame_*.png") if p.is_file())


def _best_component_bbox(mask: Image.Image) -> tuple[int, int, int, int] | None:
    width, height = mask.size
    pixels = mask.load()
    if pixels is None:
        return None

    visited = bytearray(width * height)
    best_bbox: tuple[int, int, int, int] | None = None
    best_score = -1.0

    for y in range(height):
        for x in range(width):
            idx = y * width + x
            if visited[idx] or pixels[x, y] == 0:
                continue

            stack = [(x, y)]
            visited[idx] = 1
            min_x = x
            max_x = x
            min_y = y
            max_y = y
            area = 0

            while stack:
                cx, cy = stack.pop()
                area += 1
                if cx < min_x:
                    min_x = cx
                if cx > max_x:
                    max_x = cx
                if cy < min_y:
                    min_y = cy
                if cy > max_y:
                    max_y = cy

                neighbors = ((cx - 1, cy), (cx + 1, cy), (cx, cy - 1), (cx, cy + 1))
                for nx, ny in neighbors:
                    if nx < 0 or ny < 0 or nx >= width or ny >= height:
                        continue
                    nidx = ny * width + nx
                    if visited[nidx] or pixels[nx, ny] == 0:
                        continue
                    visited[nidx] = 1
                    stack.append((nx, ny))

            if area < 8:
                continue

            comp_w = (max_x - min_x + 1)
            comp_h = (max_y - min_y + 1)
            coverage = float(comp_w * comp_h)
            score = area + (0.1 * coverage)
            if score > best_score:
                best_score = score
                best_bbox = (min_x, min_y, max_x + 1, max_y + 1)

    return best_bbox


def _extract_content(image: Image.Image, side: str | None = None) -> Image.Image:
    rgba = image.convert("RGBA")
    if side in {"left", "right"}:
        split_x = rgba.width // 2
        center_exclusion = max(10, rgba.width // 20)
        if side == "left":
            x0, x1 = 0, max(1, split_x - center_exclusion)
        else:
            x0, x1 = min(rgba.width - 1, split_x + center_exclusion), rgba.width
        rgba = rgba.crop((x0, 0, x1, rgba.height))

    alpha = rgba.getchannel("A")
    luminance = rgba.convert("L")

    alpha_mask = alpha.point(lambda value: 255 if value > 8 else 0)
    bright_mask = luminance.point(lambda value: 255 if value > 22 else 0)
    content_mask = ImageChops.multiply(alpha_mask, bright_mask)

    bbox = _best_component_bbox(content_mask)
    if bbox is None:
        bbox = content_mask.getbbox()
    if bbox is None:
        bbox = alpha.getbbox()
    if bbox is None:
        return rgba

    x0, y0, x1, y1 = bbox
    if (x1 - x0) <= 0 or (y1 - y0) <= 0:
        return rgba
    return rgba.crop(bbox)


def _fit_eye(image: Image.Image, target_w: int, target_h: int, side: str) -> Image.Image:
    src = _extract_content(image, side=None)
    out = Image.new("RGBA", (target_w, target_h), (0, 0, 0, 0))
    if src.width <= 0 or src.height <= 0:
        return out

    # Use cover-fit so the detected eye fills the target slot instead of appearing tiny.
    scale = max(target_w / src.width, target_h / src.height)
    scaled_w = max(1, int(round(src.width * scale)))
    scaled_h = max(1, int(round(src.height * scale)))
    resized = src.resize((scaled_w, scaled_h), Image.Resampling.LANCZOS)

    left = (scaled_w - target_w) // 2
    top = (scaled_h - target_h) // 2
    cropped = resized.crop((left, top, left + target_w, top + target_h))
    out.paste(cropped, (0, 0), cropped)
    return out


def compose_frame(
    left_image: Image.Image,
    right_image: Image.Image,
    *,
    canvas_size: int,
    eye_center_y: int,
    eye_spacing: int,
    eye_width: int,
    eye_height: int,
    glow_color: str,
    glow_strength: float,
) -> Image.Image:
    # Opaque black background prevents display ghosting when firmware draws PNG frames.
    canvas = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 255))

    left_eye = _fit_eye(left_image, eye_width, eye_height, side="left")
    right_eye = _fit_eye(right_image, eye_width, eye_height, side="right")

    center_x = canvas_size // 2
    left_cx = center_x - eye_spacing // 2
    right_cx = center_x + eye_spacing // 2
    eye_top = eye_center_y - eye_height // 2

    left_xy = (left_cx - eye_width // 2, eye_top)
    right_xy = (right_cx - eye_width // 2, eye_top)

    canvas.paste(left_eye, left_xy, left_eye)
    canvas.paste(right_eye, right_xy, right_eye)

    return canvas


def assemble_emotion_final_frames(emotion_name: str, spec: dict[str, Any], emotion_folder: Path) -> Path:
    left_folder = emotion_folder / "left"
    right_folder = emotion_folder / "right"
    final_folder = emotion_folder / "final"

    left_frames = _sorted_frames(left_folder)
    right_frames = _sorted_frames(right_folder)
    if not left_frames or not right_frames:
        raise ValueError(f"Missing left/right frames for compositor: {emotion_name}")
    if len(left_frames) != len(right_frames):
        raise ValueError(f"Mismatched left/right frame counts for {emotion_name}: {len(left_frames)} vs {len(right_frames)}")

    if final_folder.exists():
        for stale in final_folder.glob("*.png"):
            stale.unlink()
    else:
        final_folder.mkdir(parents=True, exist_ok=True)

    fallback = spec.get("fallback", {}) if isinstance(spec.get("fallback"), dict) else {}
    palette = fallback.get("palette", {}) if isinstance(fallback.get("palette"), dict) else {}
    compositor = spec.get("compositor", {}) if isinstance(spec.get("compositor"), dict) else {}

    canvas_size = 240
    eye_spacing = int(round(float(fallback.get("eye_gap", 84))))
    eye_center_y = int(round(float(fallback.get("eye_y", 122))))
    eye_width = int(round(float(compositor.get("eye_width", fallback.get("eye_width", 124)))))
    eye_height = int(round(float(compositor.get("eye_height", fallback.get("eye_height", 76)))))

    y_offset = int(round(float(compositor.get("y_offset", 0))))
    clamp_range = compositor.get("y_offset_clamp", [-16, 16])
    if isinstance(clamp_range, list) and len(clamp_range) == 2:
        y_offset = _clamp_int(y_offset, int(clamp_range[0]), int(clamp_range[1]))
    eye_center_y += y_offset

    spacing_delta = int(round(float(compositor.get("spacing_delta", 0))))
    spacing_rules = compositor.get("spacing_rules", {})
    if isinstance(spacing_rules, dict) and emotion_name in spacing_rules:
        spacing_delta += int(round(float(spacing_rules[emotion_name])))
    eye_spacing = max(16, eye_spacing + spacing_delta)

    glow_strength = float(compositor.get("glow_blend", 0.0))
    glow_strength = max(0.0, min(1.0, glow_strength))
    glow_color = str(palette.get("accent", "#55D9FF"))

    for left_path, right_path in zip(left_frames, right_frames):
        if left_path.name != right_path.name:
            raise ValueError(f"Mismatched left/right frame names for {emotion_name}: {left_path.name} vs {right_path.name}")

        with Image.open(left_path) as left_img, Image.open(right_path) as right_img:
            out = compose_frame(
                left_img,
                right_img,
                canvas_size=canvas_size,
                eye_center_y=eye_center_y,
                eye_spacing=eye_spacing,
                eye_width=max(96, eye_width),
                eye_height=max(56, eye_height),
                glow_color=glow_color,
                glow_strength=glow_strength,
            )
            out.save(final_folder / left_path.name, format="PNG", optimize=True)

    final_frames = _sorted_frames(final_folder)
    if len(final_frames) != len(left_frames):
        raise ValueError(
            f"Partial composite for {emotion_name}: final={len(final_frames)} left={len(left_frames)} right={len(right_frames)}"
        )

    return final_folder
