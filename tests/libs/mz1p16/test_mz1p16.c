/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file test_mz1p16.c
 * @brief Testy fáze 2 plotteru MZ-1P16: smoke boot firmwaru + mechanika +
 *        power-on selftest (rotace 4 per).
 *
 * Pokrytí:
 *  - smoke: reálný firmware 8050 nabootuje a doběhne do idle poll smyčky
 *    bez vykonání jediného nedefinovaného opcode (regrese chybějícího JNZ);
 *  - stepper dekodér: empiricky odvozená sekvence A->6->5->9 posouvá vozík
 *    DOLEVA (k levému dorazu, fyzický směr X = -1), obrácené pořadí doprava;
 *  - levý doraz: xpos se klampuje na >= 0 (vozík nemůže za levý okraj);
 *  - pen up/down: P1.1 = 0 spustí pero, P1.0 = 0 jej zvedne;
 *  - power-on selftest: firmware u levého dorazu opakovaně vjíždí do
 *    pen-change zóny a postupně prorotuje všechna 4 pera (color 0..3).
 *
 * Standalone test - používá embeddovaný firmware (mz1p16_rom_data) a model
 * mechaniky (mz1p16). Nezávisí na žádné MZARCH.
 */

#include "unity.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "emulator/hw-generic/mz1p16/mcs48.h"
#include "emulator/hw-generic/mz1p16/mz1p16.h"
#include "emulator/hw-generic/mz1p16/mz1p16_rom.h"


/* ===== Fixture ===== */

static st_MCS48  g_cpu;
static st_MZ1P16 g_mech;

/** Pomocná ROM pro syntetické buzení portů (MOV A,#; OUTL BUS/P1,A). */
static uint8_t g_dummy_rom[MCS48_ROM_SIZE];


void setUp(void)
{
    mcs48_init(&g_cpu, mz1p16_rom_data());
    mz1p16_init(&g_mech, &g_cpu);
}


void tearDown(void)
{
}


/* ===================================================================== */
/* Smoke: boot reálného firmwaru                                         */
/* ===================================================================== */

/**
 * @brief Firmware nabootuje a žádný nedefinovaný opcode se nevykoná.
 *
 * Replikuje detektor undefined opcodů z fáze 2: po opravě 96h (JNZ) musí být
 * po dlouhém běhu počet vykonání opcodů spadajících do default větve (mapa
 * ilegálních opcodů) nulový.
 */
void test_smoke_no_undefined_opcodes(void)
{
    static const uint8_t illegal[] = {
        0x01, 0x06, 0x0B, 0x22, 0x33, 0x38, 0x3B, 0x63, 0x66, 0x73, 0x82,
        0x87, 0x8B, 0x9B, 0xA2, 0xA6, 0xB7, 0xC0, 0xC1, 0xC2, 0xC3, 0xD6,
        0xE0, 0xE1, 0xE2, 0xF3
    };
    bool is_illegal[256] = { false };
    for (size_t i = 0; i < sizeof(illegal); i++) {
        is_illegal[illegal[i]] = true;
    }

    const uint8_t *rom = mz1p16_rom_data();
    long undef = 0;
    for (long i = 0; i < 500000; i++) {
        uint8_t op = rom[g_cpu.PC & MCS48_PC_MASK];
        if (is_illegal[op]) {
            undef++;
        }
        mcs48_step(&g_cpu);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, undef,
        "firmware vykonal nedefinovany opcode");
}

/**
 * @brief Po bootu se firmware ustálí v poll smyčce (PC nezůstává na 0).
 */
void test_smoke_reaches_idle(void)
{
    uint64_t c0 = g_cpu.cycles;
    for (long i = 0; i < 300000; i++) {
        mcs48_step(&g_cpu);
    }
    TEST_ASSERT_TRUE(g_cpu.cycles > c0);          /* běží */
    TEST_ASSERT_TRUE(g_cpu.PC != 0x000);          /* neuvízlo na resetu */
    TEST_ASSERT_TRUE((g_cpu.PC & MCS48_PC_MASK) < MCS48_ROM_SIZE);
}

/**
 * @brief Po power-on init drží vstupy 8050 klidovou úroveň.
 *
 * Reset jádra sám o sobě nuluje T0/T1/INT; mz1p16_init je musí nastavit na
 * klid (1), jinak by firmware viděl aktivní strobe/senzory.
 */
void test_input_idle_levels(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, g_cpu.INT);
    TEST_ASSERT_EQUAL_UINT8(1, g_cpu.T0);
    TEST_ASSERT_EQUAL_UINT8(1, g_cpu.T1);
}


/* ===================================================================== */
/* Stepper dekodér + levý doraz                                          */
/* ===================================================================== */

/** @brief Vystaví jednu fázi na BUS dolní nibble přes jádro (OUTL BUS,A). */
static void emit_bus_phase(uint8_t phase)
{
    g_dummy_rom[0] = 0x23; g_dummy_rom[1] = phase; g_dummy_rom[2] = 0x02;
    g_cpu.PC = 0;
    mcs48_run(&g_cpu, 4);
}

/**
 * @brief Sekvence A->6->5->9 pohne vozíkem DOLEVA, obrácená doprava.
 *
 * Fyzický směr X je invertovaný vůči postupu v sekvenci (potvrzeno
 * selftestem): A->6->5->9 = doleva (k dorazu), 9->5->6->A = doprava.
 * Doleva se kvůli levému dorazu klampuje na 0, proto vozík nejdřív posuneme
 * doprava a teprve pak testujeme pohyb doleva.
 */
void test_stepper_direction(void)
{
    g_cpu.rom = g_dummy_rom;

    /* Nejdřív doprava (obrácená sekvence) z dorazu, ať máme prostor. */
    g_mech.sx.have_phase = false; g_mech.sx.pos = 0;
    static const uint8_t rev[] = { 0x9, 0x5, 0x6, 0xA, 0x9, 0x5 };
    for (size_t i = 0; i < sizeof(rev); i++) emit_bus_phase(rev[i]);
    TEST_ASSERT_EQUAL_INT32(5, g_mech.sx.pos); /* 5 přechodů doprava */

    /* Teď doleva (přímá sekvence) - vrátí vozík zpět. */
    g_mech.sx.have_phase = false;
    static const uint8_t fwd[] = { 0xA, 0x6, 0x5, 0x9, 0xA, 0x6 };
    for (size_t i = 0; i < sizeof(fwd); i++) emit_bus_phase(fwd[i]);
    TEST_ASSERT_EQUAL_INT32(0, g_mech.sx.pos); /* 5 doleva: 5 -> 0 */
}

/**
 * @brief Vozík se nedostane za levý doraz (xpos klampováno na >= 0).
 *
 * Z pozice u dorazu pošleme další kroky doleva - xpos zůstane 0.
 */
void test_left_endstop_clamp(void)
{
    g_cpu.rom = g_dummy_rom;
    g_mech.sx.have_phase = false; g_mech.sx.pos = 0;

    /* Mnoho kroků doleva (přímá sekvence) - xpos nesmí klesnout pod 0. */
    static const uint8_t fwd[] = { 0xA, 0x6, 0x5, 0x9 };
    for (int rep = 0; rep < 5; rep++) {
        for (size_t i = 0; i < sizeof(fwd); i++) emit_bus_phase(fwd[i]);
    }
    TEST_ASSERT_EQUAL_INT32(0, g_mech.sx.pos);
}

/**
 * @brief Pen up/down polarita: P1.1 = 0 spustí pero, P1.0 = 0 jej zvedne.
 */
void test_pen_polarity(void)
{
    g_cpu.rom = g_dummy_rom;

    /* Po init je pero nahoře. */
    TEST_ASSERT_FALSE(g_mech.pen_down);

    /* Pero je LATCH: P1.1=0 (FD) spustí, P1.0=0 (FE) zvedne, jinak drží stav. */
    /* OUTL P1, #0FDh (P1.1=0) -> pen down. */
    g_dummy_rom[0] = 0x23; g_dummy_rom[1] = 0xFD; g_dummy_rom[2] = 0x39;
    g_cpu.PC = 0; mcs48_run(&g_cpu, 4);
    TEST_ASSERT_TRUE_MESSAGE(g_mech.pen_down, "P1.1=0 melo spustit pero");

    /* OUTL P1, #0FFh -> latch drží -> pero zůstává dole. */
    g_dummy_rom[0] = 0x23; g_dummy_rom[1] = 0xFF; g_dummy_rom[2] = 0x39;
    g_cpu.PC = 0; mcs48_run(&g_cpu, 4);
    TEST_ASSERT_TRUE_MESSAGE(g_mech.pen_down, "P1=0FFh nemelo zmenit stav (latch)");

    /* OUTL P1, #0FEh (P1.0=0) -> pen up. */
    g_dummy_rom[0] = 0x23; g_dummy_rom[1] = 0xFE; g_dummy_rom[2] = 0x39;
    g_cpu.PC = 0; mcs48_run(&g_cpu, 4);
    TEST_ASSERT_FALSE_MESSAGE(g_mech.pen_down, "P1.0=0 melo zvednout pero");
}

/**
 * @brief BUSY polarita: P1.4 = 1 znamená busy.
 */
void test_busy_bit(void)
{
    g_cpu.rom = g_dummy_rom;

    /* OUTL P1, #0EFh (P1.4=0) -> not busy. */
    g_dummy_rom[0] = 0x23; g_dummy_rom[1] = 0xEF; g_dummy_rom[2] = 0x39;
    g_cpu.PC = 0; mcs48_run(&g_cpu, 4);
    TEST_ASSERT_FALSE(g_mech.busy);

    /* OUTL P1, #0FFh (P1.4=1) -> busy. */
    g_dummy_rom[0] = 0x23; g_dummy_rom[1] = 0xFF; g_dummy_rom[2] = 0x39;
    g_cpu.PC = 0; mcs48_run(&g_cpu, 4);
    TEST_ASSERT_TRUE(g_mech.busy);
}


/* ===================================================================== */
/* Power-on selftest: rotace všech 4 per                                 */
/* ===================================================================== */

/**
 * @brief Power-on selftest prorotuje všechna 4 pera (klíčový milník fáze 2).
 *
 * Spustí firmware od resetu a nechá běžet. Open-loop homing dotlačí vozík na
 * levý doraz, kde opakované vjezdy do pen-change zóny rotují buben. Test
 * ověří, že index barvy během selftestu nabyl všech čtyř hodnot 0,1,2,3.
 */
void test_selftest_rotates_all_pens(void)
{
    bool seen[MZ1P16_NUM_COLORS] = { false };
    int seen_count = 0;
    long first_all = -1;

    for (long i = 0; i < 1500000; i++) {
        mcs48_step(&g_cpu);
        if (!seen[g_mech.color]) {
            seen[g_mech.color] = true;
            if (++seen_count == MZ1P16_NUM_COLORS && first_all < 0) {
                first_all = i;
            }
        }
    }

    for (int ci = 0; ci < MZ1P16_NUM_COLORS; ci++) {
        TEST_ASSERT_TRUE_MESSAGE(seen[ci],
            "selftest neprorotoval vsechny 4 barvy");
    }
    TEST_ASSERT_TRUE_MESSAGE(g_mech.color_changes >= 4,
        "selftest provedl mene nez 4 zmeny barvy");
    /* Vozík se drží u levého dorazu (pen-change zóna). */
    TEST_ASSERT_TRUE_MESSAGE(g_mech.sx.pos >= 0, "xpos pod levym dorazem");
}


/* ===================================================================== */
/* Runner                                                                */
/* ===================================================================== */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_smoke_no_undefined_opcodes);
    RUN_TEST(test_smoke_reaches_idle);
    RUN_TEST(test_input_idle_levels);
    RUN_TEST(test_stepper_direction);
    RUN_TEST(test_left_endstop_clamp);
    RUN_TEST(test_pen_polarity);
    RUN_TEST(test_busy_bit);
    RUN_TEST(test_selftest_rotates_all_pens);
    return UNITY_END();
}
