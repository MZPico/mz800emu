/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file test_h_suffix_sym_subst.c
 * @brief Regresní test leading-zero v HEX_H_SUFFIX symbol substituci.
 *
 * Bug (před fixem): z80_dasm_to_str_sym() v z80_dasm_symtab.c sestavoval
 * search string pro adresu bez leading '0' (např. "c000h"), zatímco
 * formátovač fmt_hex16 v z80_dasm_format.c vyrendroval "0c000h" (leading
 * zero pro horní nibble A-F kvůli kompatibilitě s asm parsery jako pasmo).
 * Strstr našel substring uvnitř, nahradil jen "c000h" za symbol a leading
 * '0' zůstal orphan -> výstup "jp 0Lc000" místo "jp Lc000".
 *
 * Pokrývá:
 *  - target_sym (JP nn / CALL nn) pro adresy >= 0xA000 (lower/upper case)
 *  - mem_sym (LD A,(nn)) pro adresy >= 0xA000
 *  - regrese pro adresy < 0xA000 (leading-0 path neaktivuje)
 *  - RST (8-bit) - leading-0 path netriggered, nesmí být dotčen
 *
 * Standalone test - linkuje jen mzlib_dasm_z80 + Unity.
 */

#include "unity.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "libs/dasm-z80/z80_dasm.h"


/**
 * @brief Read callback kontext - mapuje base_addr na opcode buffer.
 *
 * Identicky jako v test_dasm_sym.c.
 */
typedef struct {
    uint16_t base_addr;
    const uint8_t *bytes;
    size_t len;
} test_read_ctx_t;


/**
 * @brief Read callback pro z80_dasm() - čte ze zadaného bufferu s base_addr.
 *
 * @param addr      Adresa, kterou disassembler chce přečíst.
 * @param user_data test_read_ctx_t* s base_addr + bytes + len.
 * @return Hodnota bajtu nebo 0 mimo rozsah bufferu.
 */
static uint8_t test_read(uint16_t addr, void *user_data)
{
    const test_read_ctx_t *ctx = (const test_read_ctx_t *)user_data;
    uint16_t off = (uint16_t)(addr - ctx->base_addr);
    if (off < ctx->len) {
        return ctx->bytes[off];
    }
    return 0;
}


/** @brief Setup před každým testem - bez globálního stavu. */
void setUp(void)
{
}


/** @brief Teardown po každém testu - prázdný. */
void tearDown(void)
{
}


/**
 * @brief Disassembluj instrukci a vrať formátovaný string s H_SUFFIX stylem.
 *
 * Naplní inst přes z80_dasm(), zavolá z80_dasm_to_str_sym() s H_SUFFIX
 * stylem (lowercase nebo uppercase podle parametru) a předanou symtab.
 *
 * @param[in]  opcodes    Pole bajtů instrukce.
 * @param[in]  opcode_len Počet bajtů v opcodes.
 * @param[in]  base_addr  Adresa, na které instrukce začíná.
 * @param[in]  symbols    Tabulka symbolu pro resolve. Může být NULL.
 * @param[in]  uppercase  1 = velká písmena, 0 = malá.
 * @param[out] out        Výstupní buffer (>= 64 B).
 * @return Délka řetězce nebo záporné při chybě.
 */
static int dasm_h_suffix(const uint8_t *opcodes, size_t opcode_len,
                         uint16_t base_addr,
                         const z80_symtab_t *symbols,
                         int uppercase,
                         char *out)
{
    test_read_ctx_t ctx = {
        .base_addr = base_addr,
        .bytes = opcodes,
        .len = opcode_len,
    };
    z80_dasm_inst_t inst;
    z80_dasm(&inst, test_read, &ctx, base_addr);

    z80_dasm_format_t fmt;
    z80_dasm_format_default(&fmt);
    fmt.hex_style = Z80_HEX_H_SUFFIX;
    fmt.uppercase = uppercase;

    return z80_dasm_to_str_sym(out, 64, &inst, &fmt, symbols);
}


/* =========================================================================
 *  target_sym (JP nn) pro horní nibble A-F = bug zóna
 * ========================================================================= */

/**
 * @brief JP 0c000h se symbolem "Lc000" - lowercase H suffix.
 *
 * Opcody: C3 00 C0 = JP 0xC000. Render je "jp 0c000h", po substituci musí
 * být "jp Lc000". Před fixem: "jp 0Lc000" (orphan '0' zanechán).
 */
void test_h_suffix_target_sym_above_a000_lower(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    TEST_ASSERT_NOT_NULL(tab);
    z80_symtab_add(tab, 0xC000, "Lc000");

    char out[64];
    const uint8_t op[] = { 0xC3, 0x00, 0xC0 };
    int len = dasm_h_suffix(op, sizeof(op), 0x0000, tab, 0, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    /* Symbol musí být ve výstupu */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "Lc000"), out);
    /* Žádný orphan '0' před symbolem */
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "0Lc000"), out);
    /* A žádný hex residual */
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "c000h"), out);

    z80_symtab_destroy(tab);
}


/**
 * @brief JP 0C000H se symbolem "Lc000" - uppercase H suffix.
 *
 * Opcody: C3 00 C0. Render je "JP 0C000H", po substituci "JP Lc000".
 */
void test_h_suffix_target_sym_above_a000_upper(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0xC000, "Lc000");

    char out[64];
    const uint8_t op[] = { 0xC3, 0x00, 0xC0 };
    int len = dasm_h_suffix(op, sizeof(op), 0x0000, tab, 1, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "Lc000"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "0Lc000"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "C000H"), out);

    z80_symtab_destroy(tab);
}


/**
 * @brief Regrese: JP 4000h - horní nibble = '4', leading-0 path neaktivuje.
 *
 * Opcody: C3 00 40 = JP 0x4000. Render "jp 4000h", substituce "jp L4000".
 * Před fixem fungovalo, po fixu MUSÍ stále fungovat.
 */
void test_h_suffix_target_sym_below_a000(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x4000, "L4000");

    char out[64];
    const uint8_t op[] = { 0xC3, 0x00, 0x40 };
    int len = dasm_h_suffix(op, sizeof(op), 0x0000, tab, 0, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "L4000"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "4000h"), out);

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  mem_sym (LD A,(nn)) pro horní nibble A-F
 * ========================================================================= */

/**
 * @brief LD A,(0e000h) se symbolem "data1".
 *
 * Opcody: 3A 00 E0 = LD A,(0xE000). Render "ld a,(0e000h)", substituce
 * musí být "ld a,(data1)". Před fixem: "ld a,(0data1)" (orphan '0' uvnitř
 * závorek).
 */
void test_h_suffix_mem_sym_above_a000(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0xE000, "data1");

    char out[64];
    const uint8_t op[] = { 0x3A, 0x00, 0xE0 };
    int len = dasm_h_suffix(op, sizeof(op), 0x0000, tab, 0, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    /* Substituce s závorkami: "(data1)" musí být přítomna */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "(data1)"), out);
    /* Bug residual: žádný "(0data1)" */
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "(0data1)"), out);
    /* Žádný hex residual */
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "e000h"), out);

    z80_symtab_destroy(tab);
}


/**
 * @brief Regrese: LD A,(4000h) - horní nibble = '4', bez leading-0 path.
 *
 * Opcody: 3A 00 40 = LD A,(0x4000). Render "ld a,(4000h)", substituce
 * "ld a,(data2)".
 */
void test_h_suffix_mem_sym_below_a000(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x4000, "data2");

    char out[64];
    const uint8_t op[] = { 0x3A, 0x00, 0x40 };
    int len = dasm_h_suffix(op, sizeof(op), 0x0000, tab, 0, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "(data2)"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "4000h"), out);

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  RST - 8-bit, leading-0 path netriggered
 * ========================================================================= */

/**
 * @brief RST 28h se symbolem "Lr28" - bez bug ohrožení.
 *
 * Opcody: EF = RST 28h. RST je 8-bit (max 0x38), horní nibble je 0-3,
 * tj. leading-0 path nikdy neaktivuje. Test zajišťuje že fix tento případ
 * nerozbije.
 */
void test_h_suffix_rst_unaffected(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x0028, "Lr28");

    char out[64];
    const uint8_t op[] = { 0xEF };
    int len = dasm_h_suffix(op, sizeof(op), 0x0000, tab, 0, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "Lr28"), out);
    /* RST nesmí mít orphan '0' před symbolem */
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "0Lr28"), out);

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  Unity test runner
 * ========================================================================= */

/**
 * @brief Test runner - registruje 6 testů pokrývajících H_SUFFIX bug.
 *
 * Před fixem MUSÍ failovat:
 *   - test_h_suffix_target_sym_above_a000_lower
 *   - test_h_suffix_target_sym_above_a000_upper
 *   - test_h_suffix_mem_sym_above_a000
 *
 * Po fixu všech 6 PASS.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_h_suffix_target_sym_above_a000_lower);
    RUN_TEST(test_h_suffix_target_sym_above_a000_upper);
    RUN_TEST(test_h_suffix_target_sym_below_a000);
    RUN_TEST(test_h_suffix_mem_sym_above_a000);
    RUN_TEST(test_h_suffix_mem_sym_below_a000);
    RUN_TEST(test_h_suffix_rst_unaffected);
    return UNITY_END();
}
