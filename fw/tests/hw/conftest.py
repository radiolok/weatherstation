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
import re
import time

import pytest

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
