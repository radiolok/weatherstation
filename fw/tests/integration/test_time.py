"""F6: SNTP against a local server, local time by POSIX TZ."""
from helpers import status, wait_until


def test_sntp_from_local_server(dut, ntp_server):
    dut.shell(f"ws set ntp.server1 127.0.0.1:{ntp_server['port']}")
    dut.shell("ws ntp")
    wait_until(lambda: dut.shell_kv("ws ntp")["synced"] == "yes", 10, what="NTP sync")
    assert ntp_server["requests"] >= 1
    # 2026-10-01 20:00 UTC is 23:00 in MSK-3
    t = status(dut)["time"]
    assert t.startswith("2026-10-01 23:0"), t
    dut.shell("ws set ntp.tz CET-1CEST,M3.5.0,M10.5.0/3")
    assert status(dut)["time"].startswith("2026-10-01 22:0")


def test_bad_tz_rejected(dut):
    out = dut.shell("ws set ntp.tz Moscow")
    assert any("ntp.tz" in l for l in out)
