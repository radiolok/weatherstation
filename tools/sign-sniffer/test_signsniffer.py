"""Tests of the bench sniffer without hardware.

The firmware's golden frames (fw/tests/golden/sign_golden.json) go through a
capture file and through a pseudo-terminal at the 4800 baud pace, so the
whole path - port setup, splitting, decoding, timing, Python API - is
checked the same way it runs on the bench.

    python3 -m unittest tools/sign-sniffer/test_signsniffer.py   (or pytest)
"""
import json
import os
import pty
import subprocess
import sys
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import signsniffer as ss  # noqa: E402

GOLDEN = os.path.join(ss.ROOT, "fw", "tests", "golden", "sign_golden.json")


def golden():
    with open(GOLDEN) as f:
        d = json.load(f)
    return [(g["name"], g["frame"], bytes(g["mobitec"])) for g in d["ops"] + d["raw"]]


def setUpModule():
    if not os.path.exists(ss.default_binary()):
        subprocess.run([os.path.join(HERE, "build.sh")], check=True, stdout=subprocess.DEVNULL)


class FileCapture(unittest.TestCase):
    def run_capture(self, data: bytes):
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
            f.write(data)
        try:
            sniff = ss.SignSniffer(file=f.name).start()
            stats = sniff.wait_done()
            sniff.stop()
            return sniff.frames_since(0), stats
        finally:
            os.unlink(f.name)

    def test_golden_frames_decode(self):
        """Every golden frame, with noise before it, decodes to its bitmap."""
        g = golden()
        data = b"".join(b"\x00\x13\x55" + raw for _, _, raw in g)
        frames, stats = self.run_capture(data)
        self.assertEqual(len(frames), len(g))
        for (name, rows, raw), f in zip(g, frames):
            with self.subTest(name):
                self.assertTrue(f.ok, f"code {f.code}")
                self.assertEqual(f.addr, 6)
                self.assertEqual(f.raw, raw)
                self.assertEqual(f.rows, rows)
                self.assertEqual(f.garbage, 3)
        self.assertEqual(stats["ok"], len(g))
        self.assertEqual(stats["garbage"], 3 * len(g))

    def test_line_timing_from_byte_count(self):
        """At 4800 8N1 a full frame of ~330 bytes takes ~0.69 s."""
        raw = ss.encode(ss.pattern_rows("checker"))
        frames, _ = self.run_capture(raw * 2)
        self.assertAlmostEqual(frames[0].dur_ms, (len(raw) - 1) * 10 / 4800 * 1000, delta=2)
        self.assertTrue(frames[1].same)
        self.assertEqual(frames[1].diff_dots, 0)

    def test_bad_checksum(self):
        raw = bytearray(ss.encode(ss.pattern_rows("columns")))
        raw[20] ^= 0x01  # one column of the first band
        frames, stats = self.run_capture(bytes(raw))
        self.assertEqual(len(frames), 1)
        self.assertFalse(frames[0].ok)
        self.assertEqual(frames[0].code, -5)
        self.assertEqual(stats["bad"], 1)

    def test_diff_against_previous(self):
        a = ss.pattern_rows("none")
        b = [r[:30].replace(".", "#") + r[30:] for r in a]  # left zone, 30 columns
        frames, _ = self.run_capture(ss.encode(a) + ss.encode(b))
        self.assertEqual(frames[1].diff_cols, 30)
        self.assertEqual(frames[1].diff_dots, 30 * 11)


class Encode(unittest.TestCase):
    def test_encode_matches_golden(self):
        for name, rows, raw in golden():
            with self.subTest(name):
                self.assertEqual(ss.encode(rows), raw)

    def test_rows_from_shell_output(self):
        """'ws sign frame' prints |...| rows between the prompt and logs."""
        rows = ss.pattern_rows("rows")
        text = ["uart:~$ ws sign frame"] + [f"|{r}|" for r in rows] + ["uart:~$ "]
        self.assertEqual(ss.parse_rows(text), rows)
        with self.assertRaises(ValueError):
            ss.parse_rows(text[:5])


class Pty(unittest.TestCase):
    """The program on a pseudo-terminal: termios setup, reads in bursts,
    timestamps, frames sent at the pace of a 4800 baud line."""

    def setUp(self):
        self.master, self.slave = pty.openpty()
        self.path = os.ttyname(self.slave)

    def tearDown(self):
        os.close(self.master)
        os.close(self.slave)

    def send_paced(self, data: bytes, baud=4800, chunk=48):
        per_byte = 10 / baud
        for i in range(0, len(data), chunk):
            os.write(self.master, data[i:i + chunk])
            time.sleep(len(data[i:i + chunk]) * per_byte)

    def test_frames_and_timing_on_a_port(self):
        a = ss.encode(ss.pattern_rows("checker"))
        b = ss.encode(ss.pattern_rows("all"))
        with ss.SignSniffer(port=self.path) as sniff:
            mark = sniff.mark()
            sender = threading.Thread(target=lambda: (self.send_paced(a), time.sleep(0.5),
                                                      self.send_paced(b)))
            sender.start()
            f1 = sniff.wait_frame(after=mark, timeout=5)
            f2 = sniff.wait_frame(after=mark + 1, timeout=5)
            sender.join()
        self.assertTrue(f1.ok and f2.ok)
        self.assertEqual(f1.rows, ss.pattern_rows("checker"))
        self.assertEqual(f2.rows, ss.pattern_rows("all"))
        # the frame takes ~0.69 s on the line; the reads come in bursts
        self.assertGreater(f1.dur_ms, 500)
        self.assertLess(f1.dur_ms, 1000)
        self.assertGreater(f2.gap_ms, 300)
        self.assertEqual(f2.diff_cols, 102)

    def test_wait_frame_times_out(self):
        with ss.SignSniffer(port=self.path) as sniff:
            with self.assertRaises(TimeoutError):
                sniff.wait_frame(timeout=0.5)

    def test_not_a_port(self):
        with tempfile.NamedTemporaryFile() as f:
            with self.assertRaises(RuntimeError):
                ss.SignSniffer(port=f.name).start()


if __name__ == "__main__":
    unittest.main()
