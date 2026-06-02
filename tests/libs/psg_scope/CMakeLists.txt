# tests/libs/psg_scope/CMakeLists.txt
# Standalone - parita-test pro PSG Audio Scope helper algoritmy.
# Bez emu core, jen Unity + libm.

mz_add_test(psg_scope
    TYPE     STANDALONE
    SOURCES  test_psg_scope.c
    LIBS     m
    LABELS   "standalone;debugger"
)
