/* test_tracepoints_machine_stub.h - the fake machine of the tracepoint tests */
#ifndef TEST_TRACEPOINTS_MACHINE_STUB_H
#define TEST_TRACEPOINTS_MACHINE_STUB_H

#include <stdint.h>

typedef struct {
    uint16_t regs[14];
    uint8_t  mem[65536];
    uint8_t  vram[4][8192];
    int      has_vram;
    uint32_t cycles, frame;
    uint64_t ticks;
    int      pauses;
} st_TP_FAKE;

extern st_TP_FAKE g_tp_fake;
void tp_fake_reset(void);

#endif
