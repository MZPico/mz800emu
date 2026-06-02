#!/usr/bin/env python3
"""CMT-B smoke test: properties + tape ops + doc resource.

Spawns mz800emu.exe --mcp-pipe, exercises the new cmt_set_property /
cmt_open / cmt_tape_seek / cmt_tape_block_speed / cmt_tape_list commands
and verifies state via get_periph_cmt. Not a ctest target; run manually
for CMT-B acceptance.

The doc resource (emulator://docs/cmt_workflow) is verified at the file
level (the pipe protocol does not expose MCP resources directly; the
Python @mcp.resource wrapper reads the same docs/agent/cmt_workflow.md).
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path

_TESTS_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _TESTS_DIR.parent.parent


def _find_exe():
    env = os.environ.get("MZ_EMU")
    if env and Path(env).is_file():
        return Path(env)
    for c in (_REPO_ROOT / "mz800emu.exe", _REPO_ROOT / "mz800emu"):
        if c.is_file():
            return c
    print("ERROR: mz800emu binary not found", file=sys.stderr)
    sys.exit(1)


def main():
    exe = _find_exe()
    tape = os.environ.get("MZ_TAPE")
    if not tape:
        print("ERROR: set MZ_TAPE to a .mzf path", file=sys.stderr)
        sys.exit(1)
    print(f"Using binary: {exe}")
    print(f"Tape: {tape}")

    proc = subprocess.Popen(
        [str(exe), "--mcp-pipe"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True, bufsize=1,
        cwd=str(_REPO_ROOT),
    )
    passed = 0
    total = 0

    def _read_json_line(timeout_sec=10.0):
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

    rid = [0]

    def send(cmd, data=None):
        rid[0] += 1
        req = {"req_id": rid[0], "cmd": cmd}
        if data is not None:
            req["data"] = data
        proc.stdin.write(json.dumps(req) + "\n")
        proc.stdin.flush()
        return _read_json_line(10.0)

    def check(label, cond, resp):
        nonlocal passed, total
        total += 1
        if cond:
            print(f"{label}: PASS")
            passed += 1
        else:
            print(f"{label}: FAIL ({resp})")

    try:
        hello = _read_json_line(15.0)
        check("HELLO", bool(hello and hello.get("type") == "hello"), hello)
        time.sleep(0.5)

        # --- properties (no tape needed) ---
        # speed 2:1 = en_CMTSPEED value 2
        r = send("cmt_set_property", {"property": "speed", "value": 2})
        check("set_speed ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("cmtspeed==2", bool(r and r["data"].get("cmtspeed") == 2), r)

        r = send("cmt_set_property", {"property": "polarity", "value": 1})
        check("set_polarity ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("polarity_inverted==true",
              bool(r and r["data"].get("polarity_inverted") is True), r)

        r = send("cmt_set_property", {"property": "cpu_boost", "value": 1})
        check("set_cpu_boost ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("cpu_boost==true",
              bool(r and r["data"].get("cpu_boost") is True), r)

        r = send("cmt_set_property",
                 {"property": "mzfsize_check", "value": 0})
        check("set_mzfsize_check ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("mzfsize_check==false",
              bool(r and r["data"].get("mzfsize_check") is False), r)

        # invalid speed -> failure
        r = send("cmt_set_property", {"property": "speed", "value": 99})
        check("bad speed fails", bool(r and not r.get("success")), r)

        # --- open with play_immediately ---
        r = send("cmt_open", {"path": tape, "play_immediately": True})
        check("cmt_open ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("state==play after open",
              bool(r and r["data"].get("state") == "play"), r)

        # --- tape list ---
        r = send("cmt_tape_list")
        ok_list = bool(r and r.get("success")
                       and r["data"].get("available") is True
                       and r["data"].get("count", 0) >= 1)
        check("tape_list available + >=1 block", ok_list, r)
        # container_type: 0=SINGLE (e.g. plain .mzf), 1=SIMPLE_TAPE.
        ctype = r["data"].get("container_type") if ok_list else None
        is_simple_tape = (ctype == 1)
        if ok_list:
            blk0 = r["data"]["blocks"][0]
            check("block0 has block_id 0", blk0.get("block_id") == 0, blk0)

        # --- seek block 0 ---
        # seek requires STOP; stop first
        send("cmt_transport", {"action": "stop"})
        r = send("cmt_tape_seek", {"block_id": 0})
        if is_simple_tape:
            # SIMPLE_TAPE supports per-block seek.
            check("tape_seek 0 ok", bool(r and r.get("success")), r)
        else:
            # SINGLE container has no per-block seek callback; the seek
            # is correctly rejected (no crash). Verify graceful failure.
            check("tape_seek 0 rejected (SINGLE)",
                  bool(r and not r.get("success")), r)

        # --- per-block speed (block 0, 3:1 = 4) ---
        r = send("cmt_tape_block_speed", {"block_id": 0, "speed": 4})
        if is_simple_tape:
            check("tape_block_speed ok", bool(r and r.get("success")), r)
            # bad per-block speed -> failure
            r = send("cmt_tape_block_speed", {"block_id": 0, "speed": 0})
            check("tape_block_speed bad speed fails",
                  bool(r and not r.get("success")), r)
        else:
            # SINGLE has no per-block data; correctly rejected.
            check("tape_block_speed rejected (SINGLE)",
                  bool(r and not r.get("success")), r)

        send("shutdown")
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    # doc resource: file-level check (pipe does not expose resources)
    doc = _REPO_ROOT / "docs" / "agent" / "cmt_workflow.md"
    total += 1
    if doc.is_file() and "CMT cassette workflow" in doc.read_text(
            encoding="utf-8"):
        print("docs/cmt_workflow.md present: PASS")
        passed += 1
    else:
        print(f"docs/cmt_workflow.md present: FAIL ({doc})")

    print(f"\nRESULT: {passed}/{total} PASS")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
