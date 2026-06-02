/*
 * test_membrowser_fill.c - unit testy fill engine + undo/redo (V6).
 *
 * Pure logika bez ImGui / emu / SDL závislostí - STANDALONE typ.
 *
 * Pokrývá:
 *   - fill módy: ZERO/FF/RAMP/RANDOM/PATTERN/INCREMENT
 *   - pattern parser: whitespace tolerance, syntax chyby, buffer overflow
 *   - undo push/pop základní cyklus
 *   - undo overflow (>10 levels) - oldest dropped
 *   - undo + redo round-trip preserves bytes
 *   - redo invalidation by new push
 *   - per-region izolace bucketu
 *
 * Licence: GPLv3
 */

#include "mztest.h"

/* Stejný pattern jako test_membrowser_diff.c - include impl. header. */
#include "../../../src/ui-imgui/debugger/membrowser/membrowser_fill.h"
#include "../../../src/ui-imgui/debugger/membrowser/membrowser_undo.h"

#include <string.h>


void setUp(void)
{
    /* Před každým testem vyčistit undo storage (cross-test izolace). */
    membrowser_undo_clear_all ( );
}

void tearDown(void) {}


/* ============================================================ FILL ENGINE */

/* T1: ZERO mode. */
static void test_fill_zero(void)
{
    uint8_t buf[16];
    memset ( buf, 0xAA, sizeof ( buf ) );
    st_MB_FILL_PARAMS p = { 0 };
    p.mode = MB_FILL_MODE_ZERO;
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_apply ( &p, buf, sizeof ( buf ) ) );
    for ( int i = 0; i < 16; i++ ) TEST_ASSERT_EQUAL_HEX8 ( 0x00, buf[i] );
}


/* T2: FF mode. */
static void test_fill_ff(void)
{
    uint8_t buf[8] = { 0 };
    st_MB_FILL_PARAMS p = { 0 };
    p.mode = MB_FILL_MODE_FF;
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_apply ( &p, buf, sizeof ( buf ) ) );
    for ( int i = 0; i < 8; i++ ) TEST_ASSERT_EQUAL_HEX8 ( 0xFF, buf[i] );
}


/* T3: RAMP mode - wrap mod 256. */
static void test_fill_ramp_wrap(void)
{
    uint8_t buf[260];
    st_MB_FILL_PARAMS p = { 0 };
    p.mode = MB_FILL_MODE_RAMP;
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_apply ( &p, buf, sizeof ( buf ) ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0x00, buf[0] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xFF, buf[255] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x00, buf[256] );  /* wrap */
    TEST_ASSERT_EQUAL_HEX8 ( 0x03, buf[259] );
}


/* T4: PATTERN mode. */
static void test_fill_pattern(void)
{
    uint8_t buf[7] = { 0 };
    st_MB_FILL_PARAMS p = { 0 };
    p.mode = MB_FILL_MODE_PATTERN;
    p.pattern[0] = 0xDE;
    p.pattern[1] = 0xAD;
    p.pattern_len = 2;
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_apply ( &p, buf, sizeof ( buf ) ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0xDE, buf[0] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAD, buf[1] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xDE, buf[2] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAD, buf[3] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xDE, buf[6] );
}


/* T5: PATTERN s pattern_len=0 = error. */
static void test_fill_pattern_empty_fails(void)
{
    uint8_t buf[4] = { 0 };
    st_MB_FILL_PARAMS p = { 0 };
    p.mode = MB_FILL_MODE_PATTERN;
    p.pattern_len = 0;
    TEST_ASSERT_EQUAL_INT ( -1, membrowser_fill_apply ( &p, buf, sizeof ( buf ) ) );
}


/* T6: INCREMENT mode od fixed_value. */
static void test_fill_increment(void)
{
    uint8_t buf[5] = { 0 };
    st_MB_FILL_PARAMS p = { 0 };
    p.mode = MB_FILL_MODE_INCREMENT;
    p.fixed_value = 0xFE;
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_apply ( &p, buf, sizeof ( buf ) ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0xFE, buf[0] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xFF, buf[1] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x00, buf[2] );  /* wrap */
    TEST_ASSERT_EQUAL_HEX8 ( 0x01, buf[3] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x02, buf[4] );
}


/* T7: RANDOM mode - deterministický pri stejném seedu. */
static void test_fill_random_deterministic(void)
{
    uint8_t a[32], b[32];
    st_MB_FILL_PARAMS p = { 0 };
    p.mode = MB_FILL_MODE_RANDOM;
    p.random_seed = 0x12345678;
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_apply ( &p, a, sizeof ( a ) ) );
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_apply ( &p, b, sizeof ( b ) ) );
    TEST_ASSERT_EQUAL_MEMORY ( a, b, sizeof ( a ) );
}


/* T8: invalid mode = error. */
static void test_fill_invalid_mode(void)
{
    uint8_t buf[4];
    st_MB_FILL_PARAMS p = { 0 };
    p.mode = 99;
    TEST_ASSERT_EQUAL_INT ( -1, membrowser_fill_apply ( &p, buf, sizeof ( buf ) ) );
}


/* T9: len=0 vrací 0 (no-op). */
static void test_fill_zero_length(void)
{
    uint8_t buf[1] = { 0x42 };
    st_MB_FILL_PARAMS p = { 0 };
    p.mode = MB_FILL_MODE_FF;
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_apply ( &p, buf, 0 ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0x42, buf[0] );  /* nezměněno */
}


/* =========================================================== PARSER */

/* T10: parse "AA BB CC". */
static void test_parse_spaced(void)
{
    uint8_t out[8];
    int n = -1;
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_parse_pattern (
        "AA BB CC", out, sizeof ( out ), &n ) );
    TEST_ASSERT_EQUAL_INT ( 3, n );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAA, out[0] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xBB, out[1] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xCC, out[2] );
}


/* T11: parse bez mezer + lowercase. */
static void test_parse_packed_lowercase(void)
{
    uint8_t out[8];
    int n = -1;
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_parse_pattern (
        "deadbeef", out, sizeof ( out ), &n ) );
    TEST_ASSERT_EQUAL_INT ( 4, n );
    TEST_ASSERT_EQUAL_HEX8 ( 0xDE, out[0] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAD, out[1] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xBE, out[2] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xEF, out[3] );
}


/* T12: parse lichý počet hex digitů = syntax error (-1). */
static void test_parse_odd_digits_fails(void)
{
    uint8_t out[8];
    int n = 99;
    TEST_ASSERT_EQUAL_INT ( -1, membrowser_fill_parse_pattern (
        "ABC", out, sizeof ( out ), &n ) );
}


/* T13: parse non-hex char = error. */
static void test_parse_garbage_fails(void)
{
    uint8_t out[8];
    int n = 99;
    TEST_ASSERT_EQUAL_INT ( -1, membrowser_fill_parse_pattern (
        "GG", out, sizeof ( out ), &n ) );
}


/* T14: parse přesah kapacity = -2. */
static void test_parse_overflow(void)
{
    uint8_t out[2];
    int n = 99;
    TEST_ASSERT_EQUAL_INT ( -2, membrowser_fill_parse_pattern (
        "AABBCC", out, sizeof ( out ), &n ) );
}


/* T15: parse prázdný string = 0 bajtů, OK. */
static void test_parse_empty(void)
{
    uint8_t out[4];
    int n = 99;
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_fill_parse_pattern (
        "  \t  ", out, sizeof ( out ), &n ) );
    TEST_ASSERT_EQUAL_INT ( 0, n );
}


/* =========================================================== UNDO/REDO */

/* T20: push + pop základní cyklus. */
static void test_undo_basic(void)
{
    uint8_t data[4] = { 1, 2, 3, 4 };
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_undo_push (
        1, 0, 0x1000, data, sizeof ( data ) ) );

    int u = -1, r = -1;
    membrowser_undo_stats ( 1, 0, &u, &r );
    TEST_ASSERT_EQUAL_INT ( 1, u );
    TEST_ASSERT_EQUAL_INT ( 0, r );

    st_MB_UNDO_ENTRY e;
    memset ( &e, 0, sizeof ( e ) );
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_undo_pop ( 1, 0, &e ) );
    TEST_ASSERT_EQUAL_UINT32 ( 4, e.len );
    TEST_ASSERT_EQUAL_UINT64 ( 0x1000, e.offset );
    TEST_ASSERT_EQUAL_MEMORY ( data, e.bytes, 4 );
    membrowser_undo_free_entry ( &e );

    /* Po pop je stack prázdný. */
    TEST_ASSERT_EQUAL_INT ( -1, membrowser_undo_pop ( 1, 0, &e ) );
}


/* T21: undo overflow (>10 levels) zahodí nejstarší. */
static void test_undo_overflow(void)
{
    /* 12 push - oldest 2 vypadnou. */
    for ( int i = 0; i < 12; i++ ) {
        uint8_t b = ( uint8_t ) i;
        TEST_ASSERT_EQUAL_INT ( 0, membrowser_undo_push (
            1, 0, ( uint64_t ) i, &b, 1 ) );
    }
    int u = -1;
    membrowser_undo_stats ( 1, 0, &u, NULL );
    TEST_ASSERT_EQUAL_INT ( MB_UNDO_MAX_LEVELS, u );

    /* Top by měl být i=11 (poslední push). */
    st_MB_UNDO_ENTRY e;
    memset ( &e, 0, sizeof ( e ) );
    membrowser_undo_pop ( 1, 0, &e );
    TEST_ASSERT_EQUAL_HEX8 ( 11, e.bytes[0] );
    membrowser_undo_free_entry ( &e );

    /* Po dalších pop najdeme i=10, 9, 8, ... až i=2 (i=0,1 vypadly). */
    for ( int i = 10; i >= 2; i-- ) {
        memset ( &e, 0, sizeof ( e ) );
        TEST_ASSERT_EQUAL_INT ( 0, membrowser_undo_pop ( 1, 0, &e ) );
        TEST_ASSERT_EQUAL_HEX8 ( i, e.bytes[0] );
        membrowser_undo_free_entry ( &e );
    }
    /* Stack je nyní prázdný. */
    TEST_ASSERT_EQUAL_INT ( -1, membrowser_undo_pop ( 1, 0, &e ) );
}


/* T22: redo push + pop. */
static void test_redo_basic(void)
{
    uint8_t d[2] = { 0xAA, 0xBB };
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_redo_push ( 3, 5, 0x2000, d, 2 ) );

    int u = -1, r = -1;
    membrowser_undo_stats ( 3, 5, &u, &r );
    TEST_ASSERT_EQUAL_INT ( 0, u );
    TEST_ASSERT_EQUAL_INT ( 1, r );

    st_MB_UNDO_ENTRY e;
    memset ( &e, 0, sizeof ( e ) );
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_redo_pop ( 3, 5, &e ) );
    TEST_ASSERT_EQUAL_UINT32 ( 2, e.len );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAA, e.bytes[0] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xBB, e.bytes[1] );
    membrowser_undo_free_entry ( &e );
}


/* T23: novy undo push invaliduje redo. */
static void test_undo_push_invalidates_redo(void)
{
    uint8_t d[1] = { 0xFF };
    /* Naplnit redo. */
    membrowser_redo_push ( 1, 0, 0, d, 1 );
    membrowser_redo_push ( 1, 0, 0, d, 1 );
    int r = -1;
    membrowser_undo_stats ( 1, 0, NULL, &r );
    TEST_ASSERT_EQUAL_INT ( 2, r );

    /* Push do undo invaliduje redo. */
    membrowser_undo_push ( 1, 0, 0, d, 1 );
    membrowser_undo_stats ( 1, 0, NULL, &r );
    TEST_ASSERT_EQUAL_INT ( 0, r );
}


/* T24: per-region bucket izolace. */
static void test_per_region_isolation(void)
{
    uint8_t a[1] = { 1 };
    uint8_t b[1] = { 2 };
    membrowser_undo_push ( 1, 0, 0, a, 1 );
    membrowser_undo_push ( 2, 0, 0, b, 1 );

    int u1 = -1, u2 = -1;
    membrowser_undo_stats ( 1, 0, &u1, NULL );
    membrowser_undo_stats ( 2, 0, &u2, NULL );
    TEST_ASSERT_EQUAL_INT ( 1, u1 );
    TEST_ASSERT_EQUAL_INT ( 1, u2 );

    st_MB_UNDO_ENTRY e;
    memset ( &e, 0, sizeof ( e ) );
    membrowser_undo_pop ( 1, 0, &e );
    TEST_ASSERT_EQUAL_HEX8 ( 1, e.bytes[0] );
    membrowser_undo_free_entry ( &e );

    memset ( &e, 0, sizeof ( e ) );
    membrowser_undo_pop ( 2, 0, &e );
    TEST_ASSERT_EQUAL_HEX8 ( 2, e.bytes[0] );
    membrowser_undo_free_entry ( &e );
}


/* T25: push příliš velký záznam (> MB_UNDO_MAX_BYTES) vrací -1. */
static void test_undo_too_large(void)
{
    /* Pseudo-buffer (NULL OK, push by měl odmítnout dle limit). */
    uint8_t dummy[4] = { 0 };
    TEST_ASSERT_EQUAL_INT ( -1, membrowser_undo_push (
        1, 0, 0, dummy, MB_UNDO_MAX_BYTES + 1 ) );
}


int main(int argc, char **argv)
{
    UNITY_BEGIN();
    /* Fill engine */
    RUN_TEST(test_fill_zero);
    RUN_TEST(test_fill_ff);
    RUN_TEST(test_fill_ramp_wrap);
    RUN_TEST(test_fill_pattern);
    RUN_TEST(test_fill_pattern_empty_fails);
    RUN_TEST(test_fill_increment);
    RUN_TEST(test_fill_random_deterministic);
    RUN_TEST(test_fill_invalid_mode);
    RUN_TEST(test_fill_zero_length);
    /* Pattern parser */
    RUN_TEST(test_parse_spaced);
    RUN_TEST(test_parse_packed_lowercase);
    RUN_TEST(test_parse_odd_digits_fails);
    RUN_TEST(test_parse_garbage_fails);
    RUN_TEST(test_parse_overflow);
    RUN_TEST(test_parse_empty);
    /* Undo/redo */
    RUN_TEST(test_undo_basic);
    RUN_TEST(test_undo_overflow);
    RUN_TEST(test_redo_basic);
    RUN_TEST(test_undo_push_invalidates_redo);
    RUN_TEST(test_per_region_isolation);
    RUN_TEST(test_undo_too_large);
    return UNITY_END();
    (void)argc; (void)argv;
}
