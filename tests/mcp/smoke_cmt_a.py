#!/usr/bin/env python3
"""CMT-A smoke test: transport + recording + cmthack toggle.

Spawns mz800emu.exe --mcp-pipe, inserts a tape, exercises the new
cmt_transport / cmt_record / cmt_hack_set commands and verifies state
via get_periph_cmt. Not a ctest target; run manually for CMT-A
acceptance.
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
    wav = os.environ.get("MZ_WAV", "/tmp/test.wav")
    bad_wav = os.environ.get(
        "MZ_BAD_WAV", "C:/msys64/tmp/nonexistent_dir_xyz/test.wav")
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

        # insert tape
        r = send("media_insert", {"slot": "cmt", "path": tape})
        check("media_insert cmt", bool(r and r.get("success")), r)

        # play -> state=play
        r = send("cmt_transport", {"action": "play"})
        check("cmt_play ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("state==play", bool(r and r["data"].get("state") == "play"), r)

        # pause(true) -> paused=true
        r = send("cmt_transport", {"action": "pause", "pause": True})
        check("cmt_pause ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("paused==true", bool(r and r["data"].get("paused") is True), r)

        # stop -> state=stop
        r = send("cmt_transport", {"action": "stop"})
        check("cmt_stop ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("state==stop", bool(r and r["data"].get("state") == "stop"), r)

        # eject -> filled=false
        r = send("cmt_transport", {"action": "eject"})
        check("cmt_eject ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("filled==false", bool(r and r["data"].get("filled") is False), r)

        # record -> state=record + file exists
        try:
            os.remove(wav)
        except OSError:
            pass
        r = send("cmt_record", {"path": wav})
        check("cmt_record ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("state==record", bool(r and r["data"].get("state") == "record"), r)
        check("wav file exists", os.path.isfile(wav), wav)
        send("cmt_transport", {"action": "stop"})
        # eject the WAV image, otherwise cmt_record reuses the already
        # loaded recordable image instead of opening a new path.
        send("cmt_transport", {"action": "eject"})

        # record error: unwritable path -> success=false
        r = send("cmt_record", {"path": bad_wav})
        check("record bad path fails", bool(r and not r.get("success")), r)

        # hack toggle
        r = send("cmt_hack_set", {"enabled": False})
        check("hack_set false ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("cmthack_enabled==false",
              bool(r and r["data"].get("cmthack_enabled") is False), r)
        r = send("cmt_hack_set", {"enabled": True})
        check("hack_set true ok", bool(r and r.get("success")), r)
        r = send("get_periph_cmt")
        check("cmthack_enabled==true",
              bool(r and r["data"].get("cmthack_enabled") is True), r)

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

    print(f"\nRESULT: {passed}/{total} PASS")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
