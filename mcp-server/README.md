# mcp-server/ - Python MCP wrapper for mz800emu

> Czech version: [README.cs.md](README.cs.md)

Python side of the MCP (Model Context Protocol) server. FastMCP wrapper
that bridges MCP clients (Claude Code, Claude Desktop, ...) to the MCP
backend embedded in the emulator.

Full user documentation (architecture, transports, troubleshooting,
MSYS2 specifics): [docs/en/mcp-server/python-wrapper.md](../docs/en/mcp-server/python-wrapper.md).

## Quick start

### 1. Build the emulator with the MCP backend

The MCP server backend is **enabled by default** in the standard build:

```bash
mingw32-make mz800emu     # MSYS2 UCRT64 shell
# or
make mz800emu             # Linux
```

If the binary was built with `make NO_MCP=1`, the MCP backend is **not**
present and the Python wrapper cannot establish a connection. Verify:

```bash
./mz800emu.exe --help | grep -i mcp
# Expected: --mcp-pipe / --mcp-tcp-port switches are visible.
```

Compile-time toggle details (`NO_MCP`, `NO_MCP_TCP`, `NO_DEBUGGER`):
[docs/en/mcp-server/python-wrapper.md](../docs/en/mcp-server/python-wrapper.md#emulator-build).

### 2. Initialize venv + generate .mcp.json

```bash
cd mcp-server/
./mcpinit.sh
```

The script is **idempotent** and does everything required:

- creates `.venv` (skipped if it already exists)
- installs `requirements.txt` via the venv Python
- sanity check `import mcp`
- **generates `.mcp.json` with absolute paths for this install**
  (= the old `.mcp.json` is backed up as `.mcp.json.bak` if it
  differs from the new content)

You do **not** need to activate the venv in your own shell - Claude
Code spawns `mcp_server.py` as a subprocess using the explicit venv
Python path from `.mcp.json`.

Manual equivalent (if you do not want the script):

```bash
cd mcp-server/
python -m venv .venv
source .venv/bin/activate     # MSYS2: source .venv/Scripts/activate
pip install -r requirements.txt
# .mcp.json then needs manual editing (= absolute paths to the
# Python, mcp_server.py, mz800emu.exe and the repo root).
```

### 3. Configure your MCP client

Copy the generated `mcp-server/.mcp.json` to its target location:

- `~/.claude/.mcp.json` (= user-level), or
- `<project>/.claude/.mcp.json` (= project-level)

Restart Claude Code. In the chat type `/mcp` - you should see
`mz800emu` (or `mz800emu-tcp`) as connected.

### 4. AI reference docs

For advanced work with complex tools (BP DSL, watch expressions,
EventLog mask, Sharp display code, memory layout), the AI client can
read static English reference docs via `emulator://docs/<topic>`
Resources. Topic index: `emulator://docs/index`. Details in
[Resources overview](../docs/en/mcp-server/resources-overview.md).

## When it does not work

Step-by-step checklist when `/mcp` lists nothing or the server is
`failed`:

1. **Build flag**: `./mz800emu.exe --help` must list `--mcp-pipe`.
   If not, the binary was built with `NO_MCP=1`; rebuild without it.

2. **`.mcp.json` location**: Claude Code reads it from the cwd where
   the session was started. Verify with `pwd` in the session.

3. **JSON syntax**: `python -m json.tool .mcp.json`.

4. **Server can spawn manually**:

   ```bash
   cd <project-root>
   python mcp-server/mcp_server.py < /dev/null
   ```

   `ImportError` / `FileNotFoundError` = environment issue (venv,
   `requirements.txt`).

5. **Restart Claude Code**: `.mcp.json` is read once at session
   start, hot reload is not supported.

6. **Permission**: In the session type `/mcp` - a server in `pending`
   state needs explicit approval.

Full troubleshooting (= MSYS2 pydantic-core workaround, transport
selection, env variables): [docs/en/mcp-server/python-wrapper.md](../docs/en/mcp-server/python-wrapper.md).

## Files in this directory

| File | Purpose |
|------|---------|
| `mcpinit.sh` | One-liner venv setup (idempotent + sanity check) |
| `mcp_server.py` | FastMCP server, pipe + TCP bridge |
| `requirements.txt` | Python dependency (= `mcp[cli]>=1.0.0`) |
| `.mcp.json` | Skeleton config for Claude Code (2 entries: pipe + TCP) |
