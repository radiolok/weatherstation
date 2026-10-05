"""Hardware checks H1-H19 (fw/docs/implementation-plan.md) with
`west twister --device-testing`.

The twister pytest plugin (pytest-twister-harness) flashes the board and
gives the `dut`/`shell` fixtures; fw/tests/conftest.py defines a `dut` for
native_sim, so this file puts the twister one back for fw/tests/hw.

Checks that need instruments (oscilloscope, clamp meter, reference
thermometer, a router to reboot) ask the operator when pytest runs with
--hw-operator (and -s); otherwise they are skipped with the procedure in
the reason, so a plain run does every automatic part.
"""
import os
import re
import sys
import time

import pytest

# the bench sniffer (tools/sign-sniffer) for the virtual-sign checks
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tools", "sign-sniffer"))
import signsniffer  # noqa: E402

try:
    from twister_harness.fixtures import dut  # noqa: F401  (override the native_sim fixture)
except ImportError:  # not under twister: nothing to run here
    collect_ignore = ["test_hw.py"]


def pytest_addoption(parser):
    parser.addoption("--hw-operator", action="store_true",
                     help="ask the operator to do and confirm the manual steps")
    parser.addoption("--hw-ssid", default=None, help="Wi-Fi network for H12/H13")
    parser.addoption("--hw-psk", default=None)
    parser.addoption("--hw-broker", default=None, help="MQTT broker host[:port] for H15/H16")
    parser.addoption("--hw-ota-url", default=None, help="URL of a signed image for H16")
    parser.addoption("--hw-ota-sha", default=None)
    parser.addoption("--sign-port", default=None,
                     help="USB-RS485 adapter on the sign line (virtual sign, "
                          "fw/docs/bench-virtual-sign.md); enables the automatic H3-H5, H18 checks")
    parser.addoption("--sign-baud", type=int, default=4800)
    parser.addoption("--hw-h5-minutes", type=float, default=30,
                     help="length of the H5 run (30 for the release)")


class Operator:
    def __init__(self, enabled):
        self.enabled = enabled

    def step(self, text):
        """Shows a manual step and waits for the operator's verdict."""
        if not self.enabled:
            pytest.skip(f"manual step (run with --hw-operator -s): {text}")
        ans = input(f"\n[H] {text}\n    passed? [y/n] ").strip().lower()
        assert ans.startswith("y"), f"operator: failed - {text}"

    def value(self, text):
        if not self.enabled:
            pytest.skip(f"manual measurement (run with --hw-operator -s): {text}")
        return input(f"\n[H] {text}\n    value: ").strip()


@pytest.fixture
def operator(pytestconfig):
    return Operator(pytestconfig.getoption("--hw-operator"))


@pytest.fixture
def kv(shell):
    """`key: value` output of a shell command as a dict."""
    def run(cmd, timeout=10):
        out = {}
        for line in shell.exec_command(cmd, timeout=timeout):
            m = re.match(r"\s*([^:]+):\s*(.*)$", line)
            if m:
                out[m.group(1).strip()] = m.group(2).strip()
        return out
    return run


def wait_kv(kv, cmd, key, pred, timeout=60, step=2):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = kv(cmd).get(key)
        if last is not None and pred(last):
            return last
        time.sleep(step)
    raise AssertionError(f"{cmd}: {key}={last!r} after {timeout} s")


@pytest.fixture(scope="session")
def sign(pytestconfig):
    """Decoded frames from the sign line, or None without --sign-port.

    Runs tools/sign-sniffer on the USB-RS485 adapter for the whole session and
    keeps a JSON log of every frame next to the pytest output.
    """
    port = pytestconfig.getoption("--sign-port")
    if not port:
        yield None
        return
    log = os.path.join(str(pytestconfig.rootpath), "sign-frames.jsonl")
    sniff = signsniffer.SignSniffer(port=port, baud=pytestconfig.getoption("--sign-baud"), log=log)
    sniff.start()
    yield sniff
    sniff.stop()


def assert_clean_line(frames, line_errors_supported=True):
    """Every frame decoded, no bytes between frames, no framing errors."""
    bad = [f.n for f in frames if not f.ok]
    assert not bad, f"frames that do not decode: {bad}"
    garbage = sum(f.garbage for f in frames)
    assert garbage == 0, f"{garbage} bytes outside frames"
    if line_errors_supported:
        errs = sum(f.line_errors for f in frames)
        assert errs == 0, f"{errs} framing/parity errors: wrong baud rate, A/B swapped or noise"


def device_rows(shell):
    """The last frame the firmware sent, as 'ws sign frame' prints it."""
    return signsniffer.parse_rows(shell.exec_command("ws sign frame", timeout=10))
