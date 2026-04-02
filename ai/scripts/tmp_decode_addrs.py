import pathlib
import subprocess

root = pathlib.Path(r"C:\Users\jlors\OneDrive\Desktop\Flic")
elf = root / ".pio/build/m5cores3/firmware.elf"
user_home = pathlib.Path.home()

tool = None
for search_root in (root / ".pio" / "packages", user_home / ".platformio" / "packages"):
    if search_root.exists():
        match = next(search_root.glob("**/xtensa-esp32s3-elf-addr2line.exe"), None)
        if match is not None:
            tool = match
            break

if tool is None:
    raise FileNotFoundError("xtensa-esp32s3-elf-addr2line.exe not found in PlatformIO packages")
addrs = [
    "0x42017e57",
    "0x42019628",
    "0x42013fb0",
    "0x4206611a",
    "0x4037bb67",
]

output = subprocess.check_output(
    [str(tool), "-pfiaC", "-e", str(elf), *addrs],
    text=True,
    errors="replace",
)
print(output)
