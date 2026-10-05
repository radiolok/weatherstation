# sign-sniffer

The virtual sign of the bench: it reads the Mobitec line through a USB-RS485 adapter and decodes the frames with the firmware's own code (`fw/lib/sign`). The bench and the checks are described in [fw/docs/bench-virtual-sign.md](../../fw/docs/bench-virtual-sign.md).

| File | What it is |
| --- | --- |
| `sign_sniffer.c` | The program: port or capture file in, one JSON line per frame out; `--encode` makes a frame from a bitmap |
| `signsniffer.py` | Python API for the hardware tests (`SignSniffer`, `encode`, `pattern_rows`, `parse_rows`) and a live view in the terminal |
| `test_signsniffer.py` | Tests without hardware: the golden frames through a file and through a pseudo-terminal at 4800 baud |

## Build and run

```sh
tools/sign-sniffer/build.sh            # -> build/sign-sniffer/sign-sniffer ($WS_BUILD overrides build/)
python3 tools/sign-sniffer/signsniffer.py --port /dev/ws-rs485 --log frames.jsonl
python3 -m unittest -v tools/sign-sniffer/test_signsniffer.py
```

Only CMake, a C compiler and the Python standard library are needed. The program runs on Linux; line error counters come from `TIOCGICOUNT`, which the FTDI driver supports.

## Program output

```sh
sign-sniffer --port /dev/ws-rs485 [--baud 4800]
sign-sniffer --file capture.bin        # or --file - for stdin
sign-sniffer --encode < rows.txt       # 11 lines of 102 '#'/'.', optional '|' around them
```

One JSON object per line:

- `{"type":"start",...}` once the source is open;
- `{"type":"port","line_errors_supported":true}` for a port;
- `{"type":"frame",...}` for every frame between `FF` delimiters: `n`, `t` (s since start), `dur_ms`, `gap_ms`, `len`, `addr`, `ok`, `code` (decoder error, 0 if fine), `same` (byte for byte the previous frame), `diff_dots`, `diff_cols`, `garbage` and `line_errors` since the previous frame, `hex`, `rows`;
- `{"type":"overflow"}` when bytes run past the longest frame without a delimiter;
- `{"type":"stats",...}` totals at the end (Ctrl-C, end of file).

Byte times are estimated from the read time and the line speed, so `dur_ms` and `gap_ms` are accurate to a few milliseconds with an FTDI adapter (its latency timer is 16 ms by default).

## In the hardware tests

`fw/tests/hw/conftest.py` starts the sniffer for the whole session when pytest gets `--sign-port`:

```python
def test_something(shell, sign):
    mark = sign.mark()
    shell.exec_command("ws sign pattern checker")
    f = sign.wait_frame(lambda f: f.rows == signsniffer.pattern_rows("checker"), after=mark, timeout=5)
    assert f.raw == signsniffer.encode(f.rows)
```
