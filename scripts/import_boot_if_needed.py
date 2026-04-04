import os
import shutil

def import_boot_if_needed():
    boot_dir = os.path.join('firmware', 'assets', 'boot')
    if not os.path.exists(boot_dir):
        os.makedirs(boot_dir)
    if not os.listdir(boot_dir):
        src_dir = 'D:/Flic/boot'
        if os.path.exists(src_dir):
            for f in os.listdir(src_dir):
                if f.lower().endswith('.png'):
                    shutil.copy2(os.path.join(src_dir, f), os.path.join(boot_dir, f))
            print(f"Imported all PNGs from {src_dir} to {boot_dir}")
        else:
            print(f"Source directory {src_dir} does not exist.")
    else:
        print(f"Boot directory {boot_dir} is not empty. No import needed.")

if __name__ == '__main__':
    import_boot_if_needed()
