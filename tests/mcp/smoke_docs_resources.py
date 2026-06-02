#!/usr/bin/env python3
"""Smoke test: read each emulator://docs/<topic> Resource via stdio MCP.

Runs `mcp_server.py` as a subprocess (pipe transport), performs MCP
handshake, lists resources, then reads each of the 10 docs URIs and
checks the returned content is non-empty markdown.

This is an interactive ad-hoc helper - not part of CTest harness
because it pulls in the venv python explicitly and would slow CI.
Run from repo root:

    ./mcp-server/.venv/Scripts/python.exe tests/mcp/smoke_docs_resources.py
"""
import json
import os
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MCP_SERVER = REPO_ROOT / "mcp-server" / "mcp_server.py"
PYTHON_EXE = REPO_ROOT / "mcp-server" / ".venv" / "Scripts" / "python.exe"

DOCS_URIS = [
    "emulator://docs/index",
    "emulator://docs/memory_layout",
    "emulator://docs/bp_dsl",
    "emulator://docs/smart_vars",
    "emulator://docs/action_dsl",
    "emulator://docs/watch_dsl",
    "emulator://docs/eventlog_mask",
    "emulator://docs/sharp_display_code",
    "emulator://docs/mz800_keyboard",
    "emulator://docs/cmt_workflow",
]


def send(p: subprocess.Popen, msg: dict) -> None:
    p.stdin.write((json.dumps(msg) + "\n").encode("utf-8"))
    p.stdin.flush()


def read_response(p: subprocess.Popen, timeout_s: float = 10.0):
    line = p.stdout.readline()
    if not line:
        return None
    return json.loads(line.decode("utf-8"))


def main() -> int:
    env = os.environ.copy()
    # Force pipe transport with a dummy EXE path that never spawns
    # (docs Resources are pure file reads, do not need a live emu).
    env["MZ800EMU_TRANSPORT"] = "pipe"
    env["MZ800EMU_EXE"] = str(REPO_ROOT / "mz800emu.exe")

    p = subprocess.Popen(
        [str(PYTHON_EXE), str(MCP_SERVER)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        env=env,
    )
    try:
        # 1. initialize
        send(p, {
            "jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "smoke-docs", "version": "0"},
            },
        })
        init_resp = read_response(p)
        if not init_resp or "result" not in init_resp:
            print(f"FAIL initialize: {init_resp}")
            return 1
        send(p, {"jsonrpc": "2.0", "method": "notifications/initialized"})

        # 2. read each docs URI
        ok = 0
        for i, uri in enumerate(DOCS_URIS, start=10):
            send(p, {
                "jsonrpc": "2.0", "id": i, "method": "resources/read",
                "params": {"uri": uri},
            })
            resp = read_response(p)
            if not resp or "result" not in resp:
                print(f"FAIL read {uri}: {resp}")
                continue
            contents = resp["result"].get("contents", [])
            if not contents:
                print(f"FAIL read {uri}: empty contents")
                continue
            text = contents[0].get("text", "")
            n = len(text)
            # All docs files are >= 1 KB; index is ~1.5 KB.
            if n < 200:
                print(f"FAIL read {uri}: too short ({n} chars)")
                continue
            print(f"PASS read {uri}: {n} chars")
            ok += 1
        if ok == len(DOCS_URIS):
            print(f"\nSMOKE PASS: {ok}/{len(DOCS_URIS)} docs URIs returned non-empty markdown")
            return 0
        print(f"\nSMOKE FAIL: {ok}/{len(DOCS_URIS)} docs URIs OK")
        return 1
    finally:
        p.stdin.close()
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()


if __name__ == "__main__":
    sys.exit(main())
