"""H1-H19: checks on the real board (fw/docs/implementation-plan.md, table
"Проверки на железе"). Stand A: devkit on the bench; stand B: the station
assembled with the sign. Each test does what the console can check and asks
the operator for the rest (--hw-operator -s).
"""
import json
import re
import time

import pytest

import signsniffer
from conftest import assert_clean_line, device_rows, wait_kv


# ---- H1-H2: flashing, memory ------------------------------------------------

def test_h1_boot_from_mcuboot(dut, shell, kv, operator):
    """H1: the image starts from MCUboot with the partition layout of the overlay."""
    out = "\n".join(dut.readlines_until(regex=r"weatherstation \S+", timeout=30))
    assert "MCUboot" in out or "Booting Zephyr" in out, out
    st = kv("ws ota status")
    assert st["confirmed"] == "yes"
    operator.step("OpenOCD/GDB: halt, single step and a breakpoint in main() work "
                  "(west debug -d build/esp32s3)")


def test_h2_memory(dut, shell, operator):
    """H2: stack headroom >= 25 % on every thread; flash and PSRAM in the boot log."""
    lines = shell.exec_command("kernel thread stacks", timeout=10)
    worst = (100, "")
    for line in lines:
        m = re.search(r"(\S+)\s+\(real size (\d+)\):\s+unused (\d+)", line)
        if m:
            pct = int(m.group(3)) * 100 // int(m.group(2))
            worst = min(worst, (pct, m.group(1)))
    assert worst[0] >= 25, "\n".join(lines)
    operator.step("Boot log: flash ID and 16 MB size, 8 MB PSRAM detected; "
                  "'kernel heap'/'ws status' show free heap after an hour of work")


# ---- H3-H5: sign ------------------------------------------------------------
# With --sign-port the line is read by the bench sniffer (virtual sign, stand A,
# fw/docs/bench-virtual-sign.md) and these checks are automatic. Without it
# they fall back to the operator. What only the real sign shows (orientation,
# rustle of the dots) stays with the operator on stand B.

# A frame of 330 bytes at 4800 8N1 takes 687 ms; USB adapters hand bytes over
# in bursts, so the measured time is a little off.
FRAME_MS = (550, 850)
KEEPALIVE_S = 30


def wait_rows(sign, rows, after, timeout=15):
    return sign.wait_frame(lambda f: f.ok and f.rows == rows, after=after, timeout=timeout)


@pytest.mark.parametrize("pattern", ["checker", "columns", "rows", "all", "none"])
def test_h3_frame_on_rs485(shell, kv, sign, operator, pattern):
    """H3: the frame on the line is the reference frame byte for byte, 4800 8N1, ~0.7 s."""
    rows = signsniffer.pattern_rows(pattern)
    mark = sign.mark() if sign else 0
    shell.exec_command(f"ws sign pattern {pattern}")
    try:
        if not sign:
            time.sleep(2)
            assert int(kv("ws sign stats")["frames"]) >= 1
            operator.step(f"Sniffer on the RS-485 line: the '{pattern}' frame decodes byte for byte "
                          "like the reference, 4800 8N1, one frame takes ~0.7 s")
            return
        f = wait_rows(sign, rows, mark)
        assert f.addr == 0x06
        assert f.raw == signsniffer.encode(rows), "bytes differ from the encoder's reference"
        assert FRAME_MS[0] <= f.dur_ms <= FRAME_MS[1], f"frame took {f.dur_ms} ms"
        assert_clean_line(sign.frames_since(mark), sign.line_errors_supported)
    finally:
        shell.exec_command("ws sign pattern auto")


def test_h3b_clean_line_on_boot(dut, shell, sign):
    """H3b: reset and boot put nothing on the line but whole frames.

    GPIO17 floats during reset; with DE tied high the driver repeats whatever
    DI sees. Garbage here means DI needs a pull-up (10 kOhm to 3.3 V).
    """
    if not sign:
        pytest.skip("needs the virtual sign: --sign-port")
    mark = sign.mark()
    dut.write(b"kernel reboot cold\n")
    dut.readlines_until(regex=r"weatherstation \S+", timeout=60)
    sign.wait_frame(lambda f: f.ok, after=mark, timeout=30)
    time.sleep(5)
    frames = sign.frames_since(mark)
    assert frames[0].garbage == 0, f"{frames[0].garbage} bytes on the line before the first frame"
    assert_clean_line(frames, sign.line_errors_supported)


@pytest.mark.parametrize("pattern", ["checker", "columns", "rows", "all", "none"])
def test_h4_frame_on_sign(shell, sign, operator, pattern):
    """H4: orientation and bit order on the real sign (stand B)."""
    mark = sign.mark() if sign else 0
    shell.exec_command(f"ws sign pattern {pattern}")
    try:
        if sign:
            wait_rows(sign, signsniffer.pattern_rows(pattern), mark)
        else:
            time.sleep(2)
        operator.step(f"The sign shows '{pattern}' correctly (orientation, bit order); "
                      "a repeated frame does not rustle the dots")
    finally:
        shell.exec_command("ws sign pattern auto")


@pytest.mark.parametrize("sid", ["main", "stuffy", "rain", "evening", "noforecast"])
def test_h4_factory_screens(shell, sign, operator, sid):
    """H4: every factory screen reaches the line exactly as the firmware drew it."""
    mark = sign.mark() if sign else 0
    shell.exec_command(f"ws sign show {sid} 1")
    try:
        if sign:
            # the zones may turn between the frame and 'ws sign frame': retry
            for _ in range(3):
                f = sign.wait_frame(lambda f: f.ok, after=mark, timeout=15)
                if f.rows == device_rows(shell):
                    break
                mark = f.n
            else:
                pytest.fail(f"screen '{sid}': the line differs from 'ws sign frame'")
            assert_clean_line(sign.frames_since(mark), sign.line_errors_supported)
        else:
            time.sleep(3)
        if operator.enabled:
            operator.step(f"Screen '{sid}' on the sign looks like in the web simulator")
    finally:
        shell.exec_command("ws sign auto")


def test_h5_quiet_zone_changes(shell, kv, sign, operator, pytestconfig):
    """H5: in normal operation only the zones change, at most 30 columns at a
    time, and the frame is repeated byte for byte every 30 s."""
    minutes = pytestconfig.getoption("--hw-h5-minutes")
    if not sign:
        if not operator.enabled:
            pytest.skip("needs the virtual sign (--sign-port) or --hw-operator")
        before = kv("ws sign stats")
        time.sleep(minutes * 60)
        after = kv("ws sign stats")
        assert int(after["frames"]) > int(before["frames"])
        assert int(after["last_cols"]) <= 30, after
        return
    # pin the default screen so the rules do not switch screens during the run
    shell.exec_command(f"ws sign show main {int(minutes) + 2}")
    sign.wait_frame(lambda f: f.ok, timeout=15)
    mark = sign.mark()
    time.sleep(minutes * 60)
    shell.exec_command("ws sign auto")
    frames = sign.frames_since(mark)
    assert len(frames) >= minutes * 60 / KEEPALIVE_S - 2, f"only {len(frames)} frames"
    assert_clean_line(frames, sign.line_errors_supported)
    changes = [f for f in frames if f.diff_cols]
    repeats = [f for f in frames if f.diff_cols == 0]
    assert all(f.same for f in repeats), "a repeated picture was sent with different bytes"
    too_big = [(f.n, f.diff_cols) for f in changes if f.diff_cols > 30]
    assert not too_big, f"changes wider than a zone (frame, columns): {too_big}"
    gaps = [f.gap_ms / 1000 for f in frames if f.gap_ms is not None]
    assert max(gaps) <= KEEPALIVE_S + 5, f"longest pause {max(gaps):.1f} s"
    if minutes >= 2:
        assert changes, "no zone changed during the run"


# ---- H6-H7: sensors ---------------------------------------------------------

def test_h6_bme280(kv, operator):
    st = kv("ws sensors read")
    assert st["t"] != "--" and st["rh"] != "--" and st["p"] != "--", st
    assert int(st["thp_errors"]) == 0
    ref_t = float(operator.value("Reference thermometer, °C (after 1 h on the table, then in the case)"))
    ref_rh = float(operator.value("Reference hygrometer, %RH"))
    t = float(st["t"].split()[0])
    rh = float(st["rh"].split()[0])
    assert abs(t - ref_t) <= 0.5, (t, ref_t)
    assert abs(rh - ref_rh) <= 5, (rh, ref_rh)


def test_h7_mhz19b(kv, operator):
    time.sleep(1)
    st = kv("ws sensors read")
    assert st["co2"] != "--", st
    operator.step("After 3 min warm-up and airing: co2 reads ~420 ppm and stays stable")
    assert int(kv("ws sensors")["co2_errors"]) == 0


# ---- H8-H11: power and IO ---------------------------------------------------

def test_h8_lamp_key(shell, kv, operator):
    shell.exec_command("ws lamp on")
    operator.step("Lamp on at 1.5 A: 5 V rail does not drop (clamp meter on 24 V), "
                  "transistor heats <= 30 °C above ambient after 1 h")
    shell.exec_command("ws lamp off")


def test_h9_lamp_at_start(operator):
    operator.step("Scope on the gate: power-up, flashing and reset never open the key")


def test_h10_button_and_led(kv, operator):
    operator.step("Held at power-up: access point starts; long press 5 s: AP; "
                  "short presses switch screens without double steps")


def test_h11_power(operator):
    operator.step("5 V ripple during Wi-Fi TX and 24 V peak current with a full sign "
                  "change and the lamp on: no brown-outs, peak within 60 W")


# ---- H12-H17: network, time, MQTT, OTA, watchdog ----------------------------

@pytest.fixture
def online(shell, kv, pytestconfig):
    ssid = pytestconfig.getoption("--hw-ssid")
    if ssid:
        shell.exec_command(f"ws set wifi.ssid {ssid}")
        psk = pytestconfig.getoption("--hw-psk")
        if psk:
            shell.exec_command(f"ws set wifi.psk {psk}")
    try:
        wait_kv(kv, "ws net", "state", lambda s: s == "online", timeout=60)
    except AssertionError:
        pytest.skip("no Wi-Fi (--hw-ssid/--hw-psk)")


def test_h12_wifi_sta(kv, online, operator):
    rssi = kv("ws status")
    assert rssi["ip"] not in ("", "0.0.0.0")
    operator.step("Reboot the router: the station comes back online by itself")
    wait_kv(kv, "ws net", "state", lambda s: s == "online", timeout=180)
    operator.step("RSSI in the closed case with the external antenna is better than -75 dBm "
                  "(ws vars: sys.wifi_rssi)")


def test_h13_access_point(shell, kv, operator):
    scan_before = [v for k, v in kv("ws net").items() if k == "scan"]
    shell.exec_command("ws ap")
    wait_kv(kv, "ws net", "state", lambda s: s == "ap", timeout=30)
    operator.step("From a phone: join the AP, open the page, the network list is visible "
                  f"(seen before AP: {scan_before}), save Wi-Fi; the station gets online")


def test_h14_ntp_and_https(shell, kv, online):
    shell.exec_command("ws ntp")
    wait_kv(kv, "ws ntp", "synced", lambda s: s == "yes", timeout=30)
    shell.exec_command("ws set metar.icao UNNT")
    out = shell.exec_command("ws metar fetch", timeout=60)
    assert any("fetch: 0" in l for l in out), out
    st = kv("ws metar status")
    assert st["report"].startswith("UNNT "), st


def test_h15_mqtt_home_assistant(shell, kv, online, pytestconfig, operator):
    broker = pytestconfig.getoption("--hw-broker")
    if not broker:
        pytest.skip("--hw-broker host[:port]")
    host, _, port = broker.partition(":")
    shell.exec_command(f"ws set mqtt.host {host}")
    shell.exec_command(f"ws set mqtt.port {port or 1883}")
    wait_kv(kv, "ws mqtt", "connected", lambda s: s == "yes", timeout=60)
    operator.step("Home Assistant: entities appeared (discovery); lamp and screen selection "
                  "from HA react within 1 s")


def test_h16_ota(shell, kv, online, pytestconfig, operator):
    url = pytestconfig.getoption("--hw-ota-url")
    sha = pytestconfig.getoption("--hw-ota-sha")
    if not (url and sha):
        pytest.skip("--hw-ota-url/--hw-ota-sha of a signed image (CI artifact zephyr.signed.bin)")
    out = shell.exec_command(f"ws ota get {url} {sha}")
    assert any("request: 0" in l for l in out), out
    operator.step("The board reboots, MCUboot swaps, the new image reaches the broker and "
                  "'ws ota status' shows confirmed: yes")
    operator.step("Power cut during a download: the old image keeps running")
    operator.step("An image that cannot reach the broker reverts after 120 s")
    operator.step("smpmgr --ip <board> upgrade <signed image>: works over UDP (dev build)")


def test_h17_watchdog(dut, shell, kv):
    st = kv("ws wdt status")
    assert st["running"] == "yes"
    dut.write(b"ws wdt hang 30\n")
    dut.readlines_until(regex=r"watchdog: thread 'shell-test' hung", timeout=15)
    dut.readlines_until(regex=r"weatherstation \S+", timeout=60)


# ---- H18-H19: long runs -----------------------------------------------------

def test_h18_interference(shell, kv, sign, operator):
    """H18: switching the lamp does not break frames or sensor reads."""
    if not sign and not operator.enabled:
        pytest.skip("needs the virtual sign (--sign-port) or stand B with --hw-operator")
    before = kv("ws sensors")
    mark = sign.mark() if sign else 0
    t0 = time.monotonic()
    for _ in range(50):
        shell.exec_command("ws lamp on")
        time.sleep(0.5)
        shell.exec_command("ws lamp off")
        time.sleep(0.5)
    if sign and time.monotonic() - t0 < KEEPALIVE_S + 5:
        time.sleep(KEEPALIVE_S + 5 - (time.monotonic() - t0))  # at least one frame on the line
    after = kv("ws sensors read")
    assert after["thp_errors"] == before["thp_errors"]
    assert after["co2_errors"] == before["co2_errors"]
    if sign:
        frames = sign.frames_since(mark)
        assert frames, "no frames on the line while the lamp switched"
        assert_clean_line(frames, sign.line_errors_supported)
    else:
        operator.step("No broken frames on the sign while the lamp switched 50 times")


def test_h19_long_run(shell, kv, operator):
    if not operator.enabled:
        pytest.skip("72 h run on stand B: --hw-operator")
    hours = float(operator.value("Run length in hours (72 for the release)"))
    heap0 = "\n".join(shell.exec_command("kernel heap"))
    t_end = time.monotonic() + hours * 3600
    while time.monotonic() < t_end:
        st = kv("ws status")
        assert st["mqtt"] == "connected" or st["net"] != "2", st
        time.sleep(600)
    heap1 = "\n".join(shell.exec_command("kernel heap"))
    operator.step(f"Heap at start/end comparable (no leak):\n{heap0}\n---\n{heap1}")
