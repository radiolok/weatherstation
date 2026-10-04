"""F5: emulated sensors reach the variables, button and lamp through gpio-emul."""
from helpers import status, wait_until


def vars_of(dut):
    out = {}
    for line in dut.shell("ws vars"):
        if ":" in line:
            k, v = line.split(":", 1)
            out[k.strip()] = v.split()[0] if v.split() else ""
    return out


def test_bme280_and_mhz19b(dut):
    dut.shell("ws emul thp 22.5 99725 41")
    dut.shell("ws emul co2 1240")
    dut.shell("ws sensors read")
    v = vars_of(dut)
    assert v["in.t"] == "22.5"
    assert v["in.rh"] == "41"
    assert v["in.p"] == "748"
    assert v["in.co2"] == "1240"


def test_sensor_errors_are_counted(dut):
    dut.shell("ws emul co2 bad")
    before = int(dut.shell_kv("ws sensors")["co2_errors"])
    dut.shell("ws sensors read")
    assert int(dut.shell_kv("ws sensors")["co2_errors"]) > before
    dut.shell("ws emul thp fail")
    dut.shell("ws sensors read")
    assert int(dut.shell_kv("ws sensors")["thp_errors"]) >= 1


def test_lamp_restored_after_reboot(dut):
    assert dut.shell_kv("ws emul gpio")["lamp"] == "0"
    dut.shell("ws lamp on")
    wait_until(lambda: dut.shell_kv("ws emul gpio")["lamp"] == "1", 3, what="lamp on")
    assert dut.shell_kv("ws lamp")["lamp"] == "on"
    dut.restart()
    dut.wait_for(r"weatherstation \S+", timeout=15)
    # lamp.restore = last (default)
    wait_until(lambda: dut.shell_kv("ws emul gpio")["lamp"] == "1", 5, what="lamp restored")


def test_short_press_pins_next_screen(dut):
    first = status(dut)["screen"]
    dut.shell("ws emul button press 200")
    wait_until(lambda: status(dut)["reason"] == "кнопка", 5, what="debug pin")
    assert status(dut)["screen"] != first


def test_long_press_starts_access_point(dut):
    dut.shell("ws emul button press 5500", timeout=10)
    wait_until(lambda: dut.shell_kv("ws net")["state"] == "ap", 5, what="AP mode")
