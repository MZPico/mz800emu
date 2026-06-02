# tests/libs/mzglyphs/CMakeLists.txt
# Standalone test - mzlib_mz_vcode + mzlib_mzglyphs + Unity.
# Žádné emu core (knihovny jsou nezávislé).

mz_add_test(mzglyphs
    TYPE     STANDALONE
    SOURCES  test_mzglyphs.c
    LIBS     mzlib_mz_vcode mzlib_mzglyphs
    LABELS   "standalone;libs"
)
