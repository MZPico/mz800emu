#ifndef MZ1500_IORQ_H
#define MZ1500_IORQ_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "libs/cpu-z80/z80.h"

    extern uint8_t port_read_cb(z80_t *cpu, uint16_t addr, void *user_data);
    extern void port_write_cb(z80_t *cpu, uint16_t addr, uint8_t value, void *user_data);

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
    extern uint8_t port_read_with_logging_cb(z80_t *cpu, uint16_t addr, void *user_data);
    extern void port_write_with_logging_cb(z80_t *cpu, uint16_t addr, uint8_t value, void *user_data);

    /**
     * @brief Side-effect-free probe variant pro IORQ read (MZ-1500).
     *
     * V1.6+ TODO 4.2 5b. Per-chip strategie identicka s MZ-800
     * (viz mz800_iorq.h port_read_no_se_cb komentar). Joystick
     * je no_se safe; ostatni chipy vraci bus latch.
     */
    extern uint8_t port_read_no_se_cb(uint16_t addr);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MZ1500_IORQ_H */
