/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file test_z80meta.c
 * @brief Unit testy knihovny mzlib_z80meta.
 *
 * Testuje lookup metadat instrukcí Z80 přes funkci `z80_meta_lookup()`
 * pro reprezentativní vzorek 12 instrukčních tříd. Pro každou třídu se:
 *   1. Postaví umělý buffer s opcody dané instrukce
 *   2. Zavolá `z80_dasm()` pro dekódování
 *   3. Zavolá `z80_meta_lookup()` a ověří, že vrácený popis a flag
 *      effects odpovídají očekávání
 *
 * Standalone test - z80meta nemá emu závislosti, linkuje se s Unity,
 * mzlib_z80meta a mzlib_dasm_z80.
 *
 * Pokrytí (≥10 reprezentativních):
 *   - NOP, HALT (no-op CPU control)
 *   - LD A,B (REG8/REG8 generic)
 *   - LD A,I (REG8/REG8 + subtype - speciální flagy)
 *   - ADD A,B (8bit arith)
 *   - JP NN, JP Z,NN (skoky bez/s podmínkou)
 *   - CALL NN, CALL NZ,NN (volání bez/s podmínkou)
 *   - JR e (relativní skok)
 *   - RST p (restart vektor)
 *   - DJNZ e (block-like control flow)
 *   - BIT 7,A (CB rotace + bit ops)
 *   - IN A,(n) (I/O)
 *   - DD prefix - LD (IX+d),n (indexované adresování)
 */

#include "unity.h"

#include <stdint.h>
#include <string.h>

#include "libs/dasm-z80/z80_dasm.h"
#include "libs/z80meta/z80_meta.h"


/** Maximální velikost emulovaného opcode bufferu pro test. */
#define TEST_MEM_SIZE 16

/** Lokální paměťový buffer používaný read_fn callbackem. */
static uint8_t g_test_mem[TEST_MEM_SIZE];


/**
 * @brief Read callback pro z80_dasm() - čte z g_test_mem.
 *
 * @param addr      Adresa v rozsahu 0..TEST_MEM_SIZE-1; vyšší adresy vrátí 0.
 * @param user_data Nepoužito.
 * @return Hodnota bajtu na adrese addr nebo 0 mimo rozsah.
 */
static uint8_t test_read(uint16_t addr, void *user_data)
{
    (void)user_data;
    if (addr < TEST_MEM_SIZE) {
        return g_test_mem[addr];
    }
    return 0;
}


/**
 * @brief Naplní g_test_mem zadanými opcody.
 *
 * @param bytes Pole opcodů.
 * @param n     Počet bajtů z bytes ke kopírování.
 */
static void load_bytes(const uint8_t *bytes, size_t n)
{
    memset(g_test_mem, 0, sizeof(g_test_mem));
    if (n > sizeof(g_test_mem)) {
        n = sizeof(g_test_mem);
    }
    memcpy(g_test_mem, bytes, n);
}


/** @brief Setup před každým testem - vyčistí buffer. */
void setUp(void)
{
    memset(g_test_mem, 0, sizeof(g_test_mem));
}


/** @brief Teardown po každém testu - prázdný (žádný globální stav). */
void tearDown(void)
{
}


/**
 * @brief Pomocné makro: zkopíruje opcody do bufferu a disassembluje.
 *
 * Naplní lokální `inst` strukturu výsledkem `z80_dasm()`. Test pak může
 * volat `z80_meta_lookup(&inst)`.
 */
#define DASM(inst_var, ...) \
    do { \
        const uint8_t bytes[] = { __VA_ARGS__ }; \
        load_bytes(bytes, sizeof(bytes)); \
        z80_dasm(&(inst_var), test_read, NULL, 0); \
    } while (0)


/**
 * @brief Test: NOP (00h) - žádné flagy, popis "NOP: zadna operace".
 */
static void test_nop(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0x00);

    TEST_ASSERT_EQUAL_STRING("NOP", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_NONE, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_NONE, inst.op2.type);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_NOT_NULL(m->description);
    TEST_ASSERT_TRUE(strstr(m->description, "NOP") != NULL);
    /* NOP nemění žádný flag */
    for (int i = 0; i < Z80_META_FLAG_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(Z80_FE_UNAFFECTED, m->flags[i],
                                       "NOP nesmi menit flagy");
    }
}


/**
 * @brief Test: LD A,B (78h) - žádné flagy, popis "LD r,r'".
 */
static void test_ld_a_b(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0x78);

    TEST_ASSERT_EQUAL_STRING("LD", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_REG8, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_REG8, inst.op2.type);
    TEST_ASSERT_EQUAL_UINT(Z80_R8_A, inst.op1.val.reg8);
    TEST_ASSERT_EQUAL_UINT(Z80_R8_B, inst.op2.val.reg8);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
    /* LD r,r' nemění žádný flag */
    for (int i = 0; i < Z80_META_FLAG_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(Z80_FE_UNAFFECTED, m->flags[i]);
    }
}


/**
 * @brief Test: LD A,I (ED 57h) - speciální flagy: PV = IFF2.
 *
 * Tento test ověřuje že subtype lookup (op2_subtype = R8_I) funguje
 * a vrací jiný entry než generic LD A,B.
 */
static void test_ld_a_i(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0xED, 0x57);

    TEST_ASSERT_EQUAL_STRING("LD", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_REG8, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_REG8, inst.op2.type);
    TEST_ASSERT_EQUAL_UINT(Z80_R8_A, inst.op1.val.reg8);
    TEST_ASSERT_EQUAL_UINT(Z80_R8_I, inst.op2.val.reg8);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_TRUE_MESSAGE(strstr(m->description, "I") != NULL,
                              "Description LD A,I musi obsahovat 'I'");
    /* PV je SPECIAL (= IFF2) */
    TEST_ASSERT_EQUAL_INT(Z80_FE_SPECIAL, m->flags[Z80_META_F_PV]);
    /* H je RESET (0) */
    TEST_ASSERT_EQUAL_INT(Z80_FE_RESET, m->flags[Z80_META_F_H]);
    /* C zůstává unchanged */
    TEST_ASSERT_EQUAL_INT(Z80_FE_UNAFFECTED, m->flags[Z80_META_F_C]);
    /* N je RESET */
    TEST_ASSERT_EQUAL_INT(Z80_FE_RESET, m->flags[Z80_META_F_N]);
}


/**
 * @brief Test: ADD A,B (80h) - aritmetika 8bit, plný flag set.
 */
static void test_add_a_b(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0x80);

    TEST_ASSERT_EQUAL_STRING("ADD", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_REG8, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_REG8, inst.op2.type);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
    /* S Z F5 H F3 V N=0 C */
    TEST_ASSERT_EQUAL_INT(Z80_FE_RESULT, m->flags[Z80_META_F_S]);
    TEST_ASSERT_EQUAL_INT(Z80_FE_RESULT, m->flags[Z80_META_F_Z]);
    TEST_ASSERT_EQUAL_INT(Z80_FE_OVERFLOW, m->flags[Z80_META_F_PV]);
    TEST_ASSERT_EQUAL_INT(Z80_FE_RESET, m->flags[Z80_META_F_N]);
    TEST_ASSERT_EQUAL_INT(Z80_FE_RESULT, m->flags[Z80_META_F_C]);
}


/**
 * @brief Test: JP nn (C3h NN NN) - skok, bez podmínky, žádné flagy.
 */
static void test_jp_nn(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0xC3, 0x34, 0x12);

    TEST_ASSERT_EQUAL_STRING("JP", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_IMM16, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_NONE, inst.op2.type);
    TEST_ASSERT_EQUAL_UINT(0x1234, inst.op1.val.imm16);
    TEST_ASSERT_EQUAL_INT(Z80_FLOW_JUMP, inst.flow);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
    /* JP nemění flagy */
    for (int i = 0; i < Z80_META_FLAG_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(Z80_FE_UNAFFECTED, m->flags[i]);
    }
}


/**
 * @brief Test: JP Z,nn (CAh NN NN) - podmíněný skok.
 *
 * Klíč pro lookup: (mnemonic="JP", op1.type=CONDITION, op2.type=IMM16).
 */
static void test_jp_cc_nn(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0xCA, 0x00, 0x80);  /* JP Z,8000h */

    TEST_ASSERT_EQUAL_STRING("JP", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_CONDITION, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_IMM16, inst.op2.type);
    TEST_ASSERT_EQUAL_INT(Z80_FLOW_JUMP_COND, inst.flow);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_TRUE(strstr(m->description, "cc") != NULL ||
                     strstr(m->description, "podmin") != NULL);
}


/**
 * @brief Test: CALL nn (CDh NN NN) - bezpodmínečné volání.
 */
static void test_call_nn(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0xCD, 0x00, 0x40);

    TEST_ASSERT_EQUAL_STRING("CALL", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_IMM16, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_NONE, inst.op2.type);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
}


/**
 * @brief Test: CALL NZ,nn (C4h NN NN) - podmíněné volání.
 */
static void test_call_cc_nn(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0xC4, 0x00, 0x40);

    TEST_ASSERT_EQUAL_STRING("CALL", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_CONDITION, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_IMM16, inst.op2.type);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
}


/**
 * @brief Test: JR e (18h ee) - relativní skok.
 */
static void test_jr_e(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0x18, 0x05);

    TEST_ASSERT_EQUAL_STRING("JR", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_REL8, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_NONE, inst.op2.type);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
}


/**
 * @brief Test: RST 38h (FFh) - restart vektor.
 */
static void test_rst(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0xFF);

    TEST_ASSERT_EQUAL_STRING("RST", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_RST_VEC, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_NONE, inst.op2.type);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
}


/**
 * @brief Test: DJNZ e (10h ee) - decrement-jump.
 */
static void test_djnz(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0x10, 0x05);

    TEST_ASSERT_EQUAL_STRING("DJNZ", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_REL8, inst.op1.type);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
}


/**
 * @brief Test: BIT 7,A (CB 7Fh) - bit ops, CB prefix.
 */
static void test_bit_7_a(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0xCB, 0x7F);

    TEST_ASSERT_EQUAL_STRING("BIT", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_BIT_INDEX, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_REG8, inst.op2.type);
    TEST_ASSERT_EQUAL_UINT(7, inst.op1.val.bit_index);
    TEST_ASSERT_EQUAL_UINT(Z80_R8_A, inst.op2.val.reg8);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
    /* H = 1, N = 0, C unchanged */
    TEST_ASSERT_EQUAL_INT(Z80_FE_SET, m->flags[Z80_META_F_H]);
    TEST_ASSERT_EQUAL_INT(Z80_FE_RESET, m->flags[Z80_META_F_N]);
    TEST_ASSERT_EQUAL_INT(Z80_FE_UNAFFECTED, m->flags[Z80_META_F_C]);
}


/**
 * @brief Test: IN A,(n) (DBh nn) - I/O.
 */
static void test_in_a_n(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0xDB, 0xFE);

    TEST_ASSERT_EQUAL_STRING("IN", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_REG8, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_MEM_IMM8, inst.op2.type);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
    /* IN A,(n) nemění flagy */
    for (int i = 0; i < Z80_META_FLAG_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(Z80_FE_UNAFFECTED, m->flags[i]);
    }
}


/**
 * @brief Test: LD (IX+5),0xAA (DD 36 05 AA) - DD prefix, indexed mem.
 */
static void test_ld_ix_d_n(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0xDD, 0x36, 0x05, 0xAA);

    TEST_ASSERT_EQUAL_STRING("LD", inst.mnemonic);
    TEST_ASSERT_EQUAL_INT(Z80_OP_MEM_IX_D, inst.op1.type);
    TEST_ASSERT_EQUAL_INT(Z80_OP_IMM8, inst.op2.type);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
}


/**
 * @brief Test: HALT (76h) - CPU control.
 */
static void test_halt(void)
{
    z80_dasm_inst_t inst;
    DASM(inst, 0x76);

    TEST_ASSERT_EQUAL_STRING("HALT", inst.mnemonic);

    const z80_meta_t *m = z80_meta_lookup(&inst);
    TEST_ASSERT_NOT_NULL(m);
}


/**
 * @brief Test: z80_flag_effect_to_char() - kontrola všech enum hodnot.
 */
static void test_flag_effect_to_char(void)
{
    TEST_ASSERT_EQUAL_INT('-', z80_flag_effect_to_char(Z80_FE_UNAFFECTED));
    TEST_ASSERT_EQUAL_INT('0', z80_flag_effect_to_char(Z80_FE_RESET));
    TEST_ASSERT_EQUAL_INT('1', z80_flag_effect_to_char(Z80_FE_SET));
    TEST_ASSERT_EQUAL_INT('*', z80_flag_effect_to_char(Z80_FE_RESULT));
    TEST_ASSERT_EQUAL_INT('P', z80_flag_effect_to_char(Z80_FE_PARITY));
    TEST_ASSERT_EQUAL_INT('V', z80_flag_effect_to_char(Z80_FE_OVERFLOW));
    TEST_ASSERT_EQUAL_INT('?', z80_flag_effect_to_char(Z80_FE_UNDEFINED));
    TEST_ASSERT_EQUAL_INT('X', z80_flag_effect_to_char(Z80_FE_SPECIAL));
}


/**
 * @brief Test: z80_meta_lookup(NULL) musí vrátit NULL.
 */
static void test_lookup_null(void)
{
    TEST_ASSERT_NULL(z80_meta_lookup(NULL));
}


/* ====================================================================== */
/* Unity main runner                                                       */
/* ====================================================================== */

/**
 * @brief Vstupní bod test runneru.
 */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_nop);
    RUN_TEST(test_halt);
    RUN_TEST(test_ld_a_b);
    RUN_TEST(test_ld_a_i);
    RUN_TEST(test_add_a_b);
    RUN_TEST(test_jp_nn);
    RUN_TEST(test_jp_cc_nn);
    RUN_TEST(test_call_nn);
    RUN_TEST(test_call_cc_nn);
    RUN_TEST(test_jr_e);
    RUN_TEST(test_rst);
    RUN_TEST(test_djnz);
    RUN_TEST(test_bit_7_a);
    RUN_TEST(test_in_a_n);
    RUN_TEST(test_ld_ix_d_n);
    RUN_TEST(test_flag_effect_to_char);
    RUN_TEST(test_lookup_null);

    return UNITY_END();
}
