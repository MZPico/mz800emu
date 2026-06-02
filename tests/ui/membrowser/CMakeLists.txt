# Memory Browser V0 hex MVP - state/encoding unit testy.
# Pure logika bez emu/SDL/ImGui - STANDALONE typ.

mz_add_test(membrowser_state
    TYPE     STANDALONE
    SOURCES  test_membrowser_state.c
    LIBS     mzlib_sharpmz_ascii mzlib_mz_vcode mzlib_mzglyphs
    LABELS   "standalone;libs;ui-membrowser"
)

# Memory Browser V4 search engine - pure parser/match logika.
# Drží lokalní kopii parse helperů (zrcadlí membrowser_search.cpp).
# STANDALONE - žádný emu / regex / bp_expr potřeba.
mz_add_test(membrowser_search
    TYPE     STANDALONE
    SOURCES  test_membrowser_search.c
    LABELS   "standalone;ui-membrowser"
)

# Memory Browser V5 diff engine - byte comparison + stats.
# Pure C, žádný emu / ImGui dependency. STANDALONE typ.
# Linkuje engine impl. soubor přímo (.c bez ifdef MZ800EMU_CFG_DEBUGGER_ENABLED).
mz_add_test(membrowser_diff
    TYPE     STANDALONE
    SOURCES
        test_membrowser_diff.c
        ${CMAKE_SOURCE_DIR}/src/ui-imgui/debugger/membrowser/membrowser_diff_engine.c
    LABELS   "standalone;ui-membrowser"
)

# Memory Browser V6 fill engine + undo/redo.
# Pure C, STANDALONE typ - linkuje fill + undo .c soubory.
mz_add_test(membrowser_fill
    TYPE     STANDALONE
    SOURCES
        test_membrowser_fill.c
        ${CMAKE_SOURCE_DIR}/src/ui-imgui/debugger/membrowser/membrowser_fill.c
        ${CMAKE_SOURCE_DIR}/src/ui-imgui/debugger/membrowser/membrowser_undo.c
    LABELS   "standalone;ui-membrowser"
)

# Memory Browser V6 annotation storage + file roundtrip.
# Pure C, STANDALONE typ.
mz_add_test(membrowser_annotations
    TYPE     STANDALONE
    SOURCES
        test_membrowser_annotations.c
        ${CMAKE_SOURCE_DIR}/src/ui-imgui/debugger/membrowser/membrowser_annotations.c
    LABELS   "standalone;ui-membrowser"
)

# Edit semantics: recently-edited storage + per-byte undo cycle.
# Pure C, STANDALONE typ - linkuje edited + undo .c soubory.
mz_add_test(membrowser_edited
    TYPE     STANDALONE
    SOURCES
        test_membrowser_edited.c
        ${CMAKE_SOURCE_DIR}/src/ui-imgui/debugger/membrowser/membrowser_edited.c
        ${CMAKE_SOURCE_DIR}/src/ui-imgui/debugger/membrowser/membrowser_undo.c
    LABELS   "standalone;ui-membrowser"
)
