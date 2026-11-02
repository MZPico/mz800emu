/*
 * File:   tracepoints_machine.c
 *
 * The emulator side of tracepoints.c: registers, memory (the CPU view
 * through the current banking, without side effects, as mem_read), the
 * VRAM planes (MZ-800), the time stamps and the pause.
 *
 * ----------------------------- License -------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 */

#include "main.h"

#include <stdint.h>
#include <glib.h>

#include "tracepoints.h"
#include "debugger.h"
#include "emulator.h"
#include "mzarch/mzarch.h"
#include "memory/memory.h"
#include "hw-generic/gdg/gdgclk.h"

#if MZARCH == 800
#include "mzarch/mz800/gdg/mz800_gdg.h"
#include "mzarch/mz800/memory/mz800_memory.h"
#elif MZARCH == 1500
#include "mzarch/mz1500/gdg/mz1500_gdg.h"
#elif MZARCH == 700
#include "mzarch/mz700/gdg/mz700_gdg.h"
#endif

uint16_t tp_machine_reg(uint8_t id)
{
    return z80_get_reg(g_mzarch_main.cpu, (z80_reg_t)id);
}

uint8_t tp_machine_mem(uint16_t addr)
{
#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
    return debugger_memory_read_byte(addr);
#else
    (void)addr;
    return 0;
#endif
}

const uint8_t *tp_machine_vram_plane(int plane)
{
#if MZARCH == 800
    switch (plane) {
        case 0: return g_memory.VRAM;
        case 1: return &g_memory.VRAM[MEMORY_SIZE_VRAM_BANK];
        case 2: return g_memory.EXVRAM;
        case 3: return &g_memory.EXVRAM[MEMORY_SIZE_VRAM_BANK];
        default: return NULL;
    }
#else
    (void)plane;
    return NULL;
#endif
}

void tp_machine_now(uint32_t *cycles, uint32_t *frame, uint64_t *ticks)
{
    *cycles = g_mzarch_main.cpu->total_cycles;
    *frame = (uint32_t)g_gdg.total_elapsed.screens;
    *ticks = gdg_get_total_ticks();
}

unsigned tp_machine_cpu_divider(void)
{
    return GDGCLK2CPU_DIVIDER;
}

void tp_machine_pause(void)
{
    g_emulator.pause_reason = EMU_PAUSE_REASON_TRACE_FULL;
    emulator_pause(true);
}
