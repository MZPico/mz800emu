# tests/libs/dasm_export/CMakeLists.txt
# Standalone test - dasm_export modul + mzlib_dasm_z80 + Unity.
#
# Pokrývá disassembler V1 (F2/F3): auto-label scanner S/L/D/W,
# konflikt resolve W>L>S>D, mid-instruction WARN detekce, sym_db
# override (per-call gating, NULL/non-NULL větve), edge case
# mid-instruction sym_db match (sym name wins on name, type stays WARN).

mz_add_test(dasm_export
    TYPE     STANDALONE
    SOURCES  test_dasm_export.c
             ${CMAKE_SOURCE_DIR}/src/emulator/debugger/dasm_export.c
    LIBS     mzlib_dasm_z80
    LABELS   "standalone;libs;debugger"
)

# dasm_export.c se kompiluje jen pokud MZ800EMU_CFG_DEBUGGER_ENABLED.
# Define dorazí přes include "mzarch/mzarch_config.h", který modul
# zařazuje na začátku - test_dasm_export.c jej taky zařadí.
