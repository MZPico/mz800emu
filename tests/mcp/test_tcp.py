#!/usr/bin/env python3
"""Manual end-to-end test of the MCP TCP transport (V0.A.5).

This test is NOT integrated into ctest in V0.A.5 - it requires a
running emulator with the TCP listener enabled. Two supported ways
to drive it:

  A) Manual: start mz800emu(.exe) via Tools -> MCP TCP Server -> Start,
     then run this script. Default loopback port 23800.
  B) Headless: start mz800emu(.exe) --headless --mcp-tcp-port=23800
     in another shell, then run this script.

The script verifies the same dispatch surface as test_pipe.py (V0.A.3
9-command set), but over a TCP socket instead of stdin/stdout pipe.

Exit code: 0 if all assertions PASS, 1 otherwise.

Future (V0.B): ctest integration via subprocess spawn of mz800emu
with --headless --mcp-tcp-port=PORT, similar to test_pipe.py.
"""

import argparse
import json
import socket
import sys
import time


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 23800


def _connect(host: str, port: int, timeout: float = 5.0) -> socket.socket:
    """Open a TCP connection to the MCP TCP listener.

    Polls connect() for up to `timeout` seconds before giving up. Returns
    a connected socket configured for line buffered stream reads.
    """
    deadline = time.monotonic() + timeout
    last_err = None
    while time.monotonic() < deadline:
        try:
            s = socket.create_connection((host, port), timeout=2.0)
            s.settimeout(5.0)
            return s
        except OSError as e:
            last_err = e
            time.sleep(0.2)
    raise RuntimeError(f"TCP connect to {host}:{port} failed: {last_err}")


def _readline(f) -> str:
    """Read a JSONL line from the buffered stream wrapper (strip newline)."""
    line = f.readline()
    if not line:
        raise RuntimeError("server closed connection unexpectedly")
    return line.rstrip("\r\n")


def _send(f, payload: dict) -> None:
    """Send a JSON request followed by '\\n', flush the stream."""
    f.write(json.dumps(payload) + "\n")
    f.flush()


def _assert(cond: bool, msg: str) -> None:
    if not cond:
        print(f"FAIL: {msg}", file=sys.stderr)
        sys.exit(1)


def run_e2e(host: str, port: int) -> None:
    """Connect to MCP TCP server and exercise the V0.A.3 dispatch surface."""
    s = _connect(host, port)
    f = s.makefile("rw", buffering=1, encoding="utf-8", newline="\n")

    # 0. Read hello
    hello_line = _readline(f)
    hello = json.loads(hello_line)
    _assert(hello.get("type") == "hello",
            f"expected hello message, got: {hello_line}")
    _assert(isinstance(hello.get("commands"), list),
            "hello message missing commands list")
    print(f"OK hello (version={hello.get('version')}, "
          f"cmds={len(hello.get('commands', []))})")

    # 1. ping
    _send(f, {"req_id": 1, "cmd": "ping"})
    resp = json.loads(_readline(f))
    _assert(resp.get("success") is True, f"ping failed: {resp}")
    _assert(resp.get("data", {}).get("pong") is True, f"ping: pong missing: {resp}")
    print("OK ping")

    # 2. pause
    _send(f, {"req_id": 2, "cmd": "pause"})
    resp = json.loads(_readline(f))
    _assert(resp.get("success") is True, f"pause failed: {resp}")
    print("OK pause")

    # 3. get_state (should report paused)
    _send(f, {"req_id": 3, "cmd": "get_state"})
    resp = json.loads(_readline(f))
    _assert(resp.get("success") is True, f"get_state failed: {resp}")
    _assert(resp.get("data", {}).get("paused") is True,
            f"get_state: not paused: {resp}")
    print("OK get_state (paused)")

    # 4. get_registers
    _send(f, {"req_id": 4, "cmd": "get_registers"})
    resp = json.loads(_readline(f))
    _assert(resp.get("success") is True, f"get_registers failed: {resp}")
    regs = resp.get("data", {})
    # V0.A.3 returns AF, BC, DE, HL, IX, IY, SP, PC, AF', BC', DE', HL', I, R
    for key in ("PC", "SP", "AF", "BC", "DE", "HL"):
        _assert(key in regs, f"get_registers: missing {key}: {regs}")
    print(f"OK get_registers (PC={regs.get('PC'):#06x})")

    # 5. mem_read addr=0xE800 len=16
    _send(f, {"req_id": 5, "cmd": "mem_read",
              "params": {"addr": 0xE800, "len": 16}})
    resp = json.loads(_readline(f))
    _assert(resp.get("success") is True, f"mem_read failed: {resp}")
    _assert("base64" in resp.get("data", {}),
            f"mem_read: base64 missing: {resp}")
    print("OK mem_read")

    # 6. bp_add addr=0xE800
    _send(f, {"req_id": 6, "cmd": "bp_add",
              "params": {"addr": 0xE800}})
    resp = json.loads(_readline(f))
    _assert(resp.get("success") is True, f"bp_add failed: {resp}")
    print("OK bp_add")

    # 7. bp_list
    _send(f, {"req_id": 7, "cmd": "bp_list"})
    resp = json.loads(_readline(f))
    _assert(resp.get("success") is True, f"bp_list failed: {resp}")
    print("OK bp_list")

    # 8. unknown command -> error
    _send(f, {"req_id": 8, "cmd": "nonexistent_cmd"})
    resp = json.loads(_readline(f))
    _assert(resp.get("success") is False, f"unknown cmd not rejected: {resp}")
    print("OK unknown cmd rejected")

    # 9. run (un-pause for cleanup)
    _send(f, {"req_id": 9, "cmd": "run"})
    resp = json.loads(_readline(f))
    _assert(resp.get("success") is True, f"run failed: {resp}")
    print("OK run")

    # Clean disconnect
    f.close()
    s.close()
    print("OK all assertions passed")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default=DEFAULT_HOST,
                    help=f"server host (default: {DEFAULT_HOST})")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT,
                    help=f"server port (default: {DEFAULT_PORT})")
    args = ap.parse_args()

    try:
        run_e2e(args.host, args.port)
    except Exception as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
