/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file mz1p16_print.c
 * @brief Standalone harness: OFFLINE tisk zachycených dat na plotter MZ-1P16.
 *
 * Vezme syrový stream bajtů (typicky capture z MZ-800 LPRINT, soubor
 * "printer-<timestamp>.bin") a PŘEDÁ HO REÁLNÉMU FIRMWARU plotteru ke
 * zpracování - bez připojeného MZ-800 a bez živého emulátoru. Žádné
 * reverzování protokolu: firmware ho umí, my mu jen po jednom dodáme bajty
 * přes host handshake (data na P2 + puls /INT) přesně tak, jak to dělá
 * mechanika v emulátoru (viz mz1p16_host_send_byte v mz1p16.c) a zaznamenáme,
 * co pero nakreslí.
 *
 * Postup:
 *  1) Boot firmwaru + open-loop homing vozíku (T1=1, BEZ kreslicího self-testu).
 *  2) Pro každý bajt: host_data = bajt, /INT na chvíli LOW (STROBE assert),
 *     pak HIGH, a doběh (settle) - během toho se vzorkuje poloha pera.
 *  3) Render zaznamenaných tahů do PNG (spojité úsečky, 4 barvy).
 *
 * Render i PNG writer jsou převzaty z mz1p16_drawtest.c (stejný styl výstupu).
 *
 * Použití (z kořene repa mz800new-mz1p16):
 *   tools/mz1p16-harness/mz1p16_print.exe <vstup.bin> [vystup.png] \
 *                                         [settle_steps] [warmup_cycles]
 *
 * @note NENÍ napojeno do architektury (standalone, čistě C).
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "emulator/hw-generic/mz1p16/mcs48.h"
#include "emulator/hw-generic/mz1p16/mz1p16.h"
#include "emulator/hw-generic/mz1p16/mz1p16_rom.h"

/* ===================================================================== */
/* Záznam pen-down bodů                                                   */
/* ===================================================================== */

/** @brief Jeden zaznamenaný bod stopy pera (s perem dole). */
typedef struct st_Point {
    int32_t x;         /**< Pozice X (kroky vozíku). */
    int32_t y;         /**< Pozice Y (kroky papíru). */
    uint8_t color;     /**< Index barvy 0-3 v okamžiku položení. */
    uint8_t newstroke; /**< 1 = začátek nového tahu (pero právě dosedlo). */
} st_Point;

#define MAX_POINTS 200000

static st_Point g_pts[MAX_POINTS];
static long     g_npts = 0;
static long     g_cur_byte = -1;   /**< Index právě doručovaného bajtu (pro trace). */
static long     g_pt_byte[MAX_POINTS]; /**< Byte index, při kterém vznikl bod. */

/* ===================================================================== */
/* Minimální PNG writer (deflate "stored", bez zlib) - z mz1p16_drawtest  */
/* ===================================================================== */

static uint32_t g_crc_tab[256];
static bool     g_crc_ready = false;

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_tab[n] = c;
    }
    g_crc_ready = true;
}

static void put_be32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v>>24), (uint8_t)(v>>16),
                     (uint8_t)(v>>8), (uint8_t)v };
    fwrite(b, 1, 4, f);
}

static void png_chunk2(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    put_be32(f, len);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);

    if (!g_crc_ready) crc_init();
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < 4; i++)
        crc = g_crc_tab[(crc ^ (uint8_t)type[i]) & 0xFF] ^ (crc >> 8);
    for (uint32_t i = 0; i < len; i++)
        crc = g_crc_tab[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    put_be32(f, crc ^ 0xFFFFFFFFu);
}

static uint32_t adler32(const uint8_t *buf, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + buf[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static bool png_write(const char *path, const uint8_t *rgb, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    ihdr[0]=(uint8_t)(w>>24); ihdr[1]=(uint8_t)(w>>16);
    ihdr[2]=(uint8_t)(w>>8);  ihdr[3]=(uint8_t)w;
    ihdr[4]=(uint8_t)(h>>24); ihdr[5]=(uint8_t)(h>>16);
    ihdr[6]=(uint8_t)(h>>8);  ihdr[7]=(uint8_t)h;
    ihdr[8]=8; ihdr[9]=2; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;
    png_chunk2(f, "IHDR", ihdr, 13);

    size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) { fclose(f); return false; }
    size_t p = 0;
    for (int y = 0; y < h; y++) {
        raw[p++] = 0;
        memcpy(raw + p, rgb + (size_t)y * w * 3, (size_t)w * 3);
        p += (size_t)w * 3;
    }

    size_t max_block = 65535;
    size_t nblocks = (raw_len + max_block - 1) / max_block;
    size_t zlen = 2 + raw_len + nblocks * 5 + 4;
    uint8_t *z = (uint8_t *)malloc(zlen);
    if (!z) { free(raw); fclose(f); return false; }
    size_t zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    size_t off = 0;
    while (off < raw_len) {
        size_t chunk = raw_len - off;
        if (chunk > max_block) chunk = max_block;
        int final = (off + chunk >= raw_len) ? 1 : 0;
        z[zp++] = (uint8_t)final;
        z[zp++] = (uint8_t)(chunk & 0xFF);
        z[zp++] = (uint8_t)((chunk >> 8)&0xFF);
        z[zp++] = (uint8_t)(~chunk & 0xFF);
        z[zp++] = (uint8_t)((~chunk >> 8)&0xFF);
        memcpy(z + zp, raw + off, chunk);
        zp += chunk; off += chunk;
    }
    uint32_t ad = adler32(raw, raw_len);
    z[zp++]=(uint8_t)(ad>>24); z[zp++]=(uint8_t)(ad>>16);
    z[zp++]=(uint8_t)(ad>>8);  z[zp++]=(uint8_t)ad;

    png_chunk2(f, "IDAT", z, (uint32_t)zp);
    png_chunk2(f, "IEND", NULL, 0);

    free(z); free(raw); fclose(f);
    return true;
}

/* ===================================================================== */
/* Rasterizace - z mz1p16_drawtest                                        */
/* ===================================================================== */

typedef struct st_Canvas {
    int w, h;
    uint8_t *px;
} st_Canvas;

static void cv_set(st_Canvas *c, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    uint8_t *p = c->px + ((size_t)y * c->w + x) * 3;
    p[0]=r; p[1]=g; p[2]=b;
}

static void cv_line(st_Canvas *c, int x0, int y0, int x1, int y1,
                    uint8_t r, uint8_t g, uint8_t b)
{
    int dx = abs(x1-x0), sx = x0<x1?1:-1;
    int dy = -abs(y1-y0), sy = y0<y1?1:-1;
    int err = dx+dy;
    for (;;) {
        cv_set(c, x0, y0, r, g, b);
        if (x0==x1 && y0==y1) break;
        int e2 = 2*err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/** @brief RGB barvy per index (black, red, green, blue) - jako drawtest. */
static void color_rgb(uint8_t idx, uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (idx & 3) {
        case 0: *r=20;  *g=20;  *b=20;  break; /* černá */
        case 1: *r=220; *g=50;  *b=50;  break; /* červená */
        case 2: *r=30;  *g=170; *b=60;  break; /* zelená */
        case 3: *r=40;  *g=90;  *b=220; break; /* modrá */
        default:*r=0;*g=0;*b=0;break;
    }
}

/* ===================================================================== */
/* Stav jádra + sampling pera                                            */
/* ===================================================================== */

static st_MCS48  g_cpu;
static st_MZ1P16 g_mech;

/**
 * @brief Vzorkne aktuální polohu pera; pokud je dole a posunulo se (nebo právě
 * dosedlo), zaznamená bod. Drží stav pro detekci nového tahu (pen-up hrana).
 */
static void rec_sample(int32_t *lastx, int32_t *lasty, bool *prev_pen)
{
    bool pd = g_mech.pen_down;
    if (pd && (!*prev_pen ||
               g_mech.sx.pos != *lastx || g_mech.sy.pos != *lasty)) {
        if (g_npts < MAX_POINTS) {
            g_pts[g_npts].x = g_mech.sx.pos;
            g_pts[g_npts].y = g_mech.sy.pos;
            g_pts[g_npts].color = g_mech.color;
            g_pts[g_npts].newstroke = *prev_pen ? 0u : 1u;
            g_pt_byte[g_npts] = g_cur_byte;
            g_npts++;
        }
        *lastx = g_mech.sx.pos; *lasty = g_mech.sy.pos;
    }
    *prev_pen = pd;
}

/* ===================================================================== */
/* Hlavní                                                                */
/* ===================================================================== */

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
            "use: %s <vstup.bin> [vystup.png] [settle_steps] [warmup_cycles]\n"
            "  predani zachycenych dat tiskarny realnemu firmwaru MZ-1P16 offline\n",
            argv[0]);
        return 2;
    }
    const char *inpath = argv[1];
    const char *out    = (argc > 2) ? argv[2] : "../out/print.png";
    long maxper = (argc > 3) ? atol(argv[3]) : 400000;  /* strop kroků na bajt */
    long warmup = (argc > 4) ? atol(argv[4]) : 3000000; /* boot + homing */

    /* --- Načti vstupní stream --- */
    FILE *fi = fopen(inpath, "rb");
    if (!fi) { fprintf(stderr, "print: cannot open \"%s\"\n", inpath); return 1; }
    fseek(fi, 0, SEEK_END);
    long fsz = ftell(fi);
    fseek(fi, 0, SEEK_SET);
    if (fsz <= 0) { fprintf(stderr, "print: empty input\n"); fclose(fi); return 1; }
    uint8_t *data = (uint8_t *)malloc((size_t)fsz);
    if (!data) { fclose(fi); fprintf(stderr, "oom\n"); return 1; }
    size_t n = fread(data, 1, (size_t)fsz, fi);
    fclose(fi);

    /* --- Boot + homing (T1=1 => BEZ kreslicího self-testu) --- */
    mcs48_init(&g_cpu, mz1p16_rom_data());
    mz1p16_init(&g_mech, &g_cpu);
    for (long i = 0; i < warmup; i++) mcs48_step(&g_cpu);

    /* Zahoď cokoliv, co vzniklo během bootu/rotace karuselu (pero nahoře). */
    mz1p16_clear_drawing(&g_mech);
    g_npts = 0;

    /* --- Předej stream firmwaru, bajt po bajtu; pacuj přes REÁLNÝ BUSY (P1.4) ---
     *
     * Firmware v klidu (po homingu) drží P1.4 = 0 (ready) a při zpracování bajtu
     * P1.4 = 1 (busy). Korektní host handshake pro každý bajt:
     *   1) počkej na READY (P1.4=0),
     *   2) polož data na P2, STROBE puls (/INT 0 -> 1),
     *   3) počkej, až firmware převezme = BUSY (P1.4=1).
     * Cap maxper = anti-hang. Pero vzorkujeme po každém kroku. */
    int32_t lastx = INT32_MIN, lasty = INT32_MIN;
    bool prev_pen = false;
    long overruns = 0;
#define P14(c) ( ( (c)->P1 >> 4 ) & 1 )
    /* ROBUSTNÍ handshake řízený BUSY hranou (deterministický, imunní vůči
     * časování). Odpovídá reálnému STROBE-BUSY protokolu (viz handshake-SOLVED
     * §3). Pro každý bajt:
     *   1) počkej na READY (P1.4=0),
     *   2) polož data na P2, STROBE assert (/INT=0),
     *   3) DRŽ strobe, dokud firmware bajt nepřijme = zvedne BUSY (P1.4 0->1),
     *   4) STROBE deassert (/INT=1),
     *   5) počkej, až firmware dokončí a vrátí se do READY (P1.4 1->0).
     * Signál "přijato" = náběžná hrana BUSY (spolehlivější než pouhé IN A,P2,
     * které firmware dělá i spekulativně v idle smyčce). */
    const char *trace_env = getenv("MZ1P16_TRACE_BYTE");
    long trace_lo = trace_env ? atol(trace_env) : -1;
    long trace_hi = trace_lo + 20;
    bool tr_pen = false;
    for (size_t bi = 0; bi < n; bi++) {
        long st = 0;
        g_cur_byte = (long)bi;
        bool tracing = ((long)bi >= trace_lo && (long)bi <= trace_hi);
        g_mech.log_phases = tracing;
        if (tracing) fprintf(stderr, "[byte %zu = 0x%02X] start xpos=%ld ypos=%ld pen=%d\n",
                             bi, data[bi], (long)g_mech.sx.pos, (long)g_mech.sy.pos, g_mech.pen_down);
        /* 1) ready */
        while (P14(&g_cpu) != 0 && st < maxper) { mcs48_step(&g_cpu); rec_sample(&lastx,&lasty,&prev_pen);
            if (tracing && g_mech.pen_down != tr_pen) { tr_pen = g_mech.pen_down;
                fprintf(stderr, "    [b%zu rdy] pen->%d xpos=%ld ypos=%ld\n", bi, tr_pen, (long)g_mech.sx.pos, (long)g_mech.sy.pos); }
            st++; }
        /* 2) deliver + strobe assert */
        g_mech.host_data = data[bi];
        g_cpu.INT = 0;
        /* 3) drž strobe, dokud firmware nezvedne BUSY (přijetí bajtu) */
        while (P14(&g_cpu) == 0 && st < maxper) { mcs48_step(&g_cpu); rec_sample(&lastx,&lasty,&prev_pen);
            if (tracing && g_mech.pen_down != tr_pen) { tr_pen = g_mech.pen_down;
                fprintf(stderr, "    [b%zu bsy] pen->%d xpos=%ld ypos=%ld\n", bi, tr_pen, (long)g_mech.sx.pos, (long)g_mech.sy.pos); }
            st++; }
        /* 4) strobe deassert */
        g_cpu.INT = 1;
        /* 5) počkej na návrat do READY */
        while (P14(&g_cpu) != 0 && st < maxper) { mcs48_step(&g_cpu); rec_sample(&lastx,&lasty,&prev_pen); st++; }
        if (st >= maxper) {
            overruns++;
            fprintf(stderr, "  OVERRUN byte[%zu]=0x%02X ('%c') P1=0x%02X xpos=%ld ypos=%ld pen=%d\n",
                    bi, data[bi], (data[bi]>=32&&data[bi]<127)?data[bi]:'.',
                    g_cpu.P1, (long)g_mech.sx.pos, (long)g_mech.sy.pos, g_mech.pen_down);
        }
    }
#undef P14
    free(data);

    printf("print: input=%s bytes=%zu maxper=%ld warmup=%ld overruns=%ld\n",
           inpath, n, maxper, warmup, overruns);
    printf("  pen_downs=%llu color_changes=%llu n_strokes=%u dropped=%u\n",
           (unsigned long long)g_mech.pen_downs,
           (unsigned long long)g_mech.color_changes,
           g_mech.n_strokes, g_mech.dropped);

    if (g_npts < 2) {
        fprintf(stderr, "print: too few pen-down points (%ld) - firmware nenic nenakreslil\n", g_npts);
        return 1;
    }

    /* --- Volitelný CSV dump bodů (pro analýzu zubů) --- */
    const char *csv = getenv("MZ1P16_DUMP_CSV");
    if (csv) {
        FILE *fc = fopen(csv, "wb");
        if (fc) {
            fprintf(fc, "idx,byte,x,y,color,newstroke\n");
            for (long i = 0; i < g_npts; i++) {
                fprintf(fc, "%ld,%ld,%d,%d,%d,%d\n", i, g_pt_byte[i],
                        g_pts[i].x, g_pts[i].y, g_pts[i].color, g_pts[i].newstroke);
            }
            fclose(fc);
            fprintf(stderr, "  dumped CSV: %s (%ld points)\n", csv, g_npts);
        }
    }

    /* --- Bounding box --- */
    int32_t minx=g_pts[0].x, maxx=g_pts[0].x;
    int32_t miny=g_pts[0].y, maxy=g_pts[0].y;
    for (long i = 1; i < g_npts; i++) {
        if (g_pts[i].x < minx) minx = g_pts[i].x;
        if (g_pts[i].x > maxx) maxx = g_pts[i].x;
        if (g_pts[i].y < miny) miny = g_pts[i].y;
        if (g_pts[i].y > maxy) maxy = g_pts[i].y;
    }

    const int margin = 12;
    const double scale = 3.0;
    int span_x = maxx - minx + 1, span_y = maxy - miny + 1;
    int W = (int)(span_x * scale) + 2*margin;
    int H = (int)(span_y * scale) + 2*margin;
    if (W < 32) W = 32;
    if (H < 32) H = 32;

    st_Canvas cv = { W, H, NULL };
    cv.px = (uint8_t *)malloc((size_t)W * H * 3);
    if (!cv.px) { fprintf(stderr, "oom\n"); return 1; }
    memset(cv.px, 0xF6, (size_t)W * H * 3);

    /* --- Render: spojité úsečky tahů (přeruš na novém tahu nebo změně barvy) --- */
    long segs = 0;
    for (long i = 0; i < g_npts; i++) {
        uint8_t r,g,b; color_rgb(g_pts[i].color, &r,&g,&b);
        int px = margin + (int)((g_pts[i].x - minx) * scale);
        int py = margin + (int)((g_pts[i].y - miny) * scale);
        if (i > 0 && !g_pts[i].newstroke &&
            g_pts[i].color == g_pts[i-1].color) {
            int qx = margin + (int)((g_pts[i-1].x - minx) * scale);
            int qy = margin + (int)((g_pts[i-1].y - miny) * scale);
            cv_line(&cv, qx,   qy,   px,   py,   r, g, b);
            cv_line(&cv, qx+1, qy,   px+1, py,   r, g, b);
            cv_line(&cv, qx,   qy+1, px,   py+1, r, g, b);
            cv_line(&cv, qx+1, qy+1, px+1, py+1, r, g, b);
            segs++;
        }
    }

    if (!png_write(out, cv.px, W, H)) {
        fprintf(stderr, "print: PNG write failed: %s\n", out);
        free(cv.px);
        return 1;
    }

    printf("  points=%ld segments=%ld bbox X[%d..%d] Y[%d..%d] -> PNG %dx%d\n",
           g_npts, segs, minx, maxx, miny, maxy, W, H);
    printf("  wrote: %s\n", out);

    free(cv.px);
    return 0;
}
