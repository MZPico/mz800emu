/*
 * test_tracepoints_machine_stub.c - a fake machine for src/emulator/debugger/tracepoints.c
 * (the tp_machine_* functions): registers, 64 KB of memory, four VRAM planes, the time stamps and
 * a pause counter, all set by the tests (tests/mcp/test_tracepoints.c).
 */

#include <stdint.h>
#include <string.h>
#include "emulator/debugger/tracepoints.h"
#include "test_tracepoints_machine_stub.h"

st_TP_FAKE g_tp_fake;

void tp_fake_reset(void)
{
    memset(&g_tp_fake, 0, sizeof(g_tp_fake));
    g_tp_fake.has_vram = 1;
}

uint16_t tp_machine_reg(uint8_t id) { return id < 14 ? g_tp_fake.regs[id] : 0; }
uint8_t tp_machine_mem(uint16_t addr) { return g_tp_fake.mem[addr]; }
const uint8_t *tp_machine_vram_plane(int plane)
{
    if (!g_tp_fake.has_vram || plane < 0 || plane > 3) return NULL;
    return g_tp_fake.vram[plane];
}
void tp_machine_now(uint32_t *cycles, uint32_t *frame, uint64_t *ticks)
{
    *cycles = g_tp_fake.cycles; *frame = g_tp_fake.frame; *ticks = g_tp_fake.ticks;
}
unsigned tp_machine_cpu_divider(void) { return 5; }
void tp_machine_pause(void) { g_tp_fake.pauses++; }
