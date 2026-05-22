# tests/libs/cmtspeed/CMakeLists.txt
# Standalone - mzlib_cmtspeed + Unity, potřebuje libm

mz_add_test(cmtspeed
    TYPE     STANDALONE
    SOURCES  test_cmtspeed.c
    LIBS     mzlib_cmtspeed m
    LABELS   "standalone;libs"
)
