#!/usr/bin/env python3
"""Checks that every CONFIG_ symbol in the firmware .conf files is defined in
some Kconfig file of the workspace (Zephyr, its modules, fw/). A cheap
pre-build check: a typo in a .conf file fails the Zephyr build late.

    check-kconfig.py --workspace <west topdir>
"""
import argparse
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def kconfig_symbols(dirs):
    syms = set()
    rx = re.compile(r"^\s*(?:menu)?config\s+([A-Za-z0-9_]+)", re.M)
    for d in dirs:
        for base, _, files in os.walk(d):
            if "/.git" in base:
                continue
            for f in files:
                if f.startswith("Kconfig") or f.endswith(".kconfig"):
                    try:
                        syms.update(rx.findall(open(os.path.join(base, f), errors="replace").read()))
                    except OSError:
                        pass
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workspace", required=True)
    a = ap.parse_args()
    dirs = [os.path.join(a.workspace, "zephyr"), os.path.join(a.workspace, "modules"),
            os.path.join(a.workspace, "bootloader"), os.path.join(ROOT, "fw")]
    syms = kconfig_symbols([d for d in dirs if os.path.isdir(d)])
    bad = 0
    for base, _, files in os.walk(os.path.join(ROOT, "fw")):
        for f in files:
            if not f.endswith(".conf"):
                continue
            p = os.path.join(base, f)
            for n, line in enumerate(open(p), 1):
                m = re.match(r"\s*(SB_)?CONFIG_([A-Za-z0-9_]+)=", line)
                if not m:
                    continue
                if m.group(2) not in syms:
                    print(f"{os.path.relpath(p, ROOT)}:{n}: unknown symbol {m.group(1) or ''}CONFIG_{m.group(2)}")
                    bad += 1
    print(f"checked against {len(syms)} symbols, {bad} unknown")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
