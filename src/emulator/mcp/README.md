# src/emulator/mcp/ - MCP server backend (C)

Backend MCP (Model Context Protocol) serveru emulátoru. Zprostředkovává
přístup k debuggeru přes line-delimited JSON (JSONL) - sdílený transport
pro pipe i loopback TCP.

## Aktuální stav (V0.A.2)

| Soubor | Popis | Velikost |
|--------|-------|----------|
| `jsonl_io.h` | Public API JSONL parseru / encoderu | ~400 ř. |
| `jsonl_io.c` | Implementace nad json-glib | ~500 ř. |

Hotovo:
- 6 typů zpráv: REQUEST, RESPONSE, EVENT, TRAP, TRAP_RESPONSE, HELLO
- Parser s klasifikací podle `type` pole + fingerprint fallback
- Encoder builders pro všech 6 typů (single-line JSON output)
- Stream I/O nad `FILE *` (`jsonl_read_line` / `jsonl_write_line`)
- Unit testy v `tests/mcp/test_jsonl_parser.c`

## Plánovaný obsah (V0.A.3+)

- `dispatch.{c,h}` - V0.A.3: mapování JSON `cmd` -> DBGAPI příkaz
- `main_pipe.{c,h}` - V0.A.4: pipe transport (stdin/stdout) + emu thread
- `tcp.{c,h}` - V0.A.5: TCP listener na 127.0.0.1, guard `MZ800EMU_CFG_MCP_TCP_ENABLED`
- `queue.{c,h}` - V0.A.4: thread-safe fronta mezi MCP a emu vláknem

## Kompilační guard

Celý podstrom je podmíněn `MZ800EMU_CFG_MCP_SERVER_ENABLED` (viz
`src/emulator/mzarch/mzarch_config.h`). Při buildu s `make NO_MCP=1`
(případně cascade z `NO_DEBUGGER=1`) jsou zde umístěné `.c` soubory
zkompilovány jako prázdné translation units (= žádné symboly, žádná
závislost na `json-glib`).

TCP-specifické soubory v dalších fázích navíc obalí celý obsah do
`#ifdef MZ800EMU_CFG_MCP_TCP_ENABLED ... #endif`.

## Build model

Zdrojáky tohoto podstromu sbírá `mz_glob_sources(... src/emulator/mcp)`
v `cmake/AddMzEmu.cmake` (rekurzivně, jako pro `hw-generic/`,
`debugger/`, `snapshot/`). `add_subdirectory` zde NENÍ použité.

Pro testy: stejný podstrom je přidán do `mz_test_core_mz800` v
`cmake/AddMzTest.cmake`. Standalone unit test `tests/mcp/test_jsonl_parser`
nicméně linkuje pouze `jsonl_io.c` přímo (= bez závislosti na celém
emu core), s definicí `MZ800EMU_MCP_TEST_BUILD` která vypne
`mzarch_config.h` include.

## Reference

- Specifikace transportu: `debugger/rozbory/budoucnost/mcp-server/README.md` sekce 6.3
- INSTRUKCE V0.A.2: `mcp-server/plans/INSTRUKCE-faze-V0.A.2-jsonl-parser.md`
- Inspirace (ne port): `ai2-mz800emu/mcp-emu/jsonl_io.{h,c}`
- Devdoc: `devdoc/mcp-server/jsonl-transport.md`
