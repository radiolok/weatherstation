"""F4: the sign service puts the selected screen on the RS-485 line."""
import time

from helpers import forecast, set_clock, sign_frame, status, wait_until

FC = {"now": {"t": -2, "cond": "pcloud", "wind": 5, "dir": "nw", "rh": 78, "p": 748},
      "day": {"max": 4, "min": -7}, "rain": None,
      "flags": {"snow": False, "ice": False, "storm": False}}


def test_frames_reach_the_line(dut):
    rows, st = wait_until(lambda: (lambda r: r if int(r[1]["frames"]) > 0 else None)(sign_frame(dut)),
                          5, what="first frame")
    assert len(rows) == 11 and all(len(r) == 102 for r in rows)
    assert int(st["errors"]) == 0
    # what the service thinks it sent is what the sniffer decoded
    sent = [l.strip("|") for l in dut.shell("ws sign frame") if l.startswith("|")]
    assert sent == rows


def test_screen_follows_variables(dut):
    set_clock(dut, 1790863200)  # 2026-10-01 14:00 UTC = 17:00 MSK, Thursday
    forecast(dut, FC)
    wait_until(lambda: status(dut)["screen"] == "main", 15, what="main screen")
    # dry: rain screen comes when rain is less than 3 h away
    fc = dict(FC, rain={"from": 19, "to": 21})
    forecast(dut, fc)
    wait_until(lambda: status(dut)["screen"] == "rain", 15, what="rain screen")
    assert "fc.rain_in" in status(dut)["reason"]
    rows, _ = sign_frame(dut)
    assert any("#" in r for r in rows)


def test_patterns_and_pin(dut):
    dut.shell("ws sign pattern checker")
    want = ["".join("#" if (x + y) % 2 == 0 else "." for x in range(102)) for y in range(11)]
    wait_until(lambda: sign_frame(dut)[0] == want, 5, what="checker on the line")
    dut.shell("ws sign show evening")
    wait_until(lambda: status(dut)["screen"] == "evening", 5, what="pinned screen")
    assert status(dut)["pinned"] == "evening"
    dut.shell("ws sign auto")
    wait_until(lambda: status(dut)["pinned"] == "no", 5, what="auto")


def test_unchanged_frame_is_repeated(dut):
    wait_until(lambda: int(sign_frame(dut)[1]["frames"]) > 0, 5, what="first frame")
    _, st0 = sign_frame(dut)
    time.sleep(32)
    _, st1 = sign_frame(dut)
    stats = dut.shell_kv("ws sign stats")
    # at least one repeat in 30 s even though nothing changed
    assert int(st1["frames"]) > int(st0["frames"])
    assert int(stats["repeats"]) >= 1
