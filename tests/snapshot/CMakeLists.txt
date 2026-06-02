mz_add_test(snapshot-io
    TYPE     EMU
    SOURCES  test_snapshot_io.c
    LABELS   "emu;snapshot"
)

mz_add_test(snapshot-xml
    TYPE     EMU
    SOURCES  test_snapshot_xml.c
    LABELS   "emu;snapshot"
)

mz_add_test(snapshot-mgr
    TYPE     EMU
    SOURCES  test_snapshot_mgr.c
    LABELS   "emu;snapshot"
)

mz_add_test(snapshot-buffer
    TYPE     EMU
    SOURCES  test_snapshot_buffer.c
    LABELS   "emu;snapshot"
)
