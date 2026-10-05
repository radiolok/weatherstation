#!/usr/bin/env python3
"""Bench sniffer for the Mobitec sign line: Python side.

Runs the sign-sniffer program (tools/sign-sniffer, the firmware's decoder
from fw/lib/sign) on a USB-RS485 adapter and collects decoded frames.

Library (used by fw/tests/hw):

    with SignSniffer(port="/dev/ttyUSB0") as sign:
        mark = sign.mark()
        ...                                  # make the board send something
        f = sign.wait_frame(lambda f: f.ok, after=mark, timeout=5)
        assert f.rows == expected_rows

Command line (live view in the terminal):

    python3 tools/sign-sniffer/signsniffer.py --port /dev/ttyUSB0 [--log frames.jsonl]

Only the standard library is needed. The program is built by
tools/sign-sniffer/build.sh; its path can be given with --bin or
SIGN_SNIFFER_BIN.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Callable, List, Optional

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W, H = 102, 11


def default_binary() -> str:
    env = os.environ.get("SIGN_SNIFFER_BIN")
    if env:
        return env
    build = os.environ.get("WS_BUILD", os.path.join(ROOT, "build"))
    path = os.path.join(build, "sign-sniffer", "sign-sniffer")
    if os.path.exists(path):
        return path
    return shutil.which("sign-sniffer") or path


@dataclass
class Frame:
    """One frame seen on the line."""
    n: int
    t: float                  # seconds since the sniffer started, first byte
    dur_ms: float             # first to last byte
    gap_ms: Optional[float]   # since the end of the previous frame
    length: int
    addr: int
    ok: bool                  # decoded and the checksum matches
    code: int                 # 0 or the decoder error
    same: bool                # byte for byte the same as the previous frame
    diff_dots: Optional[int]  # flipped dots against the previous decoded frame
    diff_cols: Optional[int]
    garbage: int              # bytes outside frames before this one
    line_errors: int          # framing/parity errors before this one
    raw: bytes
    rows: Optional[List[str]] = None
    received: float = field(default_factory=time.monotonic)

    @classmethod
    def from_json(cls, d: dict) -> "Frame":
        return cls(n=d["n"], t=d["t"], dur_ms=d["dur_ms"], gap_ms=d["gap_ms"],
                   length=d["len"], addr=d["addr"], ok=d["ok"], code=d["code"],
                   same=d["same"], diff_dots=d["diff_dots"], diff_cols=d["diff_cols"],
                   garbage=d["garbage"], line_errors=d["line_errors"],
                   raw=bytes.fromhex(d["hex"]), rows=d["rows"])

    def dot(self, x: int, y: int) -> bool:
        return bool(self.rows) and self.rows[y][x] == "#"


def parse_rows(lines) -> List[str]:
    """Rows of '#'/'.' from text such as the output of 'ws sign frame'
    ("|#.#...|" lines); other lines are ignored."""
    rows = []
    for line in lines:
        s = line.strip().strip("|")
        if len(s) == W and set(s) <= {"#", "."}:
            rows.append(s)
    if len(rows) != H:
        raise ValueError(f"expected {H} rows of {W} dots, got {len(rows)}")
    return rows


def pattern_rows(name: str) -> List[str]:
    """The test patterns of 'ws sign pattern' (fw/src/display/display.c)."""
    def on(x, y):
        return {"checker": (x + y) % 2 == 0, "columns": x % 2 == 0, "rows": y % 2 == 0,
                "all": True, "none": False}[name]
    return ["".join("#" if on(x, y) else "." for x in range(W)) for y in range(H)]


def encode(rows: List[str], binary: Optional[str] = None) -> bytes:
    """Mobitec frame bytes for a bitmap, made by the firmware's encoder."""
    out = subprocess.run([binary or default_binary(), "--encode"], input="\n".join(rows) + "\n",
                         capture_output=True, text=True, check=True)
    return bytes.fromhex(out.stdout.strip())


class SignSniffer:
    """Runs sign-sniffer on a port or a capture file and keeps the frames."""

    def __init__(self, port: Optional[str] = None, file: Optional[str] = None,
                 baud: int = 4800, binary: Optional[str] = None, log: Optional[str] = None):
        if (port is None) == (file is None):
            raise ValueError("give either port or file")
        self.args = [binary or default_binary(), "--baud", str(baud)]
        self.args += ["--port", port] if port else ["--file", file]
        self.frames: List[Frame] = []
        self.stats: dict = {}
        self.errors: List[str] = []
        self._cond = threading.Condition()
        self._proc: Optional[subprocess.Popen] = None
        self._log = open(log, "a") if log else None
        self._done = False
        # set from the program's "port" line: can the driver count line errors?
        self.line_errors_supported: Optional[bool] = None

    # -- lifetime --------------------------------------------------------------
    def start(self) -> "SignSniffer":
        self._proc = subprocess.Popen(self.args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      text=True, bufsize=1)
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()
        threading.Thread(target=self._read_err, daemon=True).start()
        # the program prints "start" once the port is open; fail early otherwise
        with self._cond:
            self._cond.wait_for(lambda: self._started or self._done, timeout=5)
        if not self._started:
            self.stop()
            raise RuntimeError("sign-sniffer did not start: " + " ".join(self.errors))
        return self

    _started = False

    def stop(self) -> dict:
        if self._proc and self._proc.poll() is None:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._proc.kill()
        if self._proc:
            self._reader.join(timeout=5)
            for pipe in (self._proc.stdout, self._proc.stderr):
                if pipe:
                    pipe.close()
        if self._log:
            self._log.close()
            self._log = None
        return self.stats

    def wait_done(self, timeout: float = 30) -> dict:
        """For file input: waits for the end of the file."""
        with self._cond:
            self._cond.wait_for(lambda: self._done, timeout=timeout)
        return self.stats

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.stop()

    # -- frames ----------------------------------------------------------------
    def mark(self) -> int:
        """Position to wait or count from: frames after it are new."""
        with self._cond:
            return len(self.frames)

    def frames_since(self, mark: int) -> List[Frame]:
        with self._cond:
            return list(self.frames[mark:])

    def last(self) -> Optional[Frame]:
        with self._cond:
            return self.frames[-1] if self.frames else None

    def wait_frame(self, pred: Optional[Callable[[Frame], bool]] = None, after: Optional[int] = None,
                   timeout: float = 10) -> Frame:
        """First frame after `after` (default: now) for which pred is true."""
        start = self.mark() if after is None else after
        deadline = time.monotonic() + timeout
        i = start
        with self._cond:
            while True:
                while i < len(self.frames):
                    f = self.frames[i]
                    i += 1
                    if pred is None or pred(f):
                        return f
                left = deadline - time.monotonic()
                if left <= 0 or self._done:
                    seen = len(self.frames) - start
                    raise TimeoutError(f"no matching frame in {timeout} s ({seen} frames seen)")
                self._cond.wait(left)

    # -- reader ----------------------------------------------------------------
    def _read(self):
        for line in self._proc.stdout:
            if self._log:
                self._log.write(line)
                self._log.flush()
            try:
                d = json.loads(line)
            except ValueError:
                continue
            with self._cond:
                kind = d.get("type")
                if kind == "start":
                    self._started = True
                elif kind == "port":
                    self.line_errors_supported = d.get("line_errors_supported", False)
                elif kind == "frame":
                    self.frames.append(Frame.from_json(d))
                elif kind == "stats":
                    self.stats = d
                self._cond.notify_all()
        with self._cond:
            self._done = True
            self._cond.notify_all()

    def _read_err(self):
        for line in self._proc.stderr:
            self.errors.append(line.strip())


# -- live view -------------------------------------------------------------------

def render(f: Frame) -> str:
    on, off = "█", "·"
    lines = []
    for y in range(H):
        lines.append("".join(on if f.dot(x, y) else off for x in range(W)))
    return "\n".join(lines)


def live(args) -> int:
    sniff = SignSniffer(port=args.port, file=args.file, baud=args.baud, binary=args.bin, log=args.log)
    sniff.start()
    tty = sys.stdout.isatty()
    shown = 0
    try:
        while True:
            try:
                f = sniff.wait_frame(after=shown, timeout=1)
            except TimeoutError:
                if sniff._done:
                    break
                continue
            shown = f.n
            frames = sniff.frames_since(0)
            bad = sum(1 for x in frames if not x.ok)
            errs = sum(x.line_errors for x in frames)
            garbage = sum(x.garbage for x in frames)
            if tty:
                sys.stdout.write("\x1b[H\x1b[2J")
            if f.rows:
                print(render(f))
            else:
                print(f"frame #{f.n}: decoder error {f.code}")
            gap = "-" if f.gap_ms is None else f"{f.gap_ms / 1000:.1f} s"
            diff = "-" if f.diff_cols is None else f"{f.diff_dots} dots / {f.diff_cols} cols"
            print(f"\n#{f.n}  t={f.t:.1f} s  {f.length} bytes in {f.dur_ms:.0f} ms  gap {gap}  "
                  f"changed {diff}{'  (repeat)' if f.same else ''}")
            print(f"frames {len(frames)}  bad {bad}  line errors {errs}  garbage bytes {garbage}")
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    stats = sniff.stop()
    print(json.dumps(stats))
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description="Sign line sniffer: live view of decoded frames")
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="USB-RS485 adapter, e.g. /dev/ttyUSB0")
    src.add_argument("--file", help="raw capture to replay")
    p.add_argument("--baud", type=int, default=4800)
    p.add_argument("--bin", help="sign-sniffer program (default: build/sign-sniffer/sign-sniffer)")
    p.add_argument("--log", help="append every frame as a JSON line to this file")
    return live(p.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
