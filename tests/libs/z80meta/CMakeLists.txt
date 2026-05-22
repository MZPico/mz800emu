# tests/libs/z80meta/CMakeLists.txt
# Standalone test - mzlib_z80meta + mzlib_dasm_z80 + Unity
# (zadny emu core, jen ciste C knihovny)

mz_add_test(z80meta
    TYPE     STANDALONE
    SOURCES  test_z80meta.c
    LIBS     mzlib_z80meta mzlib_dasm_z80
    LABELS   "standalone;libs"
)
