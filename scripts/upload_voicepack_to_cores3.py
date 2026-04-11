import argparse
import os
import sys
import time
import zlib

import serial
from tqdm import tqdm

CHUNK_SIZE = 4096


def send_line(ser, line):
    ser.write((line + "\n").encode())
    ser.flush()


def wait_ok(ser, expect=None, timeout_s=20.0):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        resp = ser.readline().decode(errors="ignore").strip()
        if not resp:
            continue
        if resp.startswith("OK"):
            if expect is None or expect in resp:
                return resp
            continue
        if resp.startswith("ERR"):
            raise RuntimeError(f"Device error: {resp}")
    raise TimeoutError(f"Timed out waiting for device response: {expect}")


def find_wav_files(source_root):
    files = []
    for root, _, names in os.walk(source_root):
        for name in names:
            if name.lower().endswith(".wav"):
                full = os.path.join(root, name)
                rel = os.path.relpath(full, source_root)
                files.append((full, rel.replace("\\", "/")))
    return files


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
    with open(src_path, "rb") as f, tqdm(total=size, unit="B", unit_scale=True, desc=sd_path) as pbar:
        for chunk_num in range(1, total_chunks + 1):
            chunk = f.read(CHUNK_SIZE)
            send_line(ser, "CHUNK:")
            ser.write(chunk)
            ser.flush()
            wait_ok(ser, f"OK:CHUNK {chunk_num}/{total_chunks}")
            pbar.update(len(chunk))

    send_line(ser, "END_UPLOAD")
    wait_ok(ser, "OK:WRITE_COMPLETE")
    wait_ok(ser, "OK:CRC_MATCH")
    wait_ok(ser, "OK:DONE")


def main():
    parser = argparse.ArgumentParser(description="Upload WAV voicepack files to CoreS3 SD over serial")
    parser.add_argument("--source", default="tools/voicepack/generated", help="Folder containing WAV files")
    parser.add_argument("--port", default="COM9", help="Serial port, e.g. COM9")
    parser.add_argument("--baud", default=115200, type=int, help="Serial baud")
    parser.add_argument("--sd-root", default="/voicepack", help="Target folder on SD")
    args = parser.parse_args()

    source = os.path.abspath(args.source)
    if not os.path.isdir(source):
        print(f"Source folder not found: {source}")
        return 1

    wav_files = find_wav_files(source)
    if not wav_files:
        print(f"No .wav files found under: {source}")
        return 1

    print(f"Found {len(wav_files)} WAV files in {source}")

    try:
        ser = serial.Serial(args.port, args.baud, timeout=10)
    except Exception as exc:
        print(f"Serial open failed on {args.port}: {exc}")
        return 1

    uploaded = 0
    try:
        for local_path, rel_path in wav_files:
            sd_path = f"{args.sd_root.rstrip('/')}/{rel_path}"
            upload_file(ser, local_path, sd_path)
            uploaded += 1
    except Exception as exc:
        print(f"Upload failed after {uploaded} files: {exc}")
        ser.close()
        return 1

    ser.close()
    print(f"Upload complete: {uploaded} files -> {args.sd_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
