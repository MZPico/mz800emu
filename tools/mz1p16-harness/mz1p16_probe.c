/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file mz1p16_probe.c
 * @brief Diagnostický harness: měří pen/stepper aktivitu kreslicího self-testu.
 *
 * NEkreslí PNG. Instrumentuje běh firmwaru 8050 nad modelem mechaniky a
 * vypisuje měřitelná fakta pro hledání root cause řídkosti výstupu:
 *  - počet zápisů P1 a rozklad podle pen bitů (P1.0, P1.1),
 *  - počet pen-down náběžných hran (edge) vs. počet cyklů s perem dole,
 *  - počet zápisů BUS a kolik z nich vyvolalo X/Y krok,
 *  - histogram PC zápisů P1 a BUS (top adresy = kreslicí rutina),
 *  - okno detailního logu sekvence P1+BUS kolem prvních N pen-down událostí.
 *
 * Standalone (gcc -pipe), linkuje mcs48.c + mz1p16.c + mz1p16_rom.c.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "emulator/hw-generic/mz1p16/mcs48.h"
#include "emulator/hw-generic/mz1p16/mz1p16.h"
#include "emulator/hw-generic/mz1p16/mz1p16_rom.h"

#define CYCLES_PER_SEC 400000.0

/* --- Globální čítače sbírané přes hooky --- */
static st_MCS48  g_cpu;
static st_MZ1P16 g_mech;

static uint64_t cnt_p1_writes = 0;
static uint64_t cnt_p1_pen_down_bit = 0; /* zápisů P1 s P1.1=0 */
static uint64_t cnt_p1_pen_up_bit = 0;   /* zápisů P1 s P1.0=0 */
static uint64_t cnt_bus_writes = 0;

/* PC poslední vykonané instrukce (před port_write se PC už posunulo za opcode,
 * ale pro histogram stačí PC v okamžiku hooku - to je adresa NÁSLEDUJÍCÍ
 * instrukce; relativně to stačí pro lokalizaci rutiny). */
static uint16_t g_last_pc = 0;

/* Histogram PC pro P1 zápisy a BUS zápisy (4096 adres). */
static uint32_t hist_p1[4096];
static uint32_t hist_bus[4096];

/* Detailní log prvních událostí. */
static long detail_budget = 0;

/** Uložený původní write hook mechaniky (volá se z wrapperu). */
static mcs48_port_write_fn g_mech_write_ptr = NULL;

static void probe_write_then_mech(st_MCS48 *c, int port, uint8_t val, void *ctx)
{
    if (port == MCS48_PORT_P1) {
        cnt_p1_writes++;
        if ((val & 0x02u) == 0u) cnt_p1_pen_down_bit++;
        if ((val & 0x01u) == 0u) cnt_p1_pen_up_bit++;
        hist_p1[c->PC & 0xFFF]++;
        if (detail_budget > 0) {
            fprintf(stderr, "P1<-%02X pc~%03X (pen.0=%d pen.1=%d)\n",
                    val, c->PC & 0xFFF, !!(val&1), !!(val&2));
            detail_budget--;
        }
    } else if (port == MCS48_PORT_BUS) {
        cnt_bus_writes++;
        hist_bus[c->PC & 0xFFF]++;
    }
    /* Předej řízení původnímu hooku mechaniky (pohyb/pero/barva). */
    if (g_mech_write_ptr) g_mech_write_ptr(c, port, val, ctx);
}

int main(int argc, char **argv)
{
    double run_s = (argc > 1) ? atof(argv[1]) : 90.0;
    detail_budget = (argc > 2) ? atol(argv[2]) : 0;

    mcs48_init(&g_cpu, mz1p16_rom_data());
    mz1p16_init(&g_mech, &g_cpu);

    /* Zřetězit probe hook PŘED hook mechaniky: měříme a pak předáme řízení
     * původnímu hooku (pohyb/pero). */
    g_mech_write_ptr = g_cpu.on_port_write;
    g_cpu.on_port_write = probe_write_then_mech;

    g_cpu.T1 = 0;
    g_cpu.T1_prev = 0;

    long total = (long)(run_s * CYCLES_PER_SEC), done = 0;

    uint64_t pen_down_cycles = 0;
    uint64_t pen_down_edges = 0;
    bool prev_pen = false;
    /* Sledování pohybu pera-dole: kolik UNIKÁTNÍCH pozic s perem dole. */
    int32_t lastx = INT32_MIN, lasty = INT32_MIN;
    uint64_t unique_pendown_pos = 0;

    /* PC histogram VŠECH vykonaných instrukcí (kde firmware tráví čas). */
    static uint32_t hist_pc[4096];
    while (done < total) {
        uint16_t pc_before = g_cpu.PC;
        int c = mcs48_step(&g_cpu);
        hist_pc[pc_before & 0xFFF]++;
        done += c;
        g_last_pc = g_cpu.PC;
        if (g_mech.pen_down) {
            pen_down_cycles += (uint64_t)c;
            if (!prev_pen) pen_down_edges++;
            if (g_mech.sx.pos != lastx || g_mech.sy.pos != lasty) {
                unique_pendown_pos++;
                lastx = g_mech.sx.pos; lasty = g_mech.sy.pos;
            }
        }
        prev_pen = g_mech.pen_down;
    }

    printf("=== MZ-1P16 probe (run=%.1fs, %ld cyklu) ===\n", run_s, done);
    printf("P1 writes:            %llu\n", (unsigned long long)cnt_p1_writes);
    printf("  s P1.1=0 (pen down): %llu\n", (unsigned long long)cnt_p1_pen_down_bit);
    printf("  s P1.0=0 (pen up):   %llu\n", (unsigned long long)cnt_p1_pen_up_bit);
    printf("BUS writes:           %llu\n", (unsigned long long)cnt_bus_writes);
    printf("--- mechanika ---\n");
    printf("pen_downs (edge):     %llu\n", (unsigned long long)g_mech.pen_downs);
    printf("pen_down_edges(probe):%llu\n", (unsigned long long)pen_down_edges);
    printf("pen_down_cycles:      %llu (%.1f%% casu)\n",
           (unsigned long long)pen_down_cycles, 100.0*pen_down_cycles/done);
    printf("unique pendown pos:   %llu\n", (unsigned long long)unique_pendown_pos);
    printf("x_steps fwd/rev:      %llu / %llu\n",
           (unsigned long long)g_mech.x_steps_fwd,
           (unsigned long long)g_mech.x_steps_rev);
    printf("y_steps fwd/rev:      %llu / %llu\n",
           (unsigned long long)g_mech.y_steps_fwd,
           (unsigned long long)g_mech.y_steps_rev);
    printf("color_changes:        %llu  final color=%d\n",
           (unsigned long long)g_mech.color_changes, g_mech.color);
    printf("xpos=%ld ypos=%ld\n", (long)g_mech.sx.pos, (long)g_mech.sy.pos);

    /* Top adresy histogramů. */
    printf("--- top PC P1 writes ---\n");
    for (int top = 0; top < 8; top++) {
        int best = -1; uint32_t bv = 0;
        for (int i = 0; i < 4096; i++) if (hist_p1[i] > bv) { bv = hist_p1[i]; best = i; }
        if (best < 0 || bv == 0) break;
        printf("  PC~%03X : %u\n", best, bv);
        hist_p1[best] = 0;
    }
    printf("--- top PC BUS writes ---\n");
    for (int top = 0; top < 8; top++) {
        int best = -1; uint32_t bv = 0;
        for (int i = 0; i < 4096; i++) if (hist_bus[i] > bv) { bv = hist_bus[i]; best = i; }
        if (best < 0 || bv == 0) break;
        printf("  PC~%03X : %u\n", best, bv);
        hist_bus[best] = 0;
    }
    /* Pokrytí kódu: kolik různých adres bylo vůbec vykonáno + top hotspoty. */
    int touched = 0; uint32_t maxlo = 0, maxhi = 0;
    for (int i = 0; i < 4096; i++) if (hist_pc[i]) { touched++; if (i<0x100) maxlo+=hist_pc[i]; else maxhi+=hist_pc[i]; }
    printf("--- pokryti kodu ---\n");
    printf("ruznych adres vykonano: %d / 4096\n", touched);
    printf("instr v 000-0FF: %u, v 100-FFF: %u\n", maxlo, maxhi);
    printf("--- top 16 PC (hotspoty) ---\n");
    for (int top = 0; top < 16; top++) {
        int best = -1; uint32_t bv = 0;
        for (int i = 0; i < 4096; i++) if (hist_pc[i] > bv) { bv = hist_pc[i]; best = i; }
        if (best < 0 || bv == 0) break;
        printf("  PC %03X : %u\n", best, bv);
        hist_pc[best] = 0;
    }
    return 0;
}
