from __future__ import annotations

from pathlib import Path

try:
    from PIL import Image
except Exception as exc:  # pragma: no cover
    Image = None
    _PIL_IMPORT_ERROR = exc
else:
    _PIL_IMPORT_ERROR = None


def mirror_animation_frames(animation_folder: Path) -> Path:
    if Image is None:
        raise RuntimeError(f"Pillow is required for mirroring: {_PIL_IMPORT_ERROR}")

    animation_folder = Path(animation_folder)
    right_folder = animation_folder.parent / f"{animation_folder.name}_right"
    right_folder.mkdir(parents=True, exist_ok=True)

    for src in sorted(animation_folder.glob("*.png")):
        dst = right_folder / src.name
        with Image.open(src) as img:
            mirrored = img.transpose(Image.FLIP_LEFT_RIGHT)
            mirrored.save(dst, format="PNG")

    return right_folder
