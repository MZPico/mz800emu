# tests/libs/vram_sim/CMakeLists.txt
#
# LIB test - vram_sim core + bit-for-bit identity proti emu vramctrl.
# Vyžaduje emu core (TYPE LIB) protože smoke identity test volá
# vramctrl_mz800_memop_read_byte() a vramctrl_mz800_memop_write_byte().
# Capture adapter vram_sim_capture_from_emu() je v
# src/emulator/mzarch/mz800/vram_sim_emu_capture.c (součást emu core sběru).

mz_add_test(vram_sim
    TYPE     LIB
    SOURCES  test_vram_sim.c
    LIBS     mzlib_vram_sim
    LABELS   "lib;libs;vram_sim"
)
