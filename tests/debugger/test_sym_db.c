/*
 * test_sym_db.c - testy symbol DB (D.8)
 *
 * Pokrývá:
 *   - load NoICE / sdldz80 .map / sjasmplus .sym / .lbl
 *   - priority při kolizi (LBL > MAP > NOI > SYM)
 *   - lookup_by_name + lookup_by_addr
 *   - user add/remove/set_comment
 *   - save_lbl + round-trip (load .lbl, save, load znovu, ověřit shodu)
 *   - auto-detekce formátu dle suffixu
 *
 * Test fixtures se generují inline jako tmpfile v cwd. Po testu se
 * odstraní.
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include "debugger/symbols/sym_db.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


void setUp ( void ) {
    sym_db_init ( );
    sym_db_clear ( );
}


void tearDown ( void ) {
    sym_db_destroy ( );
}


/**
 * Helper: zapíše obsah do tmpfile.
 */
static void write_file ( const char *path, const char *content ) {
    FILE *fp = fopen ( path, "wb" );
    TEST_ASSERT_NOT_NULL ( fp );
    fwrite ( content, 1, strlen ( content ), fp );
    fclose ( fp );
}


/* ========================================================================= */
/*  NoICE parser                                                             */
/* ========================================================================= */


/**
 * Triviální .noi soubor se 3 symbolami.
 */
void test_load_noi_basic ( void ) {
    const char *path = "test_sym_basic.noi";
    write_file ( path,
        "DEF wboot 0xD8A2\n"
        "DEF cpm_bdos_shim 0xD9C4\n"
        "DEF print_char 0x4042\n" );

    int n = sym_db_load_noi ( path );
    TEST_ASSERT_EQUAL_INT ( 3, n );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "wboot" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 0xD8A2, s->addr );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_NOI, s->source );

    s = sym_db_lookup_by_name ( "print_char" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 0x4042, s->addr );

    remove ( path );
}


/**
 * .noi - lookup_by_addr
 */
void test_lookup_by_addr ( void ) {
    const char *path = "test_sym_lba.noi";
    write_file ( path, "DEF foo 0x1234\nDEF bar 0xFF00\n" );
    sym_db_load_noi ( path );

    const st_SYMBOL *s = sym_db_lookup_by_addr ( 0x1234, 0 );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_STRING ( "foo", s->name );

    s = sym_db_lookup_by_addr ( 0xDEAD, 0 );
    TEST_ASSERT_NULL ( s );

    remove ( path );
}


/* ========================================================================= */
/*  sdldz80 .map parser                                                      */
/* ========================================================================= */


/**
 * .map - skip "Global Defined In Module" header, vyfiltrovat artefakty
 * (.__.ABS., l__BSEG, ...).
 */
void test_load_map_basic ( void ) {
    const char *path = "test_sym_basic.map";
    write_file ( path,
        "ASxxxx Linker V03.00/V05.40 + sdld,  page 1.\n"
        "Hexadecimal  [32-Bits]\n"
        "\n"
        "Area  ...  ...\n"
        "\n"
        "      Value  Global                              Global Defined In Module\n"
        "      -----  --------------------------------   ------------------------\n"
        "     00000000  .__.ABS.                           vectors\n"
        "     00000000  l__BSEG\n"
        "     0000D8A2  wboot                              bdos\n"
        "     0000D9C4  cpm_bdos_shim                      bdos\n"
        "     00004042  print_char                         crt0\n"
        "\n"
        "ASxxxx Linker V03.00/V05.40 + sdld,  page 2.\n" );

    int n = sym_db_load_map ( path );
    TEST_ASSERT_EQUAL_INT ( 3, n );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "wboot" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 0xD8A2, s->addr );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_MAP, s->source );
    TEST_ASSERT_NOT_NULL ( s->module );
    TEST_ASSERT_EQUAL_STRING ( "bdos", s->module );

    /* artefakty se nesmí načíst */
    TEST_ASSERT_NULL ( sym_db_lookup_by_name ( ".__.ABS." ) );
    TEST_ASSERT_NULL ( sym_db_lookup_by_name ( "l__BSEG" ) );

    remove ( path );
}


/* ========================================================================= */
/*  sjasmplus .sym parser                                                    */
/* ========================================================================= */


/**
 * .sym - "NAME EQU value" se třemi formáty hodnoty.
 */
void test_load_sym_basic ( void ) {
    const char *path = "test_sym_basic.sym";
    write_file ( path,
        "; comment\n"
        "print_char EQU $4042\n"
        "some_const EQU 0x0010\n"
        "counter    EQU 100\n" );

    int n = sym_db_load_sym ( path );
    TEST_ASSERT_EQUAL_INT ( 3, n );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "print_char" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 0x4042, s->addr );

    s = sym_db_lookup_by_name ( "some_const" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 0x0010, s->addr );

    s = sym_db_lookup_by_name ( "counter" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 100, s->addr );

    remove ( path );
}


/* ========================================================================= */
/*  .lbl parser + serializer                                                 */
/* ========================================================================= */


/**
 * .lbl - načtení s comments + bank.
 */
void test_load_lbl_basic ( void ) {
    const char *path = "test_sym_basic.lbl";
    write_file ( path,
        "# header comment\n"
        "D8A2  wboot       0\n"
        "2A00  ccp_main    9   ; CCP entry, jen v PEHU bank 9\n"
        "4042  print_char  0   ; user note\n" );

    int n = sym_db_load_lbl ( path );
    TEST_ASSERT_EQUAL_INT ( 3, n );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "ccp_main" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 0x2A00, s->addr );
    TEST_ASSERT_EQUAL_UINT ( 9, s->bank_id );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_LBL, s->source );
    TEST_ASSERT_NOT_NULL ( s->comment );
    TEST_ASSERT_EQUAL_STRING ( "CCP entry, jen v PEHU bank 9", s->comment );

    remove ( path );
}


/**
 * Save → reload → equivalent (= round-trip).
 */
void test_lbl_save_reload_roundtrip ( void ) {
    const char *path = "test_sym_roundtrip.lbl";

    /* Setup pomocí user_add API. */
    sym_db_add_user_label ( 0x4000, "main_loop", "primary loop" );
    sym_db_add_user_label ( 0xC000, "vsync_handler", NULL );

    int err = sym_db_save_lbl ( path );
    TEST_ASSERT_EQUAL_INT ( 0, err );

    /* clear, znovu načti */
    sym_db_clear ( );
    TEST_ASSERT_EQUAL_UINT ( 0, (unsigned) sym_db_count ( ) );

    int n = sym_db_load_lbl ( path );
    TEST_ASSERT_EQUAL_INT ( 2, n );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "main_loop" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 0x4000, s->addr );
    TEST_ASSERT_EQUAL_STRING ( "primary loop", s->comment );

    s = sym_db_lookup_by_name ( "vsync_handler" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_NULL ( s->comment );

    remove ( path );
}


/* ========================================================================= */
/*  Source priority při kolizi addr                                          */
/* ========================================================================= */


/**
 * .lbl > .noi: load NoI pak LBL, lookup_by_addr vrátí LBL.
 */
void test_priority_lbl_over_noi_by_addr ( void ) {
    const char *p_noi = "test_sym_pri.noi";
    const char *p_lbl = "test_sym_pri.lbl";

    write_file ( p_noi, "DEF wboot_noi 0xD8A2\n" );
    write_file ( p_lbl, "D8A2  wboot_lbl  0  ; user override\n" );

    sym_db_load_noi ( p_noi );
    sym_db_load_lbl ( p_lbl );

    const st_SYMBOL *s = sym_db_lookup_by_addr ( 0xD8A2, 0 );
    TEST_ASSERT_NOT_NULL ( s );
    /* Nejvyšší priorita na addr 0xD8A2 = LBL (= wboot_lbl). */
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_LBL, s->source );
    TEST_ASSERT_EQUAL_STRING ( "wboot_lbl", s->name );

    remove ( p_noi );
    remove ( p_lbl );
}


/**
 * Stejné jméno z dvou zdrojů: pravidlo "novější s >= priority přepíše".
 * Načteme NOI, pak SYM (= nižší priorita) - SYM se ignoruje.
 */
void test_priority_lower_does_not_overwrite ( void ) {
    const char *p_noi = "test_sym_lo.noi";
    const char *p_sym = "test_sym_lo.sym";

    write_file ( p_noi, "DEF foo 0x1000\n" );
    write_file ( p_sym, "foo EQU $2000\n" );

    sym_db_load_noi ( p_noi );
    sym_db_load_sym ( p_sym );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "foo" );
    TEST_ASSERT_NOT_NULL ( s );
    /* NOI je vyšší než SYM, takže addr zůstává 0x1000. */
    TEST_ASSERT_EQUAL_UINT32 ( 0x1000, s->addr );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_NOI, s->source );

    remove ( p_noi );
    remove ( p_sym );
}


/* ========================================================================= */
/*  User add/remove/set_comment                                              */
/* ========================================================================= */


/**
 * add_user_label - new symbol s comment.
 */
void test_user_add ( void ) {
    int err = sym_db_add_user_label ( 0x1234, "my_label", "user comment" );
    TEST_ASSERT_EQUAL_INT ( 0, err );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "my_label" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 0x1234, s->addr );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_LBL, s->source );
    TEST_ASSERT_EQUAL_STRING ( "user comment", s->comment );
}


/**
 * remove - existující se odstraní, neexistující vrací -1.
 */
void test_user_remove ( void ) {
    sym_db_add_user_label ( 0x1234, "x", NULL );
    TEST_ASSERT_EQUAL_INT ( 0, sym_db_remove_user_label ( "x" ) );
    TEST_ASSERT_NULL ( sym_db_lookup_by_name ( "x" ) );
    TEST_ASSERT_EQUAL_INT ( -1, sym_db_remove_user_label ( "nonexistent" ) );
}


/**
 * set_comment promotuje SYM_SOURCE_NOI -> SYM_SOURCE_LBL (write-back).
 */
void test_set_comment_promotes_to_lbl ( void ) {
    const char *path = "test_sym_promote.noi";
    write_file ( path, "DEF wboot 0xD8A2\n" );
    sym_db_load_noi ( path );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "wboot" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_NOI, s->source );

    int err = sym_db_set_comment ( "wboot", "warm boot entry" );
    TEST_ASSERT_EQUAL_INT ( 0, err );

    s = sym_db_lookup_by_name ( "wboot" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_LBL, s->source );
    TEST_ASSERT_EQUAL_STRING ( "warm boot entry", s->comment );

    remove ( path );
}


/* ========================================================================= */
/*  Auto-detect format dle suffixu                                           */
/* ========================================================================= */


/**
 * load_auto - .noi suffix volá NoICE parser.
 */
void test_auto_detect_noi ( void ) {
    const char *path = "test_sym_auto.noi";
    write_file ( path, "DEF foo 0xABCD\n" );

    int n = sym_db_load_auto ( path );
    TEST_ASSERT_EQUAL_INT ( 1, n );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "foo" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_NOI, s->source );

    remove ( path );
}


/**
 * load_auto - neznámý suffix vrátí -1.
 */
void test_auto_detect_unknown_suffix ( void ) {
    int n = sym_db_load_auto ( "blah.txt" );
    TEST_ASSERT_EQUAL_INT ( -1, n );
}


/* ========================================================================= */
/*  Iteration                                                                */
/* ========================================================================= */


/**
 * count + get_by_index.
 */
void test_iteration ( void ) {
    sym_db_add_user_label ( 0x100, "a", NULL );
    sym_db_add_user_label ( 0x200, "b", NULL );
    sym_db_add_user_label ( 0x300, "c", NULL );

    TEST_ASSERT_EQUAL_UINT ( 3, (unsigned) sym_db_count ( ) );

    /* Insertion order garantován. */
    const st_SYMBOL *s0 = sym_db_get_by_index ( 0 );
    const st_SYMBOL *s1 = sym_db_get_by_index ( 1 );
    const st_SYMBOL *s2 = sym_db_get_by_index ( 2 );
    TEST_ASSERT_NOT_NULL ( s0 );
    TEST_ASSERT_NOT_NULL ( s1 );
    TEST_ASSERT_NOT_NULL ( s2 );
    TEST_ASSERT_EQUAL_STRING ( "a", s0->name );
    TEST_ASSERT_EQUAL_STRING ( "b", s1->name );
    TEST_ASSERT_EQUAL_STRING ( "c", s2->name );

    /* Out of range. */
    TEST_ASSERT_NULL ( sym_db_get_by_index ( 99 ) );
}


/* ========================================================================= */
/*  V1.5.D - Symbols UI rework patterny                                      */
/* ========================================================================= */


/**
 * Rename pattern (= add_user_label new + remove_user_label old).
 * Simuluje sym_apply_rename z sym_window.cpp pro LBL source symbol.
 */
void test_rename_lbl_via_add_remove ( void ) {
    sym_db_add_user_label ( 0x4042, "old_name", "comment" );
    const st_SYMBOL *s = sym_db_lookup_by_name ( "old_name" );
    TEST_ASSERT_NOT_NULL ( s );

    /* Snapshot. */
    uint32_t addr = s->addr;
    char *cmt = s->comment ? g_strdup ( s->comment ) : NULL;
    en_SYM_SOURCE src = s->source;

    /* Apply rename. */
    sym_db_add_user_label ( addr, "new_name", cmt );
    if ( src == SYM_SOURCE_LBL ) {
        sym_db_remove_user_label ( "old_name" );
    };
    g_free ( cmt );

    /* Verify. */
    TEST_ASSERT_NULL ( sym_db_lookup_by_name ( "old_name" ) );
    const st_SYMBOL *n = sym_db_lookup_by_name ( "new_name" );
    TEST_ASSERT_NOT_NULL ( n );
    TEST_ASSERT_EQUAL_UINT32 ( 0x4042, n->addr );
    TEST_ASSERT_EQUAL_STRING ( "comment", n->comment );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_LBL, n->source );
}


/**
 * Rename non-LBL (NOI source) - add new LBL alias, NOI symbol zůstává.
 * Simuluje sym_apply_rename pro non-LBL source (= rename nemůže smazat
 * import-driven symbol).
 */
void test_rename_non_lbl_keeps_old ( void ) {
    const char *path = "test_sym_rename_noi.noi";
    write_file ( path, "DEF wboot 0xD8A2\n" );
    sym_db_load_noi ( path );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "wboot" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_NOI, s->source );

    /* Apply rename pattern: add new, NEremove old (non-LBL). */
    uint32_t addr = s->addr;
    en_SYM_SOURCE src = s->source;
    sym_db_add_user_label ( addr, "boot", NULL );
    if ( src == SYM_SOURCE_LBL ) {
        sym_db_remove_user_label ( "wboot" );
    };

    /* Old NOI symbol stále existuje (= per implementace). */
    TEST_ASSERT_NOT_NULL ( sym_db_lookup_by_name ( "wboot" ) );
    /* Nový LBL alias na stejnou addr. */
    const st_SYMBOL *n = sym_db_lookup_by_name ( "boot" );
    TEST_ASSERT_NOT_NULL ( n );
    TEST_ASSERT_EQUAL_UINT32 ( 0xD8A2, n->addr );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_LBL, n->source );

    remove ( path );
}


/**
 * Addr relocate pattern (= add_user_label se stejným name overwrite addr).
 * Simuluje sym_apply_addr_change z sym_window.cpp.
 */
void test_addr_relocate_overwrite ( void ) {
    sym_db_add_user_label ( 0x1000, "func", "original" );
    const st_SYMBOL *s = sym_db_lookup_by_name ( "func" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 0x1000, s->addr );

    /* Snapshot comment (reálná UI by snapshot dělala). */
    char *cmt = g_strdup ( s->comment );
    sym_db_add_user_label ( 0x2000, "func", cmt );
    g_free ( cmt );

    s = sym_db_lookup_by_name ( "func" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_UINT32 ( 0x2000, s->addr );
    TEST_ASSERT_EQUAL_STRING ( "original", s->comment );
    /* Symbol stále jen jeden (= overwrite, ne duplicate). */
    TEST_ASSERT_EQUAL_UINT ( 1, (unsigned) sym_db_count ( ) );
}


/**
 * Bulk delete iteration - snapshot names + remove loop.
 * Simuluje sym_window confirm dialog "Delete N selected".
 */
void test_bulk_delete_iteration ( void ) {
    sym_db_add_user_label ( 0x1000, "a", NULL );
    sym_db_add_user_label ( 0x2000, "b", NULL );
    sym_db_add_user_label ( 0x3000, "c", NULL );
    sym_db_add_user_label ( 0x4000, "d", NULL );
    TEST_ASSERT_EQUAL_UINT ( 4, (unsigned) sym_db_count ( ) );

    /* Snapshot jmen pro iteraci (UI by mělo std::vector<std::string>
     * z selection set). Hard-coded zde. */
    const char *to_delete[] = { "a", "c", "d" };
    for ( size_t i = 0; i < 3; i++ ) {
        TEST_ASSERT_EQUAL_INT ( 0,
            sym_db_remove_user_label ( to_delete[ i ] ) );
    };

    TEST_ASSERT_EQUAL_UINT ( 1, (unsigned) sym_db_count ( ) );
    TEST_ASSERT_NULL ( sym_db_lookup_by_name ( "a" ) );
    TEST_ASSERT_NOT_NULL ( sym_db_lookup_by_name ( "b" ) );
    TEST_ASSERT_NULL ( sym_db_lookup_by_name ( "c" ) );
    TEST_ASSERT_NULL ( sym_db_lookup_by_name ( "d" ) );
}


/**
 * add_user_label overwrite zachová priority - vždy LBL.
 * (= UI Add Label form přepíše existing NOI/MAP/SYM na LBL).
 */
void test_add_user_label_overwrites_to_lbl ( void ) {
    const char *path = "test_sym_overwrite_to_lbl.noi";
    write_file ( path, "DEF wboot 0xD8A2\n" );
    sym_db_load_noi ( path );

    const st_SYMBOL *s = sym_db_lookup_by_name ( "wboot" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_NOI, s->source );

    /* User explicit add se stejným name = LBL overwrite. */
    sym_db_add_user_label ( 0xD8A2, "wboot", "user override" );

    s = sym_db_lookup_by_name ( "wboot" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_EQUAL_INT ( SYM_SOURCE_LBL, s->source );
    TEST_ASSERT_EQUAL_STRING ( "user override", s->comment );

    remove ( path );
}


/**
 * Filter pattern - name + comment + addr-hex substring match.
 * Simuluje sym_filter_match z sym_window.cpp pomocí lookup výsledku.
 */
void test_filter_addr_hex_match ( void ) {
    sym_db_add_user_label ( 0x40FF, "frame_ptr", "ptr to current frame" );
    sym_db_add_user_label ( 0x9001, "score",     "user score counter" );

    /* Filter "40" by mělo matchovat 0x40FF (= addr substring). */
    const st_SYMBOL *s = sym_db_lookup_by_addr ( 0x40FF, 0 );
    TEST_ASSERT_NOT_NULL ( s );
    char addr_str[ 16 ];
    snprintf ( addr_str, sizeof ( addr_str ), "0x%04X", (unsigned) s->addr );
    /* Verify substring "40" v "0x40FF". */
    TEST_ASSERT_NOT_NULL ( strstr ( addr_str, "40" ) );

    /* Filter "score" by mělo matchovat name. */
    s = sym_db_lookup_by_name ( "score" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_NOT_NULL ( strstr ( s->name, "score" ) );

    /* Filter "frame" by mělo matchovat comment. */
    s = sym_db_lookup_by_name ( "frame_ptr" );
    TEST_ASSERT_NOT_NULL ( s );
    TEST_ASSERT_NOT_NULL ( strstr ( s->comment, "frame" ) );
}


/* ========================================================================= */
/*  MAIN                                                                     */
/* ========================================================================= */


int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    RUN_TEST ( test_load_noi_basic );
    RUN_TEST ( test_lookup_by_addr );
    RUN_TEST ( test_load_map_basic );
    RUN_TEST ( test_load_sym_basic );
    RUN_TEST ( test_load_lbl_basic );
    RUN_TEST ( test_lbl_save_reload_roundtrip );
    RUN_TEST ( test_priority_lbl_over_noi_by_addr );
    RUN_TEST ( test_priority_lower_does_not_overwrite );
    RUN_TEST ( test_user_add );
    RUN_TEST ( test_user_remove );
    RUN_TEST ( test_set_comment_promotes_to_lbl );
    RUN_TEST ( test_auto_detect_noi );
    RUN_TEST ( test_auto_detect_unknown_suffix );
    RUN_TEST ( test_iteration );

    /* V1.5.D - Symbols UI rework patterny */
    RUN_TEST ( test_rename_lbl_via_add_remove );
    RUN_TEST ( test_rename_non_lbl_keeps_old );
    RUN_TEST ( test_addr_relocate_overwrite );
    RUN_TEST ( test_bulk_delete_iteration );
    RUN_TEST ( test_add_user_label_overwrites_to_lbl );
    RUN_TEST ( test_filter_addr_hex_match );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
