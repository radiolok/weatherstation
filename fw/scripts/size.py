#!/usr/bin/env python3
"""Compare firmware ROM/RAM usage with fw/size-budget.json.

    size.py --elf zephyr.elf --budget fw/size-budget.json [--json out.json]

Prints a table and, when a budget is exceeded, a GitHub Actions warning.
Always exits 0: the CI "size" job only warns.
"""
import argparse
import json
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.constants import SH_FLAGS


def usage(path):
    rom = ram = 0
    with open(path, "rb") as f:
        elf = ELFFile(f)
        for s in elf.iter_sections():
            flags = s["sh_flags"]
            if not flags & SH_FLAGS.SHF_ALLOC or s["sh_size"] == 0:
                continue
            size = s["sh_size"]
            nobits = s["sh_type"] == "SHT_NOBITS"
            writable = bool(flags & SH_FLAGS.SHF_WRITE)
            if nobits:
                ram += size
            elif writable:
                ram += size  # .data lives in RAM ...
                rom += size  # ... and its initial copy in flash
            else:
                rom += size
    return rom, ram


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--budget", required=True)
    ap.add_argument("--json")
    a = ap.parse_args()
    budget = json.load(open(a.budget))
    rom, ram = usage(a.elf)
    res = {"rom": rom, "ram": ram, "rom_budget": budget["rom_kb"] * 1024,
           "ram_budget": budget["ram_kb"] * 1024}
    for k in ("rom", "ram"):
        used, lim = res[k], res[k + "_budget"]
        pct = 100.0 * used / lim if lim else 0
        print(f"{k.upper():4} {used / 1024:9.1f} KB of {lim / 1024:7.0f} KB ({pct:5.1f} %)")
        if used > lim:
            print(f"::warning title=size::{k.upper()} {used / 1024:.1f} KB exceeds budget "
                  f"{lim / 1024:.0f} KB (fw/size-budget.json)")
    if a.json:
        json.dump(res, open(a.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
