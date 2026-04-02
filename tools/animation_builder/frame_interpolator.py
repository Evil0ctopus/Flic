from __future__ import annotations

from typing import Iterable, List


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def interpolate_sequence(values: Iterable[float], target_count: int) -> List[float]:
    src = list(values)
    if not src:
        return []
    if len(src) == 1 or target_count <= 1:
        return [src[0]] * max(target_count, 1)

    out: List[float] = []
    max_idx = len(src) - 1
    for i in range(target_count):
        x = i * max_idx / (target_count - 1)
        left = int(x)
        right = min(left + 1, max_idx)
        t = x - left
        out.append(lerp(src[left], src[right], t))
    return out
