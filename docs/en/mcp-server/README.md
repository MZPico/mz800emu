# MCP server (Model Context Protocol)

The mz800emu emulator can be controlled by an AI client (Claude Code,
Claude Desktop, or a custom LLM application) via the **Model Context
Protocol** - an open standard by Anthropic for connecting AI to external
tools and data.

Once connected, the AI client can operate the emulator as a **full user
in any role**:

| Role | What the AI does | Use case |
|------|------------------|----------|
| **Player** | reads screen, sends keys / joystick | game testing, demo orchestration |
| **Regular user** | + loads MZF/MZS/FDC, switches platform | operating CP/M, BASIC, ROM monitor |
| **Investigator** | + reads memory, registers, sets BPs | analyzing unknown program, cheat search |
| **Reverse engineer** | + symbols, callstack, watch, CDL | annotating ROM routines, identifying algorithms |
| **Developer / hacker** | + chip-level, snapshot diff, IRQ inject | live debug of own Z80 code, regression testing |

## What the MCP server is for

1. **Deep testing of integrated emulator features** - regression suite,
   stress testing of subsystems (FDC, GDG, CTC, PSG), fault injection
2. **Analysis and debugging of existing systems, programs, games** -
   bug hunting, reverse engineering, symbol auto-discovery, trace analysis
3. **Development of new programs, systems, and games** - live developer
   assist (= the AI sees your debug state and suggests hypotheses), user
   simulation testing (= the AI tests your program as a user would)

## Getting started

1. **Build the emulator with the MCP backend** (UCRT64 shell):

   ```bash
   mingw32-make mz800emu
   ```

   The MCP backend is enabled by default. Compile-time toggle details
   (`NO_MCP`, `NO_DEBUGGER`) are in the [Python wrapper](python-wrapper.md).

2. **Initialize the venv and generate `.mcp.json`**:

   ```bash
   cd mcp-server/
   ./mcpinit.sh
   ```

   The script creates `.venv`, installs dependencies and generates
   `mcp-server/.mcp.json` with absolute paths for this install.
   Manual procedure + MSYS2 pydantic-core workaround are in the
   [Python wrapper](python-wrapper.md).

3. **Choose transport**:

   - [Pipe transport](transport-pipe.md) - AI launches the emulator
     as a subprocess (CI, batch, Claude Desktop, headless workflows)
   - [TCP transport](transport-tcp.md) - AI attaches to a running GUI
     emulator ("human in the loop")

   The generated `.mcp.json` contains both entries (`mz800emu` for
   pipe, `mz800emu-tcp` for TCP); you can use them in parallel.

4. **Copy `mcp-server/.mcp.json`** to `~/.claude/.mcp.json` (user-level)
   or `<project>/.claude/.mcp.json` (project-level) and restart
   Claude Code.

Detailed instructions, troubleshooting and workflow diagram are in
[Python wrapper](python-wrapper.md).

### AI reference docs (for advanced clients)

The MCP server exposes static English reference documents via
`emulator://docs/<topic>` Resources (auto-registered from `docs/agent/`).
The AI client (Claude Code etc.) can read them on demand when it needs
detail about complex tools (BP condition DSL, watch expressions,
EventLog categories, Sharp display code, per-platform memory layout,
keyboard). Start with `emulator://docs/index` - it lists all topics.
A user can add their own notes via `emulator://kb/*`.

Details in [Resources overview](resources-overview.md), section
"AI reference docs".

## Security

The MCP server has a **4-tier security profile** (`wild` / `confined`
/ `sandboxed` / `observer`) with default `wild` in dev builds. In
end-user release distribution, MCP is **fully disabled**.

Currently the profile is **only stored in the configuration** and
propagated into the dispatch layer; full enforcement (file whitelisting,
destructive tool blocking, audit log) is not available in this release.
In a trusted environment it is recommended to stay on `wild`, otherwise
manually limit writes via `mem_write` to selected regions.

Security profile configuration: [Configuration](configuration.md).

## Related documentation

- [Headless mode](headless-mode.md) - running the emulator headlessly
  (CLI flag `--headless`, prerequisite for MCP subprocess workflow)
- [Snapshot to/from memory](snapshot-buffer.md) - in-memory snapshot
  buffer (prerequisite for MCP `state_save` / `state_restore`)
- [Python wrapper](python-wrapper.md) - installation, venv, `.mcp.json`,
  workflow with Claude Code
- [Pipe transport](transport-pipe.md) - subprocess transport, CLI
  flag `--mcp-pipe`
- [TCP transport](transport-tcp.md) - attach to running GUI emu,
  Tools menu, CLI flag `--mcp-tcp-port`
- [Configuration](configuration.md) - INI section `[MCP]`, GUI
  Settings, CLI overrides
- [Tools overview](tools-overview.md) - available tools, sensitive
  flag, usage examples
- [Resources overview](resources-overview.md) - read-only resources,
  URI scheme, examples
