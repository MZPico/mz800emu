/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file mz1p16_rawdump.c
 * @brief Syrový dump BUS/P1 zápisů self-testu plotteru MZ-1P16.
 *
 * Diagnostika: zaznamenává KAŽDÝ zápis na BUS (obě nibble) a P1 (pero) v
 * pořadí, s PC, akumulovanou fází obou stepperů a interpretovaným raw směrem.
 * NEpoužívá model mz1p16 (mz1p16.c) pro dekódování - dekóduje fáze nezávisle,
 * abychom viděli SUROVÁ data firmwaru a mohli re-derivovat stepper dekodér
 * a mapování bez kontaminace stávajícím (možná chybným) modelem.
 *
 * Cíl: odvodit jak firmware tvoří jeden znak (sekvence X/Y kroků + pen tapů),
 * periodicitu (rozteč buněk, řádků), a ověřit granularitu kroku.
 *
 * Použití:
 *   mz1p16_rawdump.exe <sekundy> [skip_cycles]
 *   - vypíše surový log zápisů od skip_cycles do konce běhu.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "emulator/hw-generic/mz1p16/mcs48.h"
#include "emulator/hw-generic/mz1p16/mz1p16_rom.h"

#define CYCLES_PER_SEC 400000.0

static st_MCS48 g_cpu;

/* Nezávislý dekodér fáze - jen pro klasifikaci raw směru, NEaplikuje model. */
static const uint8_t SEQ[4] = { 0xA, 0x6, 0x5, 0x9 };
static int seq_idx(uint8_t ph) {
    for (int i=0;i<4;i++) if (SEQ[i]==ph) return i;
    return -1;
}

static uint8_t prev_xlo = 0xFF, prev_yhi = 0xFF;
static bool have_x=false, have_y=false;
static long g_skip = 0;
static long g_cycle = 0;
static bool g_enabled = false;

/* Akumulované raw pozice (nezávislé na modelu, dir = +1 v sekvenci). */
static long rawx = 0, rawy = 0;
static int last_pen = -1; /* P1.1 bit (1=up,0=down) */

static void on_write(st_MCS48 *c, int port, uint8_t val, void *ctx) {
    (void)ctx;
    if (port == MCS48_PORT_BUS) {
        uint8_t xlo = val & 0x0F;
        uint8_t yhi = (val >> 4) & 0x0F;
        int dx_raw=0, dy_raw=0;
        if (have_x && xlo!=prev_xlo) {
            int o=seq_idx(prev_xlo), n=seq_idx(xlo);
            if (o>=0&&n>=0){int d=(n-o)&3; if(d==1)dx_raw=1; else if(d==3)dx_raw=-1; else dx_raw=2*((d==2)?1:0);}
        }
        if (have_y && yhi!=prev_yhi) {
            int o=seq_idx(prev_yhi), n=seq_idx(yhi);
            if (o>=0&&n>=0){int d=(n-o)&3; if(d==1)dy_raw=1; else if(d==3)dy_raw=-1; else dy_raw=2*((d==2)?1:0);}
        }
        rawx += dx_raw; rawy += dy_raw;
        if (g_enabled && (dx_raw||dy_raw)) {
            printf("c=%-9ld BUS=%02X xlo=%X yhi=%X dxr=%+d dyr=%+d rawx=%ld rawy=%ld pen=%d pc=%03X\n",
                   g_cycle, val, xlo, yhi, dx_raw, dy_raw, rawx, rawy,
                   last_pen, c->PC & 0xFFF);
        }
        prev_xlo=xlo; prev_yhi=yhi; have_x=have_y=true;
    } else if (port == MCS48_PORT_P1) {
        int pen = (val & 0x02) ? 1 : 0; /* 0 = down */
        if (pen != last_pen && g_enabled) {
            printf("c=%-9ld P1=%02X PEN%s rawx=%ld rawy=%ld pc=%03X\n",
                   g_cycle, val, pen?"_UP  ":"_DOWN", rawx, rawy, c->PC & 0xFFF);
        }
        last_pen = pen;
    }
}

static uint8_t on_read(st_MCS48 *c, int port, void *ctx) {
    (void)c;(void)ctx;
    if (port == MCS48_PORT_P2) return 0xFF;
    return 0xFF;
}

int main(int argc, char **argv) {
    double run_s = (argc>1)?atof(argv[1]):90.0;
    g_skip = (argc>2)?atol(argv[2]):0;

    mcs48_init(&g_cpu, mz1p16_rom_data());
    g_cpu.on_port_write = on_write;
    g_cpu.on_port_read = on_read;
    g_cpu.INT=1; g_cpu.T0=1; g_cpu.T1=0; g_cpu.T1_prev=0; /* PAPER FEED */
    g_cpu.P1_in=0xFF; g_cpu.P2_in=0xFF; g_cpu.BUS_in=0xFF;

    long total = (long)(run_s*CYCLES_PER_SEC);
    while (g_cycle < total) {
        if (!g_enabled && g_cycle >= g_skip) g_enabled = true;
        g_cycle += mcs48_step(&g_cpu);
    }
    fprintf(stderr, "rawdump done: %ld cycles, rawx=%ld rawy=%ld\n", g_cycle, rawx, rawy);
    return 0;
}
