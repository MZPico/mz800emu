/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file test_motorola_ix.c
 * @brief Unit testy Motorola/sdas IX/IY render stylu.
 *
 * Pokrývá F6 (dialect sdcc-asz80) - rozšíření z80_dasm_format_t o field
 * @c ix_iy_style. Sdas-z80 (asxxxx rodina) používá Motorola konvenci
 * "(d,ix)" / "(-d,iy)" oproti Zilog mainstreamu "(IX+d)" / "(IY-d)".
 *
 * Test scénáře:
 *
 *   1) Motorola IX positive displacement: DD 7E 05 -> "ld a,(5,ix)"
 *   2) Motorola IX negative displacement: DD 7E FE -> "ld a,(-2,ix)"
 *   3) Motorola IX zero displacement:     DD 7E 00 -> "ld a,(0,ix)"
 *   4) Motorola IY positive displacement: FD 7E 0A -> "ld a,(10,iy)"
 *   5) Zilog default (backward-compat):   DD 7E 05 -> "ld a,(ix+0x05)"
 *   6) Motorola IX extrémně negativní:    DD 7E 80 -> "ld a,(-128,ix)"
 *
 * Standalone test - linkuje jen mzlib_dasm_z80 + Unity.
 */

#include "unity.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "libs/dasm-z80/z80_dasm.h"


/* =========================================================================
 *  Pomocné: read callback nad lokálním bufferem
 * ========================================================================= */

/**
 * @brief Kontext read callbacku - lineární čtení z bufferu od adresy 0.
 *
 * Mimo rozsah vrací 0x00 (= NOP) jako bezpečný default.
 */
typedef struct {
    const uint8_t *bytes;  /**< Buffer instrukcí (čte se od adresy 0). */
    size_t len;            /**< Velikost bufferu. */
} test_ctx_t;


/**
 * @brief Read callback pro @ref z80_dasm.
 *
 * @param addr      Požadovaná adresa.
 * @param user_data @ref test_ctx_t pointer.
 * @return Bajt z bufferu nebo 0 mimo rozsah.
 */
static uint8_t test_read(uint16_t addr, void *user_data)
{
    const test_ctx_t *ctx = (const test_ctx_t *)user_data;
    if (addr < ctx->len) {
        return ctx->bytes[addr];
    }
    return 0x00;
}


/* =========================================================================
 *  Unity setUp/tearDown
 * ========================================================================= */

void setUp(void) {}
void tearDown(void) {}


/* =========================================================================
 *  Pomocná: dekódování + formátování s daným ix_iy_style
 * ========================================================================= */

/**
 * @brief Dekóduje instrukci na adrese 0 z @p bytes a vyformátuje s daným
 *        ix_iy_style + Z80_HEX_0X + lowercase.
 *
 * Helper pro většinu testů: vrátí null-terminated string ve @p out buferu.
 *
 * @param[in]  bytes        Buffer s opcode.
 * @param      n_bytes      Velikost bufferu.
 * @param      style        Z80_IX_ZILOG nebo Z80_IX_MOTOROLA.
 * @param[out] out          Cíl, naplní se vyformátovaným textem.
 * @param      out_sz       Velikost @p out.
 */
static void dasm_format(const uint8_t *bytes, size_t n_bytes,
                        z80_ix_style_t style, char *out, int out_sz)
{
    test_ctx_t ctx = { .bytes = bytes, .len = n_bytes };
    z80_dasm_inst_t inst;
    z80_dasm(&inst, test_read, &ctx, 0);

    z80_dasm_format_t fmt;
    z80_dasm_format_default(&fmt);
    fmt.hex_style = Z80_HEX_0X;
    fmt.uppercase = 0;  /* lowercase pro citelnost srovnani */
    fmt.ix_iy_style = style;

    z80_dasm_to_str(out, out_sz, &inst, &fmt);
}


/* =========================================================================
 *  Test 1: Motorola IX positive displacement
 * ========================================================================= */

/**
 * @brief DD 7E 05 = LD A,(IX+5) -> Motorola: "ld a,(5,ix)".
 */
void test_motorola_ix_positive(void)
{
    const uint8_t bytes[] = { 0xDD, 0x7E, 0x05 };
    char out[64];
    dasm_format(bytes, sizeof bytes, Z80_IX_MOTOROLA, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("ld a,(5,ix)", out);
}


/* =========================================================================
 *  Test 2: Motorola IX negative displacement
 * ========================================================================= */

/**
 * @brief DD 7E FE = LD A,(IX-2) -> Motorola: "ld a,(-2,ix)".
 *
 * 0xFE jako int8_t = -2. Test ověří že signed dekadická forma vypisuje
 * mínus a že hexa konverze 0xFE neuteče do unsigned zobrazení.
 */
void test_motorola_ix_negative(void)
{
    const uint8_t bytes[] = { 0xDD, 0x7E, 0xFE };
    char out[64];
    dasm_format(bytes, sizeof bytes, Z80_IX_MOTOROLA, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("ld a,(-2,ix)", out);
}


/* =========================================================================
 *  Test 3: Motorola IX zero displacement
 * ========================================================================= */

/**
 * @brief DD 7E 00 = LD A,(IX+0) -> Motorola: "ld a,(0,ix)".
 *
 * Nula je hraniční případ: žádné znaménko, ale i tak validní displacement
 * (sdas i z toho udělá funkční opcode). Ověříme že se vypíše prostě "0".
 */
void test_motorola_ix_zero(void)
{
    const uint8_t bytes[] = { 0xDD, 0x7E, 0x00 };
    char out[64];
    dasm_format(bytes, sizeof bytes, Z80_IX_MOTOROLA, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("ld a,(0,ix)", out);
}


/* =========================================================================
 *  Test 4: Motorola IY positive displacement
 * ========================================================================= */

/**
 * @brief FD 7E 0A = LD A,(IY+10) -> Motorola: "ld a,(10,iy)".
 *
 * Symetrie s IX cestou - oba indexové registry musí mít identický render.
 */
void test_motorola_iy_positive(void)
{
    const uint8_t bytes[] = { 0xFD, 0x7E, 0x0A };
    char out[64];
    dasm_format(bytes, sizeof bytes, Z80_IX_MOTOROLA, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("ld a,(10,iy)", out);
}


/* =========================================================================
 *  Test 5: Zilog default - backward compatibility
 * ========================================================================= */

/**
 * @brief Regression: ix_iy_style = Z80_IX_ZILOG (default po
 *        z80_dasm_format_default) musí zachovat původní render
 *        "(ix+0x05)". Žádný existující caller se nesmí rozbít.
 */
void test_zilog_ix_default(void)
{
    const uint8_t bytes[] = { 0xDD, 0x7E, 0x05 };
    char out[64];
    dasm_format(bytes, sizeof bytes, Z80_IX_ZILOG, out, sizeof out);
    /* Z80_HEX_0X + lowercase: fmt_hex8 vždy emituje 2 hex cifry,
     * tj. displacement 0x05 -> "(ix+0x05)". */
    TEST_ASSERT_EQUAL_STRING("ld a,(ix+0x05)", out);
}


/* =========================================================================
 *  Test 6: Motorola IX extrémně negativní displacement
 * ========================================================================= */

/**
 * @brief DD 7E 80 = LD A,(IX-128) -> Motorola: "ld a,(-128,ix)".
 *
 * 0x80 jako int8_t = -128 (= minimální možný displacement). Test ověří
 * že snprintf("%d", (int)(int8_t)0x80) skutečně produkuje "-128" a buffer
 * 4 znaků (sign + 3 cifry) se vejde do pomocného bufferu.
 */
void test_motorola_ix_extreme_negative(void)
{
    const uint8_t bytes[] = { 0xDD, 0x7E, 0x80 };
    char out[64];
    dasm_format(bytes, sizeof bytes, Z80_IX_MOTOROLA, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("ld a,(-128,ix)", out);
}


/* =========================================================================
 *  Main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_motorola_ix_positive);
    RUN_TEST(test_motorola_ix_negative);
    RUN_TEST(test_motorola_ix_zero);
    RUN_TEST(test_motorola_iy_positive);
    RUN_TEST(test_zilog_ix_default);
    RUN_TEST(test_motorola_ix_extreme_negative);
    return UNITY_END();
}
