from __future__ import annotations

import json
import math
import shutil
from io import BytesIO
from pathlib import Path
from typing import Any

from PIL import Image, ImageColor, ImageDraw

PACKAGE_ROOT = Path(__file__).resolve().parent
SPECS_ROOT = PACKAGE_ROOT / "animation_specs"
CURVE_SPECS_ROOT = PACKAGE_ROOT / "specs"
PROMPT_TEMPLATES_PATH = PACKAGE_ROOT / "prompt_templates.json"
OUTPUT_ROOT = Path("build/animations/output")

DEFAULT_STYLE_PROMPT = (
    "soft glowing creature eye, friendly, expressive, cyan glow, "
    "slightly asymmetrical, smooth geometry, flux style"
)
DEFAULT_NEGATIVE_PROMPT = "text, watermark, logo, extra limbs, distortion, artifacts"

EMOTION_SUITE = [
    "idle",
    "blink",
    "listening",
    "thinking",
    "speaking",
    "happy",
    "sad",
    "surprised",
    "angry",
    "sleepy",
    "error",
    "neutral",
]

_EMOTION_STYLE_PROMPTS = {
    "idle": "calm idle creature eye",
    "blink": "fast blink creature eye",
    "listening": "attentive listening creature eye",
    "thinking": "pondering thinking creature eye",
    "speaking": "animated expressive speaking eye",
    "happy": "happy glowing creature eye",
    "sad": "melancholy sad creature eye",
    "surprised": "wide-eyed surprised creature eye",
    "angry": "intense angry creature eye",
    "sleepy": "drowsy sleepy creature eye",
    "error": "alert error creature eye",
    "neutral": "neutral balanced creature eye",
}


def _load_prompt_templates() -> dict[str, dict[str, str]]:
    if not PROMPT_TEMPLATES_PATH.exists():
        return {}
    try:
        raw = json.loads(PROMPT_TEMPLATES_PATH.read_text(encoding="utf-8"))
    except Exception:
        return {}
    if not isinstance(raw, dict):
        return {}
    out: dict[str, dict[str, str]] = {}
    for emotion, values in raw.items():
        if not isinstance(values, dict):
            continue
        out[str(emotion)] = {
            "positive": str(values.get("positive", "")),
            "negative": str(values.get("negative", "")),
        }
    return out


PROMPT_TEMPLATES = _load_prompt_templates()

_EMOTION_BROW = {
    "idle": "neutral",
    "blink": "neutral",
    "listening": "arched",
    "thinking": "raise_right",
    "speaking": "neutral",
    "happy": "happy",
    "sad": "sad",
    "surprised": "surprised",
    "angry": "angry",
    "sleepy": "sleepy",
    "error": "angry",
    "neutral": "neutral",
}

_EMOTION_MOUTH = {
    "idle": "flat",
    "blink": "flat",
    "listening": "flat",
    "thinking": "flat",
    "speaking": "talk",
    "happy": "smile",
    "sad": "frown",
    "surprised": "open",
    "angry": "flat",
    "sleepy": "flat",
    "error": "open",
    "neutral": "flat",
}

_EMOTION_MOTION = {
    "idle": "steady",
    "blink": "steady",
    "listening": "bounce",
    "thinking": "lean_right",
    "speaking": "bounce",
    "happy": "bounce",
    "sad": "lean_left",
    "surprised": "steady",
    "angry": "jitter",
    "sleepy": "lean_left",
    "error": "shiver",
    "neutral": "steady",
}


def _emotion_index(emotion_name: str) -> int:
    try:
        return EMOTION_SUITE.index(emotion_name)
    except ValueError:
        return len(EMOTION_SUITE)


def emotion_spec_path(emotion_name: str) -> Path:
    curve_path = CURVE_SPECS_ROOT / f"{emotion_name}.json"
    if curve_path.exists():
        return curve_path
    return SPECS_ROOT / f"{emotion_name}.json"


def _normalize_curve_spec(emotion_name: str, spec: dict[str, Any]) -> dict[str, Any]:
    required = [
        "open_curve",
        "close_curve",
        "pupil_curve",
        "glow_curve",
        "frame_count",
        "easing",
        "speed",
        "micro_movement",
        "eyelid_behavior",
        "pupil_dilation_rules",
        "emotion_timing",
    ]
    missing = [key for key in required if key not in spec]
    if missing:
        raise ValueError(f"Curve spec '{emotion_name}' missing required keys: {', '.join(missing)}")

    open_curve = spec.get("open_curve", [])
    if not isinstance(open_curve, list) or not open_curve:
        raise ValueError(f"Curve spec '{emotion_name}' has invalid open_curve")

    frame_count = int(spec.get("frame_count", len(open_curve)))
    if frame_count != len(open_curve):
        raise ValueError(
            f"Curve spec '{emotion_name}' frame_count={frame_count} does not match open_curve length={len(open_curve)}"
        )

    eyelid = spec.get("eyelid_behavior", {}) if isinstance(spec.get("eyelid_behavior", {}), dict) else {}
    pupil_rules = (
        spec.get("pupil_dilation_rules", {}) if isinstance(spec.get("pupil_dilation_rules", {}), dict) else {}
    )
    micro = spec.get("micro_movement", {}) if isinstance(spec.get("micro_movement", {}), dict) else {}
    glow_curve = spec.get("glow_curve", []) if isinstance(spec.get("glow_curve", []), list) else []
    compositor = spec.get("compositor", {}) if isinstance(spec.get("compositor", {}), dict) else {}

    avg_glow = sum(float(v) for v in glow_curve) / len(glow_curve) if glow_curve else 0.4
    glow_bias = max(0.7, min(1.5, 0.8 + avg_glow))

    return {
        "name": emotion_name,
        "seed": 20001 + _emotion_index(emotion_name),
        "open_values": [float(v) for v in open_curve],
        "style_prompt": _EMOTION_STYLE_PROMPTS.get(emotion_name, DEFAULT_STYLE_PROMPT),
        "prompt_tags": [
            f"easing:{spec.get('easing')}",
            f"speed:{float(spec.get('speed', 1.0)):.2f}",
            f"micro_freq:{float(micro.get('frequency_hz', 0.0)):.2f}",
        ],
        "negative_prompt": DEFAULT_NEGATIVE_PROMPT,
        "curve_spec": spec,
        "fallback": {
            "eye_gap": 84,
            "eye_y": 122,
            "eye_width": 64,
            "eye_height": 32,
            "iris_scale": 1.0,
            "pupil_scale": float(pupil_rules.get("base", 0.53)) * 1.8,
            "glow_bias": glow_bias,
            "motion": _EMOTION_MOTION.get(emotion_name, "steady"),
            "brow": _EMOTION_BROW.get(emotion_name, "neutral"),
            "mouth": _EMOTION_MOUTH.get(emotion_name, "flat"),
            "palette": {
                "accent": "#55D9FF",
                "primary": "#EAF9FF",
                "shadow": "#102338",
                "highlight": "#FFFFFF",
                "warning": "#FF5A5A",
                "secondary": "#9EE3FF",
            },
        },
        "eyelid_behavior": eyelid,
        "pupil_dilation_rules": pupil_rules,
        "emotion_timing": spec.get("emotion_timing", {}),
        "compositor": compositor,
    }


def load_emotion_spec(emotion_name: str) -> dict[str, Any]:
    path = emotion_spec_path(emotion_name)
    if not path.exists():
        raise FileNotFoundError(f"Emotion spec not found: {path}")

    spec = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(spec, dict):
        raise ValueError(f"Emotion spec must be a JSON object: {path}")

    if "open_values" not in spec and "open_curve" in spec:
        return _normalize_curve_spec(emotion_name, spec)

    open_values = spec.get("open_values", [])
    if not isinstance(open_values, list) or not open_values:
        raise ValueError(f"Emotion spec '{emotion_name}' has no open_values")

    return spec


def clean_output_root() -> None:
    if OUTPUT_ROOT.exists():
        shutil.rmtree(OUTPUT_ROOT)
    OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)


def suite_output_folder(emotion_name: str) -> Path:
    return OUTPUT_ROOT / emotion_name


def _clamp(value: float, low: float = 0.0, high: float = 1.0) -> float:
    return max(low, min(high, value))


def _hex_rgba(value: str, alpha: int = 255) -> tuple[int, int, int, int]:
    red, green, blue = ImageColor.getrgb(value)
    return red, green, blue, alpha


def _profile(spec: dict[str, Any]) -> dict[str, Any]:
    fallback = spec.get("fallback", {}) if isinstance(spec.get("fallback", {}), dict) else {}
    palette = fallback.get("palette", {}) if isinstance(fallback.get("palette", {}), dict) else {}
    return {
        "eye_gap": float(fallback.get("eye_gap", 84.0)),
        "eye_y": float(fallback.get("eye_y", 122.0)),
        "eye_width": float(fallback.get("eye_width", 64.0)),
        "eye_height": float(fallback.get("eye_height", 32.0)),
        "iris_scale": float(fallback.get("iris_scale", 1.0)),
        "pupil_scale": float(fallback.get("pupil_scale", 1.0)),
        "glow_bias": float(fallback.get("glow_bias", 1.0)),
        "motion": str(fallback.get("motion", "steady")),
        "brow": str(fallback.get("brow", "neutral")),
        "mouth": str(fallback.get("mouth", "none")),
        "accent": str(palette.get("accent", "#55D9FF")),
        "primary": str(palette.get("primary", "#EAF9FF")),
        "shadow": str(palette.get("shadow", "#102338")),
        "highlight": str(palette.get("highlight", "#FFFFFF")),
        "warning": str(palette.get("warning", "#FF5A5A")),
        "secondary": str(palette.get("secondary", "#9EE3FF")),
    }


def build_style_prompt(emotion_name: str, spec: dict[str, Any], frame_idx: int, total_frames: int, open_value: float) -> str:
    tags = spec.get("prompt_tags", [])
    if not isinstance(tags, list):
        tags = [str(tags)]
    prompt = spec.get("style_prompt", DEFAULT_STYLE_PROMPT)
    template = PROMPT_TEMPLATES.get(emotion_name, {})
    if template.get("positive"):
        prompt = template["positive"]
    tag_text = ", ".join(str(tag) for tag in tags if str(tag).strip())
    prompt_bits = [
        str(prompt),
        f"emotion={emotion_name}",
        f"frame={frame_idx + 1}/{total_frames}",
        f"open={open_value:.3f}",
    ]
    if tag_text:
        prompt_bits.append(tag_text)
    return ", ".join(prompt_bits)


def build_negative_prompt(spec: dict[str, Any], emotion_name: str | None = None) -> str:
    emotion_name = str(emotion_name or spec.get("name", ""))
    template = PROMPT_TEMPLATES.get(emotion_name, {})
    if template.get("negative"):
        return template["negative"]
    neg = spec.get("negative_prompt", DEFAULT_NEGATIVE_PROMPT)
    return str(neg)


def render_fallback_frame(emotion_name: str, spec: dict[str, Any], frame_idx: int, total_frames: int, open_value: float) -> bytes:
    profile = _profile(spec)
    size = 240
    openness = _clamp(float(open_value), 0.0, 1.2)
    phase = 0.0 if total_frames <= 1 else frame_idx / max(total_frames - 1, 1)
    wave = math.sin(phase * 2.0 * math.pi)
    drift = int(round(wave * 5.0))
    vertical = int(round(wave * 3.0))
    base_motion = profile["motion"]
    if base_motion == "jitter":
        drift += (frame_idx % 3) - 1
    elif base_motion == "bounce":
        vertical += int(round(math.sin(phase * 4.0 * math.pi) * 4.0))
    elif base_motion == "lean_left":
        drift -= 3
    elif base_motion == "lean_right":
        drift += 3
    elif base_motion == "shiver":
        drift += ((frame_idx % 4) - 1) * 2
        vertical += ((frame_idx + 1) % 3) - 1

    accent = _hex_rgba(profile["accent"], int(40 + 55 * _clamp(profile["glow_bias"], 0.0, 2.0)))
    primary = _hex_rgba(profile["primary"])
    shadow = _hex_rgba(profile["shadow"], 230)
    highlight = _hex_rgba(profile["highlight"], 220)
    warning = _hex_rgba(profile["warning"], 230)
    secondary = _hex_rgba(profile["secondary"], 210)

    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img, "RGBA")

    eye_gap = int(profile["eye_gap"])
    center_y = int(profile["eye_y"]) + vertical
    eye_width = int(profile["eye_width"])
    eye_height = max(10, int(profile["eye_height"] * (0.7 + openness * 0.35)))
    iris_scale = profile["iris_scale"] + (0.12 * openness)
    pupil_scale = profile["pupil_scale"]
    mouth = profile["mouth"]
    brow = profile["brow"]

    eye_centers = [(size // 2 - eye_gap // 2 + drift, center_y), (size // 2 + eye_gap // 2 + drift, center_y)]
    under_eye_lift = int(round((1.0 - openness) * 18))
    lid_cover = int(round((1.2 - openness) * 26))
    glow_radius = 30 + int(8 * profile["glow_bias"])

    if emotion_name == "error":
        draw.rounded_rectangle((24, 24, size - 24, size - 24), radius=22, outline=warning, width=4)

    for index, (cx, cy) in enumerate(eye_centers):
        x0 = cx - eye_width // 2
        x1 = cx + eye_width // 2
        y0 = cy - eye_height // 2
        y1 = cy + eye_height // 2

        draw.ellipse((x0 - glow_radius // 3, y0 - glow_radius // 3, x1 + glow_radius // 3, y1 + glow_radius // 3), fill=accent)
        draw.ellipse((x0, y0, x1, y1), fill=primary)

        iris_r = max(5, int(13 * iris_scale))
        pupil_r = max(2, int(5 * pupil_scale))
        iris_x = cx + (2 if index == 0 and brow in {"raise_left", "asymmetric"} else 0) - (2 if index == 1 and brow in {"raise_right", "asymmetric"} else 0)
        iris_y = cy + (1 if mouth == "smile" else 0) - (1 if mouth == "frown" else 0)
        if emotion_name == "thinking" and index == 0:
            iris_x -= 4
        elif emotion_name == "thinking" and index == 1:
            iris_x += 4
        elif emotion_name == "angry":
            iris_y += 1
        elif emotion_name == "sad":
            iris_y += 2
        elif emotion_name == "sleepy":
            iris_y += 3

        draw.ellipse((iris_x - iris_r, iris_y - iris_r, iris_x + iris_r, iris_y + iris_r), fill=secondary)
        draw.ellipse((iris_x - pupil_r, iris_y - pupil_r, iris_x + pupil_r, iris_y + pupil_r), fill=shadow)
        draw.ellipse((iris_x - 4, iris_y - 6, iris_x - 1, iris_y - 3), fill=highlight)

        if lid_cover > 0 and emotion_name not in {"surprised", "happy", "neutral"}:
            draw.rectangle((x0, y0, x1, y0 + lid_cover), fill=shadow)
            draw.rectangle((x0, y1 - lid_cover, x1, y1), fill=shadow)

        if emotion_name == "blink":
            blink_band = int(round((1.0 - openness) * 36))
            draw.rectangle((x0, cy - blink_band // 2, x1, cy + blink_band // 2), fill=shadow)

    left_eye, right_eye = eye_centers
    brow_y = center_y - eye_height // 2 - 18 + under_eye_lift // 2
    mouth_y = center_y + 35 + under_eye_lift // 2

    if brow in {"arched", "happy"}:
        draw.arc((left_eye[0] - 28, brow_y - 18, left_eye[0] + 28, brow_y + 14), start=200, end=340, fill=shadow, width=4)
        draw.arc((right_eye[0] - 28, brow_y - 18, right_eye[0] + 28, brow_y + 14), start=200, end=340, fill=shadow, width=4)
    elif brow == "sad":
        draw.line((left_eye[0] - 26, brow_y + 6, left_eye[0] + 20, brow_y - 10), fill=shadow, width=4)
        draw.line((right_eye[0] - 20, brow_y - 10, right_eye[0] + 26, brow_y + 6), fill=shadow, width=4)
    elif brow == "angry":
        draw.line((left_eye[0] - 28, brow_y - 2, left_eye[0] + 24, brow_y - 14), fill=warning, width=4)
        draw.line((right_eye[0] - 24, brow_y - 14, right_eye[0] + 28, brow_y - 2), fill=warning, width=4)
    elif brow == "sleepy":
        draw.line((left_eye[0] - 24, brow_y + 12, left_eye[0] + 24, brow_y + 8), fill=shadow, width=3)
        draw.line((right_eye[0] - 24, brow_y + 8, right_eye[0] + 24, brow_y + 12), fill=shadow, width=3)
    elif brow == "surprised":
        draw.arc((left_eye[0] - 26, brow_y - 14, left_eye[0] + 26, brow_y + 12), start=180, end=360, fill=shadow, width=4)
        draw.arc((right_eye[0] - 26, brow_y - 14, right_eye[0] + 26, brow_y + 12), start=180, end=360, fill=shadow, width=4)

    if mouth == "smile":
        draw.arc((72, mouth_y - 10, 168, mouth_y + 34), start=20, end=160, fill=accent, width=5)
    elif mouth == "frown":
        draw.arc((72, mouth_y - 8, 168, mouth_y + 32), start=200, end=340, fill=warning, width=5)
    elif mouth == "open":
        draw.ellipse((106, mouth_y - 2, 134, mouth_y + 28), fill=shadow)
        draw.ellipse((110, mouth_y + 4, 130, mouth_y + 22), fill=warning)
    elif mouth == "talk":
        open_height = 14 + int(10 * openness)
        draw.rounded_rectangle((100, mouth_y, 140, mouth_y + open_height), radius=10, fill=shadow)
        draw.rectangle((104, mouth_y + 4, 136, mouth_y + open_height - 4), fill=accent)
    elif mouth == "flat":
        draw.line((86, mouth_y + 8, 154, mouth_y + 8), fill=shadow, width=4)

    if emotion_name == "error":
        draw.line((96, 88, 118, 108), fill=warning, width=5)
        draw.line((124, 88, 102, 108), fill=warning, width=5)

    out = BytesIO()
    img.save(out, format="PNG", optimize=True)
    return out.getvalue()
