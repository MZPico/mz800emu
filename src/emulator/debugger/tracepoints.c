/*
 * File:   tracepoints.c
 *
 * Non-stopping tracepoints, the in-memory PSG write log and the VRAM hash
 * (see tracepoints.h for the semantics). The machine access goes through
 * the tp_machine_* functions (tracepoints_machine.c in the emulator, a stub
 * in the unit tests), so this file has no emulator dependencies.
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

#include "tracepoints.h"

#include <string.h>
#include <glib.h>

volatile uint8_t g_tracepoints_hot = 0;
uint8_t g_tracepoint_map[65536];
volatile uint8_t g_tp_psg_log_active = 0;

/* record header: u32 len, u16 id, u16 addr, u32 cycles, u32 frame, u64 ticks,
 * u8 nregs, u8 nmem, u8 flags, u8 0; then nregs x (u8 id, u16 value), nmem x
 * (u16 addr, u32 len, bytes), flags bit 0: 4 x u32 VRAM CRC-32 */
#define TP_HDR 28
#define TP_FLAG_VRAM 1

/* PSG log: the pause is requested this many records before the end (an OTIR
 * writes up to 256 bytes before the hot loop sees the request) */
#define TP_PSG_RESERVE 1024

typedef struct {
    int       id;
    st_TP_DEF def;
    uint32_t  hits;
    size_t    rec_size;
} st_TP_POINT;

static struct {
    GMutex      mutex;
    st_TP_POINT points[TP_MAX_POINTS];
    int         npoints;
    int         next_id;
    size_t      max_rec;            /* the largest record of the defined points */
    /* the trace buffer: records in buf[head..tail) */
    uint8_t    *buf;
    size_t      alloc, cap, head, tail;
    uint32_t    nrec;
    bool        pause_on_full;
    uint64_t    dropped, total;
    /* one record per executed instruction */
    bool        last_valid;
    uint16_t    last_pc;
    uint64_t    last_ticks;
    /* the PSG log: psg[phead..ptail) */
    st_TP_PSG_REC *psg;
    size_t      palloc, pcap, phead, ptail;
    bool        ppause_on_full;
    uint64_t    pdropped, ptotal;
} g_tp = { .cap = TP_DEFAULT_CAPACITY, .pause_on_full = true, .next_id = 1,
           .pcap = TP_PSG_DEFAULT_CAPACITY, .ppause_on_full = true };

static volatile uint8_t g_tp_pause_request = 0;

static void _update_hot(void)
{
    g_tracepoints_hot = (g_tp.npoints > 0 || g_tp_pause_request) ? 1 : 0;
}

/* ---------------------------------------------------------------- CRC-32 */

static uint32_t g_crc_table[256];
static gsize g_crc_init = 0;

static void _crc_init(void)
{
    if (g_once_init_enter(&g_crc_init)) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            g_crc_table[i] = c;
        }
        g_once_init_leave(&g_crc_init, 1);
    }
}

uint32_t tp_crc32(uint32_t crc, const uint8_t *buf, size_t n)
{
    _crc_init();
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = g_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool tracepoints_vram_hash(uint16_t len, uint32_t out[TP_VRAM_PLANES])
{
    if (len == 0 || len > TP_VRAM_PLANE_SIZE) len = TP_VRAM_HASH_LEN;
    for (int p = 0; p < TP_VRAM_PLANES; p++) {
        const uint8_t *pl = tp_machine_vram_plane(p);
        if (!pl) return false;
        out[p] = tp_crc32(0, pl, len);
    }
    return true;
}

/* ------------------------------------------------------------- the buffer */

static inline void _put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void _put32(uint8_t *p, uint32_t v) { _put16(p, (uint16_t)v); _put16(p + 2, (uint16_t)(v >> 16)); }
static inline void _put64(uint8_t *p, uint64_t v) { _put32(p, (uint32_t)v); _put32(p + 4, (uint32_t)(v >> 32)); }
static inline uint16_t _get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t _get32(const uint8_t *p) { return _get16(p) | ((uint32_t)_get16(p + 2) << 16); }
static inline uint64_t _get64(const uint8_t *p) { return _get32(p) | ((uint64_t)_get32(p + 4) << 32); }

static size_t _rec_size(const st_TP_DEF *d)
{
    size_t n = TP_HDR + 3u * d->nregs + (d->vram_hash ? 4u * TP_VRAM_PLANES : 0u);
    for (int i = 0; i < d->nmem; i++) n += 6u + d->mem_len[i];
    return n;
}

static void _update_max_rec(void)
{
    g_tp.max_rec = 0;
    for (int i = 0; i < g_tp.npoints; i++)
        if (g_tp.points[i].rec_size > g_tp.max_rec) g_tp.max_rec = g_tp.points[i].rec_size;
}

/* room for n more bytes at tail (compacting / growing up to cap) */
static uint8_t *_reserve(size_t n)
{
    if (g_tp.tail + n <= g_tp.alloc) return g_tp.buf + g_tp.tail;
    if (g_tp.head > 0) {
        memmove(g_tp.buf, g_tp.buf + g_tp.head, g_tp.tail - g_tp.head);
        g_tp.tail -= g_tp.head; g_tp.head = 0;
        if (g_tp.tail + n <= g_tp.alloc) return g_tp.buf + g_tp.tail;
    }
    if (g_tp.tail + n > g_tp.cap) return NULL;
    size_t na = g_tp.alloc ? g_tp.alloc * 2 : 256u * 1024u;
    if (na < g_tp.tail + n) na = g_tp.tail + n;
    if (na > g_tp.cap) na = g_tp.cap;
    g_tp.buf = g_realloc(g_tp.buf, na);
    g_tp.alloc = na;
    return g_tp.buf + g_tp.tail;
}

static void _record(const st_TP_POINT *tp, uint16_t pc, uint32_t cycles, uint32_t frame, uint64_t ticks)
{
    const st_TP_DEF *d = &tp->def;
    uint8_t *p = _reserve(tp->rec_size);
    g_tp.total++;
    if (!p) {
        g_tp.dropped++;
        if (g_tp.pause_on_full) g_tp_pause_request = 1;
        return;
    }
    uint8_t *q = p;
    _put32(q, (uint32_t)tp->rec_size); _put16(q + 4, (uint16_t)tp->id); _put16(q + 6, pc);
    _put32(q + 8, cycles); _put32(q + 12, frame); _put64(q + 16, ticks);
    q[24] = d->nregs; q[25] = d->nmem; q[26] = d->vram_hash ? TP_FLAG_VRAM : 0; q[27] = 0;
    q += TP_HDR;
    for (int i = 0; i < d->nregs; i++) {
        q[0] = d->regs[i]; _put16(q + 1, tp_machine_reg(d->regs[i])); q += 3;
    }
    for (int i = 0; i < d->nmem; i++) {
        _put16(q, d->mem_addr[i]); _put32(q + 2, d->mem_len[i]); q += 6;
        uint16_t a = d->mem_addr[i];
        for (uint32_t k = 0; k < d->mem_len[i]; k++) *q++ = tp_machine_mem((uint16_t)(a + k));
    }
    if (d->vram_hash) {
        uint32_t crc[TP_VRAM_PLANES] = { 0, 0, 0, 0 };
        (void)tracepoints_vram_hash(d->vram_len, crc);
        for (int k = 0; k < TP_VRAM_PLANES; k++) { _put32(q, crc[k]); q += 4; }
    }
    g_tp.tail += tp->rec_size;
    g_tp.nrec++;
    /* about to be full: pause while the next record still fits (lossless) */
    if (g_tp.pause_on_full && g_tp.cap - (g_tp.tail - g_tp.head) < g_tp.max_rec)
        g_tp_pause_request = 1;
}

/* ---------------------------------------------------------- the hot path */

bool tracepoints_on_instruction(uint16_t pc)
{
    if (g_tracepoint_map[pc]) {
        uint32_t cycles, frame; uint64_t ticks;
        tp_machine_now(&cycles, &frame, &ticks);
        g_mutex_lock(&g_tp.mutex);
        if (!(g_tp.last_valid && g_tp.last_pc == pc && g_tp.last_ticks == ticks)) {
            g_tp.last_valid = true; g_tp.last_pc = pc; g_tp.last_ticks = ticks;
            for (int i = 0; i < g_tp.npoints; i++) {
                if (g_tp.points[i].def.addr == pc) {
                    g_tp.points[i].hits++;
                    _record(&g_tp.points[i], pc, cycles, frame, ticks);
                }
            }
        }
        g_mutex_unlock(&g_tp.mutex);
    }
    if (g_tp_pause_request) {
        g_tp_pause_request = 0;
        g_mutex_lock(&g_tp.mutex); _update_hot(); g_mutex_unlock(&g_tp.mutex);
        tp_machine_pause();
        return true;
    }
    return false;
}

void tracepoints_psg_write(uint8_t value, unsigned channel, uint64_t ticks)
{
    /* the CPU's total_cycles is synchronised only between instructions: the
     * write's CPU time is its GDG time over the clock divider (equal to
     * total_cycles' count as long as the CPU was not reset) */
    uint32_t cycles, frame; uint64_t now;
    tp_machine_now(&cycles, &frame, &now);
    cycles = (uint32_t)(ticks / tp_machine_cpu_divider());
    g_mutex_lock(&g_tp.mutex);
    if (g_tp_psg_log_active) {
        g_tp.ptotal++;
        if (g_tp.ptail == g_tp.palloc && g_tp.phead > 0) {
            memmove(g_tp.psg, g_tp.psg + g_tp.phead, (g_tp.ptail - g_tp.phead) * sizeof(st_TP_PSG_REC));
            g_tp.ptail -= g_tp.phead; g_tp.phead = 0;
        }
        if (g_tp.ptail == g_tp.palloc && g_tp.palloc < g_tp.pcap) {
            size_t na = g_tp.palloc ? g_tp.palloc * 2 : 4096u;
            if (na > g_tp.pcap) na = g_tp.pcap;
            g_tp.psg = g_realloc(g_tp.psg, na * sizeof(st_TP_PSG_REC));
            g_tp.palloc = na;
        }
        if (g_tp.ptail < g_tp.palloc) {
            st_TP_PSG_REC *r = &g_tp.psg[g_tp.ptail++];
            r->ticks = ticks; r->cycles = cycles; r->frame = frame;
            r->value = value; r->channel = (uint8_t)channel;
        } else {
            g_tp.pdropped++;
        }
        size_t n = g_tp.ptail - g_tp.phead;
        if (g_tp.ppause_on_full && n + TP_PSG_RESERVE >= g_tp.pcap) {
            g_tp_pause_request = 1; g_tracepoints_hot = 1;
        }
    }
    g_mutex_unlock(&g_tp.mutex);
}

/* ------------------------------------------------- configuration, reading */

int tracepoints_add(const st_TP_DEF *def, const char **err)
{
    static const char *e_full = "too many tracepoints";
    static const char *e_regs = "invalid register list";
    static const char *e_mem = "invalid memory ranges (count or total length)";
    static const char *e_vram = "this machine has no VRAM planes to hash";
    if (def->nregs > TP_MAX_REGS) { *err = e_regs; return -1; }
    for (int i = 0; i < def->nregs; i++) if (def->regs[i] >= TP_MAX_REGS) { *err = e_regs; return -1; }
    if (def->nmem > TP_MAX_MEM) { *err = e_mem; return -1; }
    size_t m = 0;
    for (int i = 0; i < def->nmem; i++) {
        if (def->mem_len[i] == 0 || def->mem_len[i] > 65536u) { *err = e_mem; return -1; }
        m += def->mem_len[i];
    }
    if (m > TP_MAX_MEM_BYTES) { *err = e_mem; return -1; }
    if (def->vram_hash && !tp_machine_vram_plane(0)) { *err = e_vram; return -1; }
    g_mutex_lock(&g_tp.mutex);
    if (g_tp.npoints >= TP_MAX_POINTS) { g_mutex_unlock(&g_tp.mutex); *err = e_full; return -1; }
    st_TP_POINT *tp = &g_tp.points[g_tp.npoints++];
    tp->id = g_tp.next_id++;
    tp->def = *def;
    if (tp->def.vram_len == 0 || tp->def.vram_len > TP_VRAM_PLANE_SIZE) tp->def.vram_len = TP_VRAM_HASH_LEN;
    tp->hits = 0;
    tp->rec_size = _rec_size(&tp->def);
    _update_max_rec();
    g_tracepoint_map[def->addr]++;
    _update_hot();
    int id = tp->id;
    g_mutex_unlock(&g_tp.mutex);
    return id;
}

int tracepoints_clear(int id)
{
    g_mutex_lock(&g_tp.mutex);
    int n = 0;
    if (id <= 0) {
        n = g_tp.npoints;
        g_tp.npoints = 0;
        memset(g_tracepoint_map, 0, sizeof(g_tracepoint_map));
        g_tp.head = g_tp.tail = 0; g_tp.nrec = 0;
        g_tp.dropped = 0; g_tp.total = 0;
        g_tp.last_valid = false;
    } else {
        for (int i = 0; i < g_tp.npoints; i++) {
            if (g_tp.points[i].id == id) {
                g_tracepoint_map[g_tp.points[i].def.addr]--;
                memmove(&g_tp.points[i], &g_tp.points[i + 1], (size_t)(g_tp.npoints - i - 1) * sizeof(st_TP_POINT));
                g_tp.npoints--; n = 1;
                break;
            }
        }
        if (!n) n = -1;
    }
    _update_max_rec();
    _update_hot();
    g_mutex_unlock(&g_tp.mutex);
    return n;
}

int tracepoints_list(st_TP_DEF *out, int *ids, uint32_t *hits, int max)
{
    g_mutex_lock(&g_tp.mutex);
    int n = g_tp.npoints < max ? g_tp.npoints : max;
    for (int i = 0; i < n; i++) {
        if (out) out[i] = g_tp.points[i].def;
        if (ids) ids[i] = g_tp.points[i].id;
        if (hits) hits[i] = g_tp.points[i].hits;
    }
    g_mutex_unlock(&g_tp.mutex);
    return n;
}

void tracepoints_config(size_t capacity, bool pause_on_full)
{
    g_mutex_lock(&g_tp.mutex);
    if (capacity) {
        if (capacity < 4096) capacity = 4096;
        /* never below what is buffered now */
        if (capacity < g_tp.tail - g_tp.head) capacity = g_tp.tail - g_tp.head;
        if (g_tp.alloc > capacity) {
            memmove(g_tp.buf, g_tp.buf + g_tp.head, g_tp.tail - g_tp.head);
            g_tp.tail -= g_tp.head; g_tp.head = 0;
            g_tp.buf = g_realloc(g_tp.buf, capacity); g_tp.alloc = capacity;
        }
        g_tp.cap = capacity;
    }
    g_tp.pause_on_full = pause_on_full;
    g_mutex_unlock(&g_tp.mutex);
}

static void _status_locked(st_TP_STATUS *st)
{
    st->pending = g_tp.nrec; st->dropped = g_tp.dropped; st->overflow = g_tp.dropped != 0;
    st->total = g_tp.total; st->capacity = g_tp.cap; st->pause_on_full = g_tp.pause_on_full;
    st->active = g_tp.npoints > 0;
}

void tracepoints_status(st_TP_STATUS *st)
{
    g_mutex_lock(&g_tp.mutex); _status_locked(st); g_mutex_unlock(&g_tp.mutex);
}

int tracepoints_read(int max, st_TP_RECORD *recs, uint8_t **out_blob, st_TP_STATUS *st)
{
    *out_blob = NULL;
    g_mutex_lock(&g_tp.mutex);
    _status_locked(st);
    int n = 0; size_t bytes = 0;
    while (n < max && (uint32_t)n < g_tp.nrec) {
        bytes += _get32(g_tp.buf + g_tp.head + bytes);
        n++;
    }
    if (n) {
        *out_blob = g_malloc(bytes);
        memcpy(*out_blob, g_tp.buf + g_tp.head, bytes);
        g_tp.head += bytes; g_tp.nrec -= (uint32_t)n;
        if (g_tp.head == g_tp.tail) g_tp.head = g_tp.tail = 0;
    }
    g_tp.dropped = 0;
    st->pending = g_tp.nrec;
    g_mutex_unlock(&g_tp.mutex);
    const uint8_t *p = *out_blob;
    for (int i = 0; i < n; i++) {
        st_TP_RECORD *r = &recs[i];
        uint32_t len = _get32(p);
        r->id = _get16(p + 4); r->addr = _get16(p + 6); r->cycles = _get32(p + 8); r->frame = _get32(p + 12);
        r->ticks = _get64(p + 16); r->nregs = p[24]; r->nmem = p[25]; r->has_vram = (p[26] & TP_FLAG_VRAM) != 0;
        const uint8_t *q = p + TP_HDR;
        for (int k = 0; k < r->nregs; k++) { r->reg_id[k] = q[0]; r->reg_val[k] = _get16(q + 1); q += 3; }
        for (int k = 0; k < r->nmem; k++) {
            r->mem_addr[k] = _get16(q); r->mem_len[k] = _get32(q + 2); r->mem_data[k] = q + 6;
            q += 6 + r->mem_len[k];
        }
        if (r->has_vram) for (int k = 0; k < TP_VRAM_PLANES; k++) { r->vram_crc[k] = _get32(q); q += 4; }
        p += len;
    }
    return n;
}

/* ------------------------------------------------------------ PSG log */

void tracepoints_psg_log_start(size_t capacity, bool pause_on_full)
{
    g_mutex_lock(&g_tp.mutex);
    g_tp.pcap = capacity ? (capacity < 2 * TP_PSG_RESERVE ? 2 * TP_PSG_RESERVE : capacity) : TP_PSG_DEFAULT_CAPACITY;
    if (g_tp.palloc > g_tp.pcap) { g_tp.psg = g_realloc(g_tp.psg, g_tp.pcap * sizeof(st_TP_PSG_REC)); g_tp.palloc = g_tp.pcap; }
    g_tp.phead = g_tp.ptail = 0; g_tp.pdropped = 0; g_tp.ptotal = 0;
    g_tp.ppause_on_full = pause_on_full;
    g_tp_psg_log_active = 1;
    g_mutex_unlock(&g_tp.mutex);
}

void tracepoints_psg_log_stop(void)
{
    g_mutex_lock(&g_tp.mutex);
    g_tp_psg_log_active = 0;
    g_mutex_unlock(&g_tp.mutex);
}

static void _pstatus_locked(st_TP_STATUS *st)
{
    st->pending = (uint32_t)(g_tp.ptail - g_tp.phead); st->dropped = g_tp.pdropped; st->overflow = g_tp.pdropped != 0;
    st->total = g_tp.ptotal; st->capacity = g_tp.pcap; st->pause_on_full = g_tp.ppause_on_full;
    st->active = g_tp_psg_log_active != 0;
}

void tracepoints_psg_log_status(st_TP_STATUS *st)
{
    g_mutex_lock(&g_tp.mutex); _pstatus_locked(st); g_mutex_unlock(&g_tp.mutex);
}

int tracepoints_psg_log_read(int max, st_TP_PSG_REC *out, st_TP_STATUS *st)
{
    g_mutex_lock(&g_tp.mutex);
    _pstatus_locked(st);
    size_t avail = g_tp.ptail - g_tp.phead;
    int n = (size_t)max < avail ? max : (int)avail;
    if (n > 0) memcpy(out, g_tp.psg + g_tp.phead, (size_t)n * sizeof(st_TP_PSG_REC));
    g_tp.phead += (size_t)(n > 0 ? n : 0);
    if (g_tp.phead == g_tp.ptail) g_tp.phead = g_tp.ptail = 0;
    g_tp.pdropped = 0;
    st->pending = (uint32_t)(g_tp.ptail - g_tp.phead);
    g_mutex_unlock(&g_tp.mutex);
    return n;
}
