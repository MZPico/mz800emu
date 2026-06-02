# tests/libs/dasm_motorola_ix/CMakeLists.txt
# Standalone test - Motorola IX/IY render styl pro dialect sdcc-asz80.
#
# Pokrývá fázi F6 (dialect sdcc-asz80) z plánu disassembler-window-v1:
# z80_dasm_format_t.ix_iy_style field, render varianty Z80_IX_ZILOG
# (default, backward-compat) a Z80_IX_MOTOROLA ("(d,IX)" / "(-d,IY)").

mz_add_test(dasm_motorola_ix
    TYPE     STANDALONE
    SOURCES  test_motorola_ix.c
    LIBS     mzlib_dasm_z80
    LABELS   "standalone;libs;debugger"
)
