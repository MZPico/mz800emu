# tests/libs/mcs48/CMakeLists.txt
# Standalone test - mzlib_mcs48 + Unity (zadny emu core).
#
# Pokriva jadro mikrokontroleru Intel MCS-48 (8050) plotteru MZ-1P16:
# kompletni ISA, flagy, timer/counter, preruseni, banky, 12-bit adresace.
# Viz emu-experiments/mz1p16-plotter/plans/FAZE-1-mcs48-core.md.

mz_add_test(mcs48
    TYPE     STANDALONE
    SOURCES  test_mcs48.c
    LIBS     mzlib_mcs48
    LABELS   "standalone;libs;mz1p16"
)
