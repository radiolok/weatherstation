"""F9: METAR fallback over HTTPS (spec section 14).

A local HTTPS server with a test CA plays the weather service. The device
trusts the test CA through the metar.ca setting (POST /api/settings, the PEM
does not fit a shell line). A second server with a certificate of another CA
must be rejected, and the device must switch to the fallback URL.
"""
import datetime
import json
import ssl
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pytest

from helpers import set_clock, wait_until

x509 = pytest.importorskip("cryptography.x509")
from cryptography.hazmat.primitives import hashes, serialization  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import ec  # noqa: E402
from cryptography.x509.oid import NameOID  # noqa: E402

NOW = 1791114600  # 2026-10-04 11:50 UTC
REPORT = "UNNT 041130Z 24005MPS 9999 -FZRA OVC010 M02/M03 Q1012 R25/290245 NOSIG"


def make_ca(name):
    key = ec.generate_private_key(ec.SECP256R1())
    subj = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
    now = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(subj).issuer_name(subj).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now)
            .not_valid_after(now + datetime.timedelta(days=3650))
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
            .sign(key, hashes.SHA256()))
    return key, cert


def make_server_cert(ca_key, ca_cert, tmp_path, tag):
    key = ec.generate_private_key(ec.SECP256R1())
    now = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc)
    cert = (x509.CertificateBuilder()
            .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")]))
            .issuer_name(ca_cert.subject).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now)
            .not_valid_after(now + datetime.timedelta(days=365))
            .add_extension(x509.SubjectAlternativeName([x509.DNSName("localhost")]), critical=False)
            .sign(ca_key, hashes.SHA256()))
    crt = tmp_path / f"{tag}.crt"
    kf = tmp_path / f"{tag}.key"
    crt.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    kf.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                     serialization.NoEncryption()))
    return crt, kf


class MetarServer:
    def __init__(self, crt, kf):
        self.hits = []
        self.body = "2026/10/04 11:30\n" + REPORT + "\n"
        outer = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                outer.hits.append(self.path)
                data = outer.body.encode()
                self.send_response(200 if self.path.endswith("UNNT.TXT") else 404)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        self.srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(crt, kf)
        self.srv.socket = ctx.wrap_socket(self.srv.socket, server_side=True)
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def url(self):
        return f"https://localhost:{self.port}/{{icao}}.TXT"

    def close(self):
        self.srv.shutdown()


@pytest.fixture
def metar_servers(tmp_path):
    ca_key, ca = make_ca("weatherstation test CA")
    rogue_key, rogue = make_ca("someone else")
    good = MetarServer(*make_server_cert(ca_key, ca, tmp_path, "good"))
    bad = MetarServer(*make_server_cert(rogue_key, rogue, tmp_path, "bad"))
    yield {"ca": ca.public_bytes(serialization.Encoding.PEM).decode(), "good": good, "bad": bad}
    good.close()
    bad.close()


def api(pytestconfig, path, body=None):
    port = pytestconfig.getoption("--http-port")
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}",
                                 data=json.dumps(body).encode() if body is not None else None,
                                 headers={"Content-Type": "application/json"},
                                 method="POST" if body is not None else "GET")
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read().decode())


def configure(dut, pytestconfig, servers, url1, url2):
    set_clock(dut, NOW)
    api(pytestconfig, "/api/settings", {"metar.icao": "UNNT", "metar.url1": url1, "metar.url2": url2,
                                        "metar.ca": servers["ca"]})


def var(pytestconfig, name):
    return next(v for v in api(pytestconfig, "/api/vars")["vars"] if v["name"] == name)


def test_fetch_and_map(dut, pytestconfig, metar_servers):
    good = metar_servers["good"]
    configure(dut, pytestconfig, metar_servers, good.url(), "")
    out = dut.shell("ws metar fetch", timeout=30)
    assert "fetch: 0" in out, out
    assert "/UNNT.TXT" in good.hits
    st = dut.shell_kv("ws metar status")
    assert st["report"] == REPORT
    assert var(pytestconfig, "obs.t")["value"] == -2
    assert var(pytestconfig, "obs.ice")["value"] is True
    assert var(pytestconfig, "obs.age")["value"] == 20  # 11:30 -> 11:50
    # no forecast at all: out.* come from METAR
    t = var(pytestconfig, "out.t")
    assert t["value"] == -2 and t["src"] == "metar"


def test_wrong_certificate_then_fallback_url(dut, pytestconfig, metar_servers):
    good, bad = metar_servers["good"], metar_servers["bad"]
    configure(dut, pytestconfig, metar_servers, bad.url(), good.url())
    out = dut.shell("ws metar fetch", timeout=40)
    assert "fetch: 0" in out, out
    assert bad.hits == [], "the request must not reach a server with a foreign certificate"
    assert "/UNNT.TXT" in good.hits
    assert dut.shell_kv("ws metar status")["source"] == "2"


def test_wrong_certificate_only(dut, pytestconfig, metar_servers):
    bad = metar_servers["bad"]
    configure(dut, pytestconfig, metar_servers, bad.url(), "")
    out = dut.shell("ws metar fetch", timeout=40)
    assert "fetch: 0" not in out
    assert bad.hits == []
    assert dut.shell_kv("ws metar status")["report"] == "-"


def test_screen_falls_back_and_ages(dut, pytestconfig, metar_servers):
    """Spec F9 'done': no forecast -> METAR data on the sign; stale METAR -> 'Нет прогноза'."""
    good = metar_servers["good"]
    configure(dut, pytestconfig, metar_servers, good.url(), "")
    assert "fetch: 0" in dut.shell("ws metar fetch", timeout=30)
    wait_until(lambda: api(pytestconfig, "/api/display/state")["screen"] != "noforecast", 10,
               what="screen with METAR data")
    # 2 hours later the report is older than 90 minutes
    set_clock(dut, NOW + 2 * 3600)
    time.sleep(0.5)
    wait_until(lambda: api(pytestconfig, "/api/display/state")["screen"] == "noforecast", 75,
               what="'no forecast' screen")


def test_shell_parse_without_network(dut, pytestconfig):
    set_clock(dut, NOW)
    out = dut.shell("ws metar parse UNNT 040300Z 00000MPS CAVOK M15/M18 Q1030 R88/790250 RMK QFE745/0994")
    assert "ok" in out
    assert var(pytestconfig, "obs.p")["value"] == 745
    assert var(pytestconfig, "obs.ice")["value"] is True
    assert "not a METAR" in " ".join(dut.shell("ws metar parse HELLO"))
