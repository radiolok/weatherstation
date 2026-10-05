#!/usr/bin/env python3
"""Merge MCUboot and the signed application into one image for the first
USB flashing of a blank ESP32-S3 (offsets from fw/boards/esp32s3_ws_partitions.dtsi).

    merge_bin.py --out merged.bin 0x0:mcuboot.bin 0x20000:zephyr.signed.bin

Flash it with:  esptool.py --chip esp32s3 write_flash 0x0 merged.bin
"""
import argparse
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("parts", nargs="+", help="offset:file")
    a = ap.parse_args()
    image = bytearray()
    for part in a.parts:
        off_s, path = part.split(":", 1)
        off = int(off_s, 0)
        data = open(path, "rb").read()
        if off < len(image):
            sys.exit(f"{path} at {off:#x} overlaps the previous part (ends {len(image):#x})")
        image += b"\xff" * (off - len(image)) + data
    open(a.out, "wb").write(image)
    print(f"{a.out}: {len(image)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
