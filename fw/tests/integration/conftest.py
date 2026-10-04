"""Fixtures for the integration tests: broker client, NTP server, helpers."""
import json
import socket
import struct
import threading
import time

import pytest

from helpers import wait_until


@pytest.fixture
def ntp_server():
    """Minimal SNTP server on 127.0.0.1:<port> answering with a fixed time."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(0.2)
    port = sock.getsockname()[1]
    state = {"unix": 1790884800, "requests": 0, "run": True}  # 2026-10-01 20:00 UTC

    def serve():
        while state["run"]:
            try:
                data, addr = sock.recvfrom(512)
            except socket.timeout:
                continue
            if len(data) < 48:
                continue
            state["requests"] += 1
            ntp = state["unix"] + 2208988800
            # LI=0, VN=4, mode=4 (server), stratum 2
            pkt = struct.pack("!BBbbII4sIIIIIIII", 0x24, 2, 6, -20, 0, 0, b"TEST",
                              ntp, 0, struct.unpack("!I", data[40:44])[0],
                              struct.unpack("!I", data[44:48])[0], ntp, 0, ntp, 0)
            sock.sendto(pkt, addr)

    t = threading.Thread(target=serve, daemon=True)
    t.start()
    state["port"] = port
    yield state
    state["run"] = False
    t.join(1)
    sock.close()


@pytest.fixture
def broker(pytestconfig):
    """paho client connected to the test broker; skips when there is none."""
    paho = pytest.importorskip("paho.mqtt.client")
    host = pytestconfig.getoption("--mqtt-host")
    port = pytestconfig.getoption("--mqtt-port")
    try:
        socket.create_connection((host, port), timeout=1).close()
    except OSError:
        pytest.skip(f"no MQTT broker at {host}:{port}")

    class Broker:
        def __init__(self):
            self.messages = []
            self.lock = threading.Lock()
            self.host, self.port = host, port
            self.c = paho.Client(paho.CallbackAPIVersion.VERSION2)
            self.c.on_message = self._on_message
            self.c.connect(host, port)
            self.c.loop_start()

        def _on_message(self, client, userdata, msg):
            with self.lock:
                self.messages.append((msg.topic, msg.payload.decode("utf-8", "replace"), msg.retain))

        def subscribe(self, topic):
            self.c.subscribe(topic)

        def publish(self, topic, payload, retain=False):
            self.c.publish(topic, payload, retain=retain).wait_for_publish(5)

        def find(self, topic, pred=lambda p: True):
            with self.lock:
                for t, p, r in reversed(self.messages):
                    if t == topic and pred(p):
                        return p
            return None

        def wait(self, topic, pred=lambda p: True, timeout=10):
            return wait_until(lambda: self.find(topic, pred), timeout, what=f"MQTT {topic}")

        def clear(self, topic):
            # remove a retained message
            self.publish(topic, b"", retain=True)

        def close(self):
            self.c.loop_stop()
            self.c.disconnect()

    b = Broker()
    yield b
    b.close()

