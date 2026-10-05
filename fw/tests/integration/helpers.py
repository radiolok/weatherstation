"""Helpers shared by the integration and web tests."""
import json
import time


def sign_frame(dut):
    """The frame seen on the emulated RS-485 line, as 11 strings of '#' and '.'."""
    out = dut.shell("ws emul sign")
    rows = [l.strip("|") for l in out if l.startswith("|")]
    stats = {k.strip(): v.strip() for k, v in (l.split(":", 1) for l in out if ":" in l and not l.startswith("|"))}
    return rows, stats


def wait_until(fn, timeout=10.0, step=0.2, what="condition"):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = fn()
        if last:
            return last
        time.sleep(step)
    raise AssertionError(f"timeout waiting for {what} (last: {last!r})")


def status(dut):
    return dut.shell_kv("ws status")


def set_clock(dut, unix):
    dut.shell(f"ws time set {int(unix)}")


def forecast(dut, obj):
    # single quotes keep the JSON in one shell argument
    dut.shell("ws forecast '" + json.dumps(obj, ensure_ascii=False) + "'")


def connect_mqtt(dut, broker):
    """Connects the device and waits until it has subscribed: "connected"
    comes with CONNACK, the subscriptions follow, and a command published in
    between is lost. The session publishes display/cfg after subscribing;
    a fresh (not retained) copy of it marks the moment."""
    broker.subscribe("ws/#")
    dut.shell(f"ws set mqtt.host {broker.host}")
    dut.shell(f"ws set mqtt.port {broker.port}")
    wait_until(lambda: "connected" == status(dut)["mqtt"], 15, what="MQTT connected")
    dev = status(dut)["id"]

    def subscribed():
        with broker.lock:
            return any(t == f"ws/{dev}/display/cfg" and not r for t, _, r in broker.messages)
    wait_until(subscribed, 10, what="MQTT session set up")
    return dev
