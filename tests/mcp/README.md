# tests/mcp/ - MCP test suite

Unity testy pro MCP backend (`src/emulator/mcp/`).

## Aktuální stav (V0.A.2)

| Test | Typ | Popis |
|------|-----|-------|
| `test_mcp_jsonl_parser` | STANDALONE | JSONL transport core (parser, encoder, stream I/O) |

Pokrytí `test_mcp_jsonl_parser`:
- 6 typů zpráv (REQUEST, RESPONSE, EVENT, TRAP, TRAP_RESPONSE, HELLO)
  - parser klasifikace podle pole `type` (primární)
  - parser fingerprint fallback (sekundární, bez `type`)
- Encoder builders + roundtrip pro všech 6 typů
- Edge cases: empty/whitespace/invalid JSON, not-an-object, unknown
  schema/type, NULL args, `jsonl_msg_free(NULL)` safety
- Data sub-tree payload roundtrip (JsonNode deep-copy)
- Stream I/O: write/read N řádků nad `tmpfile()`, EOF detekce, CRLF
  stripping, NULL args

## Plánovaný obsah (V0.A.3+)

- `test_mcp_dispatch.c` - mapování `cmd` -> DBGAPI příkaz (V0.A.3)
- `test_mcp_queue.c` - thread-safe fronta (V0.A.4)
- `test_pipe.py` - end-to-end pipe transport (V0.A.4)
- `test_tcp.py` - end-to-end TCP (V0.A.5, vyžaduje `MZ_NO_MCP_TCP=OFF`)

## Build / spuštění

```bash
# Sestavení + spuštění všech testů (běžný flow)
make test

# Jen MCP testy
ctest --test-dir build -R "^mcp_"

# Jen po labelu mcp
ctest --test-dir build -L mcp

# Manuálně přímo binárku
./build/build-tests/test_mcp_jsonl_parser.exe
```

## Kompilační guard

`tests/mcp/CMakeLists.txt` má `if(MZ_NO_MCP) return() endif()` - při
buildu `make NO_MCP=1` se test nezaregistruje (jeho jediný zdroj
`jsonl_io.c` by se v guard-blockovaném režimu zkompiloval jako prázdný
TU a test by selhal na undefined symbol).
