"""Playwright fixtures for the web UI.

Backend: the native_sim firmware (CI job "web", --web-backend=device) or the
mock server with the host build of fw/lib (--web-backend=mock, default when
zephyr.exe is not built). Both serve the same files from fw/web/src.
"""
import os
import socket
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
import mock_server  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]


def pytest_addoption(parser):
    parser.addoption("--web-backend", choices=["auto", "mock", "device"],
                     default=os.environ.get("WS_WEB_BACKEND", "auto"))
    parser.addoption("--wsapi", default=os.environ.get("WSAPI", str(ROOT / "build/host/wsapi")))


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture(scope="session")
def backend(pytestconfig):
    b = pytestconfig.getoption("--web-backend")
    if b == "auto":
        b = "device" if Path(pytestconfig.getoption("--zephyr-exe")).exists() else "mock"
    return b


@pytest.fixture
def base_url(backend, pytestconfig, request, tmp_path):
    if backend == "mock":
        wsapi = Path(pytestconfig.getoption("--wsapi"))
        if not wsapi.exists():
            subprocess.run([str(ROOT / "fw/scripts/host-tests.sh"), "nothing-matches"], check=False,
                           capture_output=True)
        if not wsapi.exists():
            pytest.skip(f"{wsapi} not built (fw/scripts/host-tests.sh)")
        port = free_port()
        srv = mock_server.serve(port, wsapi)
        yield f"http://127.0.0.1:{port}"
        srv.shutdown()
        return
    dut = request.getfixturevalue("dut")
    port = pytestconfig.getoption("--http-port")
    dut.wait_for(r"web server on port \d+: 0", timeout=15)
    yield f"http://127.0.0.1:{port}"


@pytest.fixture(scope="session")
def browser():
    sync_api = pytest.importorskip("playwright.sync_api")
    with sync_api.sync_playwright() as p:
        try:
            b = p.chromium.launch()
        except Exception as e:  # browser not installed
            pytest.skip(f"chromium: {e}")
        yield b
        b.close()


@pytest.fixture
def page(browser, base_url):
    ctx = browser.new_context(viewport={"width": 1400, "height": 900})
    pg = ctx.new_page()
    errors = []
    pg.on("pageerror", lambda e: errors.append(str(e)))
    pg.on("dialog", lambda d: d.accept())
    pg.goto(base_url + "/")
    pg.wait_for_selector("body.ready", timeout=15000)
    pg.errors = errors
    yield pg
    ctx.close()
    assert not errors, errors
