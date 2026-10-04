"""Mock of the device web server for UI tests without a Zephyr build.

Serves fw/web/src as the device does and answers /api/* with the same JSON:
validation, catalogue and variables come from the host build of fw/lib
(build/host/wsapi, see fw/tests/host/wsapi.c), so the configuration compiler is
the real one. Everything the UI sends to the sign (preview, pin, lamp) is kept
in a log available at /__mock/log for assertions.

    python3 fw/tests/web/mock_server.py --port 8099 --wsapi build/host/wsapi
"""
import argparse
import json
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "fw/web/src"
FACTORY = ROOT / "fw/lib/screens/factory_screens.json"
TYPES = {".html": "text/html; charset=utf-8", ".js": "application/javascript",
         ".css": "text/css", ".json": "application/json"}

SAMPLE_VARS = {
    "out.t": -7.4, "out.cond": "snow", "out.wind": 4, "out.wind_dir": "nw",
    "fc.tmax": -3, "fc.tmin": -11, "fc.rain_from": 5, "fc.rain_to": 9, "fc.rain_in": 5,
    "fc.snow": True, "fc.hours": [[-7, 10], [-6, 20], [-5, 60], [-4, 80], [-4, 70], [-5, 30],
                                  [-6, 10], [-7, 0], [-8, 0], [-9, 0], [-10, 0], [-11, 0]],
    "in.t": 22.5, "in.rh": 41, "in.p": 748, "in.p_trend": -1.2, "in.co2": 850,
    "sun.up": True, "sys.mqtt": True,
}


class State:
    def __init__(self, wsapi):
        self.wsapi = str(wsapi)
        self.lock = threading.Lock()
        self.screens = FACTORY.read_text(encoding="utf-8")
        self.previous = None
        self.settings = {
            "wifi.ssid": "home", "wifi.psk_set": True, "mqtt.host": "192.168.1.10", "mqtt.port": 1883,
            "mqtt.user": "", "mqtt.pass_set": False, "ntp.server1": "pool.ntp.org", "ntp.server2": "",
            "ntp.interval": 60, "ntp.tz": "<+07>-7", "metar.icao": "UNNT",
            "metar.url1": "https://aviationweather.gov/api/data/metar?ids={icao}&format=raw",
            "metar.url2": "", "metar.period": 30, "geo.lat": 55030000, "geo.lon": 82920000,
            "web.user": "admin", "web.hash_set": False, "lamp.restore": "last", "lamp.last": 0,
            "sensor.t_offset": 0, "sensor.rh_offset": 0, "dev.id": "ws_a1b2c3", "metar.ca": "",
        }
        self.lamp = False
        self.log = []

    def run(self, cmd, data):
        p = subprocess.run([self.wsapi, cmd], input=data.encode(), capture_output=True, timeout=10)
        return p.returncode, p.stdout.decode()


class Handler(BaseHTTPRequestHandler):
    state: State = None

    def log_message(self, fmt, *args):  # quiet
        pass

    def send(self, code, body, ctype="application/json"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def json(self, code, obj):
        self.send(code, json.dumps(obj, ensure_ascii=False))

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n).decode("utf-8") if n else ""

    def do_GET(self):
        st = self.state
        path = self.path.split("?")[0]
        if path in ("/", "/index.html"):
            path = "/index.html"
        f = SRC / path.lstrip("/")
        if not path.startswith("/api") and not path.startswith("/__mock") and f.is_file():
            self.send(200, f.read_bytes(), TYPES.get(f.suffix, "application/octet-stream"))
            return
        with st.lock:
            if path == "/api/screens":
                self.send(200, st.screens)
            elif path == "/api/catalog":
                self.send(200, st.run("catalog", st.screens)[1])
            elif path == "/api/glyphs":
                g = json.loads((SRC / "glyphs.json").read_text(encoding="utf-8"))
                g["user_pictos"] = json.loads(st.screens).get("pictos", [])
                self.json(200, g)
            elif path == "/api/vars":
                self.send(200, st.run("vars", json.dumps(SAMPLE_VARS))[1])
            elif path == "/api/status":
                self.json(200, {"id": "ws_a1b2c3", "version": "mock", "uptime": 42, "net": "connected",
                                "ip": "127.0.0.1", "rssi": -55, "mqtt": True, "time": "2026-10-04 12:00:00",
                                "ntp_server": "pool.ntp.org", "ntp_last": 1, "screens_source": "user",
                                "lamp": st.lamp, "ota": "idle"})
            elif path == "/api/display/state":
                self.json(200, {"screen": "main", "name": "Основной", "reason": "по умолчанию",
                                "pinned": None, "candidates": [], "flips_24h": 3, "frames": 10})
            elif path == "/api/settings":
                self.json(200, st.settings)
            elif path == "/api/wifi/scan":
                self.json(200, {"networks": [{"ssid": "home", "rssi": -50, "open": False},
                                             {"ssid": "guest", "rssi": -70, "open": True}]})
            elif path == "/__mock/log":
                self.json(200, st.log)
            else:
                self.json(404, {"error": "нет такого ресурса"})

    def do_PUT(self):
        self.do_POST()

    def do_POST(self):
        st = self.state
        path = self.path.split("?")[0]
        data = self.body()
        with st.lock:
            st.log.append({"method": self.command, "path": path, "body": data})
            if path == "/api/screens/validate":
                rc, out = st.run("validate", data)
                self.send(200 if rc == 0 else 422, out)
            elif path == "/api/screens" and self.command == "PUT":
                rc, out = st.run("validate", data)
                if rc:
                    self.send(422, out)
                    return
                st.previous, st.screens = st.screens, data
                self.json(200, {"ok": True})
            elif path == "/api/screens/rollback":
                if not st.previous:
                    self.json(422, {"ok": False, "errors": [{"path": "", "msg": "нет предыдущей версии"}]})
                    return
                st.screens, st.previous = st.previous, st.screens
                self.json(200, {"ok": True})
            elif path == "/api/screens/factory":
                st.previous, st.screens = st.screens, FACTORY.read_text(encoding="utf-8")
                self.json(200, {"ok": True})
            elif path in ("/api/display/preview", "/api/display/pin"):
                self.json(200, {"ok": True})
            elif path == "/api/lamp":
                st.lamp = bool(json.loads(data).get("on"))
                self.json(200, {"ok": True})
            elif path == "/api/settings":
                upd = json.loads(data)
                errors = []
                if "metar.icao" in upd and not (len(upd["metar.icao"]) == 4 and upd["metar.icao"].isupper()):
                    errors.append({"path": "metar.icao", "msg": "четыре заглавные буквы, например UWGG"})
                if errors:
                    self.json(422, {"ok": False, "errors": errors})
                    return
                for k, v in upd.items():
                    if k in ("wifi.psk", "mqtt.pass", "web.password"):
                        st.settings[{"web.password": "web.hash"}.get(k, k) + "_set"] = bool(v)
                    else:
                        st.settings[k] = v
                self.json(200, {"ok": True})
            else:
                self.json(404, {"error": "нет такого ресурса"})


def serve(port, wsapi):
    Handler.state = State(wsapi)
    srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--wsapi", default=str(ROOT / "build/host/wsapi"))
    a = ap.parse_args()
    s = serve(a.port, a.wsapi)
    print(f"mock on http://127.0.0.1:{a.port}/")
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        s.shutdown()
