/*
 * test_bookmarks.c - testy backend storage Bookmarks (debugger).
 *
 * Pokrývá:
 *   - lifecycle (init / cleanup / version)
 *   - CRUD (add / remove / set_user_input / set_comment / clear)
 *   - iterace (count, get_by_id, get_by_index)
 *   - resolve_addr - hex formáty + symbol fallback (přes sym_db)
 *   - persistence (save / load round-trip, merge skip-duplicate)
 *
 * Type: EMU (= sdílí emu core a sym_db).
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include "debugger/bookmarks/bookmarks.h"
#include "debugger/symbols/sym_db.h"
#include "main.h"
#include "libs/sdlapp/sdlapp_paths.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>


void setUp ( void ) {
    bookmarks_init ( );
    bookmarks_clear ( );
    sym_db_init ( );
    sym_db_clear ( );
}


void tearDown ( void ) {
    bookmarks_clear ( );
    sym_db_clear ( );
}


/* ========================================================================= */
/*  CRUD                                                                     */
/* ========================================================================= */


/**
 * Po init je storage prázdný a count = 0.
 */
void test_init_empty ( void ) {
    TEST_ASSERT_EQUAL_size_t ( 0, bookmarks_count ( ) );
    TEST_ASSERT_NULL ( bookmarks_get_by_index ( 0 ) );
    TEST_ASSERT_NULL ( bookmarks_get_by_id ( 1 ) );
}


/**
 * Add vrací nenulové ID a inkrementuje count.
 */
void test_add_basic ( void ) {
    uint32_t id1 = bookmarks_add ( "0x1234", "first" );
    uint32_t id2 = bookmarks_add ( "MAIN", "" );
    TEST_ASSERT_TRUE ( id1 != 0 );
    TEST_ASSERT_TRUE ( id2 != 0 );
    TEST_ASSERT_TRUE ( id1 != id2 );
    TEST_ASSERT_EQUAL_size_t ( 2, bookmarks_count ( ) );
}


/**
 * Add s NULL / prázdným user_input vrací 0 a nemění storage.
 */
void test_add_invalid_input_noop ( void ) {
    TEST_ASSERT_EQUAL_UINT32 ( 0, bookmarks_add ( NULL, "x" ) );
    TEST_ASSERT_EQUAL_UINT32 ( 0, bookmarks_add ( "", "x" ) );
    TEST_ASSERT_EQUAL_size_t ( 0, bookmarks_count ( ) );
}


/**
 * Get_by_id vrací správný záznam.
 */
void test_get_by_id ( void ) {
    uint32_t id = bookmarks_add ( "0xABCD", "comment" );
    const bookmark_t *bm = bookmarks_get_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bm );
    TEST_ASSERT_EQUAL_UINT32 ( id, bm->id );
    TEST_ASSERT_EQUAL_STRING ( "0xABCD", bm->user_input );
    TEST_ASSERT_EQUAL_STRING ( "comment", bm->comment );
}


/**
 * Get_by_index vrací správný záznam podle pořadí.
 */
void test_get_by_index ( void ) {
    bookmarks_add ( "0x0001", NULL );
    bookmarks_add ( "0x0002", NULL );
    const bookmark_t *bm0 = bookmarks_get_by_index ( 0 );
    const bookmark_t *bm1 = bookmarks_get_by_index ( 1 );
    TEST_ASSERT_NOT_NULL ( bm0 );
    TEST_ASSERT_NOT_NULL ( bm1 );
    TEST_ASSERT_EQUAL_STRING ( "0x0001", bm0->user_input );
    TEST_ASSERT_EQUAL_STRING ( "0x0002", bm1->user_input );
    TEST_ASSERT_NULL ( bookmarks_get_by_index ( 2 ) );
}


/**
 * Add s NULL / prázdným comment uloží comment = NULL.
 */
void test_add_null_comment ( void ) {
    uint32_t id = bookmarks_add ( "0x1000", NULL );
    const bookmark_t *bm = bookmarks_get_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bm );
    TEST_ASSERT_NULL ( bm->comment );

    uint32_t id2 = bookmarks_add ( "0x2000", "" );
    const bookmark_t *bm2 = bookmarks_get_by_id ( id2 );
    TEST_ASSERT_NULL ( bm2->comment );
}


/**
 * Remove odstraní záznam, count se zmenší.
 */
void test_remove ( void ) {
    uint32_t id = bookmarks_add ( "0x1234", NULL );
    TEST_ASSERT_EQUAL_size_t ( 1, bookmarks_count ( ) );
    TEST_ASSERT_TRUE ( bookmarks_remove ( id ) );
    TEST_ASSERT_EQUAL_size_t ( 0, bookmarks_count ( ) );
    TEST_ASSERT_FALSE ( bookmarks_remove ( id ) );  /* už neexistuje */
}


/**
 * Set_user_input změní vstup, set_comment změní komentář.
 */
void test_set_user_input_and_comment ( void ) {
    uint32_t id = bookmarks_add ( "0x1234", "old" );
    TEST_ASSERT_TRUE ( bookmarks_set_user_input ( id, "0x5678" ) );
    TEST_ASSERT_TRUE ( bookmarks_set_comment ( id, "new" ) );
    const bookmark_t *bm = bookmarks_get_by_id ( id );
    TEST_ASSERT_EQUAL_STRING ( "0x5678", bm->user_input );
    TEST_ASSERT_EQUAL_STRING ( "new", bm->comment );

    /* Set_comment NULL = clear */
    TEST_ASSERT_TRUE ( bookmarks_set_comment ( id, NULL ) );
    TEST_ASSERT_NULL ( bookmarks_get_by_id ( id )->comment );
}


/**
 * Set na neexistující ID vrací false a nedělá nic.
 */
void test_set_invalid_id_returns_false ( void ) {
    TEST_ASSERT_FALSE ( bookmarks_set_user_input ( 999, "x" ) );
    TEST_ASSERT_FALSE ( bookmarks_set_comment ( 999, "x" ) );
}


/**
 * Clear smaže všechny záznamy.
 */
void test_clear ( void ) {
    bookmarks_add ( "0x0001", NULL );
    bookmarks_add ( "0x0002", NULL );
    bookmarks_add ( "0x0003", NULL );
    bookmarks_clear ( );
    TEST_ASSERT_EQUAL_size_t ( 0, bookmarks_count ( ) );
}


/**
 * Version se inkrementuje při každé mutaci.
 */
void test_version_bumps ( void ) {
    unsigned v0 = bookmarks_get_version ( );
    bookmarks_add ( "0x0001", NULL );
    unsigned v1 = bookmarks_get_version ( );
    TEST_ASSERT_TRUE ( v1 > v0 );

    bookmarks_clear ( );
    unsigned v2 = bookmarks_get_version ( );
    TEST_ASSERT_TRUE ( v2 > v1 );
}


/* ========================================================================= */
/*  Resolve hex formáty                                                      */
/* ========================================================================= */


/**
 * 0x prefix.
 */
void test_resolve_hex_0x_prefix ( void ) {
    uint16_t a = 0;
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "0x1234", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1234, a );
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "0X00AB", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0x00AB, a );
}


/**
 * # prefix a $ prefix.
 */
void test_resolve_hex_hash_dollar_prefix ( void ) {
    uint16_t a = 0;
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "#1234", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1234, a );
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "$ABCD", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0xABCD, a );
}


/**
 * h-suffix.
 */
void test_resolve_hex_h_suffix ( void ) {
    uint16_t a = 0;
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "1234h", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1234, a );
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "01234H", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1234, a );
}


/**
 * Holé hex digity (max 4).
 */
void test_resolve_hex_bare ( void ) {
    uint16_t a = 0;
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "1234", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1234, a );
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "ABCD", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0xABCD, a );
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "ff", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0x00FF, a );
}


/**
 * Whitespace trim na obou koncích.
 */
void test_resolve_hex_trimmed ( void ) {
    uint16_t a = 0;
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "  0x1234  ", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1234, a );
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "\t#ABCD\n", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0xABCD, a );
}


/**
 * Symbol lookup fallback po hex parse selhání.
 */
void test_resolve_symbol_fallback ( void ) {
    /* Naimportujeme symbol přes sym_db (= test EMU type má sym_db dostupné). */
    sym_db_clear ( );

    /* Použijeme .lbl loader = nejjednodušší. Vytvoříme dočasný .lbl. */
    char *cfg_dir = g_strdup ( g_get_tmp_dir ( ) );
    char *lbl_path = g_build_filename ( cfg_dir, "test_bookmarks.lbl", NULL );
    FILE *fp = g_fopen ( lbl_path, "wb" );
    TEST_ASSERT_NOT_NULL ( fp );
    fprintf ( fp, "# test\n" );
    fprintf ( fp, "ABCD MAIN_LOOP 00 ; entry\n" );
    fclose ( fp );

    int n = sym_db_load_lbl ( lbl_path );
    TEST_ASSERT_TRUE ( n >= 1 );

    uint16_t a = 0;
    TEST_ASSERT_TRUE ( bookmarks_resolve_addr ( "MAIN_LOOP", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0xABCD, a );

    g_remove ( lbl_path );
    g_free ( lbl_path );
    g_free ( cfg_dir );
}


/**
 * Unresolvable -> false a addr_out se nemění.
 */
void test_resolve_unresolvable_returns_false ( void ) {
    uint16_t a = 0xDEAD;
    TEST_ASSERT_FALSE ( bookmarks_resolve_addr ( "NONEXISTENT_SYM", &a ) );
    TEST_ASSERT_EQUAL_HEX16 ( 0xDEAD, a );  /* nezměněno */
    TEST_ASSERT_FALSE ( bookmarks_resolve_addr ( NULL, &a ) );
    TEST_ASSERT_FALSE ( bookmarks_resolve_addr ( "", &a ) );
}


/**
 * Neplatný hex (znaky mimo 0-9a-f) bez prefix -> false a fallback na symbol.
 */
void test_resolve_invalid_hex_no_prefix ( void ) {
    uint16_t a = 0;
    /* "12XX" má hex digit + non-hex; 4 znaky takže by se zkusil bare hex,
     * ale 'X' není hex -> false. Nemáme symbol "12XX" -> false. */
    TEST_ASSERT_FALSE ( bookmarks_resolve_addr ( "12XX", &a ) );
}


/* ========================================================================= */
/*  Persistence - save / load                                                */
/* ========================================================================= */


/**
 * Save + load round-trip.
 */
void test_save_load_round_trip ( void ) {
    const char *tmpfile = "test_bookmarks_rt.bookmarks";

    bookmarks_add ( "0x1234", "first" );
    bookmarks_add ( "MAIN", "" );
    bookmarks_add ( "$ABCD", "third" );

    TEST_ASSERT_TRUE ( bookmarks_save ( tmpfile ) );

    bookmarks_clear ( );
    TEST_ASSERT_EQUAL_size_t ( 0, bookmarks_count ( ) );

    TEST_ASSERT_TRUE ( bookmarks_load ( tmpfile ) );
    TEST_ASSERT_EQUAL_size_t ( 3, bookmarks_count ( ) );

    const bookmark_t *bm0 = bookmarks_get_by_index ( 0 );
    const bookmark_t *bm1 = bookmarks_get_by_index ( 1 );
    const bookmark_t *bm2 = bookmarks_get_by_index ( 2 );
    TEST_ASSERT_EQUAL_STRING ( "0x1234", bm0->user_input );
    TEST_ASSERT_EQUAL_STRING ( "first",  bm0->comment );
    TEST_ASSERT_EQUAL_STRING ( "MAIN",   bm1->user_input );
    TEST_ASSERT_NULL ( bm1->comment );  /* "" -> NULL po normalizaci */
    TEST_ASSERT_EQUAL_STRING ( "$ABCD",  bm2->user_input );
    TEST_ASSERT_EQUAL_STRING ( "third",  bm2->comment );

    /* Cleanup tmpfile - resolve cestu (cfg dir relative) */
    char *resolved = sdlapp_paths_resolve_cfg ( g_sdlapp->paths, tmpfile );
    if ( resolved ) {
        g_remove ( resolved );
        g_free ( resolved );
    };
}


/**
 * Merge skip-duplicate (= identický záznam).
 */
void test_merge_skip_duplicate ( void ) {
    const char *tmpfile = "test_bookmarks_merge.bookmarks";

    bookmarks_add ( "0x1234", "stay" );  /* ID 1 */
    bookmarks_add ( "0xAAAA", "extra" ); /* ID 2 */
    TEST_ASSERT_TRUE ( bookmarks_save ( tmpfile ) );
    size_t before = bookmarks_count ( );

    /* Druhé volání merge se stejným souborem = duplicate identical -> skip. */
    TEST_ASSERT_TRUE ( bookmarks_merge ( tmpfile ) );
    TEST_ASSERT_EQUAL_size_t ( before, bookmarks_count ( ) );

    /* Cleanup */
    char *resolved = sdlapp_paths_resolve_cfg ( g_sdlapp->paths, tmpfile );
    if ( resolved ) {
        g_remove ( resolved );
        g_free ( resolved );
    };
}


/**
 * Default filepath vrací non-NULL non-prázdný řetězec.
 */
void test_default_filepath ( void ) {
    const char *p = bookmarks_default_filepath ( );
    TEST_ASSERT_NOT_NULL ( p );
    TEST_ASSERT_TRUE ( p[0] != '\0' );
}


/* ========================================================================= */
/*  Main                                                                     */
/* ========================================================================= */


int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    /* CRUD */
    RUN_TEST ( test_init_empty );
    RUN_TEST ( test_add_basic );
    RUN_TEST ( test_add_invalid_input_noop );
    RUN_TEST ( test_get_by_id );
    RUN_TEST ( test_get_by_index );
    RUN_TEST ( test_add_null_comment );
    RUN_TEST ( test_remove );
    RUN_TEST ( test_set_user_input_and_comment );
    RUN_TEST ( test_set_invalid_id_returns_false );
    RUN_TEST ( test_clear );
    RUN_TEST ( test_version_bumps );

    /* Resolve */
    RUN_TEST ( test_resolve_hex_0x_prefix );
    RUN_TEST ( test_resolve_hex_hash_dollar_prefix );
    RUN_TEST ( test_resolve_hex_h_suffix );
    RUN_TEST ( test_resolve_hex_bare );
    RUN_TEST ( test_resolve_hex_trimmed );
    RUN_TEST ( test_resolve_symbol_fallback );
    RUN_TEST ( test_resolve_unresolvable_returns_false );
    RUN_TEST ( test_resolve_invalid_hex_no_prefix );

    /* Persistence */
    RUN_TEST ( test_save_load_round_trip );
    RUN_TEST ( test_merge_skip_duplicate );
    RUN_TEST ( test_default_filepath );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
