"""F10: firmware update into slot 1 of the simulated flash, self-test,
task watchdog and MCUmgr over UDP.

On native_sim there is no MCUboot: the tests stop at the marked slot and the
reboot; the slot swap itself is H-check territory (fw/tests/hw).
"""
import asyncio
import hashlib
import json
import os
import subprocess
import threading
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

from helpers import connect_mqtt, wait_until

ROOT = Path(__file__).resolve().parents[3]
IMAGE = os.urandom(64 * 1024 + 123)  # not a multiple of the flash write block
SHA = hashlib.sha256(IMAGE).hexdigest()


@pytest.fixture
def image_server():
    hits = []

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_GET(self):
            hits.append(self.path)
            if self.path != "/ws.bin":
                self.send_response(404)
                self.end_headers()
                return
            self.send_response(200)
            self.send_header("Content-Length", str(len(IMAGE)))
            self.end_headers()
            self.wfile.write(IMAGE)

    srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    yield f"http://127.0.0.1:{srv.server_address[1]}", hits
    srv.shutdown()


def ota(dut):
    return dut.shell_kv("ws ota status")


def http_port(pytestconfig):
    return pytestconfig.getoption("--http-port")


def test_download_verify_mark_reboot(dut, image_server):
    base, hits = image_server
    out = dut.shell(f"ws ota get {base}/ws.bin {SHA} 9.9.9")
    assert "request: 0" in out, out
    m = dut.wait_for(r"slot 1: (\d+) bytes, sha256 ([0-9a-f]{64})", timeout=60)
    assert int(m.group(1)) == len(IMAGE)
    assert m.group(2) == SHA
    dut.wait_for(r"image 9\.9\.9 ready, rebooting", timeout=10)
    # native_sim restarts the executable on reboot with the same flash file
    dut.wait_for(r"weatherstation \S+", timeout=30)
    st = ota(dut)
    assert st["state"] == "idle"
    # slot 1 is marked for a test boot: MCUboot would swap on this reboot
    assert st["swap_type"] in ("2", "3"), st
    assert hits == ["/ws.bin"]


def test_sha_mismatch_is_rejected(dut, image_server):
    base, _ = image_server
    bad = "0" * 64
    assert "request: 0" in dut.shell(f"ws ota get {base}/ws.bin {bad}")
    wait_until(lambda: ota(dut)["state"] == "failed", 60, what="failed state")
    assert ota(dut)["error"] == "sha256 не совпадает"
    assert ota(dut)["swap_type"] not in ("2", "3"), "nothing marked"


def test_bad_requests(dut, image_server):
    base, _ = image_server
    assert "request: 0" not in dut.shell(f"ws ota get ftp://x/y {SHA}")
    assert "request: 0" not in dut.shell(f"ws ota get {base}/ws.bin 1234")
    assert "request: 0" in dut.shell(f"ws ota get {base}/missing.bin {SHA}")
    wait_until(lambda: ota(dut)["state"] == "failed", 30, what="404 fails")
    assert "загрузка" in ota(dut)["error"]


def upload(pytestconfig, data, sha=None):
    req = urllib.request.Request(f"http://127.0.0.1:{http_port(pytestconfig)}/api/ota/upload", data=data,
                                 method="POST", headers={"Content-Type": "application/octet-stream"})
    if sha:
        req.add_header("X-Image-Sha256", sha)
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read().decode() or "{}")


def test_web_upload(dut, pytestconfig):
    code, body = upload(pytestconfig, IMAGE, SHA)
    assert code == 200, body
    assert body["state"] == "ready"
    assert body["bytes"] == len(IMAGE)
    dut.wait_for(r"weatherstation \S+", timeout=30)


def test_web_upload_wrong_hash(dut, pytestconfig):
    code, body = upload(pytestconfig, IMAGE, "f" * 64)
    assert code == 422, body
    assert body["error"] == "sha256 не совпадает"


def test_selftest_times_out_and_reboots(dut):
    dut.shell("ws ota selftest 3")
    dut.wait_for(r"rebooting \(самопроверка не пройдена\)", timeout=15)
    dut.wait_for(r"weatherstation \S+", timeout=30)


def test_selftest_confirmed_by_broker(dut, broker):
    connect_mqtt(dut, broker)
    dut.shell("ws ota selftest 3")
    dut.wait_for(r"image confirmed", timeout=10)
    st = ota(dut)
    assert st["state"] == "idle" and st["confirmed"] == "yes"


def test_mqtt_command_starts_download(dut, broker, image_server):
    base, hits = image_server
    dev = connect_mqtt(dut, broker)
    broker.publish(f"ws/{dev}/ota", json.dumps({"url": f"{base}/ws.bin", "sha256": SHA, "version": "9.9.10"}))
    dut.wait_for(r"image 9\.9\.10 ready", timeout=60)
    dut.wait_for(r"weatherstation \S+", timeout=30)


def test_watchdog_catches_a_hung_thread(dut):
    st = dut.shell_kv("ws wdt status")
    assert st["running"] == "yes"
    assert any("ws_net" in v for v in st.values()), st
    dut.proc.stdin.write(b"ws wdt hang 20\n")
    dut.proc.stdin.flush()
    dut.wait_for(r"watchdog: thread 'shell-test' hung", timeout=10)
    dut.wait_for(r"weatherstation \S+", timeout=30)


def test_smp_over_udp(dut, tmp_path):
    """MCUmgr: echo and an image upload into slot 1 with smpclient (smpmgr's library)."""
    smpclient = pytest.importorskip("smpclient")
    from smpclient.requests.image_management import ImageStatesRead
    from smpclient.requests.os_management import EchoWrite
    from smpclient.transport.udp import SMPUDPTransport

    payload = tmp_path / "payload.bin"
    payload.write_bytes(os.urandom(20000))
    signed = tmp_path / "signed.bin"
    key = ROOT / "fw/keys/dev-ecdsa-p256-TEST-ONLY.pem"
    try:
        subprocess.run(["imgtool", "sign", "--key", str(key), "--header-size", "0x200", "--pad-header",
                        "--align", "4", "--version", "9.9.11", "--slot-size", "0x100000",
                        str(payload), str(signed)], check=True, capture_output=True)
    except (FileNotFoundError, subprocess.CalledProcessError) as e:
        pytest.skip(f"imgtool: {e}")
    dut.wait_for(r"SMP over UDP port 1337: 0", timeout=20)

    async def run():
        async with smpclient.SMPClient(SMPUDPTransport(), "127.0.0.1") as c:
            r = await c.request(EchoWrite(d="weatherstation"))
            assert r.r == "weatherstation"
            async for _ in c.upload(signed.read_bytes()):
                pass
            states = await c.request(ImageStatesRead())
            return states

    states = asyncio.run(asyncio.wait_for(run(), 120))
    slots = {s.slot: s for s in states.images}
    assert 1 in slots and slots[1].version.startswith("9.9.11"), states
