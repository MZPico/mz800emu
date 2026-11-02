/*
 * File:   tracepoints.h
 *
 * Non-stopping tracepoints, an in-memory PSG write log and a VRAM hash.
 *
 * A tracepoint is an execution address at which the emulator records a
 * snapshot (the requested registers, memory ranges and optionally a hash of
 * the VRAM planes) into a bounded buffer and goes on running. A client
 * (MCP trace_add / trace_read / trace_clear / trace_config) collects the
 * records in bulk instead of stopping the emulation at a breakpoint for
 * every event. The PSG write log (psg_log_start / psg_log_read /
 * psg_log_stop) records every byte written to the SN76489 with its time.
 *
 * Time stamps (both buffers):
 *   - cycles: the CPU's total T-state counter (z80_t.total_cycles, 32 bits,
 *     the same counter get_raster_pos reports as total_cycles; it wraps at
 *     2^32, ~20 minutes of emulated time),
 *   - ticks:  the GDG pixel-clock time since the emulator started (64 bits,
 *     monotonic; on the MZ-800 one CPU T-state = 5 ticks; the same time base
 *     as the GUI's PSG TSV write log),
 *   - frame:  the number of completed frames (g_gdg.total_elapsed.screens).
 * A tracepoint record is taken before the instruction at its address
 * executes (ticks = the instruction's start); a PSG record at the I/O write
 * itself (ticks = the end of the write's I/O cycle, wait states included;
 * its cycles = ticks / the CPU clock divider, as total_cycles is only
 * synchronised between instructions). A write with ticks <= a record's ticks
 * happened before that record's instruction, one with ticks > after it.
 *
 * Buffers are bounded. When a buffer is about to fill, the emulation pauses
 * (stopped_by "trace_full" in run's answer) so the client can drain it and
 * go on: nothing is lost. With on_full "drop" (trace_config) records that do
 * not fit are dropped and counted; trace_read / psg_log_read report the
 * overflow flag and the dropped count - never silently.
 *
 * VRAM hash: CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320, initial
 * value 0xFFFFFFFF, final XOR 0xFFFFFFFF - the zlib / Python zlib.crc32
 * function) of each of the four MZ-800 VRAM planes I, II, III, IV, bytes
 * 0..len-1 of each plane (default len 8000 = the 320x200 screen: 40 bytes
 * per row, bit 0 = leftmost pixel; the same bytes region_read returns for
 * regions 8..11).
 *
 * Threading: the record functions run on the emulator thread (hot loop, PSG
 * write); the configuration and read functions on the MCP dispatch thread.
 * One mutex guards the buffers and the tracepoint list.
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

#ifndef TRACEPOINTS_H
#define TRACEPOINTS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TP_MAX_POINTS        64      /**< tracepoints at most */
#define TP_MAX_REGS          14      /**< registers per tracepoint (z80_reg_t ids 0..13) */
#define TP_MAX_MEM           16      /**< memory ranges per tracepoint */
#define TP_MAX_MEM_BYTES     65536   /**< memory bytes per tracepoint record (all ranges) */
#define TP_VRAM_PLANES       4
#define TP_VRAM_PLANE_SIZE   8192    /**< bytes in one VRAM plane */
#define TP_VRAM_HASH_LEN     8000    /**< default hashed bytes per plane (320x200) */

#define TP_DEFAULT_CAPACITY      (64u * 1024u * 1024u)  /**< trace buffer bytes */
#define TP_PSG_DEFAULT_CAPACITY  (1024u * 1024u)        /**< PSG log records */

/** A tracepoint's definition. */
typedef struct st_TP_DEF {
    uint16_t addr;
    uint8_t  nregs;
    uint8_t  regs[TP_MAX_REGS];     /**< z80_reg_t ids (0 AF .. 13 IR) */
    uint8_t  nmem;
    uint16_t mem_addr[TP_MAX_MEM];  /**< the ranges' start addresses (CPU view, wrapping at 0xFFFF) */
    uint32_t mem_len[TP_MAX_MEM];   /**< the ranges' lengths (1..65536; all ranges <= TP_MAX_MEM_BYTES) */
    bool     vram_hash;
    uint16_t vram_len;              /**< bytes hashed per plane (1..8192) */
} st_TP_DEF;

/** A decoded tracepoint record (trace_read). Pointers point into a buffer
 *  owned by the caller of tracepoints_read. */
typedef struct st_TP_RECORD {
    uint16_t id;
    uint16_t addr;
    uint32_t cycles;
    uint32_t frame;
    uint64_t ticks;
    uint8_t  nregs;
    uint8_t  reg_id[TP_MAX_REGS];
    uint16_t reg_val[TP_MAX_REGS];
    uint8_t  nmem;
    uint16_t mem_addr[TP_MAX_MEM];
    uint32_t mem_len[TP_MAX_MEM];
    const uint8_t *mem_data[TP_MAX_MEM];
    bool     has_vram;
    uint32_t vram_crc[TP_VRAM_PLANES];
} st_TP_RECORD;

/** A PSG write. */
typedef struct st_TP_PSG_REC {
    uint64_t ticks;
    uint32_t cycles;
    uint32_t frame;
    uint8_t  value;
    uint8_t  channel;      /**< PSG channel mask (mono = 1) */
} st_TP_PSG_REC;

/** Buffer state (trace_read / psg_log_read / trace_config answers). */
typedef struct st_TP_STATUS {
    uint32_t pending;      /**< records waiting to be read */
    bool     overflow;     /**< a record was dropped since the last read */
    uint64_t dropped;      /**< records dropped since the last read */
    uint64_t total;        /**< records taken since the buffer was (re)started */
    size_t   capacity;     /**< bytes (trace) / records (PSG log) */
    bool     pause_on_full;
    bool     active;       /**< PSG log: recording */
} st_TP_STATUS;

/* ---- hot path (emulator thread) --------------------------------------- */

/** non-zero when the hot loop must call tracepoints_on_instruction: some
 *  tracepoint exists or a pause was requested */
extern volatile uint8_t g_tracepoints_hot;
/** per address: the number of tracepoints there */
extern uint8_t g_tracepoint_map[65536];
/** non-zero while the PSG write log records */
extern volatile uint8_t g_tp_psg_log_active;

/**
 * Before the instruction at pc executes (hot loop, only if g_tracepoints_hot).
 * Records the tracepoints at pc (once per executed instruction: re-entering
 * the same instruction after a pause does not record it again).
 * @return true when the emulation was paused (a buffer is full): the caller
 *         enters the pause loop and restarts the instruction.
 */
bool tracepoints_on_instruction(uint16_t pc);

/** A PSG write (psg_write_byte, emulator thread; only if g_tp_psg_log_active). */
void tracepoints_psg_write(uint8_t value, unsigned channel, uint64_t ticks);

/* ---- configuration and reading (dispatch thread) ---------------------- */

/** Adds a tracepoint; returns its id (>= 1) or -1 with *err set (static text). */
int  tracepoints_add(const st_TP_DEF *def, const char **err);
/** Removes the tracepoint id (id <= 0: all) and, for all, the buffered
 *  records and the overflow state. Returns the number removed (-1: unknown id). */
int  tracepoints_clear(int id);
/** Lists the tracepoints into out (max entries); returns the count. */
int  tracepoints_list(st_TP_DEF *out, int *ids, uint32_t *hits, int max);
/** Buffer capacity in bytes (0 = keep) and the full policy. Clears no records. */
void tracepoints_config(size_t capacity, bool pause_on_full);
/**
 * Removes up to max records from the buffer and decodes them into recs.
 * The records' memory pointers point into *out_blob (caller g_free()s it).
 * Resets the overflow flag and dropped count (reported in st).
 * @return the number of records.
 */
int  tracepoints_read(int max, st_TP_RECORD *recs, uint8_t **out_blob, st_TP_STATUS *st);
void tracepoints_status(st_TP_STATUS *st);

/** Starts (and clears) the PSG write log; capacity in records (0 = default). */
void tracepoints_psg_log_start(size_t capacity, bool pause_on_full);
/** Stops recording (the buffered writes stay readable). */
void tracepoints_psg_log_stop(void);
/** Removes up to max writes into out; resets the overflow state. */
int  tracepoints_psg_log_read(int max, st_TP_PSG_REC *out, st_TP_STATUS *st);
void tracepoints_psg_log_status(st_TP_STATUS *st);

/** CRC-32 (zlib) of the VRAM planes' first len bytes; false if the machine has no planes. */
bool tracepoints_vram_hash(uint16_t len, uint32_t out[TP_VRAM_PLANES]);

/** CRC-32, zlib-compatible: crc = tp_crc32(0, buf, n) continues with tp_crc32(crc, ...). */
uint32_t tp_crc32(uint32_t crc, const uint8_t *buf, size_t n);

/* ---- the machine (tracepoints_machine.c; a stub in the unit tests) ------ */

uint16_t tp_machine_reg(uint8_t id);
uint8_t  tp_machine_mem(uint16_t addr);
/** plane 0..3 (I..IV), NULL if not available */
const uint8_t *tp_machine_vram_plane(int plane);
void     tp_machine_now(uint32_t *cycles, uint32_t *frame, uint64_t *ticks);
/** GDG ticks per CPU T-state (MZ-800: 5) */
unsigned tp_machine_cpu_divider(void);
/** pause the emulation with the reason "trace_full" */
void     tp_machine_pause(void);

#ifdef __cplusplus
}
#endif

#endif /* TRACEPOINTS_H */
