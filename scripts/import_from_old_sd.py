import os
import shutil

SRC_BOOT = r"D:/Flic/boot"
SRC_FACE = r"D:/Flic/animations/face/default"
DST_BOOT = os.path.join("firmware", "assets", "boot")
DST_FACE = os.path.join("firmware", "assets", "faces", "default")

def copy_pngs(src, dst):
    os.makedirs(dst, exist_ok=True)
    count = 0
    for fname in os.listdir(src):
        if fname.lower().endswith('.png'):
            src_path = os.path.join(src, fname)
            dst_path = os.path.join(dst, fname)
            shutil.copy2(src_path, dst_path)
            count += 1
    return count

def main():
    boot_count = copy_pngs(SRC_BOOT, DST_BOOT)
    face_count = copy_pngs(SRC_FACE, DST_FACE)
    print(f"Imported {boot_count} boot PNGs to {DST_BOOT}")
    print(f"Imported {face_count} face PNGs to {DST_FACE}")
    print("Import complete.")

if __name__ == "__main__":
    main()
