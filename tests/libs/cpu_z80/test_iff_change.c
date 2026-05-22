/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file test_iff_change.c
 * @brief Unit testy pro z80_set_iff_change callback (V17+ debugger BP).
 *
 * Pokrytí:
 *   - DI instrukce -> reason=DI, IFF1=0, IFF2=0
 *   - EI instrukce -> reason=EI, IFF1=1, IFF2=1
 *   - IM 1 INT ack -> reason=INT_ACK, IFF1=0, IFF2=0
 *   - IM 2 INT ack -> reason=INT_ACK, IFF1=0, IFF2=0
 *   - IM 0 INT ack (RST 38h injected) -> reason=INT_ACK, IFF1=0, IFF2=0
 *   - NMI dispatch -> reason=NMI_ACK, IFF1=0, IFF2=puvodni IFF1
 *   - RETN po NMI -> reason=RETN, IFF1<-IFF2
 *   - RETI -> reason=RETI, IFF1<-IFF2
 *   - z80_reset -> reason=RESET, IFF1=0, IFF2=0 (po nastavenem cb)
 *   - Baseline regression: ei_cb/di_cb/intack_cb/reti_cb/nmi_cb stale fire
 *   - NULL callback hot path = no crash
 *
 * Standalone test - cpu-z80 je samostatna lib, ovladame ji uzivatelskymi
 * callbacks (memory/IO simulace v test souboru).
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

/** Vektor preruseni pro IM 2 (predavany pres int_vector pri z80_irq). */
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

/** Posledni zaznamenana IFF change udalost. */
typedef struct {
    int        fire_count;     /**< Pocet volani iff_change_cb celkem */
    uint8_t    last_iff1;      /**< Posledni new_iff1 */
    uint8_t    last_iff2;      /**< Posledni new_iff2 */
    uint8_t    last_reason;    /**< Posledni reason */
    void      *last_user_data; /**< Posledni user_data */
    int        ei_count;       /**< Counter pro ei_cb (baseline) */
    int        di_count;       /**< Counter pro di_cb (baseline) */
    int        intack_count;   /**< Counter pro intack_cb (baseline) */
    int        reti_count;     /**< Counter pro reti_cb (baseline) */
    int        nmi_assert_count; /**< Counter pro nmi_cb (= z80_nmi assert) */
} iff_track_t;


static iff_track_t g_track;


/** Resetuje globalni tracker + memory. */
static void reset_track(void)
{
    memset(&g_track, 0, sizeof(g_track));
    memset(g_mem, 0, sizeof(g_mem));
    g_last_io_port = 0;
    g_int_vector = 0xFF;
}


static void iff_cb(struct z80_s *cpu, uint8_t new_iff1, uint8_t new_iff2,
                   uint8_t reason, void *user_data)
{
    (void)cpu;
    g_track.fire_count++;
    g_track.last_iff1 = new_iff1;
    g_track.last_iff2 = new_iff2;
    g_track.last_reason = reason;
    g_track.last_user_data = user_data;
}


static void ei_baseline_cb(struct z80_s *cpu, void *user_data)
{
    (void)cpu;
    (void)user_data;
    g_track.ei_count++;
}


static void di_baseline_cb(struct z80_s *cpu, void *user_data)
{
    (void)cpu;
    (void)user_data;
    g_track.di_count++;
}


static void intack_baseline_cb(struct z80_s *cpu, void *user_data)
{
    (void)cpu;
    (void)user_data;
    g_track.intack_count++;
}


static void reti_baseline_cb(struct z80_s *cpu, void *user_data)
{
    (void)cpu;
    (void)user_data;
    g_track.reti_count++;
}


static void nmi_assert_cb(struct z80_s *cpu, void *user_data)
{
    (void)cpu;
    (void)user_data;
    g_track.nmi_assert_count++;
}


/* ===== Helper: stavba CPU s plne propojenymi callbacks ===== */

static z80_t *make_cpu_with_baselines(void)
{
    z80_t *cpu = z80_create(
        test_mread, NULL,
        test_mwrite, NULL,
        test_pread, NULL,
        test_pwrite, NULL,
        test_intread, NULL
    );
    TEST_ASSERT_NOT_NULL(cpu);

    z80_set_ei(cpu, ei_baseline_cb, NULL);
    z80_set_di(cpu, di_baseline_cb, NULL);
    z80_set_intack(cpu, intack_baseline_cb, NULL);
    z80_set_reti(cpu, reti_baseline_cb, NULL);
    z80_set_nmi_cb(cpu, nmi_assert_cb, NULL);

    return cpu;
}


/* ===== Unity setUp/tearDown ===== */

static z80_t *g_cpu;


void setUp(void)
{
    reset_track();
    g_cpu = make_cpu_with_baselines();
    z80_set_iff_change(g_cpu, iff_cb, (void *)0xC0FFEE);
}


void tearDown(void)
{
    z80_destroy(g_cpu);
    g_cpu = NULL;
}


/* ===== Testy ===== */

/**
 * @test DI instrukce fire iff_change_cb s reason=DI, IFF1=0, IFF2=0.
 *       Baseline di_cb stale fire (= zero regression).
 */
void test_DI_fires_iff_change_with_reason_DI(void)
{
    g_mem[0x0000] = 0xFB; /* EI - nastavi IFF1=1, IFF2=1 */
    g_mem[0x0001] = 0xF3; /* DI - nastavi IFF1=0, IFF2=0 */
    g_mem[0x0002] = 0x00; /* NOP */

    g_cpu->pc = 0x0000;
    z80_step(g_cpu); /* EI */
    z80_step(g_cpu); /* DI */

    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff1);
    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff2);
    TEST_ASSERT_EQUAL_UINT8(Z80_IFF_REASON_DI, g_track.last_reason);
    TEST_ASSERT_EQUAL_UINT8(0, g_track.last_iff1);
    TEST_ASSERT_EQUAL_UINT8(0, g_track.last_iff2);
    TEST_ASSERT_EQUAL_INT(2, g_track.fire_count); /* EI + DI */
    TEST_ASSERT_EQUAL_INT(1, g_track.di_count);   /* baseline OK */
    TEST_ASSERT_EQUAL_INT(1, g_track.ei_count);
    TEST_ASSERT_EQUAL_PTR((void *)0xC0FFEE, g_track.last_user_data);
}


/**
 * @test EI instrukce fire iff_change_cb s reason=EI, IFF1=1, IFF2=1.
 */
void test_EI_fires_iff_change_with_reason_EI(void)
{
    g_mem[0x0000] = 0xFB; /* EI */
    g_mem[0x0001] = 0x00; /* NOP */

    g_cpu->pc = 0x0000;
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_UINT8(1, g_cpu->iff1);
    TEST_ASSERT_EQUAL_UINT8(1, g_cpu->iff2);
    TEST_ASSERT_EQUAL_UINT8(Z80_IFF_REASON_EI, g_track.last_reason);
    TEST_ASSERT_EQUAL_UINT8(1, g_track.last_iff1);
    TEST_ASSERT_EQUAL_UINT8(1, g_track.last_iff2);
    TEST_ASSERT_EQUAL_INT(1, g_track.fire_count);
    TEST_ASSERT_EQUAL_INT(1, g_track.ei_count);
}


/**
 * @test IM 1 dispatch fire iff_change_cb s reason=INT_ACK, IFF1=0, IFF2=0.
 *       Baseline intack_cb stale fire.
 *
 *       Sequence: EI, NOP (= odlozi po EI delay), pak z80_int + step.
 *       Z80 prijme INT mezi instrukcemi (= EI ei_delay flag zabrani prijetimu
 *       hned po EI). RST 38h zapise PC na stack, skoci na 0038h.
 *
 *       Pozn: z80_step po INT pending = dispatch + jedna instrukce z
 *       handleru. Pokud handler[0]=HALT, dispatch postavi CPU do halt
 *       stavu a PC zustane na handler entry adrese.
 */
void test_IM1_int_ack_fires_INT_ACK(void)
{
    /* Sequence: IM 1, EI, NOP, NOP */
    g_mem[0x0000] = 0xED; g_mem[0x0001] = 0x56; /* IM 1 */
    g_mem[0x0002] = 0xFB; /* EI */
    g_mem[0x0003] = 0x00; /* NOP - prvni instrukce po EI, blokuje INT */
    g_mem[0x0004] = 0x00; /* NOP - tady uz INT muze byt prijat */
    g_mem[0x0038] = 0x76; /* HALT v handleru - PC zustane na 0x0038 */

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu); /* IM 1 */
    z80_step(g_cpu); /* EI - po nem ei_delay */
    z80_step(g_cpu); /* NOP - ei_delay se shodi */

    int prev_fire = g_track.fire_count;
    int prev_intack = g_track.intack_count;

    /* Asserto INT a step - mezi instrukcemi Z80 vstoupi do dispatch + HALT */
    z80_int(g_cpu);
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff1);
    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff2);
    TEST_ASSERT_EQUAL_UINT8(Z80_IFF_REASON_INT_ACK, g_track.last_reason);
    TEST_ASSERT_EQUAL_UINT8(0, g_track.last_iff1);
    TEST_ASSERT_EQUAL_UINT8(0, g_track.last_iff2);
    TEST_ASSERT_EQUAL_INT(prev_fire + 1, g_track.fire_count);
    TEST_ASSERT_EQUAL_INT(prev_intack + 1, g_track.intack_count);
    /* PC = handler+1 (HALT inkrementuje PC pri fetch, hardware pak
     * M1 opakuje stejne PC az do INT - test staci PC=0x39 = halt vykonan) */
    TEST_ASSERT_EQUAL_UINT16(0x0039, g_cpu->pc);
    TEST_ASSERT_TRUE(z80_is_halted(g_cpu));
}


/**
 * @test IM 2 dispatch fire iff_change_cb s reason=INT_ACK, IFF1=0, IFF2=0.
 *       Vektor=0x80, I=0x40 -> vec_addr=0x4080 -> handler PC.
 */
void test_IM2_int_ack_fires_INT_ACK(void)
{
    g_mem[0x0000] = 0xED; g_mem[0x0001] = 0x5E; /* IM 2 */
    g_mem[0x0002] = 0xFB; /* EI */
    g_mem[0x0003] = 0x00; /* NOP */
    g_mem[0x0004] = 0x00; /* NOP */
    g_mem[0x4080] = 0x34; /* vector LSB */
    g_mem[0x4081] = 0x12; /* vector MSB -> handler @ 0x1234 */
    g_mem[0x1234] = 0x76; /* HALT v handleru - PC zustane */

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    g_cpu->i = 0x40;
    z80_step(g_cpu); /* IM 2 */
    z80_step(g_cpu); /* EI */
    z80_step(g_cpu); /* NOP - shod ei_delay */

    int prev_fire = g_track.fire_count;

    z80_irq(g_cpu, 0x80); /* explicit vektor pro IM 2 */
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff1);
    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff2);
    TEST_ASSERT_EQUAL_UINT8(Z80_IFF_REASON_INT_ACK, g_track.last_reason);
    TEST_ASSERT_EQUAL_INT(prev_fire + 1, g_track.fire_count);
    /* PC = handler+1 (HALT fetched + halted=true) */
    TEST_ASSERT_EQUAL_UINT16(0x1235, g_cpu->pc);
    TEST_ASSERT_TRUE(z80_is_halted(g_cpu));
}


/**
 * @test IM 0 dispatch fire iff_change_cb s reason=INT_ACK.
 *
 *       Aktualni z80.c implementace nedeli na IM mode pri clearu IFF -
 *       fire vsechny IM 0/1/2 stejne. Knowledge base i Zilog datasheet
 *       potvrzuje: Z80 vzdy clearuje IFF1+IFF2 pri INTACK strobe nezavisle
 *       na IM mode. IM mode urcuje co se vykona po clearu (ne zda se
 *       IFF clearuji).
 */
void test_IM0_int_ack_fires_INT_ACK(void)
{
    g_mem[0x0000] = 0xED; g_mem[0x0001] = 0x46; /* IM 0 */
    g_mem[0x0002] = 0xFB; /* EI */
    g_mem[0x0003] = 0x00; /* NOP */
    g_mem[0x0004] = 0x00; /* NOP */

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu); /* IM 0 */
    z80_step(g_cpu); /* EI */
    z80_step(g_cpu); /* NOP */

    int prev_fire = g_track.fire_count;

    /* IM 0 - z80 pouziva int_vector & 0x38 jako RST adresu (default = 0) */
    z80_irq(g_cpu, 0xFF); /* RST 38h (FF = RST 38 opcode pattern) */
    z80_step(g_cpu);

    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff1);
    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff2);
    TEST_ASSERT_EQUAL_UINT8(Z80_IFF_REASON_INT_ACK, g_track.last_reason);
    TEST_ASSERT_EQUAL_INT(prev_fire + 1, g_track.fire_count);
}


/**
 * @test NMI dispatch fire iff_change_cb s reason=NMI_ACK.
 *       IFF1=0, IFF2 zachovano (= puvodni IFF1).
 */
void test_NMI_ack_fires_NMI_ACK(void)
{
    g_mem[0x0000] = 0xFB; /* EI - IFF1=1, IFF2=1 */
    g_mem[0x0001] = 0x00; /* NOP */
    g_mem[0x0066] = 0x76; /* HALT v NMI handleru - PC zustane */

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu); /* EI */
    z80_step(g_cpu); /* NOP - shod ei_delay (preventivne) */

    int prev_fire = g_track.fire_count;
    int prev_nmi_assert = g_track.nmi_assert_count;

    z80_nmi(g_cpu);
    /* z80_nmi() fire nmi_cb (= externi assert), ne iff_change yet. */
    TEST_ASSERT_EQUAL_INT(prev_nmi_assert + 1, g_track.nmi_assert_count);
    TEST_ASSERT_EQUAL_INT(prev_fire, g_track.fire_count); /* yet */

    z80_step(g_cpu); /* dispatch NMI mezi instrukcemi + HALT */

    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff1);
    TEST_ASSERT_EQUAL_UINT8(1, g_cpu->iff2); /* zachovano kopie puvodniho IFF1 */
    TEST_ASSERT_EQUAL_UINT8(Z80_IFF_REASON_NMI_ACK, g_track.last_reason);
    TEST_ASSERT_EQUAL_UINT8(0, g_track.last_iff1);
    TEST_ASSERT_EQUAL_UINT8(1, g_track.last_iff2);
    TEST_ASSERT_EQUAL_INT(prev_fire + 1, g_track.fire_count);
    /* PC = handler+1 (HALT fetched) */
    TEST_ASSERT_EQUAL_UINT16(0x0067, g_cpu->pc);
    TEST_ASSERT_TRUE(z80_is_halted(g_cpu));
}


/**
 * @test RETN fire iff_change_cb s reason=RETN. IFF1 <- IFF2.
 *
 *       Setup: EI, NOP, NMI -> handler@0x66 obsahuje RETN. Z80 step po
 *       NMI assert vykoná dispatch + jednu instrukci handleru. Pokud
 *       handler[0]=RETN, jediny step udela vse: NMI ack + RETN.
 *       Dva iff_change fire: NMI_ACK + RETN.
 */
void test_RETN_fires_RETN(void)
{
    /* Setup: EI, NOP, NMI, v handleru RETN */
    g_mem[0x0000] = 0xFB; /* EI */
    g_mem[0x0001] = 0x00; /* NOP */
    g_mem[0x0066] = 0xED; g_mem[0x0067] = 0x45; /* RETN */

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu); /* EI */
    z80_step(g_cpu); /* NOP */

    int prev_fire = g_track.fire_count;
    z80_nmi(g_cpu);
    z80_step(g_cpu); /* NMI dispatch + RETN handler */

    /* RETN: IFF1 <- IFF2 (= 1, puvodni stav pred NMI). */
    TEST_ASSERT_EQUAL_UINT8(1, g_cpu->iff1);
    TEST_ASSERT_EQUAL_UINT8(1, g_cpu->iff2);
    /* Posledni fire byl RETN. */
    TEST_ASSERT_EQUAL_UINT8(Z80_IFF_REASON_RETN, g_track.last_reason);
    /* Dva fire: NMI_ACK + RETN. */
    TEST_ASSERT_EQUAL_INT(prev_fire + 2, g_track.fire_count);
}


/**
 * @test RETI fire iff_change_cb s reason=RETI. IFF1 <- IFF2.
 *       Baseline reti_cb stale fire.
 *
 *       Step po z80_int udela: INT dispatch (IFF=0,0) + RETI handler
 *       (IFF1 <- IFF2 = 0,0). Dva iff_change fire: INT_ACK + RETI.
 */
void test_RETI_fires_RETI(void)
{
    /* Setup: IM 1, EI, NOP, INT, v handleru RETI */
    g_mem[0x0000] = 0xED; g_mem[0x0001] = 0x56; /* IM 1 */
    g_mem[0x0002] = 0xFB; /* EI */
    g_mem[0x0003] = 0x00; /* NOP */
    g_mem[0x0004] = 0x00; /* NOP */
    g_mem[0x0038] = 0xED; g_mem[0x0039] = 0x4D; /* RETI */

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu); /* IM 1 */
    z80_step(g_cpu); /* EI */
    z80_step(g_cpu); /* NOP */

    int prev_fire = g_track.fire_count;
    int prev_reti = g_track.reti_count;

    z80_int(g_cpu);
    z80_step(g_cpu); /* INT dispatch + RETI handler */

    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff1);
    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff2);
    TEST_ASSERT_EQUAL_UINT8(Z80_IFF_REASON_RETI, g_track.last_reason);
    /* Dva fire: INT_ACK + RETI */
    TEST_ASSERT_EQUAL_INT(prev_fire + 2, g_track.fire_count);
    TEST_ASSERT_EQUAL_INT(prev_reti + 1, g_track.reti_count);
}


/**
 * @test z80_reset fire iff_change_cb s reason=RESET. IFF1=0, IFF2=0.
 *       Reset musi zachovat iff_change_cb (save/restore).
 */
void test_reset_fires_RESET(void)
{
    /* g_cpu uz ma cb nastaveny v setUp. */
    int prev_fire = g_track.fire_count;

    z80_reset(g_cpu);

    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff1);
    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff2);
    TEST_ASSERT_EQUAL_UINT8(Z80_IFF_REASON_RESET, g_track.last_reason);
    TEST_ASSERT_EQUAL_INT(prev_fire + 1, g_track.fire_count);
    /* user_data zachovano pres reset */
    TEST_ASSERT_EQUAL_PTR((void *)0xC0FFEE, g_track.last_user_data);
}


/**
 * @test NULL callback hot path = no crash, instrukce projde normalne.
 *       Baseline ei_cb/di_cb stale fire (= zero regression).
 */
void test_NULL_cb_no_crash(void)
{
    z80_set_iff_change(g_cpu, NULL, NULL);

    g_mem[0x0000] = 0xFB; /* EI */
    g_mem[0x0001] = 0xF3; /* DI */
    g_mem[0x0002] = 0x00; /* NOP */

    g_cpu->pc = 0x0000;
    z80_step(g_cpu); /* EI */
    z80_step(g_cpu); /* DI */

    TEST_ASSERT_EQUAL_INT(0, g_track.fire_count); /* cb nenastaven */
    TEST_ASSERT_EQUAL_INT(1, g_track.ei_count);   /* baseline OK */
    TEST_ASSERT_EQUAL_INT(1, g_track.di_count);
    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff1);
    TEST_ASSERT_EQUAL_UINT8(0, g_cpu->iff2);
}


/**
 * @test Baseline regression - vsechny existujici callbacks fire stejne jako
 *       pred zmenou. ei_cb, di_cb, intack_cb, reti_cb pridruzene s
 *       iff_change_cb v jedne instrukci.
 */
void test_baseline_callbacks_no_regression(void)
{
    /* IM 1, EI, NOP, INT, RETI */
    g_mem[0x0000] = 0xED; g_mem[0x0001] = 0x56;
    g_mem[0x0002] = 0xFB;
    g_mem[0x0003] = 0x00;
    g_mem[0x0004] = 0x00;
    g_mem[0x0038] = 0xED; g_mem[0x0039] = 0x4D;

    g_cpu->pc = 0x0000;
    g_cpu->sp = 0xFFFE;
    z80_step(g_cpu); /* IM 1 */
    z80_step(g_cpu); /* EI -> ei_cb */
    z80_step(g_cpu); /* NOP */
    z80_int(g_cpu);
    z80_step(g_cpu); /* INT dispatch (intack_cb) + RETI handler (reti_cb) */

    TEST_ASSERT_EQUAL_INT(1, g_track.ei_count);
    TEST_ASSERT_EQUAL_INT(1, g_track.intack_count);
    TEST_ASSERT_EQUAL_INT(1, g_track.reti_count);
    /* iff_change fire: EI + INT_ACK + RETI = 3 */
    TEST_ASSERT_EQUAL_INT(3, g_track.fire_count);
}


/* ===== Unity main ===== */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_DI_fires_iff_change_with_reason_DI);
    RUN_TEST(test_EI_fires_iff_change_with_reason_EI);
    RUN_TEST(test_IM1_int_ack_fires_INT_ACK);
    RUN_TEST(test_IM2_int_ack_fires_INT_ACK);
    RUN_TEST(test_IM0_int_ack_fires_INT_ACK);
    RUN_TEST(test_NMI_ack_fires_NMI_ACK);
    RUN_TEST(test_RETN_fires_RETN);
    RUN_TEST(test_RETI_fires_RETI);
    RUN_TEST(test_reset_fires_RESET);
    RUN_TEST(test_NULL_cb_no_crash);
    RUN_TEST(test_baseline_callbacks_no_regression);
    return UNITY_END();
}
