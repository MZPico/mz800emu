# mcp-server/ - Python MCP wrapper pro mz800emu

> Anglická verze: [README.md](README.md)

Python část MCP (Model Context Protocol) serveru. FastMCP wrapper,
který přemosťuje MCP klienty (Claude Code, Claude Desktop, ...) na
MCP backend zabudovaný v emulátoru.

Plná uživatelská dokumentace (= architektura, transporty, troubleshooting,
MSYS2 specifika): [docs/cz/mcp-server/python-wrapper.md](../docs/cz/mcp-server/python-wrapper.md).

## Quick start

### 1. Sestavení emulátoru s MCP backendem

MCP server backend je **default** zapnutý ve standardním buildu:

```bash
mingw32-make mz800emu     # MSYS2 UCRT64 shell
# nebo
make mz800emu             # Linux
```

Pokud byla binárka sestavena s `make NO_MCP=1`, MCP backend v ní
**není** a Python wrapper nedokáže navázat spojení. Ověř:

```bash
./mz800emu.exe --help | grep -i mcp
# Očekáváno: viditelné --mcp-pipe / --mcp-tcp-port přepínače.
```

Detail compile-time přepínačů (`NO_MCP`, `NO_MCP_TCP`, `NO_DEBUGGER`):
[docs/cz/mcp-server/python-wrapper.md](../docs/cz/mcp-server/python-wrapper.md#sestaveni-emulatoru).

### 2. Inicializace venv + generace .mcp.json

```bash
cd mcp-server/
./mcpinit.sh
```

Skript je **idempotent** a dělá vše potřebné:

- vytvoří `.venv` (skipne pokud už existuje)
- nainstaluje `requirements.txt` přes venv Python
- sanity check `import mcp`
- **vygeneruje `.mcp.json` s absolutními cestami pro tento install**
  (= staré `.mcp.json` zazálohuje jako `.mcp.json.bak` pokud se liší)

Aktivace venv ve tvém shellu **není** potřeba - Claude Code spustí
`mcp_server.py` jako subprocess s explicit cestou k venv Pythonu
z `.mcp.json`.

Manuální ekvivalent (pokud nechceš skript):

```bash
cd mcp-server/
python -m venv .venv
source .venv/bin/activate     # MSYS2: source .venv/Scripts/activate
pip install -r requirements.txt
# .mcp.json pak musíš upravit ručně (= absolutní cesty k Pythonu,
# mcp_server.py, mz800emu.exe a repo rootu).
```

### 3. Konfigurace MCP klienta

Zkopíruj vygenerovaný `mcp-server/.mcp.json` na cílovou pozici:

- `~/.claude/.mcp.json` (= user-level), nebo
- `<project>/.claude/.mcp.json` (= project-level)

Restartuj Claude Code. V chatu napiš `/mcp` - mělo by se objevit
`mz800emu` (případně `mz800emu-tcp`) jako connected.

### 4. AI reference docs

Pro pokročilé práce s komplexními nástroji (BP DSL, watch expressions,
EventLog mask, Sharp display code, memory layout) si AI klient čte
statické EN reference docs přes `emulator://docs/<topic>` Resources.
Rozcestník: `emulator://docs/index`. Detail viz
[Resources overview](../docs/cz/mcp-server/resources-overview.md).

## Když to nejede

Postupný checklist v případě že `/mcp` nic nevypíše nebo server
hlásí `failed`:

1. **Build flag**: `./mz800emu.exe --help` musí ukazovat `--mcp-pipe`.
   Pokud ne, sestavena s `NO_MCP=1` - rebuild bez tohoto flagu.

2. **`.mcp.json` location**: Claude Code ho čte z cwd v které byla
   session spuštěna. Ověř `pwd` v session.

3. **JSON syntaxe**: `python -m json.tool .mcp.json`.

4. **Server jde spustit ručně**:

   ```bash
   cd <project-root>
   python mcp-server/mcp_server.py < /dev/null
   ```

   `ImportError` / `FileNotFoundError` = chyba prostředí (= venv,
   `requirements.txt`).

5. **Restart Claude Code**: `.mcp.json` se načítá jen při startu
   session.

6. **Permission**: V session napiš `/mcp` - server v `pending`
   stavu potřebuje explicit schválení.

Plný troubleshooting (= MSYS2 pydantic-core workaround, transport
volba, env proměnné): [docs/cz/mcp-server/python-wrapper.md](../docs/cz/mcp-server/python-wrapper.md).

## Soubory v této složce

| Soubor | Význam |
|--------|--------|
| `mcpinit.sh` | One-liner venv setup (idempotent + sanity check) |
| `mcp_server.py` | FastMCP server, pipe + TCP bridge |
| `requirements.txt` | Python dependency (= `mcp[cli]>=1.0.0`) |
| `.mcp.json` | Skeleton config pro Claude Code (2 entries: pipe + TCP) |
