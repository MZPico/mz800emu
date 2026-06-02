# src/emulator/mcp/CMakeLists.txt - MCP server backend (placeholder)
#
# V0.A.2 stav: zdrojáky tohoto podstromu se sbírají centrálně přes
# `mz_glob_sources(... src/emulator/mcp)` v `cmake/AddMzEmu.cmake` (=
# stejný pattern jako src/emulator/hw-generic/, debugger/, snapshot/).
# Per-podstrom `add_subdirectory(...)` se nepoužívá.
#
# Tento soubor zatím zůstává jako placeholder pro budoucí extrakci
# do samostatného `add_library(STATIC)` cíle (= pokud V0.A.3+ rozhodne,
# že MCP backend má smysl mít jako separátní knihovnu).
#
# Soubory aktuálně v podstromu (V0.A.3):
#   jsonl_io.{h,c}  - JSONL transport core (parser + encoder + stream I/O)
#   dispatch.{h,c}  - dispatch tabulka cmd -> dbgapi + 9 core handlerů
#
# Plánované v dalších fázích:
#   main_pipe.{c,h} - V0.A.4 (stdin/stdout transport thread)
#   tcp.{c,h}       - V0.A.5 (loopback TCP listener, MZ_NO_MCP_TCP guard)
#
# Pokud V0.A.3+ tento podstrom dostane vlastní static library:
#
#   add_library(mz_mcp STATIC
#       jsonl_io.c
#       dispatch.c       # V0.A.3 - implementováno
#       main_pipe.c      # V0.A.4 - plánováno
#       $<$<NOT:$<BOOL:${MZ_NO_MCP_TCP}>>:tcp.c>
#   )
#   target_include_directories(mz_mcp PRIVATE ${CMAKE_SOURCE_DIR}/src)
#   target_link_libraries(mz_mcp PUBLIC mz::glib mz::json_glib)
#   add_library(mz::mcp ALIAS mz_mcp)
#
# A v AddMzEmu.cmake by se mz::mcp linkovalo do mz_add_emulator() místo
# globbing src/emulator/mcp/ do _sources_emu.
