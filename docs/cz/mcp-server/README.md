# MCP server (Model Context Protocol)

mz800emu může být ovládán AI klientem (Claude Code, Claude Desktop nebo
vlastní LLM aplikací) přes **Model Context Protocol** - otevřený standard
od Anthropic pro propojení AI s externími nástroji a daty.

Po připojení může AI klient ovládat emulátor jako **plnohodnotný uživatel
s libovolnou rolí**:

| Role | Co AI dělá | K čemu slouží |
|------|------------|----------------|
| **Hráč** | čte obrazovku, posílá klávesy / joystick | testování her, demo orchestrace |
| **Běžný uživatel** | + načítá MZF/MZS/FDC, mění platformu | obsluha CP/M, BASIC, ROM monitoru |
| **Investigátor** | + čte paměť, registry, nastaví BP | analýza neznámého programu, cheat search |
| **Reverse engineer** | + symbols, callstack, watch, CDL | anotace ROM rutin, identifikace algoritmů |
| **Developer / hacker** | + chip-level, snapshot diff, IRQ inject | live debug vlastního Z80 kódu, regression testy |

## K čemu MCP server slouží

1. **Hloubkové testování integrovaných funkcí emulátoru** - regression
   suite, stress test subsystémů (FDC, GDG, CTC, PSG), fault injection
2. **Analýza a debug existujících systémů, programů, her** - bug
   hunting, reverse engineering, symbol auto-discovery, trace analysis
3. **Vývoj nových programů, systémů a her** - live developer assist
   (= AI vidí váš debug stav, navrhuje hypotézy), user simulation
   testing (= AI testuje vámi napsaný program jako uživatel)

## Jak začít

1. **Sestav emulátor s MCP backendem** (UCRT64 shell):

   ```bash
   mingw32-make mz800emu
   ```

   MCP backend je default zapnutý. Detail compile-time přepínačů
   (`NO_MCP`, `NO_DEBUGGER`) je v [Python wrapperu](python-wrapper.md).

2. **Inicializuj venv a vygeneruj `.mcp.json`**:

   ```bash
   cd mcp-server/
   ./mcpinit.sh
   ```

   Skript vytvoří `.venv`, nainstaluje závislosti a vygeneruje
   `mcp-server/.mcp.json` s absolutními cestami pro tento install.
   Manuální postup + MSYS2 pydantic-core workaround je v
   [Python wrapperu](python-wrapper.md).

3. **Vyber transport**:

   - [Pipe transport](transport-pipe.md) - AI spustí emulátor jako
     subprocess (CI, batch, Claude Desktop, headless workflows)
   - [TCP transport](transport-tcp.md) - AI se připojí k běžícímu GUI
     emulátoru ("human in the loop")

   Vygenerovaný `.mcp.json` obsahuje obě entries (`mz800emu` pro pipe,
   `mz800emu-tcp` pro TCP), můžeš použít obě paralelně.

4. **Zkopíruj `mcp-server/.mcp.json`** do `~/.claude/.mcp.json`
   (user-level) nebo `<project>/.claude/.mcp.json` (project-level)
   a restartuj Claude Code.

Detailní postup, troubleshooting a workflow diagram je v
[Python wrapper](python-wrapper.md).

### AI reference docs (pro pokročilé klienty)

MCP server vystavuje statické EN reference dokumenty přes
`emulator://docs/<topic>` Resources (autoregistrované z `docs/agent/`).
AI klient (Claude Code apod.) si je může číst on-demand když potřebuje
znát detaily komplexních nástrojů (BP condition DSL, watch expressions,
EventLog kategorie, Sharp display code, per-platform memory layout,
klávesnice). Začni s `emulator://docs/index` - rozcestník přes všechna
témata. Uživatel může přidat vlastní poznámky přes `emulator://kb/*`.

Detail v [Resources overview](resources-overview.md), sekce
"Dokumentace pro AI klienty".

## Bezpečnost

MCP server má **4-úrovňový profil přístupu** (`wild` / `confined`
/ `sandboxed` / `observer`) s výchozím `wild` v dev buildu. V release
distribuci pro koncové uživatele je MCP **celý vypnut**.

Aktuálně se profil **pouze ukládá do konfigurace** a propaguje do
dispatch vrstvy; plné vynucení (whitelist souborů, blokace destruktivních
tools, audit log) zatím není v této verzi k dispozici. V důvěryhodném
prostředí je doporučeno zůstat na `wild`, jinak ručně omezit zápis
přes `mem_write` na vybrané regiony.

Konfigurace bezpečnostního profilu: [Konfigurace](configuration.md).

## Související dokumentace

- [Headless režim](headless-mode.md) - bezhlavé spuštění emulátoru
  (CLI flag `--headless`, předpoklad MCP subprocess workflow)
- [Snapshot do/z paměti](snapshot-buffer.md) - in-memory snapshot
  buffer (předpoklad pro MCP `state_save` / `state_restore`)
- [Python wrapper](python-wrapper.md) - instalace, venv, `.mcp.json`,
  workflow s Claude Code
- [Pipe transport](transport-pipe.md) - subprocess transport, CLI
  flag `--mcp-pipe`
- [TCP transport](transport-tcp.md) - attach k běžícímu GUI emu,
  Tools menu, CLI flag `--mcp-tcp-port`
- [Konfigurace](configuration.md) - INI sekce `[MCP]`, GUI Settings,
  CLI overrides
- [Přehled tools](tools-overview.md) - dostupné tools, sensitive flag,
  příklady použití
- [Přehled resources](resources-overview.md) - read-only resources,
  URI schéma, příklady
