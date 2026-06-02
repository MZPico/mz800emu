#!/usr/bin/env python3
"""V1.E.6.C - manuální smoke test screenshot_raw v MCP pipe (headless) mode.

Volání:
    python tests/mcp/smoke_screenshot_v1e6c.py

Cíl:
- Ověřit že get_frame_screenshot_raw vrací available=true v headless
  mode, fallback_source je buď "sdl_snapshot" nebo "gdg_live", obraz
  není all-zero (= ROM menu zobrazen po reset).
"""
import json
import os
import subprocess
import sys
import time
import base64
from pathlib import Path


def _read_json_line(proc, timeout_sec=15.0):
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        line = proc.stdout.readline()
        if not line:
            return None
        s = line.strip()
        if not s or not s.startswith("{"):
            continue
        try:
            return json.loads(s)
        except json.JSONDecodeError:
            continue
    return None


def main():
    repo = Path(__file__).resolve().parent.parent.parent
    exe = repo / "mz800emu.exe"
    print(f"Using binary: {exe}")
    if not exe.is_file():
        print("ERROR: mz800emu.exe missing", file=sys.stderr)
        return 1

    proc = subprocess.Popen(
        [str(exe), "--mcp-pipe"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
        cwd=str(repo),
    )

    rc = 1
    try:
        hello = _read_json_line(proc, 15.0)
        if not hello or hello.get("type") != "hello":
            print(f"FAIL: no HELLO ({hello})")
            return 1
        print(f"HELLO OK: commands={len(hello.get('commands', []))}")

        # Pockame na rozjezd emu thread.
        time.sleep(0.5)

        # Probudime emulator (pro jistotu - pokud start-paused, run jej rozjede)
        proc.stdin.write(json.dumps({"req_id": 100, "cmd": "run",
                                      "data": {}}) + "\n")
        proc.stdin.flush()
        _read_json_line(proc, 5.0)
        # Necheme bezet par sekund (= cca 150 frame_done @ 50 Hz)
        time.sleep(3.0)

        req = {"req_id": 1, "cmd": "get_frame_screenshot_raw", "data": {}}
        proc.stdin.write(json.dumps(req) + "\n")
        proc.stdin.flush()
        resp = _read_json_line(proc, 10.0)
        if not resp or not resp.get("success"):
            print(f"FAIL: get_frame_screenshot_raw response ({resp})")
            return 1
        data = resp.get("data", {})
        avail = data.get("available")
        fallback = data.get("fallback_source")
        w = data.get("width")
        h = data.get("height")
        sid = data.get("source_screen_id")
        b64 = data.get("data_b64", "")
        print(f"available: {avail}")
        print(f"fallback_source: {fallback}")
        print(f"width x height: {w} x {h}")
        print(f"source_screen_id: {sid}")
        print(f"data_b64 length: {len(b64)}")

        if not avail:
            print(f"FAIL: available=False, reason: {data.get('reason')}")
            return 1
        if fallback not in ("sdl_snapshot", "gdg_live"):
            print(f"FAIL: unknown fallback_source: {fallback}")
            return 1

        raw = base64.b64decode(b64)
        # Pocet non-zero pixelu indikuje ze obraz neni cely cerny.
        non_zero = sum(1 for i in range(0, len(raw), 4)
                       if raw[i] != 0 or raw[i+1] != 0 or raw[i+2] != 0)
        total = len(raw) // 4
        print(f"non-zero pixels: {non_zero} / {total}")
        if non_zero < 100:
            print("WARN: image is almost all-zero (no ROM menu visible?)")
        else:
            print("OK: image has visible content")

        # Sledovat 2x screenshot aby ID rostlo
        time.sleep(0.5)
        req = {"req_id": 2, "cmd": "get_frame_screenshot_raw", "data": {}}
        proc.stdin.write(json.dumps(req) + "\n")
        proc.stdin.flush()
        resp2 = _read_json_line(proc, 10.0)
        data2 = resp2.get("data", {}) if resp2 else {}
        sid2 = data2.get("source_screen_id")
        print(f"second source_screen_id: {sid2}")
        print(f"second fallback_source: {data2.get('fallback_source')}")

        rc = 0
        print("\nSMOKE PASS")
    finally:
        try:
            proc.stdin.write(json.dumps({"req_id": 99, "cmd": "shutdown",
                                          "data": {}}) + "\n")
            proc.stdin.flush()
        except Exception:
            pass
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    return rc


if __name__ == "__main__":
    sys.exit(main())
