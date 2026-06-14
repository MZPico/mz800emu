/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file mz1p16_drawtest.c
 * @brief Standalone harness: kreslicí (drawing) self-test plotteru MZ-1P16.
 *
 * Spustí embeddovaný firmware 8050 (mz1p16_rom) nad modelem mechaniky
 * (mz1p16), simuluje podržení tlačítka PAPER FEED (vstup T1, aktivní LOW),
 * čímž firmware vstoupí do vestavěného kreslicího self-testu, zaznamená
 * vykreslené body/tahy pera a vyexportuje výsledek do PNG.
 *
 * Tohle je cesta jak rozkreslit pero BEZ připojeného MZ-800: kreslicí vzor
 * pochází z vlastní ROM plotteru.
 *
 * EMPIRICKÁ ZJIŠTĚNÍ (fáze 2c, tento harness):
 *  - Podržení PAPER FEED (T1=0) spustí kreslicí self-test. Nejspolehlivěji
 *    naběhne při T1=0 už od resetu; firmware nejdřív odhomuje vozík a po
 *    ~2.15 mil. strojových cyklech (~5.4 s @ 6 MHz/15) začne klást body.
 *  - Firmware kreslí TEČKOVÁNÍM: spustí pero na místě (drag=0), zvedne,
 *    přejede jinam. Hustá řada teček tvoří souvislou čáru.
 *  - Vzor: řádek napříč šířkou papíru (X ~63..525 kroků), rozdělený na 4
 *    barevné segmenty (pořadí pozorováno black -> red -> green -> blue),
 *    po dokončení řádku se posune papír (Y) a vozík se vrátí vlevo (kde
 *    pen-change mechanika rotuje buben) - další řádek.
 *  - Y STEPPER FUNGUJE: posuv papíru se vyvolá oběma směry (horní nibble
 *    BUS), Y narůstá s časem - potvrzeno (dřív byl Y v testech nevyvolán).
 *
 * Render: po sobě jdoucí pen-down body STEJNÉ barvy se spojí úsečkou (přeruší
 * se při změně barvy nebo skoku vozíku zpět vlevo = nový řádek); navíc se
 * vykreslí tečka v každém bodě. Výstup škálovaný na bounding box.
 *
 * PNG se zapisuje vlastním minimálním writerem (deflate "stored" bloky), bez
 * externí knihovny.
 *
 * @note NENÍ napojeno do architektury (žádná změna mz*_config.h). Standalone.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "emulator/hw-generic/mz1p16/mcs48.h"
#include "emulator/hw-generic/mz1p16/mz1p16.h"
#include "emulator/hw-generic/mz1p16/mz1p16_rom.h"

/*
 * Přepočet sekund -> strojové cykly 8050.
 * MCS-48 strojový cyklus = 15 oscilátorových period; plotter 6 MHz krystal
 * (knowledge/hw/mz1p16-8050-port-mapping.md). => 1 cyklus = 2.5 us,
 * 1 s ~= 400000 cyklů. [neověřeno přesně pro 8050; standard MCS-48 manuál].
 */
#define CYCLES_PER_SEC 400000.0

/* ===================================================================== */
/* Záznam pen-down bodů                                                   */
/* ===================================================================== */

/** @brief Jeden zaznamenaný bod stopy pera (s perem dole). */
typedef struct st_Point {
    int32_t x;     /**< Pozice X (kroky vozíku). */
    int32_t y;     /**< Pozice Y (kroky papíru). */
    uint8_t color; /**< Index barvy 0-3 v okamžiku položení. */
    uint8_t newstroke; /**< 1 = začátek nového tahu (pero právě dosedlo). */
} st_Point;

#define MAX_POINTS 100000

static st_Point g_pts[MAX_POINTS];
static long     g_npts = 0;

/* ===================================================================== */
/* Minimální PNG writer (deflate "stored", bez zlib)                     */
/* ===================================================================== */

/** @brief Tabulka CRC-32 (lazy init). */
static uint32_t g_crc_tab[256];
static bool     g_crc_ready = false;

/** @brief Inicializuje CRC-32 tabulku (polynom 0xEDB88320). */
static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crc_tab[n] = c;
    }
    g_crc_ready = true;
}

/** @brief Zapíše 32-bit big-endian. */
static void put_be32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v>>24), (uint8_t)(v>>16),
                     (uint8_t)(v>>8), (uint8_t)v };
    fwrite(b, 1, 4, f);
}

/**
 * @brief Vypočte CRC-32 nad (type ++ data) v jednom průchodu a zapíše chunk.
 *
 * @param f    Otevřený soubor.
 * @param type 4-znakový typ chunku (IHDR/IDAT/IEND...).
 * @param data Data chunku (může být NULL při len==0).
 * @param len  Délka dat.
 */
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

/** @brief Adler-32 nad blokem dat. */
static uint32_t adler32(const uint8_t *buf, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + buf[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

/**
 * @brief Zapíše RGB obrázek do PNG (8-bit, bez komprese - deflate stored).
 *
 * @param path Cesta k souboru.
 * @param rgb  Pole w*h*3 bajtů (R,G,B po řádcích shora dolů).
 * @param w,h  Rozměry.
 * @return true při úspěchu.
 */
static bool png_write(const char *path, const uint8_t *rgb, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);

    /* IHDR */
    uint8_t ihdr[13];
    ihdr[0]=(uint8_t)(w>>24); ihdr[1]=(uint8_t)(w>>16);
    ihdr[2]=(uint8_t)(w>>8);  ihdr[3]=(uint8_t)w;
    ihdr[4]=(uint8_t)(h>>24); ihdr[5]=(uint8_t)(h>>16);
    ihdr[6]=(uint8_t)(h>>8);  ihdr[7]=(uint8_t)h;
    ihdr[8]=8;   /* bit depth */
    ihdr[9]=2;   /* color type 2 = truecolor RGB */
    ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;
    png_chunk2(f, "IHDR", ihdr, 13);

    /* Raw scanlines: každý řádek prefixován filtrem 0. */
    size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) { fclose(f); return false; }
    size_t p = 0;
    for (int y = 0; y < h; y++) {
        raw[p++] = 0;  /* filter none */
        memcpy(raw + p, rgb + (size_t)y * w * 3, (size_t)w * 3);
        p += (size_t)w * 3;
    }

    /* zlib stream: 2 bajty hlavičky + deflate stored bloky + adler32. */
    size_t max_block = 65535;
    size_t nblocks = (raw_len + max_block - 1) / max_block;
    size_t zlen = 2 + raw_len + nblocks * 5 + 4;
    uint8_t *z = (uint8_t *)malloc(zlen);
    if (!z) { free(raw); fclose(f); return false; }
    size_t zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;  /* zlib hdr (CM=8, no preset dict) */
    size_t off = 0;
    while (off < raw_len) {
        size_t chunk = raw_len - off;
        if (chunk > max_block) chunk = max_block;
        int final = (off + chunk >= raw_len) ? 1 : 0;
        z[zp++] = (uint8_t)final;            /* BFINAL + BTYPE=00 */
        z[zp++] = (uint8_t)(chunk & 0xFF);   /* LEN lo */
        z[zp++] = (uint8_t)((chunk >> 8)&0xFF);/* LEN hi */
        z[zp++] = (uint8_t)(~chunk & 0xFF);  /* NLEN lo */
        z[zp++] = (uint8_t)((~chunk >> 8)&0xFF);/* NLEN hi */
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
/* Rasterizace (tečky + úsečky)                                          */
/* ===================================================================== */

/** @brief RGB plátno. */
typedef struct st_Canvas {
    int w, h;
    uint8_t *px; /**< w*h*3. */
} st_Canvas;

/** @brief Nastaví pixel (s ořezem). */
static void cv_set(st_Canvas *c, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    uint8_t *p = c->px + ((size_t)y * c->w + x) * 3;
    p[0]=r; p[1]=g; p[2]=b;
}

/** @brief Vyplněný kotouč (tečka). */
static void cv_dot(st_Canvas *c, int x, int y, int rad,
                   uint8_t r, uint8_t g, uint8_t b)
{
    for (int dy = -rad; dy <= rad; dy++)
        for (int dx = -rad; dx <= rad; dx++)
            if (dx*dx + dy*dy <= rad*rad)
                cv_set(c, x+dx, y+dy, r, g, b);
}

/**
 * @brief Úsečka (Bresenham).
 *
 * Aktuálně nepoužita (self-test kreslí tečkováním, render kreslí jen tečky),
 * ponechána pro budoucí režim spojování tahů. Označena unused, ať build
 * zůstane bez varování.
 */
__attribute__((unused))
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

/** @brief RGB barvy per index (black, blue, green, red). */
static void color_rgb(uint8_t idx, uint8_t *r, uint8_t *g, uint8_t *b)
{
    /* Mapování indexu karuselu na barvu tak, aby pozorované pořadí self-testu
     * bylo černá, modrá, zelená, červená (změřeno: spatial index seq 0,3,2,1). */
    switch (idx & 3) {
        case 0: *r=20;  *g=20;  *b=20;  break; /* černá (tmavě šedá pro viditelnost) */
        case 1: *r=220; *g=50;  *b=50;  break; /* červená */
        case 2: *r=30;  *g=170; *b=60;  break; /* zelená */
        case 3: *r=40;  *g=90;  *b=220; break; /* modrá */
        default:*r=0;*g=0;*b=0;break;
    }
}

/* ===================================================================== */
/* Hlavní                                                                */
/* ===================================================================== */

static st_MCS48  g_cpu;
static st_MZ1P16 g_mech;

int main(int argc, char **argv)
{
    /* Default 450 s: self-test kreslí ~1 textový řádek za ~45 s reálného MCU
     * času (měřeno probe harnessem), takže 450 s dá ~10 řádků - srovnatelné
     * se záběrem okna referenčního výstupu. Kratší běh (původních 90 s) dal
     * jen ~2 řádky = část původně hlášené "řídkosti" byla nedostatečná doba
     * běhu, ne chyba modelu (viz devdoc/mz1p16/drawing_selftest_analysis.md). */
    double run_s = (argc > 1) ? atof(argv[1]) : 450.0;
    const char *out = (argc > 2) ? argv[2]
                      : "../out/drawing_selftest.png";

    /* --- Boot + drawing self-test (T1=0 od resetu) --- */
    mcs48_init(&g_cpu, mz1p16_rom_data());
    mz1p16_init(&g_mech, &g_cpu);
    g_cpu.T1 = 0;        /* PAPER FEED podržen */
    g_cpu.T1_prev = 0;

    /* Zaznamenáváme KAŽDOU pozici, kde je pero dole (= skutečná inkoustová
     * stopa), s deduplikací po sobě jdoucích identických pozic. Edge-only
     * záznam by zachytil jen začátky tahů a dal řídký rozptyl. */
    long total = (long)(run_s * CYCLES_PER_SEC), done = 0;
    int32_t lastx = INT32_MIN, lasty = INT32_MIN;
    bool prev_pen = false;
    while (done < total && g_npts < MAX_POINTS) {
        done += mcs48_step(&g_cpu);
        bool pd = g_mech.pen_down;
        if (pd && (!prev_pen ||
                   g_mech.sx.pos != lastx || g_mech.sy.pos != lasty)) {
            g_pts[g_npts].x = g_mech.sx.pos;
            g_pts[g_npts].y = g_mech.sy.pos;
            g_pts[g_npts].color = g_mech.color;
            /* Pero právě dosedlo = začátek nového tahu (nespojovat s předchozím). */
            g_pts[g_npts].newstroke = prev_pen ? 0u : 1u;
            g_npts++;
            lastx = g_mech.sx.pos; lasty = g_mech.sy.pos;
        }
        prev_pen = pd;
    }

    if (g_npts < 2) {
        fprintf(stderr, "drawtest: too few pen-down points (%ld) - "
                        "self-test did not draw\n", g_npts);
        return 1;
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

    /* --- Plátno: škála kroky -> pixely + okraj ---
     * Self-test kreslí tečkováním s relativně velkými rozestupy (~10-25 kroků
     * mezi tečkami, měřeno). Aby tečky byly viditelné, kreslíme je jako
     * vyplněné kotouče o poloměru dot_rad. Scale držíme nízkou (1 px/krok),
     * protože Y rozsah bývá velký (mnoho řádků = vysoká role papíru). */
    const int margin = 12;
    const double scale = 3.0;  /* px na krok */
    const int dot_rad = 0;     /* poloměr tečky (px); 0 = 1px (čistá geometrie) */
    int span_x = maxx - minx + 1, span_y = maxy - miny + 1;
    int W = (int)(span_x * scale) + 2*margin;
    int H = (int)(span_y * scale) + 2*margin;
    if (W < 32) W = 32;
    if (H < 32) H = 32;

    st_Canvas cv = { W, H, NULL };
    cv.px = (uint8_t *)malloc((size_t)W * H * 3);
    if (!cv.px) { fprintf(stderr, "oom\n"); return 1; }
    memset(cv.px, 0xF6, (size_t)W * H * 3);  /* světle šedé pozadí (papír) */

    /* --- Render: tečky podél nakreslené stopy (každá pozice s perem dole).
     * Self-test (PAPER FEED / T1=0 + reset) kreslí TAHY během pohybu vozíku se
     * spuštěným perem; po opravě polarity/latche pera (mz1p16.c) vykreslí celý
     * ZNAKOVÝ GENERÁTOR ve 4 barvách (shoda s referencí). Malý dot_rad +
     * vyšší scale = čistá geometrie glyfů (velké tečky je slévaly do blbů). --- */
    long segs = 0;
    for (long i = 0; i < g_npts; i++) {
        uint8_t r,g,b; color_rgb(g_pts[i].color, &r,&g,&b);
        int px = margin + (int)((g_pts[i].x - minx) * scale);
        int py = margin + (int)((g_pts[i].y - miny) * scale);
        /* Spojitý tah: spoj s předchozím bodem, pokud nejde o začátek nového
         * tahu (pero právě dosedlo) ani o změnu barvy. Tím vzniknou souvislé
         * čáry glyfů místo izolovaných teček. */
        if (i > 0 && !g_pts[i].newstroke &&
            g_pts[i].color == g_pts[i-1].color) {
            int qx = margin + (int)((g_pts[i-1].x - minx) * scale);
            int qy = margin + (int)((g_pts[i-1].y - miny) * scale);
            /* Tloušťka pera 2px: úsečka kreslená brushem 2x2 (offsety).
             * Kreslíme POUZE linie (jako reálný plotter / referenční render);
             * žádné tečky v bodech - ty dělaly blbky na tazích a izolované
             * tečky tam, kde pero ťuklo bez pohybu (to reálný plotter nekreslí). */
            cv_line(&cv, qx,   qy,   px,   py,   r, g, b);
            cv_line(&cv, qx+1, qy,   px+1, py,   r, g, b);
            cv_line(&cv, qx,   qy+1, px,   py+1, r, g, b);
            cv_line(&cv, qx+1, qy+1, px+1, py+1, r, g, b);
            segs++;
        }
        (void)dot_rad;
    }

    if (!png_write(out, cv.px, W, H)) {
        fprintf(stderr, "drawtest: PNG write failed: %s\n", out);
        free(cv.px);
        return 1;
    }

    printf("drawtest: run=%.1fs points=%ld segments=%ld\n", run_s, g_npts, segs);
    printf("  bbox X[%d..%d] Y[%d..%d] -> PNG %dx%d\n",
           minx, maxx, miny, maxy, W, H);
    printf("  barvy self-testu (pořadí): černá, modrá, zelená, červená\n");
    printf("  wrote: %s\n", out);

    free(cv.px);
    return 0;
}
