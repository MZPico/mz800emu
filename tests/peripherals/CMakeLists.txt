mz_add_test(ctc8253
    TYPE     EMU
    SOURCES  test_ctc8253.c
    LABELS   "emu;peripherals"
)

mz_add_test(pio8255
    TYPE     EMU
    SOURCES  test_pio8255.c
    LABELS   "emu;peripherals"
)

mz_add_test(psg
    TYPE     EMU
    SOURCES  test_psg.c
    LABELS   "emu;peripherals"
)

mz_add_test(unicard
    TYPE     EMU
    SOURCES  test_unicard.c
    LABELS   "emu;peripherals"
)

mz_add_test(wd279x
    TYPE     EMU
    SOURCES  test_wd279x.c
    LABELS   "emu;peripherals"
)
