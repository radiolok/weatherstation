"""F2: a broken screens.json never leaves the sign empty."""
from helpers import status


def test_broken_current_uses_previous_or_factory(dut):
    dut.shell("ws cfg factory")     # writes screens.json
    dut.shell("ws cfg corrupt")
    dut.restart()
    dut.wait_for(r"weatherstation \S+", timeout=15)
    kv = dut.shell_kv("ws cfg status")
    assert kv["source"] in ("previous", "factory")
    assert int(kv["screens"]) == 5
    assert status(dut)["screen"]


def test_both_broken_uses_factory(dut):
    dut.shell("ws cfg factory")
    dut.shell("ws cfg corrupt both")
    dut.restart()
    dut.wait_for(r"weatherstation \S+", timeout=15)
    assert dut.shell_kv("ws cfg status")["source"] == "factory"
