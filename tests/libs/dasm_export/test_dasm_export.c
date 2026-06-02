/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file test_dasm_export.c
 * @brief Unit testy auto-label scanneru dasm_export.
 *
 * Pokrývá F2 (auto-label S/L/D/W konvence) + F3 (sym_db per-call
 * gating) z plánu disassembler-window-v1.
 *
 * Test scénáře:
 *
 *  1) Klasifikace JUMP: JP nn vytvoří L<addr> label na cíli
 *  2) Klasifikace SUBROUTINE: CALL nn vytvoří S<addr> label
 *  3) Klasifikace RST: RST n vytvoří S<addr> label
 *  4) Mimo rozsah cíl není označen
 *  5) Mid-instruction cíl → WARN
 *  6) Konflikt JUMP vs SUBROUTINE → vyhrává JUMP (vyšší prioritní)
 *  7) Konflikt WARN nepřepsán nižším typem
 *  8) sym_db gating: symdb=NULL = bez override (auto názvy)
 *  9) sym_db gating: symdb=non-NULL hit = sym_db jméno + from_symdb=true
 * 10) Edge case: sym_db match na mid-instruction → name z sym_db, type=WARN
 *
 * Standalone test - linkuje mzlib_dasm_z80 + dasm_export.c + Unity.
 */

#include "unity.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "libs/dasm-z80/z80_dasm.h"
#include "emulator/debugger/dasm_export.h"


/* =========================================================================
 *  Pomocné: read callback nad lokálním bufferem
 * ========================================================================= */

/**
 * @brief Kontext read callbacku - mapuje base_addr na offset v bufferu.
 *
 * Mimo rozsah vrací 0x00 (= NOP) - testy pak nemusí dimenzovat buffer
 * přesně na rozsah z80_dasm volání (= safe default).
 */
typedef struct {
    uint16_t base_addr;     /**< Adresa, kde začíná @c bytes v paměti. */
    const uint8_t *bytes;   /**< Buffer instrukcí. */
    size_t len;             /**< Velikost @c bytes. */
} test_ctx_t;


/**
 * @brief Read callback pro z80_dasm/collect_labels - čte z bufferu.
 *
 * @param addr      Požadovaná adresa (0x0000-0xFFFF).
 * @param user_data test_ctx_t* s base_addr + bytes + len.
 * @return Bajt na dané adrese nebo 0x00 mimo rozsah bufferu.
 */
static uint8_t test_read(uint16_t addr, void *user_data)
{
    const test_ctx_t *ctx = (const test_ctx_t *)user_data;
    uint16_t off = (uint16_t)(addr - ctx->base_addr);
    if (off < ctx->len) {
        return ctx->bytes[off];
    }
    return 0x00;
}


/**
 * @brief Najde label na zadané adrese v poli; vrátí NULL pokud nenalezen.
 */
static const dasm_label_t *find_label(const dasm_label_t *labels, int n,
                                      uint16_t addr)
{
    for (int i = 0; i < n; i++) {
        if (labels[i].addr == addr) return &labels[i];
    }
    return NULL;
}


/* =========================================================================
 *  Unity setUp/tearDown
 * ========================================================================= */

void setUp(void) {}
void tearDown(void) {}


/* =========================================================================
 *  Test 1: JP nn → L<addr> label
 * ========================================================================= */

/**
 * @brief Rozsah 0x0000-0x000F: JP $0008 / NOP*. Cíl 0x0008 musí dostat
 *        L0008 label.
 */
void test_jp_creates_jump_label(void)
{
    /* 0x0000: C3 08 00      JP 0008
     * 0x0003: 00 ... NOP padding
     * 0x0008: 00 (cíl - NOP) */
    const uint8_t bytes[16] = {
        0xC3, 0x08, 0x00,  /* JP $0008 */
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,              /* target instruction (NOP) */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = 16 };

    dasm_label_t labels[16];
    int n = dasm_export_collect_labels(0x0000, 0x000F, test_read, &ctx,
                                       NULL, NULL,
                                       labels, 16);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX16(0x0008, labels[0].addr);
    TEST_ASSERT_EQUAL(DASM_LABEL_JUMP, labels[0].type);
    TEST_ASSERT_FALSE(labels[0].from_symdb);
    TEST_ASSERT_EQUAL_STRING("L0008", labels[0].name);
}


/* =========================================================================
 *  Test 2: CALL nn → S<addr> label
 * ========================================================================= */

/**
 * @brief CALL $0010 vytvoří S0010 label na cíli.
 */
void test_call_creates_subroutine_label(void)
{
    /* 0x0000: CD 10 00      CALL 0010
     * 0x0003: C9            RET
     * 0x0010: C9            RET (cíl) */
    uint8_t bytes[0x20] = { 0 };
    bytes[0x00] = 0xCD; bytes[0x01] = 0x10; bytes[0x02] = 0x00; /* CALL */
    bytes[0x03] = 0xC9;  /* RET */
    bytes[0x10] = 0xC9;  /* target RET */
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = sizeof bytes };

    dasm_label_t labels[16];
    int n = dasm_export_collect_labels(0x0000, 0x001F, test_read, &ctx,
                                       NULL, NULL,
                                       labels, 16);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX16(0x0010, labels[0].addr);
    TEST_ASSERT_EQUAL(DASM_LABEL_SUBROUTINE, labels[0].type);
    TEST_ASSERT_EQUAL_STRING("S0010", labels[0].name);
}


/* =========================================================================
 *  Test 3: RST n → S<addr> label
 * ========================================================================= */

/**
 * @brief RST 0x28 vytvoří S0028 label na cíli (rst vector).
 */
void test_rst_creates_subroutine_label(void)
{
    /* 0x0000: EF (RST 28h)
     * 0x0028: C9 (RET = cíl) */
    uint8_t bytes[0x30] = { 0 };
    bytes[0x00] = 0xEF;  /* RST 28h */
    bytes[0x28] = 0xC9;  /* RET target */
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = sizeof bytes };

    dasm_label_t labels[16];
    int n = dasm_export_collect_labels(0x0000, 0x002F, test_read, &ctx,
                                       NULL, NULL,
                                       labels, 16);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX16(0x0028, labels[0].addr);
    TEST_ASSERT_EQUAL(DASM_LABEL_SUBROUTINE, labels[0].type);
    TEST_ASSERT_EQUAL_STRING("S0028", labels[0].name);
}


/* =========================================================================
 *  Test 4: cíl mimo rozsah není označen
 * ========================================================================= */

/**
 * @brief Rozsah 0x0000-0x000F obsahuje JP $0100 (mimo). Nesmí vzniknout
 *        žádný label.
 */
void test_target_outside_range_skipped(void)
{
    uint8_t bytes[16] = { 0 };
    bytes[0x00] = 0xC3; bytes[0x01] = 0x00; bytes[0x02] = 0x01; /* JP $0100 */
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = sizeof bytes };

    dasm_label_t labels[16];
    int n = dasm_export_collect_labels(0x0000, 0x000F, test_read, &ctx,
                                       NULL, NULL,
                                       labels, 16);
    TEST_ASSERT_EQUAL_INT(0, n);
}


/* =========================================================================
 *  Test 5: mid-instruction target → WARN
 * ========================================================================= */

/**
 * @brief Vytvoří SMC pattern: JP $0004 ale 0x0003 je instrukce
 *        LD A,$00 (2 bajty 0x0003..0x0004), takže 0x0004 padá
 *        doprostřed instrukce. Cíl musí dostat W0004 label.
 *
 *  0x0000: C3 04 00      JP $0004
 *  0x0003: 3E 00         LD A,$00   (= "obklíčí" 0x0004)
 *  0x0005: ...
 */
void test_mid_instruction_target_is_warn(void)
{
    uint8_t bytes[16] = { 0 };
    bytes[0x00] = 0xC3; bytes[0x01] = 0x04; bytes[0x02] = 0x00;  /* JP $0004 */
    bytes[0x03] = 0x3E; bytes[0x04] = 0x00;                       /* LD A,$00 */
    /* další bajty = 00 (NOP) */
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = sizeof bytes };

    dasm_label_t labels[16];
    int n = dasm_export_collect_labels(0x0000, 0x000F, test_read, &ctx,
                                       NULL, NULL,
                                       labels, 16);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX16(0x0004, labels[0].addr);
    TEST_ASSERT_EQUAL_MESSAGE(DASM_LABEL_WARN, labels[0].type,
                              "Mid-instruction target musí být WARN");
    TEST_ASSERT_EQUAL_STRING("W0004", labels[0].name);
}


/* =========================================================================
 *  Test 6: konflikt SUBROUTINE vs JUMP → vyhrává JUMP
 * ========================================================================= */

/**
 * @brief Stejná cílová adresa CALL i JP → finální klasifikace JUMP.
 *
 *  0x0000: CD 10 00      CALL $0010
 *  0x0003: C3 10 00      JP   $0010
 *  0x0010: C9            RET
 */
void test_conflict_jump_wins_over_subroutine(void)
{
    uint8_t bytes[0x20] = { 0 };
    bytes[0x00] = 0xCD; bytes[0x01] = 0x10; bytes[0x02] = 0x00;  /* CALL */
    bytes[0x03] = 0xC3; bytes[0x04] = 0x10; bytes[0x05] = 0x00;  /* JP   */
    bytes[0x10] = 0xC9;                                            /* RET  */
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = sizeof bytes };

    dasm_label_t labels[16];
    int n = dasm_export_collect_labels(0x0000, 0x001F, test_read, &ctx,
                                       NULL, NULL,
                                       labels, 16);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX16(0x0010, labels[0].addr);
    TEST_ASSERT_EQUAL_MESSAGE(DASM_LABEL_JUMP, labels[0].type,
                              "L > S, JUMP klasifikace má přednost");
    TEST_ASSERT_EQUAL_STRING("L0010", labels[0].name);
}


/* =========================================================================
 *  Test 7: WARN nepřepsán nižším typem
 * ========================================================================= */

/**
 * @brief Mid-instr cíl → WARN. Druhá instrukce na něj skočí JUMP
 *        bezpečnostně, ale WARN musí zůstat (= signál chyby SMC).
 *
 *  0x0000: C3 04 00      JP $0004
 *  0x0003: 3E 00         LD A,$00   (obaluje 0x0004)
 *  0x0005: C3 04 00      JP $0004   (druhý jump na stejný mid-instr cíl)
 */
void test_warn_not_overridden_by_lower_type(void)
{
    uint8_t bytes[16] = { 0 };
    bytes[0x00] = 0xC3; bytes[0x01] = 0x04; bytes[0x02] = 0x00;  /* JP $0004 */
    bytes[0x03] = 0x3E; bytes[0x04] = 0x00;                       /* LD A,$00 */
    bytes[0x05] = 0xC3; bytes[0x06] = 0x04; bytes[0x07] = 0x00;  /* JP $0004 */
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = sizeof bytes };

    dasm_label_t labels[16];
    int n = dasm_export_collect_labels(0x0000, 0x000F, test_read, &ctx,
                                       NULL, NULL,
                                       labels, 16);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX16(0x0004, labels[0].addr);
    TEST_ASSERT_EQUAL_MESSAGE(DASM_LABEL_WARN, labels[0].type,
                              "WARN nesmí být přepsán JUMP/SUBROUTINE/DATA");
}


/* =========================================================================
 *  Test 8: symdb=NULL = bez override
 * ========================================================================= */

/**
 * @brief Per-call gating: symdb=NULL znamená "neaktivovat sym_db lookup",
 *        i kdyby existovala tabulka s match - tady ji prostě nedáváme.
 *        Výsledek = auto-název.
 */
void test_symdb_null_no_override(void)
{
    uint8_t bytes[0x20] = { 0 };
    bytes[0x00] = 0xCD; bytes[0x01] = 0x10; bytes[0x02] = 0x00;  /* CALL */
    bytes[0x10] = 0xC9;                                            /* RET  */
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = sizeof bytes };

    dasm_label_t labels[16];
    int n = dasm_export_collect_labels(0x0000, 0x001F, test_read, &ctx,
                                       /* symdb */ NULL, /* cdl */ NULL,
                                       labels, 16);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_FALSE(labels[0].from_symdb);
    TEST_ASSERT_EQUAL_STRING("S0010", labels[0].name);
}


/* =========================================================================
 *  Test 9: symdb hit přepíše name + nastaví from_symdb
 * ========================================================================= */

/**
 * @brief F3 path: symdb obsahuje "init" na 0x0010. CALL $0010 musí
 *        vytvořit label s name="init", from_symdb=true, type zachová
 *        klasifikaci (SUBROUTINE).
 */
void test_symdb_override_name(void)
{
    uint8_t bytes[0x20] = { 0 };
    bytes[0x00] = 0xCD; bytes[0x01] = 0x10; bytes[0x02] = 0x00;
    bytes[0x10] = 0xC9;
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = sizeof bytes };

    z80_symtab_t *tab = z80_symtab_create();
    TEST_ASSERT_NOT_NULL(tab);
    z80_symtab_add(tab, 0x0010, "init");

    dasm_label_t labels[16];
    int n = dasm_export_collect_labels(0x0000, 0x001F, test_read, &ctx,
                                       tab, NULL,
                                       labels, 16);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_TRUE(labels[0].from_symdb);
    TEST_ASSERT_EQUAL_STRING("init", labels[0].name);
    TEST_ASSERT_EQUAL_MESSAGE(DASM_LABEL_SUBROUTINE, labels[0].type,
                              "Type zachován (CALL → S), sym_db wins jen na name");

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  Test 10: edge case - sym_db match na mid-instruction
 *           name z sym_db, type zůstává WARN
 * ========================================================================= */

/**
 * @brief Sym_db obsahuje "foo" na 0x0004, ale 0x0004 padá doprostřed
 *        instrukce LD A,$00 (mid-instruction). Výsledek musí mít
 *        name="foo" (sym_db wins on name) a type=WARN (= zachovává se
 *        vizuální indikátor chyby).
 *
 *  0x0000: C3 04 00      JP $0004
 *  0x0003: 3E 00         LD A,$00   (obklíčí 0x0004)
 */
void test_symdb_mid_instruction_keeps_warn_type(void)
{
    uint8_t bytes[16] = { 0 };
    bytes[0x00] = 0xC3; bytes[0x01] = 0x04; bytes[0x02] = 0x00;  /* JP $0004 */
    bytes[0x03] = 0x3E; bytes[0x04] = 0x00;                       /* LD A,$00 */
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = sizeof bytes };

    z80_symtab_t *tab = z80_symtab_create();
    TEST_ASSERT_NOT_NULL(tab);
    z80_symtab_add(tab, 0x0004, "foo");

    dasm_label_t labels[16];
    int n = dasm_export_collect_labels(0x0000, 0x000F, test_read, &ctx,
                                       tab, NULL,
                                       labels, 16);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX16(0x0004, labels[0].addr);
    TEST_ASSERT_TRUE_MESSAGE(labels[0].from_symdb,
                             "sym_db wins on name");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("foo", labels[0].name,
                                     "name z sym_db");
    TEST_ASSERT_EQUAL_MESSAGE(DASM_LABEL_WARN, labels[0].type,
                              "Type zůstává WARN i přes sym_db match");

    z80_symtab_destroy(tab);
}


/* =========================================================================
 *  Test 11: prázdný rozsah (from > to) vrátí 0
 * ========================================================================= */

/**
 * @brief Sanity: from > to → 0 labelů, žádná chyba.
 */
void test_empty_range_returns_zero(void)
{
    uint8_t bytes[1] = { 0 };
    test_ctx_t ctx = { .base_addr = 0x1000, .bytes = bytes, .len = 1 };

    dasm_label_t labels[8];
    int n = dasm_export_collect_labels(0x1000, 0x0FFF, test_read, &ctx,
                                       NULL, NULL,
                                       labels, 8);
    TEST_ASSERT_EQUAL_INT(0, n);
}


/* =========================================================================
 *  Test 12: NULL guards
 * ========================================================================= */

/**
 * @brief NULL read_fn nebo NULL out nebo max_labels<=0 → -1.
 */
void test_null_arguments_return_error(void)
{
    dasm_label_t labels[1];

    int n = dasm_export_collect_labels(0, 0, NULL, NULL, NULL, NULL,
                                       labels, 1);
    TEST_ASSERT_EQUAL_INT(-1, n);

    n = dasm_export_collect_labels(0, 0, test_read, NULL, NULL, NULL,
                                   NULL, 1);
    TEST_ASSERT_EQUAL_INT(-1, n);

    n = dasm_export_collect_labels(0, 0, test_read, NULL, NULL, NULL,
                                   labels, 0);
    TEST_ASSERT_EQUAL_INT(-1, n);
}


/* =========================================================================
 *  F4: dasm_export_write() - pasmo dialekt
 * ========================================================================= */

/**
 * @brief Pomocné: načte celý obsah souboru do bufferu (NUL-terminated).
 *
 * @param path      Cesta.
 * @param[out] buf  Cíl.
 * @param          bufsz Velikost @p buf.
 * @return Počet načtených bajtů (bez null terminátoru), nebo -1 při IO chybě.
 */
static long read_file_to_buf(const char *path, char *buf, size_t bufsz)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    size_t n = fread(buf, 1, bufsz - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return (long)n;
}


/**
 * @brief Default opts pro pasmo testy.
 */
static dasm_export_opts_t default_pasmo_opts(void)
{
    dasm_export_opts_t o;
    o.dialect               = DASM_DIALECT_PASMO;
    o.include_org           = true;
    o.include_bytes_comment = true;
    o.generate_equ_for_syms = false;
    o.uppercase_mnemonics   = true;
    return o;
}


/**
 * @brief Header obsahuje "Disassembled by", "Range:" a "Dialect: pasmo".
 */
void test_export_write_pasmo_header(void)
{
    /* Triviální 1-bajtový rozsah s NOP. */
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 1 };

    const char *path = "test_pasmo_header.asm";
    dasm_export_opts_t opts = default_pasmo_opts();
    int rc = dasm_export_write(path, 0xC000, 0xC000, test_read, &ctx,
                               NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    long n = read_file_to_buf(path, buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Disassembled by"),
                                 "Header musí obsahovat 'Disassembled by'");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Range:"),
                                 "Header musí obsahovat 'Range:'");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Dialect: pasmo"),
                                 "Header musí obsahovat 'Dialect: pasmo'");
    /* Komentář char ';' na začátku header řádky. */
    TEST_ASSERT_EQUAL_CHAR(';', buf[0]);

    remove(path);
}


/**
 * @brief ORG direktiva: pro from=0xC000 musí být "ORG 0C000h" (H suffix
 *        + leading 0 protože C >= A).
 */
void test_export_write_pasmo_org(void)
{
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 1 };

    const char *path = "test_pasmo_org.asm";
    dasm_export_opts_t opts = default_pasmo_opts();
    int rc = dasm_export_write(path, 0xC000, 0xC000, test_read, &ctx,
                               NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "ORG 0C000h"),
        "Pasmo: ORG s leading 0 (C >= A) a H suffix");

    remove(path);
}


/**
 * @brief Label emit: caller-supplied label "Sc020" na adrese 0xC020
 *        musí se objevit v souboru jako "Sc020:".
 */
void test_export_write_pasmo_label_emit(void)
{
    /* 0xC020: 00 (NOP) - cíl labelu */
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC020, .bytes = bytes, .len = 1 };

    dasm_label_t labels[1];
    memset(labels, 0, sizeof labels);  /* F8: explicit zero-init kvůli novým polím */
    labels[0].addr = 0xC020;
    strcpy(labels[0].name, "Sc020");
    labels[0].type = DASM_LABEL_SUBROUTINE;
    labels[0].from_symdb = false;

    const char *path = "test_pasmo_label.asm";
    dasm_export_opts_t opts = default_pasmo_opts();
    int rc = dasm_export_write(path, 0xC020, 0xC020, test_read, &ctx,
                               labels, 1, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Sc020:"),
        "Label 'Sc020:' musí být emitován v body");

    remove(path);
}


/**
 * @brief Mnemonic substituce: JP 4020h musí být přepsán na JP S4020
 *        když label table obsahuje S4020 na 0x4020.
 *
 * Pozn.: úmyslně používáme adresu < 0xA000 aby nedošlo ke kolizi s
 * "leading 0 pro A-F" pravidlem v engine substituci (engine bug:
 * format word emituje "0C020H" ale substituce hledá "C020H" bez
 * leading 0 → substituce sice najde substring, ale výsledek je
 * "0Lc020". Tento bug NENÍ scope F4 - viz report.).
 */
void test_export_write_pasmo_mnemonic_substitute(void)
{
    /* 0x4000: C3 20 40     JP 4020h
     * 0x4003: 00...        NOP padding
     * 0x4020: 00           NOP (cíl) */
    uint8_t bytes[0x21] = { 0 };
    bytes[0x00] = 0xC3; bytes[0x01] = 0x20; bytes[0x02] = 0x40;
    test_ctx_t ctx = { .base_addr = 0x4000, .bytes = bytes, .len = sizeof bytes };

    dasm_label_t labels[1];
    memset(labels, 0, sizeof labels);  /* F8: explicit zero-init kvůli novým polím */
    labels[0].addr = 0x4020;
    strcpy(labels[0].name, "S4020");
    labels[0].type = DASM_LABEL_JUMP;
    labels[0].from_symdb = false;

    const char *path = "test_pasmo_mnem.asm";
    dasm_export_opts_t opts = default_pasmo_opts();
    /* Vypneme bytes komentář aby v bufferu nezůstal raw "C3 20 40"
     * který by mohl falešně matchnout. */
    opts.include_bytes_comment = false;
    int rc = dasm_export_write(path, 0x4000, 0x4020, test_read, &ctx,
                               labels, 1, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[4096];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));

    /* Mnemonic obsahuje "S4020" (label substituce). */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "S4020"),
        "Mnemonic musí obsahovat 'S4020' (sym substituce v JP)");
    /* Mnemonic NESMÍ obsahovat raw "4020H" jako celé slovo (= nepodařila
     * by se substituce). Hledáme "JP 4020H" který by indikoval ztracenou
     * substituci. */
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "JP 4020H"),
        "JP 4020H nesmí zůstat - musí být 'JP S4020'");

    remove(path);
}


/* =========================================================================
 *  F5: dasm_export_write() - sjasmplus dialekt
 * ========================================================================= */

/**
 * @brief Default opts pro sjasmplus testy.
 *
 * Identické s pasmo, jen @c dialect = SJASMPLUS.
 */
static dasm_export_opts_t default_sjasmplus_opts(void)
{
    dasm_export_opts_t o;
    o.dialect               = DASM_DIALECT_SJASMPLUS;
    o.include_org           = true;
    o.include_bytes_comment = true;
    o.generate_equ_for_syms = false;
    o.uppercase_mnemonics   = true;
    return o;
}


/**
 * @brief Header obsahuje "Dialect: sjasmplus" a range v $ stylu.
 */
void test_export_write_sjasmplus_header(void)
{
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 1 };

    const char *path = "test_sjasmplus_header.asm";
    dasm_export_opts_t opts = default_sjasmplus_opts();
    int rc = dasm_export_write(path, 0xC000, 0xC000, test_read, &ctx,
                               NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    long n = read_file_to_buf(path, buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Dialect: sjasmplus"),
                                 "Header musí obsahovat 'Dialect: sjasmplus'");
    /* Range v $ hex stylu (NE 0Ch suffix). */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "$C000"),
                                 "Range musí obsahovat '$C000' (sjasmplus hex styl)");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "0C000h"),
        "Sjasmplus header NESMÍ obsahovat pasmo styl '0C000h'");

    remove(path);
}


/**
 * @brief ORG direktiva: pro from=0xC000 musí být "ORG $C000" (dolar prefix).
 */
void test_export_write_sjasmplus_org(void)
{
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 1 };

    const char *path = "test_sjasmplus_org.asm";
    dasm_export_opts_t opts = default_sjasmplus_opts();
    int rc = dasm_export_write(path, 0xC000, 0xC000, test_read, &ctx,
                               NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "ORG $C000"),
        "Sjasmplus: ORG s $ prefix a 4-digit hex");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "ORG 0C000h"),
        "Sjasmplus ORG NESMÍ obsahovat pasmo styl '0C000h'");

    remove(path);
}


/**
 * @brief Mnemonic hex literal používá $ prefix, ne H suffix.
 *
 * Zpracujeme "LD SP,$D000" (31 00 D0). V sjasmplus dialektu se v
 * mnemonice musí objevit "$D000" (engine hex_style = Z80_HEX_DOLLAR
 * skrz dialect_spec_t.z80_dasm_hex_style).
 */
void test_export_write_sjasmplus_mnemonic_dollar_hex(void)
{
    /* 0xC000: 31 00 D0     LD SP,$D000 */
    uint8_t bytes[3] = { 0x31, 0x00, 0xD0 };
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 3 };

    const char *path = "test_sjasmplus_mnem.asm";
    dasm_export_opts_t opts = default_sjasmplus_opts();
    opts.include_bytes_comment = false;  /* ať raw bytes v komentáři
                                           nematchují falešně */
    int rc = dasm_export_write(path, 0xC000, 0xC002, test_read, &ctx,
                               NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "$D000"),
        "Mnemonic LD SP,nn musí mít hex literál ve formátu $D000");
    /* H suffix nesmí nikde v mnemonicích zůstat. */
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "D000H"),
        "Sjasmplus mnemonic NESMÍ obsahovat H suffix");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "0D000h"),
        "Sjasmplus mnemonic NESMÍ obsahovat pasmo '0D000h'");

    remove(path);
}


/**
 * @brief Regrese: label substituce funguje nezávisle na dialektu.
 *
 * JP 4020h (resp. JP $4020) se substituuje na "JP S4020" když caller
 * předá label table. Test pokrývá že přepnutí hex_style nerozbilo
 * symbolovou substituci v engine (z80_dasm_to_str_sym).
 */
void test_export_write_sjasmplus_label_subst(void)
{
    /* 0x4000: C3 20 40     JP 4020h
     * 0x4020: 00           NOP (cíl) */
    uint8_t bytes[0x21] = { 0 };
    bytes[0x00] = 0xC3; bytes[0x01] = 0x20; bytes[0x02] = 0x40;
    test_ctx_t ctx = { .base_addr = 0x4000, .bytes = bytes, .len = sizeof bytes };

    dasm_label_t labels[1];
    memset(labels, 0, sizeof labels);  /* F8: explicit zero-init kvůli novým polím */
    labels[0].addr = 0x4020;
    strcpy(labels[0].name, "S4020");
    labels[0].type = DASM_LABEL_JUMP;
    labels[0].from_symdb = false;

    const char *path = "test_sjasmplus_label.asm";
    dasm_export_opts_t opts = default_sjasmplus_opts();
    opts.include_bytes_comment = false;
    int rc = dasm_export_write(path, 0x4000, 0x4020, test_read, &ctx,
                               labels, 1, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[4096];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "S4020"),
        "Mnemonic musí obsahovat 'S4020' (sym substituce v JP)");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "JP $4020"),
        "JP $4020 nesmí zůstat - musí být substituováno na 'JP S4020'");

    remove(path);
}


/**
 * @brief fopen failure: cesta do neexistujícího adresáře vrátí
 *        @c DASM_EXPORT_ERR_OPEN_FAIL (F7 enum, dříve -1).
 */
void test_export_write_open_fail(void)
{
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = 1 };

    /* Adresář s vysokou pravděpodobností neexistuje na CI/dev. */
    const char *bad_path =
        "no_such_dir_xyzzy_12345/sub/disasm.asm";
    dasm_export_opts_t opts = default_pasmo_opts();
    int rc = dasm_export_write(bad_path, 0x0000, 0x0000,
                               test_read, &ctx, NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT_MESSAGE(DASM_EXPORT_ERR_OPEN_FAIL, rc,
        "fopen na neexistující directory musí vrátit DASM_EXPORT_ERR_OPEN_FAIL");
}


/* =========================================================================
 *  F6: dasm_export_write() - sdcc-asz80 dialekt
 * ========================================================================= */

/**
 * @brief Default opts pro sdcc-asz80 testy.
 */
static dasm_export_opts_t default_sdcc_opts(void)
{
    dasm_export_opts_t o;
    o.dialect               = DASM_DIALECT_SDCC_ASZ80;
    o.include_org           = true;
    o.include_bytes_comment = false;
    o.generate_equ_for_syms = false;
    o.uppercase_mnemonics   = false;  /* sdas konvence = lowercase */
    return o;
}


/**
 * @brief Header obsahuje "Dialect: sdcc-asz80".
 */
void test_export_write_sdcc_header(void)
{
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 1 };

    const char *path = "test_sdcc_header.s";
    dasm_export_opts_t opts = default_sdcc_opts();
    int rc = dasm_export_write(path, 0xC000, 0xC000, test_read, &ctx,
                               NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    long n = read_file_to_buf(path, buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Dialect: sdcc-asz80"),
        "Header musí obsahovat 'Dialect: sdcc-asz80'");

    remove(path);
}


/**
 * @brief ORG direktiva sdcc: tečková notace + 0x prefix.
 *
 * Pro from=0xC000 musí být ".org 0xC000" - NE "ORG", NE "0C000h".
 */
void test_export_write_sdcc_org_dotnotation(void)
{
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 1 };

    const char *path = "test_sdcc_org.s";
    dasm_export_opts_t opts = default_sdcc_opts();
    int rc = dasm_export_write(path, 0xC000, 0xC000, test_read, &ctx,
                               NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, ".org 0xC000"),
        "Sdcc: '.org 0xC000' s tečkovou notací + 0x prefix");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "ORG "),
        "Sdcc soubor nesmí obsahovat 'ORG ' (= pasmo styl)");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "0C000h"),
        "Sdcc soubor nesmí obsahovat '0C000h' (= pasmo H suffix)");

    remove(path);
}


/**
 * @brief Label sdcc terminator: double-colon "::" pro globální export.
 */
void test_export_write_sdcc_label_double_colon(void)
{
    /* 0xC020: 00 (NOP) - cíl labelu */
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC020, .bytes = bytes, .len = 1 };

    dasm_label_t labels[1];
    memset(labels, 0, sizeof labels);  /* F8: explicit zero-init kvůli novým polím */
    labels[0].addr = 0xC020;
    strcpy(labels[0].name, "Sc020");
    labels[0].type = DASM_LABEL_SUBROUTINE;
    labels[0].from_symdb = false;

    const char *path = "test_sdcc_label.s";
    dasm_export_opts_t opts = default_sdcc_opts();
    int rc = dasm_export_write(path, 0xC020, 0xC020, test_read, &ctx,
                               labels, 1, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Sc020::"),
        "Sdcc label terminator je '::' pro globální export");

    remove(path);
}


/**
 * @brief Motorola IX render v sdcc exportu.
 *
 * Bytes DD 7E 05 = "LD A,(IX+5)" -> v sdcc dialektu má být "(5,ix)"
 * (signed decimal displacement, Motorola syntax). Test ověří že
 * dasm_export_write správně propaguje ix_iy_style z dialect spec
 * do z80_dasm_format_t (= byla to F6 změna v render loopu).
 */
void test_export_write_sdcc_motorola_ix(void)
{
    /* 0xC000: DD 7E 05  LD A,(IX+5)
     * 0xC003: 00        NOP padding */
    uint8_t bytes[4] = { 0xDD, 0x7E, 0x05, 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 4 };

    const char *path = "test_sdcc_motorola.s";
    dasm_export_opts_t opts = default_sdcc_opts();
    opts.include_org = false;  /* méně šumu v bufferu */
    int rc = dasm_export_write(path, 0xC000, 0xC003, test_read, &ctx,
                               NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[4096];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "(5,ix)"),
        "Sdcc dialect musí emitovat Motorola IX '(5,ix)'");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "(ix+"),
        "Sdcc dialect nesmí emitovat Zilog styl '(ix+...)'");

    remove(path);
}


/**
 * @brief Regression: bytes komentář s opcode neobsahuje DB/DW pasmo
 *        keywords (= obecná sanity že per-dialekt setup nesletěl).
 *
 * V1 F4/F6 EQU sekci negeneruje, ale do bytes komentáře žádné DB/DW
 * patřit nesmí - viz default opts.include_bytes_comment chování.
 */
void test_export_write_sdcc_byte_dw_keywords(void)
{
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 1 };

    const char *path = "test_sdcc_byte_dw.s";
    dasm_export_opts_t opts = default_sdcc_opts();
    opts.include_bytes_comment = true;  /* zapneme bytes komentář */
    int rc = dasm_export_write(path, 0xC000, 0xC000, test_read, &ctx,
                               NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    /* Pokud někde v body máme 'DB ' jako klíčové slovo (= pasmo style),
     * znamená to že per-dialekt setup nesletěl. V1 negeneruje data
     * direktivy ale defenzivně testujeme regression. */
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, " DB "),
        "Sdcc dialect nesmí emitovat ' DB ' klíč (= pasmo)");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, " DW "),
        "Sdcc dialect nesmí emitovat ' DW ' klíč (= pasmo)");

    remove(path);
}


/**
 * @brief Empty / invertovaný range (from > to): konvence V1 F4 = vrátit -1
 *        (NEvytvořit prázdný soubor).
 *
 * Rozhodnutí F4: vrátit -1 protože caller by nečekal soubor pro
 * inverted range. Doxygen @ref dasm_export_write říká
 * "from <= to (jinak return -1, prázdný/invertovaný rozsah)".
 */
void test_export_write_empty_range(void)
{
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = 1 };

    const char *path = "test_pasmo_empty.asm";
    /* Cleanup před testem (kdyby zbyl z předchozího běhu). */
    remove(path);

    dasm_export_opts_t opts = default_pasmo_opts();
    int rc = dasm_export_write(path, 0x0100, 0x00FF,  /* from > to */
                               test_read, &ctx, NULL, 0, &opts);
    TEST_ASSERT_EQUAL_INT_MESSAGE(DASM_EXPORT_ERR_INVALID_RANGE, rc,
        "Invertovaný rozsah from > to musí vrátit DASM_EXPORT_ERR_INVALID_RANGE (F7)");

    /* Soubor se nesmí vytvořit. */
    FILE *fp = fopen(path, "rb");
    TEST_ASSERT_NULL_MESSAGE(fp,
        "Při invertovaném rozsahu soubor nesmí vzniknout");
    if (fp) { fclose(fp); remove(path); }
}


/* =========================================================================
 *  F7: dasm_export_to_string() - clipboard cesta + error enum
 * ========================================================================= */

/**
 * @brief Smoke test: dasm_export_to_string vrátí non-empty buffer s
 *        header komentářem pro pasmo dialekt.
 *
 * Sdílí render core s @c dasm_export_write, takže detailní per-řádek
 * testy duplikovat netřeba (= write testy implicitně pokrývají i
 * to_string). Tento test ověřuje sink wrapper sám.
 */
void test_export_to_string_pasmo_smoke(void)
{
    uint8_t bytes[3] = { 0x3E, 0x42, 0x00 };  /* LD A,#42 ; NOP */
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 3 };

    dasm_export_opts_t opts = default_pasmo_opts();
    char *out = NULL;
    int rc = dasm_export_to_string(0xC000, 0xC002,
                                   test_read, &ctx,
                                   NULL, 0, &opts, &out);
    TEST_ASSERT_EQUAL_INT_MESSAGE(DASM_EXPORT_OK, rc,
        "to_string musí vrátit OK pro validní pasmo opts");
    TEST_ASSERT_NOT_NULL_MESSAGE(out,
        "out_str musí být non-NULL při OK návratu");
    TEST_ASSERT_NOT_NULL(strstr(out, "Disassembled by mz800emu"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Dialect: pasmo"));
    free(out);
}


/**
 * @brief Invalid range pro to_string nastaví out=NULL a vrátí
 *        DASM_EXPORT_ERR_INVALID_RANGE.
 */
void test_export_to_string_invalid_range(void)
{
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = 1 };

    dasm_export_opts_t opts = default_pasmo_opts();
    char *out = (char *)0xDEADBEEF;  /* sentinel - musí být přepsáno NULL */
    int rc = dasm_export_to_string(0x0100, 0x00FF,  /* from > to */
                                   test_read, &ctx,
                                   NULL, 0, &opts, &out);
    TEST_ASSERT_EQUAL_INT_MESSAGE(DASM_EXPORT_ERR_INVALID_RANGE, rc,
        "Invertovaný rozsah from > to musí vrátit ERR_INVALID_RANGE");
    TEST_ASSERT_NULL_MESSAGE(out,
        "out_str musí být NULL při chybě (vlastní cleanup)");
}


/**
 * @brief NULL out_str parametr → ERR_INVALID_RANGE bez crash.
 */
void test_export_to_string_null_out(void)
{
    uint8_t bytes[1] = { 0x00 };
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = 1 };

    dasm_export_opts_t opts = default_pasmo_opts();
    int rc = dasm_export_to_string(0x0000, 0x0000,
                                   test_read, &ctx,
                                   NULL, 0, &opts, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(DASM_EXPORT_ERR_INVALID_RANGE, rc,
        "NULL out_str musí vrátit ERR_INVALID_RANGE");
}


/* =========================================================================
 *  F8: CDL/mhmap data zone detection + DB rendering
 * ========================================================================= */

/**
 * @brief Pomocný builder mhmap_view_t z bytewise X/R/W flag stringů.
 *
 * Každý ze stringů má délku @c (to - from + 1) a obsahuje pouze
 * '0' a '1'. Vrácený view se musí uvolnit @c free_test_cdl_view.
 *
 * @param from Rozsah start.
 * @param to   Rozsah end (inclusive).
 * @param x_pat Pattern execute flagů ('0'/'1' string).
 * @param r_pat Pattern read flagů.
 * @param w_pat Pattern write flagů.
 * @return Vlastněný view (caller je zodpovědný za free).
 */
static mhmap_view_t *make_test_cdl_view(uint16_t from, uint16_t to,
                                        const char *x_pat,
                                        const char *r_pat,
                                        const char *w_pat)
{
    const size_t span = (size_t)(to - from) + 1u;
    mhmap_view_t *v = (mhmap_view_t *)calloc(1, sizeof *v);
    v->from       = from;
    v->to         = to;
    v->exec_flag  = (uint8_t *)calloc(span, 1);
    v->read_flag  = (uint8_t *)calloc(span, 1);
    v->write_flag = (uint8_t *)calloc(span, 1);
    for (size_t i = 0; i < span; i++) {
        if (x_pat && x_pat[i] && x_pat[i] != '0') v->exec_flag[i]  = 1;
        if (r_pat && r_pat[i] && r_pat[i] != '0') v->read_flag[i]  = 1;
        if (w_pat && w_pat[i] && w_pat[i] != '0') v->write_flag[i] = 1;
    }
    return v;
}


/**
 * @brief Uvolní view vytvořený @ref make_test_cdl_view.
 */
static void free_test_cdl_view(mhmap_view_t *v)
{
    if (!v) return;
    free(v->exec_flag);
    free(v->read_flag);
    free(v->write_flag);
    free(v);
}


/**
 * @brief cdl=NULL ⇒ žádné data zóny v collect_labels output.
 *
 * I když rozsah obsahuje JP target, Pass 3 se vůbec nespustí pro NULL cdl
 * a žádný label nemá is_data_zone=true.
 */
void test_collect_labels_cdl_null_no_data_zone(void)
{
    /* JP $0008 + padding NOP, žádný čistě-data rozsah. */
    uint8_t bytes[16] = {
        0xC3, 0x08, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    test_ctx_t ctx = { .base_addr = 0x0000, .bytes = bytes, .len = 16 };

    dasm_label_t labels[16];
    memset(labels, 0, sizeof labels);
    int n = dasm_export_collect_labels(0x0000, 0x000F, test_read, &ctx,
                                       NULL, /* cdl */ NULL,
                                       labels, 16);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, n);  /* aspoň L0008 z JP */
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_FALSE_MESSAGE(labels[i].is_data_zone,
            "cdl=NULL nesmí produkovat data zone labely");
    }
}


/**
 * @brief Pass 3 detekuje souvislou data zónu při X=0, R>0.
 *
 * Range 0xC100-0xC110: všechny X=0, R=1 ⇒ jedna data zóna pokrývající
 * celý rozsah, label "Dc100" + is_data_zone=true + data_zone_end=0xC110.
 */
void test_collect_labels_cdl_data_zone_detection(void)
{
    /* 17 bajtů (C100..C110 inclusive). Buffer plný 00 = NOP, ale cdl
     * říká "čteno, neexekutováno" → celé jako data zóna. */
    uint8_t bytes[0x11] = { 0 };
    test_ctx_t ctx = { .base_addr = 0xC100, .bytes = bytes, .len = sizeof bytes };

    /* 17 znaků R=1, X=0, W=0. */
    const char *xs = "00000000000000000";
    const char *rs = "11111111111111111";
    const char *ws = "00000000000000000";
    mhmap_view_t *view = make_test_cdl_view(0xC100, 0xC110, xs, rs, ws);

    dasm_label_t labels[16];
    memset(labels, 0, sizeof labels);
    int n = dasm_export_collect_labels(0xC100, 0xC110, test_read, &ctx,
                                       NULL, view,
                                       labels, 16);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, n);

    /* Najdi data-zone label na 0xC100. */
    const dasm_label_t *dz = NULL;
    for (int i = 0; i < n; i++) {
        if (labels[i].is_data_zone && labels[i].addr == 0xC100) {
            dz = &labels[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(dz, "Data zone label na 0xC100 musí existovat");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0xC110, dz->data_zone_end,
        "data_zone_end musí pokrývat celý sledovaný rozsah");
    TEST_ASSERT_EQUAL_STRING("Dc100", dz->name);

    free_test_cdl_view(view);
}


/**
 * @brief Pass 3 produkuje 2 zóny když je uprostřed kódová "díra".
 *
 * Range 0xC100..0xC110 (17 bajtů):
 *   - 0xC100..0xC108 = data (R=1, X=0)
 *   - 0xC109         = kód   (X=1)
 *   - 0xC10A..0xC110 = data (R=1, X=0)
 */
void test_collect_labels_cdl_data_zone_with_holes(void)
{
    uint8_t bytes[0x11] = { 0 };
    test_ctx_t ctx = { .base_addr = 0xC100, .bytes = bytes, .len = sizeof bytes };

    /*           "0123456789ABCDEFG" pozice v rangi (17 znaků)
     *  bajt:    C100             C110 */
    const char *xs = "00000000010000000";  /* X=1 jen na C109 */
    const char *rs = "11111111101111111";  /* R=1 jinde než C109 */
    const char *ws = "00000000000000000";
    mhmap_view_t *view = make_test_cdl_view(0xC100, 0xC110, xs, rs, ws);

    dasm_label_t labels[16];
    memset(labels, 0, sizeof labels);
    int n = dasm_export_collect_labels(0xC100, 0xC110, test_read, &ctx,
                                       NULL, view,
                                       labels, 16);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(2, n);

    const dasm_label_t *z1 = NULL;
    const dasm_label_t *z2 = NULL;
    for (int i = 0; i < n; i++) {
        if (!labels[i].is_data_zone) continue;
        if (labels[i].addr == 0xC100) z1 = &labels[i];
        else if (labels[i].addr == 0xC10A) z2 = &labels[i];
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(z1, "První data zone musí začínat na C100");
    TEST_ASSERT_NOT_NULL_MESSAGE(z2, "Druhá data zone musí začínat na C10A");
    TEST_ASSERT_EQUAL_HEX16(0xC108, z1->data_zone_end);
    TEST_ASSERT_EQUAL_HEX16(0xC110, z2->data_zone_end);

    free_test_cdl_view(view);
}


/**
 * @brief Render data zóny: pasmo dialect emituje DB se správným hex stylem.
 *
 * Bytes 0xC100..0xC102 = AA, BB, CC (R=1, X=0). Výstupní soubor musí
 * obsahovat "Dc100:" label + řádek "DB 0AAh,0BBh,0CCh" (pasmo leading-0
 * pro hex >= 0xA).
 */
void test_render_data_zone_pasmo_db(void)
{
    uint8_t bytes[3] = { 0xAA, 0xBB, 0xCC };
    test_ctx_t ctx = { .base_addr = 0xC100, .bytes = bytes, .len = 3 };

    /* Vyrobíme data-zone label ručně (= nemíchejme s collect_labels). */
    dasm_label_t labels[1];
    memset(labels, 0, sizeof labels);
    labels[0].addr = 0xC100;
    strcpy(labels[0].name, "Dc100");
    labels[0].type = DASM_LABEL_DATA;
    labels[0].from_symdb = false;
    labels[0].is_data_zone = true;
    labels[0].data_zone_end = 0xC102;

    const char *path = "test_pasmo_dz.asm";
    dasm_export_opts_t opts = default_pasmo_opts();
    opts.include_bytes_comment = false;  /* drž buf clean */
    int rc = dasm_export_write(path, 0xC100, 0xC102, test_read, &ctx,
                               labels, 1, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Dc100:"),
        "Data zone label header musí být v výstupu");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "DB 0AAh,0BBh,0CCh"),
        "Pasmo DB řádek s leading-0 hex pro A-F nibbles");
    /* Mnemonic z80_dasm pro tyhle bajty NESMÍ být v buf (DB short-circuit). */
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "LD HL"),
        "Data zóna NESMÍ být disasmblovaná jako kód (0xAA = nějaká instrukce)");

    remove(path);
}


/**
 * @brief Render data zóny: sjasmplus dialect emituje DB se $ hex stylem.
 */
void test_render_data_zone_sjasmplus_db(void)
{
    uint8_t bytes[3] = { 0x01, 0x02, 0xFE };
    test_ctx_t ctx = { .base_addr = 0xC100, .bytes = bytes, .len = 3 };

    dasm_label_t labels[1];
    memset(labels, 0, sizeof labels);
    labels[0].addr = 0xC100;
    strcpy(labels[0].name, "Dc100");
    labels[0].type = DASM_LABEL_DATA;
    labels[0].is_data_zone = true;
    labels[0].data_zone_end = 0xC102;

    const char *path = "test_sjasmplus_dz.asm";
    dasm_export_opts_t opts = default_sjasmplus_opts();
    opts.include_bytes_comment = false;
    int rc = dasm_export_write(path, 0xC100, 0xC102, test_read, &ctx,
                               labels, 1, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "DB $01,$02,$FE"),
        "Sjasmplus DB řádek s $ hex prefixem");

    remove(path);
}


/**
 * @brief Render data zóny: sdcc-asz80 dialect emituje .byte s 0x prefixem.
 */
void test_render_data_zone_sdcc_byte(void)
{
    uint8_t bytes[2] = { 0xDE, 0xAD };
    test_ctx_t ctx = { .base_addr = 0xC100, .bytes = bytes, .len = 2 };

    dasm_label_t labels[1];
    memset(labels, 0, sizeof labels);
    labels[0].addr = 0xC100;
    strcpy(labels[0].name, "Dc100");
    labels[0].type = DASM_LABEL_DATA;
    labels[0].is_data_zone = true;
    labels[0].data_zone_end = 0xC101;

    const char *path = "test_sdcc_dz.s";
    dasm_export_opts_t opts = default_sdcc_opts();
    opts.include_bytes_comment = false;
    int rc = dasm_export_write(path, 0xC100, 0xC101, test_read, &ctx,
                               labels, 1, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, ".byte 0xDE,0xAD"),
        "Sdcc .byte řádek s 0x hex prefixem");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Dc100::"),
        "Sdcc label terminator '::' (global export) i pro data zónu");

    remove(path);
}


/**
 * @brief Render: data zóna ohraničená kódem na obou stranách.
 *
 * 0xC000..0xC005:
 *   - 0xC000: NOP (kód)
 *   - 0xC001..0xC003: data zóna
 *   - 0xC004: NOP (kód)
 *   - 0xC005: NOP
 *
 * Výstup musí obsahovat oba kódové NOP řádky + DB sekci uprostřed.
 */
void test_render_data_zone_surrounded_by_code(void)
{
    uint8_t bytes[6] = { 0x00, 0xAA, 0xBB, 0xCC, 0x00, 0x00 };
    test_ctx_t ctx = { .base_addr = 0xC000, .bytes = bytes, .len = 6 };

    dasm_label_t labels[1];
    memset(labels, 0, sizeof labels);
    labels[0].addr = 0xC001;
    strcpy(labels[0].name, "Dc001");
    labels[0].type = DASM_LABEL_DATA;
    labels[0].is_data_zone = true;
    labels[0].data_zone_end = 0xC003;

    const char *path = "test_pasmo_dz_sandwich.asm";
    dasm_export_opts_t opts = default_pasmo_opts();
    opts.include_bytes_comment = false;
    int rc = dasm_export_write(path, 0xC000, 0xC005, test_read, &ctx,
                               labels, 1, &opts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    char buf[2048];
    TEST_ASSERT_GREATER_THAN(0, read_file_to_buf(path, buf, sizeof buf));
    /* Tři NOPy: jeden před zónou, dva za. */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "Dc001:"),
        "Data zone label uprostřed listingu");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "DB 0AAh,0BBh,0CCh"),
        "DB řádek se 3 bajty z data zóny");
    /* Mnemonic NOP musí být přítomen (kód okolo zóny se zachoval). */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "NOP"),
        "Kód kolem zóny zůstává disasmblovaný");

    remove(path);
}


/* =========================================================================
 *  Main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_jp_creates_jump_label);
    RUN_TEST(test_call_creates_subroutine_label);
    RUN_TEST(test_rst_creates_subroutine_label);
    RUN_TEST(test_target_outside_range_skipped);
    RUN_TEST(test_mid_instruction_target_is_warn);
    RUN_TEST(test_conflict_jump_wins_over_subroutine);
    RUN_TEST(test_warn_not_overridden_by_lower_type);
    RUN_TEST(test_symdb_null_no_override);
    RUN_TEST(test_symdb_override_name);
    RUN_TEST(test_symdb_mid_instruction_keeps_warn_type);
    RUN_TEST(test_empty_range_returns_zero);
    RUN_TEST(test_null_arguments_return_error);
    /* F4 - pasmo dialekt write() */
    RUN_TEST(test_export_write_pasmo_header);
    RUN_TEST(test_export_write_pasmo_org);
    RUN_TEST(test_export_write_pasmo_label_emit);
    RUN_TEST(test_export_write_pasmo_mnemonic_substitute);
    RUN_TEST(test_export_write_open_fail);
    RUN_TEST(test_export_write_empty_range);
    /* F5 - sjasmplus dialekt write() */
    RUN_TEST(test_export_write_sjasmplus_header);
    RUN_TEST(test_export_write_sjasmplus_org);
    RUN_TEST(test_export_write_sjasmplus_mnemonic_dollar_hex);
    RUN_TEST(test_export_write_sjasmplus_label_subst);
    /* F6 - sdcc-asz80 dialekt */
    RUN_TEST(test_export_write_sdcc_header);
    RUN_TEST(test_export_write_sdcc_org_dotnotation);
    RUN_TEST(test_export_write_sdcc_label_double_colon);
    RUN_TEST(test_export_write_sdcc_motorola_ix);
    RUN_TEST(test_export_write_sdcc_byte_dw_keywords);
    /* F7 - to_string (clipboard) + error enum */
    RUN_TEST(test_export_to_string_pasmo_smoke);
    RUN_TEST(test_export_to_string_invalid_range);
    RUN_TEST(test_export_to_string_null_out);
    /* F8 - CDL/mhmap data zone detection + DB rendering */
    RUN_TEST(test_collect_labels_cdl_null_no_data_zone);
    RUN_TEST(test_collect_labels_cdl_data_zone_detection);
    RUN_TEST(test_collect_labels_cdl_data_zone_with_holes);
    RUN_TEST(test_render_data_zone_pasmo_db);
    RUN_TEST(test_render_data_zone_sjasmplus_db);
    RUN_TEST(test_render_data_zone_sdcc_byte);
    RUN_TEST(test_render_data_zone_surrounded_by_code);
    /* Suppress unused warning for helper */
    (void)find_label;
    return UNITY_END();
}
