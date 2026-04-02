from __future__ import annotations


def _clamp(value: float, low: float = 0.0, high: float = 1.2) -> float:
    return max(low, min(high, value))


def openness_to_params(open_value: float) -> tuple[float, float, float]:
    """
    Map openness to (eyelid_strength, iris_scale, pupil_visibility).

    - eyelid_strength: 0=open, 1=closed
    - iris_scale: slight growth when opening
    - pupil_visibility: fades down near full close
    """
    v = _clamp(open_value, 0.0, 1.2)
    eyelid_strength = _clamp(1.0 - v, 0.0, 1.0)
    iris_scale = 0.88 + 0.20 * min(v, 1.1)
    pupil_visibility = _clamp((v - 0.05) / 0.95, 0.0, 1.0)
    return eyelid_strength, iris_scale, pupil_visibility
