from __future__ import annotations

from PIL import Image, ImageOps


def mirror_frame(image: Image.Image) -> Image.Image:
    return ImageOps.mirror(image)
