mz_add_test(memory
    TYPE     EMU
    SOURCES  test_memory.c
    LABELS   "emu;memory"
)

mz_add_test(memext
    TYPE     EMU
    SOURCES  test_memext.c
    LABELS   "emu;memory"
)

mz_add_test(regions
    TYPE     EMU
    SOURCES  test_regions.c
    LABELS   "emu;memory;debugger"
)
