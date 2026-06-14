# tests/libs/mz1p16/CMakeLists.txt
# Standalone test - mzlib_mz1p16 (embeddovany firmware + mechanika) + Unity.
#
# Pokryva fazi 2 experimentu mz1p16-plotter:
#  - smoke boot realneho firmwaru 8050 (cisty doboeh do idle poll smycky,
#    zadne nedefinovane opcody),
#  - empiricky odvozeny stepper dekoder (smer kroku ze 4-fazove sekvence),
#  - host-side Centronics harness (firmware reaguje na vstup pohybem pera).
# Viz emu-experiments/mz1p16-plotter/plans/FAZE-2-rom-mechanika.md.

mz_add_test(mz1p16
    TYPE     STANDALONE
    SOURCES  test_mz1p16.c
    LIBS     mzlib_mz1p16
    LABELS   "standalone;libs;mz1p16"
)
