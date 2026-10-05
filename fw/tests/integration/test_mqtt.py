"""F7: the whole MQTT/Home Assistant scenario against a real broker:
start -> discovery -> forecast -> screen -> lamp -> link lost -> offline."""
import json

from helpers import connect_mqtt, set_clock, status, wait_until


def test_mqtt_scenario(dut, broker):
    broker.subscribe("ws/#")
    broker.subscribe("homeassistant/#")
    dev = connect_mqtt(dut, broker)
    base = f"ws/{dev}"

    # online + discovery
    broker.wait(f"{base}/status", lambda p: p == "online")
    disc = json.loads(broker.wait(f"homeassistant/select/{dev}/screen/config"))
    assert "Авто" in disc["options"] and "Обычный" in disc["options"]
    light = json.loads(broker.wait(f"homeassistant/light/{dev}/lamp/config"))
    assert light["command_topic"] == f"{base}/lamp/set"
    broker.wait(f"homeassistant/sensor/{dev}/t/config")
    broker.wait(f"{base}/display/cfg", lambda p: p.startswith('{"schema":1'))

    # retained forecast with rain in 2 h -> "rain" screen within 10 s
    set_clock(dut, 1790863200)  # 17:00 MSK
    fc = {"now": {"t": 3, "cond": "rain", "wind": 4, "dir": "s", "rh": 90, "p": 741},
          "day": {"max": 5, "min": 1}, "rain": {"from": 19, "to": 22},
          "flags": {"snow": False, "ice": False, "storm": False}}
    broker.publish(f"{base}/forecast", json.dumps(fc), retain=True)
    st = json.loads(broker.wait(f"{base}/display/state", lambda p: '"screen":"rain"' in p, 15))
    assert "fc.rain_in" in st["reason"]
    assert status(dut)["screen"] == "rain"

    # sensors every 30 s (and on request)
    dut.shell("ws emul thp 21.0 99725 40")
    dut.shell("ws sensors read")
    dut.shell("ws mqtt publish")
    s = json.loads(broker.wait(f"{base}/sensors", lambda p: '"t":21' in p))
    assert s["rh"] == 40

    # lamp from Home Assistant
    broker.publish(f"{base}/lamp/set", "ON")
    wait_until(lambda: dut.shell_kv("ws emul gpio")["lamp"] == "1", 3, what="lamp GPIO")
    broker.wait(f"{base}/lamp/state", lambda p: p == "ON", 3)

    # screen select from Home Assistant, then "Авто"
    broker.publish(f"{base}/display/pin", "Вечер дома")
    broker.wait(f"{base}/display/state", lambda p: '"screen":"evening"' in p and '"pinned":true' in p)
    broker.publish(f"{base}/display/pin", "Авто")
    broker.wait(f"{base}/display/state", lambda p: '"pinned":false' in p)

    # link lost: the broker publishes the last will
    dut.kill()
    broker.wait(f"{base}/status", lambda p: p == "offline", 120)
    broker.clear(f"{base}/forecast")


def test_cfg_set_validates(dut, broker):
    broker.subscribe("ws/#")
    dev = connect_mqtt(dut, broker)
    base = f"ws/{dev}"
    bad = {"schema": 1, "screens": [{"id": "a", "default": True,
                                     "items": [{"type": "icon", "x": 0}, {"type": "icon", "x": 5}]}]}
    broker.publish(f"{base}/display/cfg/set", json.dumps(bad))
    res = json.loads(broker.wait(f"{base}/display/cfg/result"))
    assert res["ok"] is False
    assert res["errors"][0]["path"] == "screens[0].items[1]"
    assert dut.shell_kv("ws cfg status")["screens"] == "5"  # unchanged


def test_ext_variable(dut, broker):
    broker.subscribe("ws/#")
    dev = connect_mqtt(dut, broker)
    cfg = json.loads(broker.wait(f"ws/{dev}/display/cfg"))
    cfg["ext"] = [{"id": "ext.1", "name": "Балкон", "topic": "test/balcony",
                   "field": "temperature", "unit": "°C", "ttl": 600}]
    broker.publish(f"ws/{dev}/display/cfg/set", json.dumps(cfg))
    assert json.loads(broker.wait(f"ws/{dev}/display/cfg/result"))["ok"] is True
    def ext():
        broker.publish("test/balcony", json.dumps({"temperature": 7.3}))
        out = [l for l in dut.shell("ws vars") if l.startswith("ext.1:")]
        return out and "7.3" in out[0]
    wait_until(ext, 10, step=1, what="ext.1 from MQTT")
