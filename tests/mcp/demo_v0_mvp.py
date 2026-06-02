#!/usr/bin/env python3
"""V0 MVP demo - first-time success for a developer trying the bridge.

Run:
    python tests/mcp/demo_v0_mvp.py

What it does:
    1. Connects to the emulator via the pipe transport (lazy spawn of
       mz800emu.exe --mcp-pipe --headless).
    2. Calls 6 representative commands in a typical debugging order:
       ping -> get_state -> pause -> get_registers -> bp_add ->
       mem_write -> shutdown.
    3. Prints a friendly transcript so the developer sees the bridge is
       alive end-to-end.

This script bypasses the FastMCP wire layer and talks directly to the
emulator backend through ``mcp_server.py`` internal helpers. It is NOT
a replacement for ``test_mcp_server_stdio.py`` (which covers the full
JSON-RPC stdio path used by Claude Code) - it complements it with a
human-readable smoke run.

Requirements:
    * ``mz800emu.exe`` (or ``mz800emu`` on POSIX) present in the repo
      root (``make mz800emu`` puts it there).
    * Same Python environment as ``mcp_server.py`` (FastMCP-equipped
      venv). Most easily: run via the venv directly:
          mcp-server/.venv/Scripts/python.exe tests/mcp/demo_v0_mvp.py
"""

import asyncio
import json
import sys
from pathlib import Path


# Resolve the mcp-server dir and import mcp_server.py from it so we can
# use its private bridge primitives (_send_request, _shutdown_emu).
_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
_MCP_SERVER_DIR = _REPO_ROOT / "mcp-server"
sys.path.insert(0, str(_MCP_SERVER_DIR))
import mcp_server  # noqa: E402


def _hex_dump(b64: str, limit: int = 8) -> str:
    """Decode base64 a vypíše prvních ``limit`` bajtů jako hex."""
    import base64
    try:
        raw = base64.b64decode(b64)
    except Exception:
        return "<undecodable>"
    head = raw[:limit]
    return " ".join(f"{b:02X}" for b in head) + (" ..." if len(raw) > limit else "")


async def main() -> int:
    print("=== mz800emu MCP V0 MVP demo ===")
    print()

    print("[1/7] Connecting to emulator (pipe spawn, headless)...")
    try:
        resp = await mcp_server._send_request("ping")
    except Exception as e:
        print(f"      FAILED to connect: {e}")
        print()
        print("Hint: build mz800emu first (make mz800emu) and verify")
        print("      the binary exists in the repo root.")
        return 1
    print(f"      Ping reply: {resp.get('data', {})}")

    print("[2/7] Reading emulator state...")
    resp = await mcp_server._send_request("get_state")
    state = resp.get("data", {})
    print(f"      State: running={state.get('running')} "
          f"paused={state.get('paused')}")

    print("[3/7] Pausing emulator...")
    resp = await mcp_server._send_request("pause")
    print(f"      Pause reply: {resp.get('data', {})}")

    print("[4/7] Reading Z80 registers...")
    resp = await mcp_server._send_request("get_registers")
    regs = resp.get("data", {})
    pc = regs.get("PC", 0)
    sp = regs.get("SP", 0)
    af = regs.get("AF", 0)
    print(f"      PC=0x{pc:04X}  SP=0x{sp:04X}  AF=0x{af:04X}")

    print("[5/7] Setting execution breakpoint at 0xE800 (monitor ROM)...")
    resp = await mcp_server._send_request(
        "bp_add", {"addr": 0xE800})
    bp = resp.get("data", {})
    print(f"      BP id={bp.get('id')} at 0x{bp.get('addr', 0):04X}")

    print("[6/7] Trying mem_write to RAM at 0x4000 "
          "(should succeed - RAM region)...")
    resp = await mcp_server._send_request(
        "mem_write", {"addr": 0x4000, "data_hex": "DEADBEEF"})
    if resp.get("success"):
        print(f"      Wrote {resp.get('data', {}).get('length')} bytes "
              f"at 0x{resp.get('data', {}).get('addr', 0):04X}")
        # Verify with a read-back.
        read = await mcp_server._send_request(
            "mem_read", {"addr": 0x4000, "len": 4})
        b64 = read.get("data", {}).get("data_b64", "")
        print(f"      Readback at 0x4000: {_hex_dump(b64, 4)}")
    else:
        # On MZ-800 ROM-only configurations the write may fail; demo
        # treats this as informative rather than hard failure.
        print(f"      mem_write region check: {resp.get('error')}")

    print("[7/7] Shutting down emulator subprocess...")
    await mcp_server._shutdown_emu()
    print("      Subprocess terminated.")

    print()
    print("=== Demo OK ===")
    print()
    print("Next steps:")
    print("  * Copy mcp-server/.mcp.json to your project root and ")
    print("    restart Claude Code. The 'mz800emu' / 'mz800emu-tcp'")
    print("    MCP servers will appear in /mcp.")
    print("  * For live human-in-the-loop debugging, start GUI emu with")
    print("    Tools -> MCP TCP Server -> Start and use the tcp variant.")
    return 0


if __name__ == "__main__":
    try:
        rc = asyncio.run(main())
    except KeyboardInterrupt:
        print("\nInterrupted.")
        rc = 130
    sys.exit(rc)
