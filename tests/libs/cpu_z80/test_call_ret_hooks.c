/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file test_call_ret_hooks.c
 * @brief Unit testy pro z80_set_call / z80_set_ret callback (callstack mutant).
 *
 * Pokrytí:
 *   - CALL nn (0xCD) - call_cb fire s (call_site, target, return_addr)
 *   - CALL cc, nn (všech 8 podmíněných variant) - taken-větev fire
 *   - CALL cc, nn - NOT taken nefiruje call_cb
 *   - RET (0xC9) - ret_cb fire s (pop_target, sp_before_pop)
 *   - RET cc (všech 8 podmíněných variant) - taken-větev fire
 *   - RET cc - NOT taken nefiruje ret_cb
 *   - NULL callback hot path = no crash + žádný side effect
 *   - Běžná instrukce (NOP, LD) NEgeneruje call_cb / ret_cb
 *   - RETI (ED 4D) a RETN (ED 45) NEgenerují ret_cb (= scope)
 *   - Reset zachová nastavené callbacky
 *
 * Standalone test - cpu-z80 je samostatná lib, ovládáme ji
 * uživatelskými callbacks (memory/IO simulace v test souboru).
 */

#include "unity.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "libs/cpu-z80/z80.h"


/* ===== Test memory + IO simulace ===== */

/** Plně 64 KB paměť pro test - obsahuje opcody. */
static uint8_t g_mem[0x10000];


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
    (void)port;
    (void)user_data;
    return 0xFF;
}


static void test_pwrite(struct z80_s *cpu, uint16_t port, uint8_t value,
                        void *user_data)
{
    (void)cpu;
    (void)port;
    (void)value;
    (void)user_data;
}


static uint8_t test_intread(struct z80_s *cpu, void *user_data)
{
    (void)cpu;
    (void)user_data;
    return 0xFF;
}


/* ===== Sledování callback fire ===== */

/** Maximální počet zaznamenaných eventů v test bufferu. */
#define MAX_EVENTS  32

/** Záznam o jednom CALL eventu. */
typedef struct {
    uint16_t call_site;
    uint16_t target;
    uint16_t return_addr;
} call_evt_t;

/** Záznam o jednom RET eventu. */
typedef struct {
    uint16_t pop_target;
    uint16_t sp_before_pop;
} ret_evt_t;


/** Buffer zachycených eventů pro per-test inspekci. */
typedef struct {
    int        call_count;
    call_evt_t call_evts[MAX_EVENTS];
    void      *last_call_user;

    int        ret_count;
    ret_evt_t  ret_evts[MAX_EVENTS];
    void      *last_ret_user;
} cs_track_t;


static cs_track_t g_track;


/** Resetuje globální tracker + memory. */
static void reset_track(void)
{
    memset(&g_track, 0, sizeof(g_track));
    memset(g_mem, 0, sizeof(g_mem));
}


static void call_cb(struct z80_s *cpu, uint16_t call_site, uint16_t target,
                    uint16_t return_addr, void *user_data)
{
    (void)cpu;
    if (g_track.call_count < MAX_EVENTS) {
        g_track.call_evts[g_track.call_count].call_site = call_site;
        g_track.call_evts[g_track.call_count].target = target;
        g_track.call_evts[g_track.call_count].return_addr = return_addr;
    }
    g_track.call_count++;
    g_track.last_call_user = user_data;
}


static void ret_cb(struct z80_s *cpu, uint16_t pop_target,
                   uint16_t sp_before_pop, void *user_data)
{
    (void)cpu;
    if (g_track.ret_count < MAX_EVENTS) {
        g_track.ret_evts[g_track.ret_count].pop_target = pop_target;
        g_track.ret_evts[g_track.ret_count].sp_before_pop = sp_before_pop;
    }
    g_track.ret_count++;
    g_track.last_ret_user = user_data;
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
    z80_set_call(g_cpu, call_cb, (void *)0xC11C11);
    z80_set_ret(g_cpu, ret_cb, (void *)0x9E79E7);
}


void tearDown(void)
{
    z80_destroy(g_cpu);
    g_cpu = NULL;
}


/* ===== Helper: nastav SP + pre-load flagy ===== */

static void prep_cpu(uint16_t pc, uint16_t sp, uint8_t flags)
{
    g_cpu->pc = pc;
    g_cpu->sp = sp;
    g_cpu->af.l = flags;
}


/* ===== Testy CALL nn ===== */

/**
 * @test CALL nn (0xCD) fire call_cb s (call_site, target, return_addr).
 *       call_site = adresa CALL opcode (= 0x0100), target = 0x1234,
 *       return_addr = 0x0103 (= za CALL = co se pushne).
 *       Po dispatch: PC = 0x1234, na stacku 0x0103.
 */
void test_CALL_nn_fires_call_cb(void)
{
    g_mem[0x0100] = 0xCD;       /* CALL nn */
    g_mem[0x0101] = 0x34;
    g_mem[0x0102] = 0x12;       /* target = 0x1234 */
    g_mem[0x1234] = 0x00;       /* NOP v cílové rutině */

    prep_cpu(0x0100, 0xFFFE, 0);
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(1, g_track.call_count);
    TEST_ASSERT_EQUAL_UINT16(0x0100, g_track.call_evts[0].call_site);
    TEST_ASSERT_EQUAL_UINT16(0x1234, g_track.call_evts[0].target);
    TEST_ASSERT_EQUAL_UINT16(0x0103, g_track.call_evts[0].return_addr);
    TEST_ASSERT_EQUAL_PTR((void *)0xC11C11, g_track.last_call_user);
    TEST_ASSERT_EQUAL_UINT16(0x1234, g_cpu->pc);
    /* Stack obsahuje return adresu 0x0103. */
    uint16_t pushed = (uint16_t)(g_mem[0xFFFC] | (g_mem[0xFFFD] << 8));
    TEST_ASSERT_EQUAL_UINT16(0x0103, pushed);
}


/* ===== Testy CALL cc, nn - taken ===== */

/**
 * @brief Helper: ověř že CALL cc s daným opcode + flags = taken
 *        skutečně fire call_cb.
 */
static void check_CALL_cc_taken(uint8_t opcode, uint8_t flags)
{
    reset_track();
    g_mem[0x0200] = opcode;
    g_mem[0x0201] = 0x78;
    g_mem[0x0202] = 0x56;       /* target = 0x5678 */
    g_mem[0x5678] = 0x00;

    prep_cpu(0x0200, 0xFFFE, flags);
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(1, g_track.call_count);
    TEST_ASSERT_EQUAL_UINT16(0x0200, g_track.call_evts[0].call_site);
    TEST_ASSERT_EQUAL_UINT16(0x5678, g_track.call_evts[0].target);
    TEST_ASSERT_EQUAL_UINT16(0x0203, g_track.call_evts[0].return_addr);
    TEST_ASSERT_EQUAL_UINT16(0x5678, g_cpu->pc);
}


/**
 * @test Všech 8 podmíněných CALL cc, nn fire call_cb když cc=true.
 *       Flag mask: Z=0x40 NZ, C=0x01 NC, P=0x04 PV PO, S=0x80 P M.
 */
void test_CALL_cc_taken_fires_call_cb(void)
{
    /* CALL NZ, nn (0xC4) - taken když Z=0 */
    check_CALL_cc_taken(0xC4, 0x00);
    /* CALL Z,  nn (0xCC) - taken když Z=1 */
    check_CALL_cc_taken(0xCC, 0x40);
    /* CALL NC, nn (0xD4) - taken když C=0 */
    check_CALL_cc_taken(0xD4, 0x00);
    /* CALL C,  nn (0xDC) - taken když C=1 */
    check_CALL_cc_taken(0xDC, 0x01);
    /* CALL PO, nn (0xE4) - taken když PV=0 */
    check_CALL_cc_taken(0xE4, 0x00);
    /* CALL PE, nn (0xEC) - taken když PV=1 */
    check_CALL_cc_taken(0xEC, 0x04);
    /* CALL P,  nn (0xF4) - taken když S=0 */
    check_CALL_cc_taken(0xF4, 0x00);
    /* CALL M,  nn (0xFC) - taken když S=1 */
    check_CALL_cc_taken(0xFC, 0x80);
}


/* ===== Testy CALL cc, nn - NOT taken ===== */

/**
 * @brief Helper: ověř že CALL cc s flags = not-taken NEFIRE call_cb.
 *        PC pokračuje za CALL (= 3 bytes), SP nezměněn.
 */
static void check_CALL_cc_not_taken(uint8_t opcode, uint8_t flags)
{
    reset_track();
    g_mem[0x0300] = opcode;
    g_mem[0x0301] = 0xCD;
    g_mem[0x0302] = 0xAB;
    g_mem[0x0303] = 0x00;       /* NOP za CALL */

    prep_cpu(0x0300, 0xFFFE, flags);
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(0, g_track.call_count);
    TEST_ASSERT_EQUAL_UINT16(0x0303, g_cpu->pc);
    TEST_ASSERT_EQUAL_UINT16(0xFFFE, g_cpu->sp); /* SP nezměněn */
}


/**
 * @test Všech 8 podmíněných CALL cc, nn NEfiruje call_cb když cc=false.
 *       Invertované flagy oproti taken testu.
 */
void test_CALL_cc_not_taken_no_fire(void)
{
    /* CALL NZ - not taken když Z=1 */
    check_CALL_cc_not_taken(0xC4, 0x40);
    /* CALL Z  - not taken když Z=0 */
    check_CALL_cc_not_taken(0xCC, 0x00);
    /* CALL NC - not taken když C=1 */
    check_CALL_cc_not_taken(0xD4, 0x01);
    /* CALL C  - not taken když C=0 */
    check_CALL_cc_not_taken(0xDC, 0x00);
    /* CALL PO - not taken když PV=1 */
    check_CALL_cc_not_taken(0xE4, 0x04);
    /* CALL PE - not taken když PV=0 */
    check_CALL_cc_not_taken(0xEC, 0x00);
    /* CALL P  - not taken když S=1 */
    check_CALL_cc_not_taken(0xF4, 0x80);
    /* CALL M  - not taken když S=0 */
    check_CALL_cc_not_taken(0xFC, 0x00);
}


/* ===== Testy RET ===== */

/**
 * @test RET (0xC9) fire ret_cb s (pop_target, sp_before_pop).
 *       Stack obsahuje 0x1234 na 0xFFFC. SP před pop = 0xFFFC.
 *       Po dispatch: PC = 0x1234, SP = 0xFFFE.
 */
void test_RET_fires_ret_cb(void)
{
    g_mem[0x0400] = 0xC9;       /* RET */
    g_mem[0xFFFC] = 0x34;
    g_mem[0xFFFD] = 0x12;       /* return target = 0x1234 */
    g_mem[0x1234] = 0x00;

    prep_cpu(0x0400, 0xFFFC, 0);
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(1, g_track.ret_count);
    TEST_ASSERT_EQUAL_UINT16(0x1234, g_track.ret_evts[0].pop_target);
    TEST_ASSERT_EQUAL_UINT16(0xFFFC, g_track.ret_evts[0].sp_before_pop);
    TEST_ASSERT_EQUAL_PTR((void *)0x9E79E7, g_track.last_ret_user);
    TEST_ASSERT_EQUAL_UINT16(0x1234, g_cpu->pc);
    TEST_ASSERT_EQUAL_UINT16(0xFFFE, g_cpu->sp);
}


/* ===== Testy RET cc - taken ===== */

/**
 * @brief Helper: ověř že RET cc s daným opcode + flags = taken
 *        fire ret_cb a vrátí se.
 */
static void check_RET_cc_taken(uint8_t opcode, uint8_t flags)
{
    reset_track();
    g_mem[0x0500] = opcode;
    g_mem[0xFFF8] = 0xCD;
    g_mem[0xFFF9] = 0xAB;       /* return target = 0xABCD */
    g_mem[0xABCD] = 0x00;

    prep_cpu(0x0500, 0xFFF8, flags);
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(1, g_track.ret_count);
    TEST_ASSERT_EQUAL_UINT16(0xABCD, g_track.ret_evts[0].pop_target);
    TEST_ASSERT_EQUAL_UINT16(0xFFF8, g_track.ret_evts[0].sp_before_pop);
    TEST_ASSERT_EQUAL_UINT16(0xABCD, g_cpu->pc);
    TEST_ASSERT_EQUAL_UINT16(0xFFFA, g_cpu->sp);
}


/**
 * @test Všech 8 podmíněných RET cc fire ret_cb když cc=true.
 */
void test_RET_cc_taken_fires_ret_cb(void)
{
    /* RET NZ (0xC0) */
    check_RET_cc_taken(0xC0, 0x00);
    /* RET Z  (0xC8) */
    check_RET_cc_taken(0xC8, 0x40);
    /* RET NC (0xD0) */
    check_RET_cc_taken(0xD0, 0x00);
    /* RET C  (0xD8) */
    check_RET_cc_taken(0xD8, 0x01);
    /* RET PO (0xE0) */
    check_RET_cc_taken(0xE0, 0x00);
    /* RET PE (0xE8) */
    check_RET_cc_taken(0xE8, 0x04);
    /* RET P  (0xF0) */
    check_RET_cc_taken(0xF0, 0x00);
    /* RET M  (0xF8) */
    check_RET_cc_taken(0xF8, 0x80);
}


/* ===== Testy RET cc - NOT taken ===== */

/**
 * @brief Helper: ověř že RET cc s flags = not-taken NEFIRE ret_cb.
 *        PC pokračuje za RET (= 1 byte), SP nezměněn.
 */
static void check_RET_cc_not_taken(uint8_t opcode, uint8_t flags)
{
    reset_track();
    g_mem[0x0600] = opcode;
    g_mem[0x0601] = 0x00;
    g_mem[0xFFF8] = 0xCD;
    g_mem[0xFFF9] = 0xAB;

    prep_cpu(0x0600, 0xFFF8, flags);
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(0, g_track.ret_count);
    TEST_ASSERT_EQUAL_UINT16(0x0601, g_cpu->pc);
    TEST_ASSERT_EQUAL_UINT16(0xFFF8, g_cpu->sp);
}


/**
 * @test Všech 8 podmíněných RET cc NEfiruje ret_cb když cc=false.
 */
void test_RET_cc_not_taken_no_fire(void)
{
    check_RET_cc_not_taken(0xC0, 0x40);
    check_RET_cc_not_taken(0xC8, 0x00);
    check_RET_cc_not_taken(0xD0, 0x01);
    check_RET_cc_not_taken(0xD8, 0x00);
    check_RET_cc_not_taken(0xE0, 0x04);
    check_RET_cc_not_taken(0xE8, 0x00);
    check_RET_cc_not_taken(0xF0, 0x80);
    check_RET_cc_not_taken(0xF8, 0x00);
}


/* ===== NULL callback testy ===== */

/**
 * @test NULL call_cb + NULL ret_cb hot path = no crash, instrukce
 *       projdou normálně (CALL + RET round trip).
 */
void test_NULL_cb_no_crash(void)
{
    z80_set_call(g_cpu, NULL, NULL);
    z80_set_ret(g_cpu, NULL, NULL);

    g_mem[0x0700] = 0xCD;
    g_mem[0x0701] = 0x10;
    g_mem[0x0702] = 0x08;       /* CALL 0x0810 */
    g_mem[0x0810] = 0xC9;       /* RET */

    prep_cpu(0x0700, 0xFFFE, 0);
    z80_step(g_cpu);            /* CALL */
    TEST_ASSERT_EQUAL_UINT16(0x0810, g_cpu->pc);
    TEST_ASSERT_EQUAL_UINT16(0xFFFC, g_cpu->sp);

    z80_step(g_cpu);            /* RET */
    TEST_ASSERT_EQUAL_UINT16(0x0703, g_cpu->pc);
    TEST_ASSERT_EQUAL_UINT16(0xFFFE, g_cpu->sp);

    TEST_ASSERT_EQUAL_INT(0, g_track.call_count);
    TEST_ASSERT_EQUAL_INT(0, g_track.ret_count);
}


/* ===== Sanity: běžné instrukce ===== */

/**
 * @test Běžná instrukce (NOP, LD, ADD, JP) NEgeneruje call_cb / ret_cb.
 *       Sanity check že hook není v hot path bez gate.
 */
void test_normal_instructions_no_event(void)
{
    g_mem[0x0800] = 0x00;       /* NOP */
    g_mem[0x0801] = 0x3E; g_mem[0x0802] = 0x42;  /* LD A, 0x42 */
    g_mem[0x0803] = 0x87;       /* ADD A, A */
    g_mem[0x0804] = 0xC3; g_mem[0x0805] = 0x00; g_mem[0x0806] = 0x09;
                                /* JP 0x0900 - i nepodmíněný JP nesmí fire */
    g_mem[0x0900] = 0x00;

    prep_cpu(0x0800, 0xFFFE, 0);
    for (int i = 0; i < 5; i++) {
        z80_step(g_cpu);
    }

    TEST_ASSERT_EQUAL_INT(0, g_track.call_count);
    TEST_ASSERT_EQUAL_INT(0, g_track.ret_count);
    TEST_ASSERT_EQUAL_UINT16(0x0901, g_cpu->pc);
}


/* ===== Sanity: RETI a RETN scope ===== */

/**
 * @test RETI (ED 4D) NEfiruje ret_cb.
 *       Callstack subsystem řeší RETI přes iff_change_cb (jiný dispatch).
 */
void test_RETI_no_ret_cb(void)
{
    g_mem[0x0A00] = 0xED;
    g_mem[0x0A01] = 0x4D;       /* RETI */
    g_mem[0xFFFC] = 0x00;
    g_mem[0xFFFD] = 0x0B;       /* return target = 0x0B00 */
    g_mem[0x0B00] = 0x00;

    prep_cpu(0x0A00, 0xFFFC, 0);
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(0, g_track.ret_count);
    /* PC se ale skutečně skočil na 0x0B00 (= RETI provedl POP). */
    TEST_ASSERT_EQUAL_UINT16(0x0B00, g_cpu->pc);
}


/**
 * @test RETN (ED 45) NEfiruje ret_cb.
 *       Stejný důvod jako RETI - separátní dispatch cesta.
 */
void test_RETN_no_ret_cb(void)
{
    g_mem[0x0C00] = 0xED;
    g_mem[0x0C01] = 0x45;       /* RETN */
    g_mem[0xFFFC] = 0x00;
    g_mem[0xFFFD] = 0x0D;       /* return target = 0x0D00 */
    g_mem[0x0D00] = 0x00;

    prep_cpu(0x0C00, 0xFFFC, 0);
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_INT(0, g_track.ret_count);
    TEST_ASSERT_EQUAL_UINT16(0x0D00, g_cpu->pc);
}


/* ===== Reset preserves callbacks ===== */

/**
 * @test Reset zachová nastavené callbacky (save/restore v z80_reset).
 *       Po reset musí CALL + RET stále fire.
 */
void test_reset_preserves_cbs(void)
{
    z80_reset(g_cpu);

    g_mem[0x0E00] = 0xCD;
    g_mem[0x0E01] = 0x00;
    g_mem[0x0E02] = 0x0F;       /* CALL 0x0F00 */
    g_mem[0x0F00] = 0xC9;       /* RET */

    prep_cpu(0x0E00, 0xFFFE, 0);
    z80_step(g_cpu);            /* CALL */
    z80_step(g_cpu);            /* RET */

    TEST_ASSERT_EQUAL_INT(1, g_track.call_count);
    TEST_ASSERT_EQUAL_UINT16(0x0E00, g_track.call_evts[0].call_site);
    TEST_ASSERT_EQUAL_UINT16(0x0F00, g_track.call_evts[0].target);
    TEST_ASSERT_EQUAL_UINT16(0x0E03, g_track.call_evts[0].return_addr);

    TEST_ASSERT_EQUAL_INT(1, g_track.ret_count);
    TEST_ASSERT_EQUAL_UINT16(0x0E03, g_track.ret_evts[0].pop_target);
    TEST_ASSERT_EQUAL_PTR((void *)0xC11C11, g_track.last_call_user);
    TEST_ASSERT_EQUAL_PTR((void *)0x9E79E7, g_track.last_ret_user);
}


/* ===== Unity main ===== */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_CALL_nn_fires_call_cb);
    RUN_TEST(test_CALL_cc_taken_fires_call_cb);
    RUN_TEST(test_CALL_cc_not_taken_no_fire);
    RUN_TEST(test_RET_fires_ret_cb);
    RUN_TEST(test_RET_cc_taken_fires_ret_cb);
    RUN_TEST(test_RET_cc_not_taken_no_fire);
    RUN_TEST(test_NULL_cb_no_crash);
    RUN_TEST(test_normal_instructions_no_event);
    RUN_TEST(test_RETI_no_ret_cb);
    RUN_TEST(test_RETN_no_ret_cb);
    RUN_TEST(test_reset_preserves_cbs);
    return UNITY_END();
}
