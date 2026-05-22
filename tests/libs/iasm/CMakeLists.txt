# tests/libs/iasm/CMakeLists.txt
# Standalone test - jen mzlib_iasm + Unity (žádný emulator core)

mz_add_test(iasm
    TYPE     STANDALONE
    SOURCES  test_iasm.c
    LIBS     mzlib_iasm
    LABELS   "standalone;libs"
)
