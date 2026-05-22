# tests/libs/dasm_sym/CMakeLists.txt
# Standalone test - mzlib_dasm_z80 + Unity
# Pokrývá symbol resolve (CALL/JP/JR/JR cc/DJNZ/RST + filtry JP (HL)/(IX)/(IY))
# = CHECKLIST 3.1 v debugger-v17plus.

mz_add_test(dasm_sym
    TYPE     STANDALONE
    SOURCES  test_dasm_sym.c
    LIBS     mzlib_dasm_z80
    LABELS   "standalone;libs;debugger"
)
