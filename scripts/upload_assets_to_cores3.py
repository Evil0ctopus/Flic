import os
import sys
import serial
import time
import zlib
from tqdm import tqdm

ASSET_DIRS = [
    os.path.join("firmware", "assets", "boot"),
    os.path.join("firmware", "assets", "faces", "default"),
]
PORT = "COM9"
BAUD = 115200
CHUNK_SIZE = 4096


def find_files():
    files = []
    for d in ASSET_DIRS:
        for fname in os.listdir(d):
            if fname.lower().endswith('.png'):
                files.append((os.path.join(d, fname), fname))
    return files

def send_line(ser, line):
    ser.write((line + "\n").encode())
    ser.flush()

def wait_ok(ser, expect=None):
    while True:
        resp = ser.readline().decode(errors='ignore').strip()
        if resp.startswith("OK"): return resp
        if resp.startswith("ERR"):
            print(f"Device error: {resp}")
            sys.exit(1)
        if expect and expect in resp:
            return resp

def upload_file(ser, src_path, sd_path):
    size = os.path.getsize(src_path)
    with open(src_path, "rb") as f:
        data = f.read()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    send_line(ser, "BEGIN_UPLOAD")
    wait_ok(ser, "OK:READY")
    send_line(ser, f"PATH:{sd_path}")
    wait_ok(ser, "OK:PATH")
    send_line(ser, f"SIZE:{size}")
    wait_ok(ser, "OK:SIZE")
    send_line(ser, f"CRC:{crc:08X}")
    wait_ok(ser, "OK:CRC")
    total_chunks = (size + CHUNK_SIZE - 1) // CHUNK_SIZE
    with open(src_path, "rb") as f, tqdm(total=size, unit='B', unit_scale=True, desc=sd_path) as pbar:
        for chunk_num in range(1, total_chunks + 1):
            chunk = f.read(CHUNK_SIZE)
            send_line(ser, f"CHUNK:")
            ser.write(chunk)
            ser.flush()
            wait_ok(ser, f"OK:CHUNK {chunk_num}/{total_chunks}")
            pbar.update(len(chunk))
    send_line(ser, "END_UPLOAD")
    wait_ok(ser, "OK:WRITE_COMPLETE")
    wait_ok(ser, "OK:CRC_MATCH")
    wait_ok(ser, "OK:DONE")

def main():
    files = find_files()
    if not files:
        print("No asset files found.")
        sys.exit(1)
    try:
        ser = serial.Serial(PORT, BAUD, timeout=10)
    except Exception as e:
        print(f"Serial open failed: {e}")
        sys.exit(1)
    summary = []
    for local_path, fname in files:
        if "boot" in local_path:
            sd_path = f"/Flic/boot/{fname}"
        else:
            sd_path = f"/Flic/animations/face/default/{fname}"
        try:
            upload_file(ser, local_path, sd_path)
            summary.append(f"Uploaded: {sd_path}")
        except Exception as e:
            print(f"Failed: {sd_path}: {e}")
            ser.close()
            sys.exit(1)
    ser.close()
    print("\nUpload summary:")
    for s in summary:
        print(s)
    print("All assets uploaded successfully.")

if __name__ == "__main__":
    main()
