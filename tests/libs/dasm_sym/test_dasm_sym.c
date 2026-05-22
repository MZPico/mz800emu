/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file test_dasm_sym.c
 * @brief Unit testy symbolického resolveru disassembleru Z80.
 *
 * Pokrývá CHECKLIST 3.1 (debugger-v17plus / debugger-fixes-6) - operandy
 * řídicích instrukcí (CALL/JP/JR/JR cc/DJNZ/RST + cond varianty) se musí
 * po průchodu z80_dasm_to_str_sym() s naplněnou symtab nahradit jménem
 * symbolu, pokud na cílové adrese existuje. Pro adresy bez symbolu
 * a pro nepřímé skoky (JP (HL)/(IX)/(IY)) musí výstup zůstat hex.
 *
 * Tento test je regression coverage pro symbol substituci v sám sebe
 * obsažené `mzlib_dasm_z80` knihovně - nezávisí na sym_db ani UI
 * vrstvě (= bridge cache v `dbg_disassembled.cpp` se testuje
 * implicitně tím, že volá tu samou funkci).
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
 * Disassembler čte z `base_addr`, ale buffer je vždy uložen od indexu 0.
 * Callback offsetuje adresu o base_addr - mimo rozsah vrátí 0 (= NOP).
 */
typedef struct {
    uint16_t base_addr;
    const uint8_t *bytes;
    size_t len;
} test_read_ctx_t;


/**
 * @brief Read callback pro z80_dasm() - čte ze zadaného bufferu s base_addr.
 *
 * Adresa addr je porovnána s base_addr; offset = addr - base_addr.
 * Pokud je offset v rozsahu bufferu, vrátí příslušný bajt, jinak 0.
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


/**
 * @brief Setup před každým testem - bez globálního stavu.
 *
 * Symtab i read kontext si každý test vytvoří/zruší lokálně.
 */
void setUp(void)
{
}


/** @brief Teardown po každém testu - prázdný (žádný globální stav). */
void tearDown(void)
{
}


/* =========================================================================
 *  Pomocné: disassembluj instrukci od addr=0 a vrať formátovaný string
 * ========================================================================= */

/**
 * @brief Helper: disassembluj instrukci z opcodů a vrať formátovaný řetězec.
 *
 * Naplní buffer, zavolá z80_dasm(), poté z80_dasm_to_str_sym() s předanou
 * symtab a vrátí délku výstupu. Výstup je default format (lowercase,
 * Z80_HEX_HASH).
 *
 * @param[in]  opcodes    Pole bajtů instrukce.
 * @param[in]  opcode_len Počet bajtů v opcodes.
 * @param[in]  base_addr  Adresa, na které instrukce začíná (důležité pro
 *                        relativní skoky JR/DJNZ - target = base+2+offset).
 * @param[in]  symbols    Tabulka symbolu pro resolve. Může být NULL.
 * @param[out] out        Výstupní buffer (musí mít >= 64 B).
 * @return Délka řetězce nebo záporné při chybě.
 */
static int dasm_with_sym(const uint8_t *opcodes, size_t opcode_len,
                         uint16_t base_addr,
                         const z80_symtab_t *symbols,
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
    fmt.uppercase = 0;  /* lowercase - kompatibilita se zbytkem disasm UI */

    return z80_dasm_to_str_sym(out, 64, &inst, &fmt, symbols);
}


/* =========================================================================
 *  Test: CALL nn s existujícím symbolem
 * ========================================================================= */

/**
 * @brief CALL #00AD s existujícím symbolem "rom_getchar" v tabulce.
 *
 * Opcody: CD AD 00 = CALL #00AD. Po symbol resolve musí výstup obsahovat
 * "rom_getchar" místo "#00AD".
 */
void test_call_with_symbol(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    TEST_ASSERT_NOT_NULL(tab);
    z80_symtab_add(tab, 0x00AD, "rom_getchar");

    char out[64];
    const uint8_t op[] = { 0xCD, 0xAD, 0x00 };
    int len = dasm_with_sym(op, sizeof(op), 0x0000, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    /* Výstup musí obsahovat název symbolu a NEsmí obsahovat hex #00AD */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "rom_getchar"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "#00AD"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "#00ad"), out);

    z80_symtab_destroy(tab);
}


/**
 * @brief CALL #1234 bez symbolu - hex zůstane.
 */
void test_call_without_symbol(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    TEST_ASSERT_NOT_NULL(tab);
    /* Tabulka prázdná - žádný symbol pro 0x1234 */

    char out[64];
    const uint8_t op[] = { 0xCD, 0x34, 0x12 };
    int len = dasm_with_sym(op, sizeof(op), 0x0000, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    /* Bez symbolu zůstává hex #1234 (lowercase režim -> #1234) */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "#1234"), out);

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  Test: JP nn / JP cc, nn
 * ========================================================================= */

/**
 * @brief JP #4000 se symbolem "user_prog".
 *
 * Opcody: C3 00 40 = JP #4000.
 */
void test_jp_with_symbol(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x4000, "user_prog");

    char out[64];
    const uint8_t op[] = { 0xC3, 0x00, 0x40 };
    int len = dasm_with_sym(op, sizeof(op), 0x0000, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "user_prog"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "#4000"), out);

    z80_symtab_destroy(tab);
}


/**
 * @brief JP Z,#1234 se symbolem - condition JP musí také resolvovat.
 *
 * Opcody: CA 34 12 = JP Z,#1234. Operand 1 = condition (Z),
 * operand 2 = IMM16 (#1234) - symtab resolve hledá v obou.
 */
void test_jp_cond_with_symbol(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x1234, "loop_top");

    char out[64];
    const uint8_t op[] = { 0xCA, 0x34, 0x12 };
    int len = dasm_with_sym(op, sizeof(op), 0x0000, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "loop_top"), out);
    /* Condition "z" musí ve výstupu zůstat */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "z"), out);

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  Test: JR e (REL8)
 * ========================================================================= */

/**
 * @brief JR e na cíl se symbolem.
 *
 * Opcody na addr=0x0100: 18 03 = JR $+5 (target = 0x0100+2+3 = 0x0105).
 */
void test_jr_with_symbol(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x0105, "jr_target");

    char out[64];
    const uint8_t op[] = { 0x18, 0x03 };
    int len = dasm_with_sym(op, sizeof(op), 0x0100, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "jr_target"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "#0105"), out);

    z80_symtab_destroy(tab);
}


/**
 * @brief JR NZ,e na cíl se symbolem.
 *
 * Opcody na addr=0x0200: 20 FE = JR NZ,$ (target = 0x0200+2-2 = 0x0200).
 * Self-loop = symbol "self".
 */
void test_jr_cond_with_symbol(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x0200, "self");

    char out[64];
    const uint8_t op[] = { 0x20, 0xFE };
    int len = dasm_with_sym(op, sizeof(op), 0x0200, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "self"), out);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "nz"), out);

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  Test: DJNZ e
 * ========================================================================= */

/**
 * @brief DJNZ e na cíl se symbolem.
 *
 * Opcody na addr=0x0300: 10 FE = DJNZ $ (target = 0x0300+2-2 = 0x0300).
 */
void test_djnz_with_symbol(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x0300, "inner_loop");

    char out[64];
    const uint8_t op[] = { 0x10, 0xFE };
    int len = dasm_with_sym(op, sizeof(op), 0x0300, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "inner_loop"), out);

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  Test: RST p
 * ========================================================================= */

/**
 * @brief RST #20 se symbolem "rst_bdos".
 *
 * Opcody: E7 = RST #20. Cílová adresa = 0x0020.
 */
void test_rst_with_symbol(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x0020, "rst_bdos");

    char out[64];
    const uint8_t op[] = { 0xE7 };
    int len = dasm_with_sym(op, sizeof(op), 0x0000, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "rst_bdos"), out);

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  Test: JP (HL) - nepřímý skok, NESMÍ resolve
 * ========================================================================= */

/**
 * @brief JP (HL) má jako "operand" registr HL, ne adresu.
 *
 * Symbol resolve nesmí mít žádný efekt - target není staticky znám.
 * Současně se nesmí náhodou zaměnit substring "HL" nebo cokoliv jiného.
 *
 * Opcody: E9 = JP (HL).
 */
void test_jp_hl_no_resolve(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    /* Přidáme symboly na pravděpodobné registr-derivované hodnoty - žádný
     * nesmí být vybrán, protože JP (HL) nemá IMM16 ani REL8 operand. */
    z80_symtab_add(tab, 0x0000, "should_not_appear");
    z80_symtab_add(tab, 0xFFFF, "also_not");

    char out[64];
    const uint8_t op[] = { 0xE9 };
    int len = dasm_with_sym(op, sizeof(op), 0x0000, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    /* JP (HL) musí zůstat literální - žádný symbol substituce */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "jp"), out);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "(hl)"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "should_not_appear"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "also_not"), out);

    z80_symtab_destroy(tab);
}


/**
 * @brief JP (IX) - nepřímý skok, NESMÍ resolve.
 *
 * Opcody: DD E9 = JP (IX).
 */
void test_jp_ix_no_resolve(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x0000, "should_not_appear");

    char out[64];
    const uint8_t op[] = { 0xDD, 0xE9 };
    int len = dasm_with_sym(op, sizeof(op), 0x0000, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "(ix)"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "should_not_appear"), out);

    z80_symtab_destroy(tab);
}


/**
 * @brief JP (IY) - nepřímý skok, NESMÍ resolve.
 *
 * Opcody: FD E9 = JP (IY).
 */
void test_jp_iy_no_resolve(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x0000, "should_not_appear");

    char out[64];
    const uint8_t op[] = { 0xFD, 0xE9 };
    int len = dasm_with_sym(op, sizeof(op), 0x0000, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "(iy)"), out);
    TEST_ASSERT_NULL_MESSAGE(strstr(out, "should_not_appear"), out);

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  Test: regrese paměťový operand LD A,(nn)
 * ========================================================================= */

/**
 * @brief LD A,(nn) s mem symbolem - nesmí být zapnut "control-flow only"
 *        omezením 3.1.
 *
 * Acceptance 3.1 explicitně out-of-scope LD A,(nn) - ale lib stále musí
 * pokrývat existující funkčnost (= "žádná regrese v existujících sym
 * lookup voláních"). Tj. když je symbol pro adresu, musí být použit.
 *
 * Opcody: 3A 00 80 = LD A,(#8000). Symbol "vram_a_base" na 0x8000.
 */
void test_ld_mem_with_symbol_regression(void)
{
    z80_symtab_t *tab = z80_symtab_create();
    z80_symtab_add(tab, 0x8000, "vram_a_base");

    char out[64];
    const uint8_t op[] = { 0x3A, 0x00, 0x80 };
    int len = dasm_with_sym(op, sizeof(op), 0x0000, tab, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    /* mem_sym substituce - výstup obsahuje "(vram_a_base)" */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "vram_a_base"), out);

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  Test: NULL symtab - fallback chování
 * ========================================================================= */

/**
 * @brief z80_dasm_to_str_sym() s NULL symtab musí fungovat jako bez resolve.
 *
 * Zaručuje že kód nepadá při chybějícím symtab (= V1.6+ bridge case
 * kdy alokace selže nebo cache ještě nebyl inicializován).
 */
void test_null_symtab_fallback(void)
{
    char out[64];
    const uint8_t op[] = { 0xCD, 0xAD, 0x00 };
    int len = dasm_with_sym(op, sizeof(op), 0x0000, NULL, out);
    TEST_ASSERT_GREATER_THAN(0, len);

    /* Bez symtab zůstává hex */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "#00ad"), out);
}


/* =========================================================================
 *  Unity test runner
 * ========================================================================= */

/**
 * @brief Test runner - registruje všechny testy.
 *
 * Pokrytí 3.1 acceptance:
 *   CALL nn (resolve + no-resolve), JP nn, JP cc, JR e, JR cc, DJNZ, RST,
 *   JP (HL)/(IX)/(IY) filtr, LD A,(nn) regrese, NULL symtab fallback.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_call_with_symbol);
    RUN_TEST(test_call_without_symbol);
    RUN_TEST(test_jp_with_symbol);
    RUN_TEST(test_jp_cond_with_symbol);
    RUN_TEST(test_jr_with_symbol);
    RUN_TEST(test_jr_cond_with_symbol);
    RUN_TEST(test_djnz_with_symbol);
    RUN_TEST(test_rst_with_symbol);
    RUN_TEST(test_jp_hl_no_resolve);
    RUN_TEST(test_jp_ix_no_resolve);
    RUN_TEST(test_jp_iy_no_resolve);
    RUN_TEST(test_ld_mem_with_symbol_regression);
    RUN_TEST(test_null_symtab_fallback);
    return UNITY_END();
}
