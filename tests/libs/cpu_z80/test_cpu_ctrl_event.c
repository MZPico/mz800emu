/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file test_cpu_ctrl_event.c
 * @brief Unit testy pro z80_set_cpu_ctrl_event callback (event-viewer mutant).
 *
 * Pokrytí:
 *   - HALT entry (opcode 0x76) -> HALT_ENTER + pc za HALT
 *   - HALT exit pri IM 1 INT ack -> HALT_EXIT + pc za HALT
 *   - HALT exit pri NMI ack -> HALT_EXIT + pc za HALT
 *   - RST 00h (0xC7) -> RST_00 + pc za RST
 *   - RST 38h (0xFF) -> RST_38 + pc za RST
 *   - NULL callback hot path = no crash
 *   - Bezna instrukce (LD/ADD) negeneruje cpu_ctrl_event
 *   - HALT entry NEgeneruje HALT_EXIT pri INT mezi beznymi instrukcemi
 *   - Reset zachova nastaveny callback (save/restore)
 *
 * Standalone test - cpu-z80 je samostatna lib, ovladame ji
 * uzivatelskymi callbacks (memory/IO simulace v test souboru).
 */

#include "unity.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "libs/cpu-z80/z80.h"


/* ===== Test memory + IO simulace ===== */

/** Plne 64 KB pamet pro test - obsahuje opcody. */
static uint8_t g_mem[0x10000];

/** Posledni precteny IO port (debug). */
static uint16_t g_last_io_port;

/** Vektor preruseni pro IM 2 (predavany pres z80_irq). */
static uint8_t g_int_vector;


static uint8_t test_mread(struct z80_s *cpu, uint16_t addr, int m1_state,
                          void *user_data)
{
    (void)cpu;
    (void)m1_state;
    (void)user_data;
    return g_mem[addr];
}


static void test_mwrite(struct z80_s *cpu, uint16_t addr, uint8_t value,
                        void *user_data)
{
    (void)cpu;
    (void)user_data;
    g_mem[addr] = value;
}


static uint8_t test_pread(struct z80_s *cpu, uint16_t port, void *user_data)
{
    (void)cpu;
    (void)user_data;
    g_last_io_port = port;
    return 0xFF;
}


static void test_pwrite(struct z80_s *cpu, uint16_t port, uint8_t value,
                        void *user_data)
{
    (void)cpu;
    (void)value;
    (void)user_data;
    g_last_io_port = port;
}


static uint8_t test_intread(struct z80_s *cpu, void *user_data)
{
    (void)cpu;
    (void)user_data;
    return g_int_vector;
}


/* ===== Sledovani callback fire ===== */

/** Maximalni pocet zaznamenanych eventu v test bufferu. */
#define MAX_EVENTS  32

/** Zaznam o jednom CPU control eventu. */
typedef struct {
    uint8_t  event;   /**< z80_cpu_ctrl_event_t */
    uint16_t pc;      /**< PC predany do callbacku */
} ctrl_evt_t;


/** Buffer zachycenych eventu pro per-test inspekci. */
typedef struct {
    int        count;
    ctrl_evt_t evts[MAX_EVENTS];
    void      *last_user_data;
} ctrl_track_t;


static ctrl_track_t g_track;


/** Resetuje globalni tracker + memory. */
static void reset_track(void)
{
    memset(&g_track, 0, sizeof(g_track));
    memset(g_mem, 0, sizeof(g_mem));
    g_last_io_port = 0;
    g_int_vector = 0xFF;
}


static void ctrl_cb(struct z80_s *cpu, uint8_t event, uint16_t pc,
                    void *user_data)
{
    (void)cpu;
    if (g_track.count < MAX_EVENTS) {
        g_track.evts[g_track.count].event = event;
        g_track.evts[g_track.count].pc = pc;
    }
    g_track.count++;
    g_track.last_user_data = user_data;
}


/* ===== Helper: stavba CPU ===== */

static z80_t *make_cpu(void)
{
    z80_t *cpu = z80_create(
        test_mread, NULL,
        test_mwrite, NULL,
        test_pread, NULL,
        test_pwrite, NULL,
        test_intread, NULL
    );
    TEST_ASSERT_NOT_NULL(cpu);
    return cpu;
}


/* ===== Unity setUp/tearDown ===== */

static z80_t *g_cpu;


void setUp(void)
{
    reset_track();
    g_cpu = make_cpu();
    z80_set_cpu_ctrl_event(g_cpu, ctrl_cb, (void *)0xBADF00D);
}


void tearDown(void)
{
    z80_destroy(g_cpu);
    g_cpu = NULL;
}


/* ===== Testy ===== */

/**
 * @test HALT opcode (0x76) fire cpu_ctrl_event_cb s HALT_ENTER.
 *       PC predany do cb = adresa za HALT (= 0x0001 po fetch).
 */
void test_HALT_entry_fires_HALT_ENTER(void)
{
    g_mem[0x0000] = 0x76; /* HALT */

    g_cpu->pc = 0x0000;
    z80_step(g_cpu);

    TEST_ASSERT_TRUE(z80_is_halted(g_cpu));
    TEST_ASSERT_EQUAL_INT(1, g_track.count);
    TEST_ASSERT_EQUAL_UINT8(Z80_CPU_CTRL_HALT_ENTER, g_track.evts[0].event);
    TEST_ASSERT_EQUAL_UINT16(0x0001, g_track.evts[0].pc);
    TEST_ASSERT_EQUAL_PTR((void *)0xBADF00D, g_track.last_user_data);
}


/**
 * @test HALT exit pri IM 1 INT ack fire HALT_EXIT.
 *       Sekvence: IM 1, EI, NOP (ei_delay clear), HALT. Pak z80_int +
 *       step. Pri prijeti INT je CPU v HALT -> fire HALT_EXIT.
 */
void test_HALT_exit_on_IM1_INT_fires_HALT_EXIT(void)
{
    g_mem[0x0000] = 0xED; g_mem[0x0001] = 0x56; /* IM 1 */
    g_mem[0x0002] = 0xFB; /* EI */
    g_mem[0x0003] = 0x00; /* NOP - shod ei_delay */
    g_mem[0x0004] = 0x76; /* HALT */
    g_mem[0x0038] = 0x00; /* NOP v handleru */

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu); /* IM 1 */
    z80_step(g_cpu); /* EI */
    z80_step(g_cpu); /* NOP - ei_delay clear */
    z80_step(g_cpu); /* HALT - fire HALT_ENTER */

    TEST_ASSERT_TRUE(z80_is_halted(g_cpu));
    TEST_ASSERT_EQUAL_INT(1, g_track.count);
    TEST_ASSERT_EQUAL_UINT8(Z80_CPU_CTRL_HALT_ENTER, g_track.evts[0].event);

    int prev_count = g_track.count;
    z80_int(g_cpu);
    z80_step(g_cpu); /* INT dispatch + handler instrukce */

    /*
     * Ocekavame nove eventy: HALT_EXIT + RST 38h dispatch (IM 1 zpracuje
     * jako RST 38h pres internal vector switch case 1; ale v z80.c je IM 1
     * implementovan jako primy push + jump bez RST hooku). Overime jen
     * HALT_EXIT vyskytl-li se a pc je za HALT (= 0x0005).
     */
    bool exit_found = false;
    for (int i = prev_count; i < g_track.count; i++) {
        if (g_track.evts[i].event == Z80_CPU_CTRL_HALT_EXIT) {
            exit_found = true;
            TEST_ASSERT_EQUAL_UINT16(0x0005, g_track.evts[i].pc);
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(exit_found, "HALT_EXIT event missing");
}


/**
 * @test HALT exit pri NMI ack fire HALT_EXIT.
 *       Sekvence: HALT. Pak z80_nmi + step. PC v okamziku exit = adresa
 *       za HALT.
 */
void test_HALT_exit_on_NMI_fires_HALT_EXIT(void)
{
    g_mem[0x0000] = 0x76; /* HALT */
    g_mem[0x0066] = 0x00; /* NOP v NMI handleru */

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu); /* HALT */
    TEST_ASSERT_TRUE(z80_is_halted(g_cpu));

    int prev_count = g_track.count;
    z80_nmi(g_cpu);
    z80_step(g_cpu); /* NMI dispatch + handler instrukce */

    TEST_ASSERT_FALSE(z80_is_halted(g_cpu));
    /* Najit HALT_EXIT mezi novymi eventy. */
    bool exit_found = false;
    for (int i = prev_count; i < g_track.count; i++) {
        if (g_track.evts[i].event == Z80_CPU_CTRL_HALT_EXIT) {
            exit_found = true;
            TEST_ASSERT_EQUAL_UINT16(0x0001, g_track.evts[i].pc);
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(exit_found, "HALT_EXIT event missing on NMI");
}


/**
 * @test RST 00h (opcode 0xC7) fire cpu_ctrl_event_cb s RST_00.
 *       PC predany do cb = adresa za RST opcode (= 0x0001 po fetch).
 */
void test_RST_00_fires_RST_00(void)
{
    g_mem[0x0000] = 0xC7; /* RST 00h */
    g_mem[0x0000] = 0xC7; /* shadow + skok zpet na 0x00 */
    /* Po RST 00h se PC skoci na 0x00 = znovu RST 00h. Pro test staci
     * jeden step, ktery zachyti prvni RST. */

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(1, g_track.count);
    TEST_ASSERT_EQUAL_UINT8(Z80_CPU_CTRL_RST_00, g_track.evts[0].event);
    TEST_ASSERT_EQUAL_UINT16(0x0001, g_track.evts[0].pc);
    /* Stack obsahuje navrat PC = 0x0001 (= adresa za RST). */
    uint16_t pushed = (uint16_t)(g_mem[0xFFFC] | (g_mem[0xFFFD] << 8));
    TEST_ASSERT_EQUAL_UINT16(0x0001, pushed);
    TEST_ASSERT_EQUAL_UINT16(0x0000, g_cpu->pc);
}


/**
 * @test RST 38h (opcode 0xFF) fire cpu_ctrl_event_cb s RST_38.
 */
void test_RST_38_fires_RST_38(void)
{
    g_mem[0x0100] = 0xFF; /* RST 38h */
    g_mem[0x0038] = 0x00; /* NOP v handleru */

    g_cpu->pc = 0x0100;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(1, g_track.count);
    TEST_ASSERT_EQUAL_UINT8(Z80_CPU_CTRL_RST_38, g_track.evts[0].event);
    TEST_ASSERT_EQUAL_UINT16(0x0101, g_track.evts[0].pc);
    TEST_ASSERT_EQUAL_UINT16(0x0038, g_cpu->pc);
}


/**
 * @test NULL callback hot path = no crash, instrukce projde normalne.
 */
void test_NULL_cb_no_crash(void)
{
    z80_set_cpu_ctrl_event(g_cpu, NULL, NULL);

    g_mem[0x0000] = 0x76; /* HALT */
    g_mem[0x0001] = 0xC7; /* RST 00h - po prubuzeni? Test pouziva jen krok. */

    g_cpu->pc = 0x0000;
    z80_step(g_cpu); /* HALT */

    TEST_ASSERT_EQUAL_INT(0, g_track.count); /* cb vypnut */
    TEST_ASSERT_TRUE(z80_is_halted(g_cpu));
}


/**
 * @test Bezna instrukce (LD/ADD/NOP) negeneruje cpu_ctrl_event.
 *       Sanity check ze hook neni v hot path bez gate.
 */
void test_normal_instruction_no_event(void)
{
    g_mem[0x0000] = 0x00; /* NOP */
    g_mem[0x0001] = 0x3E; /* LD A, n */
    g_mem[0x0002] = 0x42; /* n = 0x42 */
    g_mem[0x0003] = 0x87; /* ADD A, A */
    g_mem[0x0004] = 0x00; /* NOP */

    g_cpu->pc = 0x0000;
    for (int i = 0; i < 4; i++) {
        z80_step(g_cpu);
    }

    TEST_ASSERT_EQUAL_INT(0, g_track.count);
    TEST_ASSERT_EQUAL_UINT8(0x84, g_cpu->af.h); /* 0x42 + 0x42 = 0x84 */
}


/**
 * @test INT mezi beznymi instrukcemi NEgeneruje HALT_EXIT (= CPU nebyl
 *       v HALT pred ack).
 */
void test_INT_between_normal_instructions_no_HALT_EXIT(void)
{
    g_mem[0x0000] = 0xED; g_mem[0x0001] = 0x56; /* IM 1 */
    g_mem[0x0002] = 0xFB; /* EI */
    g_mem[0x0003] = 0x00; /* NOP */
    g_mem[0x0004] = 0x00; /* NOP - INT prijat zde */
    g_mem[0x0038] = 0x00; /* NOP v handleru */

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu); /* IM 1 */
    z80_step(g_cpu); /* EI */
    z80_step(g_cpu); /* NOP - shod ei_delay */

    TEST_ASSERT_FALSE(z80_is_halted(g_cpu));
    int prev_count = g_track.count;

    z80_int(g_cpu);
    z80_step(g_cpu);

    /* Zadny novy event - INT mezi instrukcemi nedela HALT_EXIT a IM 1
     * nepatri pres RST opcode dispatch (= primy push + jump v INT
     * handleru). */
    for (int i = prev_count; i < g_track.count; i++) {
        TEST_ASSERT_NOT_EQUAL_UINT8(Z80_CPU_CTRL_HALT_EXIT, g_track.evts[i].event);
    }
}


/**
 * @test Reset zachova nastaveny callback (save/restore v z80_reset).
 *       Po reset musi byt cpu_ctrl_event_cb stale aktivni.
 */
void test_reset_preserves_cb(void)
{
    z80_reset(g_cpu);

    /* Po reset overit ze HALT entry stale fire. */
    g_mem[0x0000] = 0x76;
    g_cpu->pc = 0x0000;
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(1, g_track.count);
    TEST_ASSERT_EQUAL_UINT8(Z80_CPU_CTRL_HALT_ENTER, g_track.evts[0].event);
    TEST_ASSERT_EQUAL_PTR((void *)0xBADF00D, g_track.last_user_data);
}


/* ===== Unity main ===== */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_HALT_entry_fires_HALT_ENTER);
    RUN_TEST(test_HALT_exit_on_IM1_INT_fires_HALT_EXIT);
    RUN_TEST(test_HALT_exit_on_NMI_fires_HALT_EXIT);
    RUN_TEST(test_RST_00_fires_RST_00);
    RUN_TEST(test_RST_38_fires_RST_38);
    RUN_TEST(test_NULL_cb_no_crash);
    RUN_TEST(test_normal_instruction_no_event);
    RUN_TEST(test_INT_between_normal_instructions_no_HALT_EXIT);
    RUN_TEST(test_reset_preserves_cb);
    return UNITY_END();
}
