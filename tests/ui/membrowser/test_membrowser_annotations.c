/*
 * test_membrowser_annotations.c - unit testy annotation storage (V6).
 *
 * Pure logika bez ImGui / emu / SDL závislostí - STANDALONE typ.
 *
 * Pokrývá:
 *   - set + find: round-trip text + color
 *   - set s NULL text = empty string
 *   - re-set stejného klíče přepíše hodnotu (= no duplicate slot)
 *   - per-region klíč izolace
 *   - remove + find chybí
 *   - count / at iteration
 *   - file roundtrip s escape (\\n, \\t, \\\\)
 *   - load skip syntax-bad řádků
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include "../../../src/ui-imgui/debugger/membrowser/membrowser_annotations.h"

#include <stdio.h>
#include <string.h>


void setUp(void)
{
    membrowser_annotations_clear_all ( );
}

void tearDown(void) {}


/* T1: set + find. */
static void test_set_and_find(void)
{
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_annotations_set (
        1, 0, 0x1234, "Hello world", 0xFF0000FFu ) );
    const st_MB_ANNOTATION *a = membrowser_annotations_find ( 1, 0, 0x1234 );
    TEST_ASSERT_NOT_NULL ( a );
    TEST_ASSERT_TRUE ( a->used );
    TEST_ASSERT_EQUAL_INT ( 1, a->region_kind );
    TEST_ASSERT_EQUAL_INT ( 0, a->region_sub_id );
    TEST_ASSERT_EQUAL_UINT32 ( 0x1234, a->addr );
    TEST_ASSERT_EQUAL_HEX32 ( 0xFF0000FFu, a->color_rgba );
    TEST_ASSERT_EQUAL_STRING ( "Hello world", a->text );
}


/* T2: set s NULL textem = prázdný string. */
static void test_set_null_text(void)
{
    membrowser_annotations_set ( 1, 0, 0x1, NULL, 0 );
    const st_MB_ANNOTATION *a = membrowser_annotations_find ( 1, 0, 0x1 );
    TEST_ASSERT_NOT_NULL ( a );
    TEST_ASSERT_EQUAL_STRING ( "", a->text );
}


/* T3: re-set stejného klíče přepíše hodnotu, ne duplicate. */
static void test_set_overwrites(void)
{
    membrowser_annotations_set ( 1, 0, 0xFEED, "first", 0xFFu );
    membrowser_annotations_set ( 1, 0, 0xFEED, "second", 0xAABBCCDDu );
    TEST_ASSERT_EQUAL_INT ( 1, membrowser_annotations_count ( ) );
    const st_MB_ANNOTATION *a = membrowser_annotations_find ( 1, 0, 0xFEED );
    TEST_ASSERT_EQUAL_STRING ( "second", a->text );
    TEST_ASSERT_EQUAL_HEX32 ( 0xAABBCCDDu, a->color_rgba );
}


/* T4: per-(kind, sub_id, addr) klíč izolace. */
static void test_per_key_isolation(void)
{
    membrowser_annotations_set ( 1, 0, 0x100, "A", 0 );
    membrowser_annotations_set ( 1, 1, 0x100, "B", 0 );
    membrowser_annotations_set ( 2, 0, 0x100, "C", 0 );
    membrowser_annotations_set ( 1, 0, 0x101, "D", 0 );
    TEST_ASSERT_EQUAL_INT ( 4, membrowser_annotations_count ( ) );

    TEST_ASSERT_EQUAL_STRING ( "A", membrowser_annotations_find ( 1, 0, 0x100 )->text );
    TEST_ASSERT_EQUAL_STRING ( "B", membrowser_annotations_find ( 1, 1, 0x100 )->text );
    TEST_ASSERT_EQUAL_STRING ( "C", membrowser_annotations_find ( 2, 0, 0x100 )->text );
    TEST_ASSERT_EQUAL_STRING ( "D", membrowser_annotations_find ( 1, 0, 0x101 )->text );
}


/* T5: remove + find vrací NULL. */
static void test_remove(void)
{
    membrowser_annotations_set ( 1, 0, 0xABCD, "doomed", 0 );
    TEST_ASSERT_NOT_NULL ( membrowser_annotations_find ( 1, 0, 0xABCD ) );
    TEST_ASSERT_TRUE ( membrowser_annotations_remove ( 1, 0, 0xABCD ) );
    TEST_ASSERT_NULL ( membrowser_annotations_find ( 1, 0, 0xABCD ) );
    /* Druhé remove = false. */
    TEST_ASSERT_FALSE ( membrowser_annotations_remove ( 1, 0, 0xABCD ) );
}


/* T6: count + at iteration. */
static void test_count_and_at(void)
{
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_annotations_count ( ) );
    membrowser_annotations_set ( 1, 0, 0x10, "a1", 0 );
    membrowser_annotations_set ( 1, 0, 0x20, "a2", 0 );
    membrowser_annotations_set ( 1, 0, 0x30, "a3", 0 );
    TEST_ASSERT_EQUAL_INT ( 3, membrowser_annotations_count ( ) );

    /* Iter via _at - pořadí je insertion-stable (= postupně 1.2.3). */
    TEST_ASSERT_EQUAL_STRING ( "a1", membrowser_annotations_at ( 0 )->text );
    TEST_ASSERT_EQUAL_STRING ( "a2", membrowser_annotations_at ( 1 )->text );
    TEST_ASSERT_EQUAL_STRING ( "a3", membrowser_annotations_at ( 2 )->text );
    TEST_ASSERT_NULL ( membrowser_annotations_at ( 3 ) );
    TEST_ASSERT_NULL ( membrowser_annotations_at ( -1 ) );
}


/* T7: round-trip přes tmp soubor + escape. */
static void test_file_roundtrip_escapes(void)
{
    const char *fn = "test_membrowser_annot_tmp.txt";
    membrowser_annotations_set ( 1, 2, 0xCAFE,
        "line1\nline2\twith\\backslash", 0x11223344u );
    membrowser_annotations_set ( 3, 4, 0xBEEF, "simple", 0xFFFFFFFFu );
    TEST_ASSERT_EQUAL_INT ( 0,
        membrowser_annotations_save_to_file ( fn ) );

    membrowser_annotations_clear_all ( );
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_annotations_count ( ) );

    int loaded = membrowser_annotations_load_from_file ( fn );
    TEST_ASSERT_EQUAL_INT ( 2, loaded );

    const st_MB_ANNOTATION *a = membrowser_annotations_find ( 1, 2, 0xCAFE );
    TEST_ASSERT_NOT_NULL ( a );
    TEST_ASSERT_EQUAL_STRING ( "line1\nline2\twith\\backslash", a->text );
    TEST_ASSERT_EQUAL_HEX32 ( 0x11223344u, a->color_rgba );

    const st_MB_ANNOTATION *b = membrowser_annotations_find ( 3, 4, 0xBEEF );
    TEST_ASSERT_NOT_NULL ( b );
    TEST_ASSERT_EQUAL_STRING ( "simple", b->text );

    remove ( fn );
}


/* T8: load skip syntax-bad řádek. */
static void test_load_skip_bad_lines(void)
{
    const char *fn = "test_membrowser_annot_bad.txt";
    FILE *f = fopen ( fn, "wb" );
    TEST_ASSERT_NOT_NULL ( f );
    fprintf ( f, "# comment line\n" );
    fprintf ( f, "garbage without tabs\n" );
    fprintf ( f, "1\t0\tFF\tFF000000\tgood line\n" );
    fprintf ( f, "\n" );
    fclose ( f );

    int loaded = membrowser_annotations_load_from_file ( fn );
    TEST_ASSERT_EQUAL_INT ( 1, loaded );
    const st_MB_ANNOTATION *a = membrowser_annotations_find ( 1, 0, 0xFF );
    TEST_ASSERT_NOT_NULL ( a );
    TEST_ASSERT_EQUAL_STRING ( "good line", a->text );

    remove ( fn );
}


/* T9: load z neexistujícího souboru = 0 OK. */
static void test_load_nonexistent_file(void)
{
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_annotations_load_from_file (
        "this_file_does_not_exist_xyz.txt" ) );
}


/* T10: text truncation na MB_ANNOT_TEXT_MAX-1. */
static void test_text_truncation(void)
{
    char big[600];
    memset ( big, 'X', sizeof ( big ) - 1 );
    big[sizeof ( big ) - 1] = '\0';
    membrowser_annotations_set ( 1, 0, 0, big, 0 );
    const st_MB_ANNOTATION *a = membrowser_annotations_find ( 1, 0, 0 );
    TEST_ASSERT_NOT_NULL ( a );
    /* Délka přesně MB_ANNOT_TEXT_MAX-1, ukončeno \0. */
    TEST_ASSERT_EQUAL_INT ( MB_ANNOT_TEXT_MAX - 1, ( int ) strlen ( a->text ) );
}


int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_set_and_find);
    RUN_TEST(test_set_null_text);
    RUN_TEST(test_set_overwrites);
    RUN_TEST(test_per_key_isolation);
    RUN_TEST(test_remove);
    RUN_TEST(test_count_and_at);
    RUN_TEST(test_file_roundtrip_escapes);
    RUN_TEST(test_load_skip_bad_lines);
    RUN_TEST(test_load_nonexistent_file);
    RUN_TEST(test_text_truncation);
    return UNITY_END();
    (void)argc; (void)argv;
}
