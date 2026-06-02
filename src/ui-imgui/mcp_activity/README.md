# src/ui-imgui/mcp_activity/ - MCP Activity ImGui okno

Placeholder podstrom. V této fázi (V0.A.1) je adresář záměrně prázdný
(kromě tohoto README a stub `CMakeLists.txt`). Funkční UI přibyde ve
fázi V0.A.4 a dále.

## Plánovaný obsah (V0.A.4+)

- `mcp_activity_window.{cpp,h}` - hlavní ImGui okno (seznam aktivních
  MCP klientů, log posledních příkazů, statistiky)
- `mcp_activity_state.{cpp,h}` - vnitřní stav okna (filter, scroll,
  pin selektovaných položek)
- `mcp_activity_log.{cpp,h}` - kruhový buffer pro log MCP commandů +
  origin tag (per V-1.x DBGAPI rozšíření)

## Kompilační guard

Veškerý kód v tomto podstromu MUSÍ obalit veškerou logiku do
`#ifdef MZ800EMU_CFG_MCP_SERVER_ENABLED ... #endif`. Při `NO_MCP=1`
build se zde žádný symbol neexportuje (= žádný menu entry, žádný
ImGui::Begin window).

## Buildovací model

Soubory zde sbírá `mz_glob_sources(src/ui-imgui)` z
`cmake/AddMzEmu.cmake` (= rekurzivní GLOB, vyjma per-arch
podadresářů `mz[0-9]+/`). Adresář `mcp_activity/` se globuje stejně
jako ostatní non-arch UI podstromy. Prázdný adresář = nic se nesbírá.

## Integrace s top-menu

Až přibyde okno, registruje se přes standardní `src/ui-imgui/topmenu/`
mechanismus (= `_L("MCP Activity")` jako menu entry pod **Debug**).
Visibility checkbox v menu se musí guardovat
`#ifdef MZ800EMU_CFG_MCP_SERVER_ENABLED`.
