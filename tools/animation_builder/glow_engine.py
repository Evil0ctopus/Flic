from __future__ import annotations

import math


def frame_glow_modifier(frame_index: int, total_frames: int) -> float:
    if total_frames <= 1:
        return 1.0
    phase = (frame_index / max(total_frames - 1, 1)) * (2.0 * math.pi)
    return 0.90 + 0.20 * (0.5 + 0.5 * math.sin(phase))
