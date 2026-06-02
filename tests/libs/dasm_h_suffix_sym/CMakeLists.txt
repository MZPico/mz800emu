# tests/libs/dasm_h_suffix_sym/CMakeLists.txt
# Standalone test - pokrývá leading-zero v HEX_H_SUFFIX symbol substituci.
# Regresní test pro F4 round-trip s pasmo: adresy >= 0xA000 nesmí v
# z80_dasm_to_str_sym() při H_SUFFIX stylu zanechat orphan '0' před
# symbolem (např. "JP 0Lc000" místo "JP Lc000").

mz_add_test(dasm_h_suffix_sym
    TYPE     STANDALONE
    SOURCES  test_h_suffix_sym_subst.c
    LIBS     mzlib_dasm_z80
    LABELS   "standalone;libs;debugger"
)
