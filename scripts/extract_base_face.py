#!/usr/bin/env python3
import shutil
import os

src = os.path.join('firmware', 'assets', 'boot', 'frame_075.png')
dst_dir = os.path.join('firmware', 'assets', 'base_face')
dst = os.path.join(dst_dir, 'base.png')

os.makedirs(dst_dir, exist_ok=True)
shutil.copy2(src, dst)
print(f"Copied {src} to {dst}")
