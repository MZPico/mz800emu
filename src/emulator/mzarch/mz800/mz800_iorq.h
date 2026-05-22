#ifndef MZ800_IORQ_H
#define MZ800_IORQ_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "libs/cpu-z80/z80.h"

    extern uint8_t port_read_cb(z80_t *cpu, uint16_t addr, void *user_data);
    extern void port_write_cb(z80_t *cpu, uint16_t addr, uint8_t value, void *user_data);

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
    /* Callback varianty s CDL recording. Aktivovány přes z80_set_p* swap
     * v mzarch_platform_fn_debugger_state_changed při zapnutí debuggeru. */
    extern uint8_t port_read_with_logging_cb(z80_t *cpu, uint16_t addr, void *user_data);
    extern void port_write_with_logging_cb(z80_t *cpu, uint16_t addr, uint8_t value, void *user_data);

    /**
     * @brief Side-effect-free probe variant pro IORQ read.
     *
     * V1.6+ TODO 4.2 5b: pouziva se v bp_expr `port[N]` (= bez '!')
     * pro probe portu v expression bez triggrovani realnych side-effectu
     * (= nezmeni state PIO/CTC/GDG/FDC chipu). Implementace per-chip:
     *
     *  - Joystick (0xF0-F1): realny read (= cista funkce, no_se safe).
     *  - Memory mapper (0xE0-E1): realny read (= write-only chip,
     *    read jen vraci bus latch, nemutuje state).
     *  - Unconnected porty: bus latch (= `regDBUS_latch`).
     *  - Side-effect-heavy chips (PIO 8255 PortC, CTC counter latch,
     *    GDG DMD strobe, FDC command/status, PIO Z80 IRQ ack): vraci
     *    bus latch jako safer fallback. Plne no_se per-chip refactor
     *    odlozen V1.7+ (vyzaduje cely chip dispatcher rozdvojit).
     *
     * @param addr 16-bit IORQ adresa (low byte = port_lsb).
     * @return Byte hodnota (pro side-effect-heavy chipy = bus latch).
     */
    extern uint8_t port_read_no_se_cb(uint16_t addr);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MZ800_IORQ_H */
