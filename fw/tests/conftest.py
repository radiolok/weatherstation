"""pytest harness for the native_sim application (fw/tests/integration, fw/tests/web).

The firmware runs as a host process (zephyr.exe) with its console on
stdin/stdout (CONFIG_UART_NATIVE_PTY_0_ON_STDINOUT). Tests talk to it through
the Zephyr shell, the same commands a person uses on the bench, and through
the network: native_sim uses host sockets (NSOS), so the HTTP server listens
on a host port and MQTT connects to a broker on the host.
"""
import os
import shutil
import socket
import queue
import re
import subprocess
import threading
import time
from pathlib import Path

import pytest

PROMPT = "uart:~$ "
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
# "[00:00:01.234,000] <inf> module: text"
LOG_LINE = re.compile(r"^\[\d{2}:\d{2}:\d{2}\.\d{3},\d{3}\] <")


def pytest_addoption(parser):
    parser.addoption("--zephyr-exe", default=os.environ.get("ZEPHYR_EXE", "build/native/zephyr/zephyr.exe"))
    parser.addoption("--log-dir", default=os.environ.get("WS_LOG_DIR", "build/pytest"))
    parser.addoption("--mqtt-host", default=os.environ.get("MQTT_HOST", "127.0.0.1"))
    parser.addoption("--mqtt-port", type=int, default=int(os.environ.get("MQTT_PORT", "1883")))
    parser.addoption("--http-port", type=int, default=int(os.environ.get("WS_HTTP_PORT", "8080")))


class Dut:
    """A running zephyr.exe with line-oriented console access."""

    def __init__(self, exe, workdir, log_path, args=()):
        self.exe = str(exe)
        self.workdir = Path(workdir)
        self.log_path = Path(log_path)
        self.args = list(args)
        self.proc = None
        self.strace_path = None
        self.lines = queue.Queue()
        self.history = []
        self.history_t = []  # wall time of each line, seconds since start
        self.t0 = time.monotonic()
        self.backlog = []
        self._reader = None
        self._log = None

    # -- process --------------------------------------------------------
    def start(self, extra_args=()):
        self.workdir.mkdir(parents=True, exist_ok=True)
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self._log = open(self.log_path, "a", encoding="utf-8", errors="replace")
        cmd = [self.exe, f"--flash={self.workdir / 'flash.bin'}", *self.args, *extra_args]
        if os.environ.get("WS_STRACE") and shutil.which("strace"):
            # host socket calls of NSOS, printed with a failure (CI diagnostics)
            self.strace_path = self.workdir / "strace.txt"
            cmd = ["strace", "-f", "--kill-on-exit", "-tt", "-o", str(self.strace_path), "-e",
                   "trace=%network,close,dup,epoll_ctl,eventfd2", *cmd]
        self._log.write(f"\n### start {' '.join(cmd)}\n")
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, cwd=self.workdir, bufsize=0)
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()
        return self

    def _read(self):
        buf = b""
        while True:
            chunk = self.proc.stdout.read(1)
            if not chunk:
                break
            buf += chunk
            if chunk == b"\n" or buf.endswith(PROMPT.encode()):
                line = ANSI.sub("", buf.decode("utf-8", errors="replace")).rstrip("\r\n")
                buf = b""
                log = self._log  # None once stop() closed it
                if log:
                    try:
                        log.write(line + "\n")
                        log.flush()
                    except ValueError:  # closed meanwhile
                        pass
                self.history_t.append(time.monotonic() - self.t0)
                self.history.append(line)
                self.lines.put(line)
        self.lines.put(None)

    def write(self, text):
        """Types text in pieces: the UART RX ring buffer of the shell is
        small, a long line written at once loses characters."""
        data = text.encode()
        for i in range(0, len(data), 32):
            self.proc.stdin.write(data[i:i + 32])
            self.proc.stdin.flush()
            time.sleep(0.01)

    def kill(self, sig=None):
        """Hard stop, like pulling the plug (no clean MQTT disconnect)."""
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(5)

    def stop(self):
        self.kill()
        if self._log:
            self._log.close()
            self._log = None

    def restart(self, extra_args=()):
        self.stop()
        self.lines = queue.Queue()
        return self.start(extra_args)

    # -- console --------------------------------------------------------
    def wait_for(self, pattern, timeout=10.0):
        """Waits for a console line matching `pattern`, returns the match.

        Log lines that shell() read past are kept in a backlog and searched
        first, so a log printed while a command ran is not lost."""
        rx = re.compile(pattern)
        while self.backlog:
            line = self.backlog.pop(0)
            m = rx.search(line)
            if m:
                return m
        deadline = time.monotonic() + timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                self.gdb_dump(f"no {pattern!r}")
                raise TimeoutError(f"no line matching {pattern!r} in {timeout} s")
            try:
                line = self.lines.get(timeout=left)
            except queue.Empty:
                continue
            if line is None:
                raise RuntimeError(f"zephyr.exe exited while waiting for {pattern!r}")
            m = rx.search(line)
            if m:
                return m

    def _keep(self, line):
        if LOG_LINE.match(line):
            self.backlog.append(line)
            del self.backlog[:-500]

    def gdb_dump(self, why):
        """Once per process: backtraces of all threads into the history
        (WS_GDB=1 in CI; every Zephyr thread of native_sim is a host thread,
        so this shows where each one waits)."""
        if not os.environ.get("WS_GDB") or getattr(self, "_gdb_done", False):
            return
        self._gdb_done = True
        if not self.proc or self.proc.poll() is not None or not shutil.which("gdb"):
            return
        cmd = ["gdb", "-p", str(self.proc.pid), "-batch", "-nx", "-ex", "set pagination off",
               "-ex", "thread apply all bt 25"]
        if os.geteuid() != 0 and shutil.which("sudo"):
            cmd = ["sudo", "-n", *cmd]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            text = r.stdout + r.stderr
        except Exception as e:  # noqa: BLE001 - diagnostics only
            text = str(e)
        now = time.monotonic() - self.t0
        for line in [f"---- gdb ({why}) ----", *text.splitlines(), "---- end gdb ----"]:
            self.history_t.append(now)
            self.history.append(line)

    def drain(self):
        while True:
            try:
                line = self.lines.get_nowait()
            except queue.Empty:
                return
            if line is not None:
                self._keep(line)

    def shell(self, command, timeout=5.0):
        """Runs a shell command, returns its output lines (without echo,
        prompt and log lines; log lines go to the backlog for wait_for).

        The echo of a long command is wrapped by the shell over several
        lines, so the echo is found in the concatenated text."""
        self.drain()
        self.write(command + "\n")
        out = []
        echo = ""
        deadline = time.monotonic() + timeout
        seen_echo = False
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                self.gdb_dump(f"shell command {command!r} hangs")
                raise TimeoutError(f"shell command {command!r} did not finish: {out}")
            try:
                line = self.lines.get(timeout=left)
            except queue.Empty:
                continue
            if line is None:
                raise RuntimeError("zephyr.exe exited")
            if LOG_LINE.match(line):
                self._keep(line)
                continue
            if not seen_echo:
                echo += line
                if command in echo:
                    seen_echo = True
                continue
            if line.rstrip().endswith(PROMPT.rstrip()):
                return out
            out.append(line)

    def shell_kv(self, command, timeout=5.0):
        """Parses `key: value` lines of a shell command into a dict."""
        res = {}
        for line in self.shell(command, timeout):
            if ":" in line:
                k, v = line.split(":", 1)
                res[k.strip()] = v.strip()
        return res


@pytest.fixture(scope="session")
def zephyr_exe(pytestconfig):
    exe = Path(pytestconfig.getoption("--zephyr-exe"))
    if not exe.exists():
        pytest.skip(f"{exe} not built (fw/scripts/build-native.sh)")
    return exe.resolve()


def wait_listening(port, timeout=10.0):
    """The HTTP server binds its socket in its own thread after the banner."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=1).close()
            return
        except OSError:
            time.sleep(0.05)


@pytest.fixture
def dut(zephyr_exe, pytestconfig, request, tmp_path):
    log_dir = Path(pytestconfig.getoption("--log-dir"))
    d = Dut(zephyr_exe, tmp_path, log_dir / f"{request.node.name}.log")
    d.start()
    d.wait_for(r"weatherstation \S+", timeout=15)
    wait_listening(pytestconfig.getoption("--http-port"))
    yield d
    rep = getattr(request.node, "rep_call", None)
    if rep is not None and rep.failed and d.proc.poll() is None:
        # thread states (who waits on what) for the CI output
        d.gdb_dump("test failed")
        for cmd in ("kernel uptime", "kernel thread list", "ws status", "ws net"):
            try:
                d.shell(cmd, timeout=5)
            except Exception as e:  # noqa: BLE001 - diagnostics only
                d.history_t.append(time.monotonic() - d.t0)
                d.history.append(f"<{cmd}: {e}>")
    d.stop()
    if rep is not None and rep.failed:
        # the device log in the CI output, next to the failure
        print(f"---- zephyr.exe console, last 700 of {len(d.history)} lines ----")
        # wall time next to the uptime of the log lines: is simulated time moving?
        rows = list(zip(d.history_t, d.history))[-700:]
        print("\n".join(f"{t:8.3f} {line}" for t, line in rows))
        if d.strace_path and d.strace_path.exists():
            tail = d.strace_path.read_text(errors="replace").splitlines()[-120:]
            print("---- strace, last 120 lines ----")
            print("\n".join(tail))


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    setattr(item, "rep_" + rep.when, rep)
