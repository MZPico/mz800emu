#!/usr/bin/env python3
"""Smoke test: user knowledge base resources (emulator://kb/*) via stdio MCP.

Creates a temporary KB directory with a nested tree and a front-matter
document, runs ``mcp_server.py`` with ``MZ800EMU_USER_KB_DIR`` pointing
at it, then checks that:

  * each *.md is registered recursively as ``emulator://kb/<relpath>``,
  * front matter ``title`` / ``description`` override the auto-extracted
    metadata, while documents without front matter fall back to the H1
    heading + first paragraph,
  * the content reads back as UTF-8.

Like ``smoke_docs_resources.py`` this is an interactive ad-hoc helper,
not part of the CTest harness (it needs the venv python explicitly).
Run from repo root:

    ./mcp-server/.venv/Scripts/python.exe tests/mcp/smoke_user_kb.py
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MCP_SERVER = REPO_ROOT / "mcp-server" / "mcp_server.py"
PYTHON_EXE = REPO_ROOT / "mcp-server" / ".venv" / "Scripts" / "python.exe"

# Očekávané URI -> (title, podřetězec description) po registraci KB níže.
EXPECT = {
    "emulator://kb/notes": ("Plain notes", "Some text about"),
    "emulator://kb/hw/vram": ("VRAM trick", "Plane swap"),
    "emulator://kb/os/cpm": ("CP/M boot", "How to boot"),
}


def _write_kb(root: Path) -> None:
    """Vytvoří testovací KB strom: flat + nested + front-matter dokument."""
    (root / "hw").mkdir(parents=True)
    (root / "os").mkdir(parents=True)
    (root / "notes.md").write_text(
        "# Plain notes\nSome text about mzdos and such.\n", encoding="utf-8")
    (root / "hw" / "vram.md").write_text(
        "# VRAM trick\nPlane swap during scroll.\n", encoding="utf-8")
    (root / "os" / "cpm.md").write_text(
        "---\ntitle: CP/M boot\ndescription: How to boot CP/M on MZ-800.\n"
        "---\n# CPM\nLonger body text...\n", encoding="utf-8")


def send(p: subprocess.Popen, msg: dict) -> None:
    p.stdin.write((json.dumps(msg) + "\n").encode("utf-8"))
    p.stdin.flush()


def read_response(p: subprocess.Popen):
    line = p.stdout.readline()
    if not line:
        return None
    return json.loads(line.decode("utf-8"))


def main() -> int:
    tmp = tempfile.mkdtemp(prefix="mz_kb_smoke_")
    kb_root = Path(tmp)
    _write_kb(kb_root)

    env = os.environ.copy()
    env["MZ800EMU_TRANSPORT"] = "pipe"
    env["MZ800EMU_EXE"] = str(REPO_ROOT / "mz800emu.exe")
    env["MZ800EMU_USER_KB_DIR"] = str(kb_root)

    p = subprocess.Popen(
        [str(PYTHON_EXE), str(MCP_SERVER)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        env=env,
    )
    try:
        send(p, {
            "jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "smoke-kb", "version": "0"},
            },
        })
        init_resp = read_response(p)
        if not init_resp or "result" not in init_resp:
            print(f"FAIL initialize: {init_resp}")
            return 1
        send(p, {"jsonrpc": "2.0", "method": "notifications/initialized"})

        # 1. resources/list -> mapa kb URI na metadata.
        send(p, {"jsonrpc": "2.0", "id": 2, "method": "resources/list",
                 "params": {}})
        resp = read_response(p)
        if not resp or "result" not in resp:
            print(f"FAIL resources/list: {resp}")
            return 1
        by_uri = {r["uri"]: r for r in resp["result"]["resources"]}

        ok = 0
        for uri, (exp_title, exp_desc_sub) in EXPECT.items():
            r = by_uri.get(uri)
            if r is None:
                print(f"FAIL missing resource {uri}")
                continue
            if r.get("title") != exp_title:
                print(f"FAIL {uri}: title {r.get('title')!r} != {exp_title!r}")
                continue
            if exp_desc_sub not in (r.get("description") or ""):
                print(f"FAIL {uri}: description missing {exp_desc_sub!r} "
                      f"(got {r.get('description')!r})")
                continue
            # 2. read back content
            send(p, {"jsonrpc": "2.0", "id": 100, "method": "resources/read",
                     "params": {"uri": uri}})
            rr = read_response(p)
            contents = (rr or {}).get("result", {}).get("contents", [])
            if not contents or len(contents[0].get("text", "")) < 5:
                print(f"FAIL read {uri}: empty content")
                continue
            print(f"PASS {uri}: title={exp_title!r}")
            ok += 1

        if ok == len(EXPECT):
            print(f"\nSMOKE PASS: {ok}/{len(EXPECT)} user KB resources OK")
            return 0
        print(f"\nSMOKE FAIL: {ok}/{len(EXPECT)} user KB resources OK")
        return 1
    finally:
        p.stdin.close()
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()
        # úklid temp KB
        for f in sorted(kb_root.rglob("*"), reverse=True):
            try:
                f.unlink() if f.is_file() else f.rmdir()
            except OSError:
                pass
        try:
            kb_root.rmdir()
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
