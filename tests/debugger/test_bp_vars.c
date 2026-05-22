/*
 * test_bp_vars.c - testy globálního $vars storage (D.1.3 + V1.5.B)
 *
 * Pokrývá:
 *   - V1: set/get/clear_all/clear_storage/count/get_by_index +
 *         propagaci přes bp_expr evaluator (= $name resolution)
 *   - V1.5.B: name regex validation, comment field, persist_value flag,
 *     bp_var_set_full / unset / set_persist / get_persist,
 *     file ops (.vars JSON), load modes (REPLACE / MERGE_OVERWRITE / SKIP)
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include "debugger/bp_vars.h"
#include "debugger/bp_expr.h"
#include "main.h"  /* g_sdlapp */
#include "libs/sdlapp/sdlapp_paths.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>


void setUp ( void ) {
    /* Per-test čistý stav. */
    bp_vars_init ( );
    bp_vars_clear_storage ( );
}


void tearDown ( void ) {
    bp_vars_destroy ( );
}


/* ========================================================================= */
/*  Základní get / set                                                       */
/* ========================================================================= */


/**
 * Default 0 pro neexistující proměnnou.
 */
void test_default_zero_for_missing ( void ) {
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "unknown" ) );
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "" ) );
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( NULL ) );
}


/**
 * Set + get round-trip.
 */
void test_set_get_basic ( void ) {
    bp_var_set ( "counter", 42 );
    TEST_ASSERT_EQUAL_INT32 ( 42, bp_var_get ( "counter" ) );
    bp_var_set ( "counter", -7 );
    TEST_ASSERT_EQUAL_INT32 ( -7, bp_var_get ( "counter" ) );
}


/**
 * Více proměnných současně.
 */
void test_set_multiple ( void ) {
    bp_var_set ( "a", 1 );
    bp_var_set ( "b", 2 );
    bp_var_set ( "c", 3 );
    TEST_ASSERT_EQUAL_INT32 ( 1, bp_var_get ( "a" ) );
    TEST_ASSERT_EQUAL_INT32 ( 2, bp_var_get ( "b" ) );
    TEST_ASSERT_EQUAL_INT32 ( 3, bp_var_get ( "c" ) );
    TEST_ASSERT_EQUAL_INT ( 3, (int) bp_vars_count ( ) );
}


/**
 * Update existing var (= ne nový záznam).
 */
void test_set_overwrite_existing ( void ) {
    bp_var_set ( "x", 100 );
    bp_var_set ( "x", 200 );
    TEST_ASSERT_EQUAL_INT32 ( 200, bp_var_get ( "x" ) );
    TEST_ASSERT_EQUAL_INT ( 1, (int) bp_vars_count ( ) );
}


/**
 * NULL/prázdný name = no-op.
 */
void test_set_invalid_name_noop ( void ) {
    bp_var_set ( NULL, 99 );
    bp_var_set ( "",   99 );
    TEST_ASSERT_EQUAL_INT ( 0, (int) bp_vars_count ( ) );
}


/**
 * Case-sensitive lookup (= odlišné jméno = jiný záznam).
 */
void test_set_case_sensitive ( void ) {
    bp_var_set ( "Foo", 1 );
    bp_var_set ( "foo", 2 );
    TEST_ASSERT_EQUAL_INT32 ( 1, bp_var_get ( "Foo" ) );
    TEST_ASSERT_EQUAL_INT32 ( 2, bp_var_get ( "foo" ) );
    TEST_ASSERT_EQUAL_INT ( 2, (int) bp_vars_count ( ) );
}


/* ========================================================================= */
/*  Clear                                                                    */
/* ========================================================================= */


/**
 * clear_all = záznamy zůstanou, jen value se vynuluje.
 */
void test_clear_all_resets_values ( void ) {
    bp_var_set ( "a", 1 );
    bp_var_set ( "b", 2 );
    bp_vars_clear_all ( );
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "a" ) );
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "b" ) );
    TEST_ASSERT_EQUAL_INT ( 2, (int) bp_vars_count ( ) );
}


/**
 * clear_storage = záznamy zmizí.
 */
void test_clear_storage_removes_entries ( void ) {
    bp_var_set ( "a", 1 );
    bp_var_set ( "b", 2 );
    bp_vars_clear_storage ( );
    TEST_ASSERT_EQUAL_INT ( 0, (int) bp_vars_count ( ) );
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "a" ) );
}


/* ========================================================================= */
/*  Enumerace                                                                */
/* ========================================================================= */


/**
 * count + get_by_index + out-of-range.
 */
void test_get_by_index ( void ) {
    bp_var_set ( "alpha", 10 );
    bp_var_set ( "beta",  20 );

    TEST_ASSERT_EQUAL_INT ( 2, (int) bp_vars_count ( ) );

    const bp_var_t *v0 = bp_vars_get_by_index ( 0 );
    const bp_var_t *v1 = bp_vars_get_by_index ( 1 );
    const bp_var_t *v2 = bp_vars_get_by_index ( 2 );

    TEST_ASSERT_NOT_NULL ( v0 );
    TEST_ASSERT_NOT_NULL ( v1 );
    TEST_ASSERT_NULL ( v2 );

    /* Pořadí = pořadí setů. */
    TEST_ASSERT_EQUAL_STRING ( "alpha", v0->name );
    TEST_ASSERT_EQUAL_INT32 ( 10, v0->value );
    TEST_ASSERT_EQUAL_STRING ( "beta", v1->name );
    TEST_ASSERT_EQUAL_INT32 ( 20, v1->value );
}


/* ========================================================================= */
/*  Integrace s bp_expr                                                      */
/* ========================================================================= */


/**
 * $name v expr resolvuje na bp_var_get() value.
 */
void test_expr_reads_var ( void ) {
    bp_var_set ( "score", 100 );
    bp_var_set ( "lives", 3 );

    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );

    int32_t v = 0;
    char err[128] = {0};
    bool ok = bp_expr_parse_eval ( "$score + $lives * 10", &ctx,
                                    &v, err, sizeof ( err ) );
    TEST_ASSERT_TRUE ( ok );
    TEST_ASSERT_EQUAL_INT32 ( 130, v );
}


/**
 * Neznámý $name v expr = 0 (= konzistentní s bp_var_get).
 */
void test_expr_unknown_var_zero ( void ) {
    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );

    int32_t v = 0;
    bp_expr_parse_eval ( "$nonexistent", &ctx, &v, NULL, 0 );
    TEST_ASSERT_EQUAL_INT32 ( 0, v );
}


/* ========================================================================= */
/*  Idempotence init                                                         */
/* ========================================================================= */


/**
 * Volání init() vícekrát je no-op (= storage nezmizí).
 */
void test_init_idempotent ( void ) {
    bp_var_set ( "stay", 7 );
    bp_vars_init ( );  /* už bylo init v setUp - žádný reset */
    TEST_ASSERT_EQUAL_INT32 ( 7, bp_var_get ( "stay" ) );
}


/* ========================================================================= */
/*  V1.5.B - Name regex validation                                           */
/* ========================================================================= */


void test_name_empty_invalid ( void ) {
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( NULL ) );
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( "" ) );
}


void test_name_starts_with_letter ( void ) {
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "a" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "Z" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "counter" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "myStateA" ) );
}


void test_name_starts_with_underscore ( void ) {
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "_internal" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "_" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "__double" ) );
}


void test_name_starts_with_digit_invalid ( void ) {
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( "1count" ) );
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( "9" ) );
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( "0xABC" ) );
}


void test_name_contains_invalid_chars ( void ) {
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( "count-down" ) );
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( "count.x" ) );
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( "count x" ) );
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( "count$" ) );
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( "count!" ) );
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( "count+1" ) );
}


void test_name_with_digits_after_first_char ( void ) {
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "count1" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "var123abc" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "_42" ) );
}


void test_name_max_length_31 ( void ) {
    /* 31 znaků = OK (boundary). */
    char buf31[ BP_VAR_NAME_MAX + 1 ];
    memset ( buf31, 'a', BP_VAR_NAME_MAX );
    buf31[ BP_VAR_NAME_MAX ] = '\0';
    TEST_ASSERT_EQUAL_INT ( 31, (int) strlen ( buf31 ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( buf31 ) );

    /* 32 znaků = invalid (over boundary). */
    char buf32[ BP_VAR_NAME_MAX + 2 ];
    memset ( buf32, 'a', BP_VAR_NAME_MAX + 1 );
    buf32[ BP_VAR_NAME_MAX + 1 ] = '\0';
    TEST_ASSERT_EQUAL_INT ( 32, (int) strlen ( buf32 ) );
    TEST_ASSERT_FALSE ( bp_var_name_is_valid ( buf32 ) );
}


void test_name_keyword_allowed ( void ) {
    /* $-prefix je distinktivní → keywords jsou platná uživ. jména. */
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "if" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "set" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "log" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "else" ) );
    TEST_ASSERT_TRUE ( bp_var_name_is_valid ( "then" ) );
}


void test_set_invalid_name_via_storage ( void ) {
    /* bp_var_set s invalid jménem = no-op, count zůstane 0. */
    bp_var_set ( "1invalid", 42 );
    bp_var_set ( "with-dash", 42 );
    bp_var_set ( "with.dot", 42 );
    TEST_ASSERT_EQUAL_INT ( 0, (int) bp_vars_count ( ) );
}


/* ========================================================================= */
/*  V1.5.B - Comment field                                                   */
/* ========================================================================= */


void test_comment_default_null ( void ) {
    bp_var_set ( "v", 1 );
    TEST_ASSERT_NULL ( bp_var_get_comment ( "v" ) );
}


void test_comment_set_basic ( void ) {
    bp_var_set ( "v", 1 );
    TEST_ASSERT_TRUE ( bp_var_set_comment ( "v", "moje poznámka" ) );
    TEST_ASSERT_EQUAL_STRING ( "moje poznámka", bp_var_get_comment ( "v" ) );
}


void test_comment_set_overwrites ( void ) {
    bp_var_set ( "v", 1 );
    bp_var_set_comment ( "v", "first" );
    bp_var_set_comment ( "v", "second" );
    TEST_ASSERT_EQUAL_STRING ( "second", bp_var_get_comment ( "v" ) );
}


void test_comment_set_null_clears ( void ) {
    bp_var_set ( "v", 1 );
    bp_var_set_comment ( "v", "filled" );
    TEST_ASSERT_TRUE ( bp_var_set_comment ( "v", NULL ) );
    TEST_ASSERT_NULL ( bp_var_get_comment ( "v" ) );
}


void test_comment_set_empty_clears ( void ) {
    bp_var_set ( "v", 1 );
    bp_var_set_comment ( "v", "filled" );
    TEST_ASSERT_TRUE ( bp_var_set_comment ( "v", "" ) );
    TEST_ASSERT_NULL ( bp_var_get_comment ( "v" ) );
}


void test_comment_unknown_var ( void ) {
    /* Comment lze nastavit jen pro existující variable. */
    TEST_ASSERT_FALSE ( bp_var_set_comment ( "nonexistent", "x" ) );
    TEST_ASSERT_NULL ( bp_var_get_comment ( "nonexistent" ) );
}


void test_comment_preserved_by_set ( void ) {
    /* bp_var_set na existing var nesmí smazat comment. */
    bp_var_set ( "v", 1 );
    bp_var_set_comment ( "v", "stay" );
    bp_var_set ( "v", 999 );  /* update value */
    TEST_ASSERT_EQUAL_INT32 ( 999, bp_var_get ( "v" ) );
    TEST_ASSERT_EQUAL_STRING ( "stay", bp_var_get_comment ( "v" ) );
}


/* ========================================================================= */
/*  V1.5.B - Persist value flag                                              */
/* ========================================================================= */


void test_persist_default_true ( void ) {
    bp_var_set ( "v", 1 );
    TEST_ASSERT_TRUE ( bp_var_get_persist ( "v" ) );
}


void test_persist_toggle ( void ) {
    bp_var_set ( "v", 1 );
    TEST_ASSERT_TRUE ( bp_var_set_persist ( "v", false ) );
    TEST_ASSERT_FALSE ( bp_var_get_persist ( "v" ) );
    TEST_ASSERT_TRUE ( bp_var_set_persist ( "v", true ) );
    TEST_ASSERT_TRUE ( bp_var_get_persist ( "v" ) );
}


void test_persist_unknown_var ( void ) {
    /* Set returns false. Get returns true (default sémantika). */
    TEST_ASSERT_FALSE ( bp_var_set_persist ( "nonexistent", false ) );
    TEST_ASSERT_TRUE  ( bp_var_get_persist ( "nonexistent" ) );
}


void test_persist_preserved_by_set ( void ) {
    /* bp_var_set na existing var nesmí přepsat persist flag. */
    bp_var_set ( "v", 1 );
    bp_var_set_persist ( "v", false );
    bp_var_set ( "v", 99 );  /* update value */
    TEST_ASSERT_EQUAL_INT32 ( 99, bp_var_get ( "v" ) );
    TEST_ASSERT_FALSE ( bp_var_get_persist ( "v" ) );
}


/* ========================================================================= */
/*  V1.5.B - bp_var_set_full (atomic create/update)                          */
/* ========================================================================= */


void test_set_full_creates_with_metadata ( void ) {
    bp_var_set_full ( "newvar", 42, "popis", false );
    TEST_ASSERT_EQUAL_INT32 ( 42, bp_var_get ( "newvar" ) );
    TEST_ASSERT_EQUAL_STRING ( "popis", bp_var_get_comment ( "newvar" ) );
    TEST_ASSERT_FALSE ( bp_var_get_persist ( "newvar" ) );
}


void test_set_full_overwrites_existing ( void ) {
    bp_var_set_full ( "v", 1, "first", true );
    bp_var_set_full ( "v", 2, "second", false );
    TEST_ASSERT_EQUAL_INT32 ( 2, bp_var_get ( "v" ) );
    TEST_ASSERT_EQUAL_STRING ( "second", bp_var_get_comment ( "v" ) );
    TEST_ASSERT_FALSE ( bp_var_get_persist ( "v" ) );
}


void test_set_full_with_null_comment ( void ) {
    bp_var_set_full ( "v", 5, NULL, true );
    TEST_ASSERT_NULL ( bp_var_get_comment ( "v" ) );
}


void test_set_full_invalid_name_noop ( void ) {
    bp_var_set_full ( "1bad", 42, "x", true );
    bp_var_set_full ( "with.dot", 42, "x", true );
    TEST_ASSERT_EQUAL_INT ( 0, (int) bp_vars_count ( ) );
}


/* ========================================================================= */
/*  V1.5.B - bp_var_unset                                                    */
/* ========================================================================= */


void test_unset_removes_record ( void ) {
    bp_var_set ( "a", 1 );
    bp_var_set ( "b", 2 );
    TEST_ASSERT_TRUE ( bp_var_unset ( "a" ) );
    TEST_ASSERT_EQUAL_INT ( 1, (int) bp_vars_count ( ) );
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "a" ) );  /* default 0 po unset */
    TEST_ASSERT_EQUAL_INT32 ( 2, bp_var_get ( "b" ) );  /* nedotknuté */
}


void test_unset_unknown_returns_false ( void ) {
    TEST_ASSERT_FALSE ( bp_var_unset ( "nonexistent" ) );
}


void test_unset_null_or_empty_noop ( void ) {
    bp_var_set ( "a", 1 );
    TEST_ASSERT_FALSE ( bp_var_unset ( NULL ) );
    TEST_ASSERT_FALSE ( bp_var_unset ( "" ) );
    TEST_ASSERT_EQUAL_INT ( 1, (int) bp_vars_count ( ) );
}


/* ========================================================================= */
/*  V1.5.B - clear_all preserves metadata                                    */
/* ========================================================================= */


void test_clear_all_preserves_comment_and_persist ( void ) {
    bp_var_set_full ( "a", 10, "alpha note", false );
    bp_var_set_full ( "b", 20, "beta note", true );

    bp_vars_clear_all ( );

    /* Hodnoty = 0. */
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "a" ) );
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "b" ) );
    /* Záznamy zůstávají. */
    TEST_ASSERT_EQUAL_INT ( 2, (int) bp_vars_count ( ) );
    /* Comment + persist beze změny. */
    TEST_ASSERT_EQUAL_STRING ( "alpha note", bp_var_get_comment ( "a" ) );
    TEST_ASSERT_EQUAL_STRING ( "beta note",  bp_var_get_comment ( "b" ) );
    TEST_ASSERT_FALSE ( bp_var_get_persist ( "a" ) );
    TEST_ASSERT_TRUE  ( bp_var_get_persist ( "b" ) );
}


/* ========================================================================= */
/*  V1.5.B - File ops .vars (save / load round-trip)                         */
/* ========================================================================= */


/**
 * Save → wipe → load round-trip basic.
 */
void test_file_save_load_round_trip ( void ) {
    const char *tmpfile = "test_bp_vars_roundtrip.vars";

    bp_var_set_full ( "score", 100, "hra body", true );
    bp_var_set_full ( "lives", 3,    NULL,        true );

    char err[256] = {0};
    TEST_ASSERT_TRUE ( bp_vars_save_to_filepath ( tmpfile, err, sizeof ( err ) ) );

    /* Wipe a load. */
    bp_vars_clear_storage ( );
    TEST_ASSERT_EQUAL_INT ( 0, (int) bp_vars_count ( ) );

    TEST_ASSERT_TRUE ( bp_vars_load_from_filepath ( tmpfile, BP_VARS_LOAD_REPLACE,
                                                     err, sizeof ( err ) ) );

    TEST_ASSERT_EQUAL_INT ( 2, (int) bp_vars_count ( ) );
    TEST_ASSERT_EQUAL_INT32  ( 100, bp_var_get ( "score" ) );
    TEST_ASSERT_EQUAL_INT32  ( 3,   bp_var_get ( "lives" ) );
    TEST_ASSERT_EQUAL_STRING ( "hra body", bp_var_get_comment ( "score" ) );
    TEST_ASSERT_NULL         ( bp_var_get_comment ( "lives" ) );
    TEST_ASSERT_TRUE  ( bp_var_get_persist ( "score" ) );
    TEST_ASSERT_TRUE  ( bp_var_get_persist ( "lives" ) );

    remove ( tmpfile );
}


/**
 * persist_value=false: value se neukládá, při loadu = 0.
 */
void test_file_persist_false_resets_value_on_load ( void ) {
    const char *tmpfile = "test_bp_vars_persist_false.vars";

    bp_var_set_full ( "runtime_counter", 12345, "ephemeral", false );

    char err[256] = {0};
    TEST_ASSERT_TRUE ( bp_vars_save_to_filepath ( tmpfile, err, sizeof ( err ) ) );

    bp_vars_clear_storage ( );
    TEST_ASSERT_TRUE ( bp_vars_load_from_filepath ( tmpfile, BP_VARS_LOAD_REPLACE,
                                                     err, sizeof ( err ) ) );

    TEST_ASSERT_EQUAL_INT ( 1, (int) bp_vars_count ( ) );
    /* Value RESET na 0 i kdyby v souboru byla. */
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "runtime_counter" ) );
    /* Comment + flag zachovány. */
    TEST_ASSERT_EQUAL_STRING ( "ephemeral", bp_var_get_comment ( "runtime_counter" ) );
    TEST_ASSERT_FALSE ( bp_var_get_persist ( "runtime_counter" ) );

    remove ( tmpfile );
}


/**
 * BC fallback - missing persist_value v JSON → default true.
 */
void test_file_load_missing_persist_defaults_true ( void ) {
    const char *tmpfile = "test_bp_vars_bc_no_persist.vars";

    /* Ručně vyrobený JSON V1 schema (bez persist_value, bez comment).
     * Path musí být resolvována stejně jako loader (cfg dir). */
    char *resolved = sdlapp_paths_resolve_cfg ( g_sdlapp->paths, tmpfile );
    TEST_ASSERT_NOT_NULL ( resolved );

    FILE *f = fopen ( resolved, "w" );
    TEST_ASSERT_NOT_NULL ( f );
    fprintf ( f, "{\n  \"vars\": [\n"
                 "    {\"name\": \"legacy\", \"value\": 42}\n"
                 "  ]\n}\n" );
    fclose ( f );

    char err[256] = {0};
    TEST_ASSERT_TRUE ( bp_vars_load_from_filepath ( tmpfile, BP_VARS_LOAD_REPLACE,
                                                     err, sizeof ( err ) ) );

    TEST_ASSERT_EQUAL_INT ( 1, (int) bp_vars_count ( ) );
    TEST_ASSERT_EQUAL_INT32 ( 42, bp_var_get ( "legacy" ) );
    TEST_ASSERT_NULL ( bp_var_get_comment ( "legacy" ) );
    TEST_ASSERT_TRUE ( bp_var_get_persist ( "legacy" ) );  /* BC default */

    remove ( resolved );
    g_free ( resolved );
}


/* ========================================================================= */
/*  V1.5.B - Load modes                                                      */
/* ========================================================================= */


/**
 * REPLACE: storage se před loadem wipne.
 */
void test_load_mode_replace_wipes_existing ( void ) {
    const char *tmpfile = "test_bp_vars_replace.vars";

    bp_var_set ( "in_storage", 1 );
    bp_var_set_full ( "from_file", 999, "a", true );  /* zapíšeme jen tohle */
    /* Save snapshot s "from_file" + "in_storage". */
    char err[256] = {0};
    bp_vars_save_to_filepath ( tmpfile, err, sizeof ( err ) );
    bp_vars_clear_storage ( );
    bp_vars_load_from_filepath ( tmpfile, BP_VARS_LOAD_REPLACE, err, sizeof ( err ) );
    TEST_ASSERT_EQUAL_INT ( 2, (int) bp_vars_count ( ) );

    /* Nyní setup čistého storage s něčím a REPLACE z file. */
    bp_vars_clear_storage ( );
    bp_var_set ( "should_be_wiped", 555 );
    TEST_ASSERT_EQUAL_INT ( 1, (int) bp_vars_count ( ) );

    bp_vars_load_from_filepath ( tmpfile, BP_VARS_LOAD_REPLACE, err, sizeof ( err ) );

    /* Po REPLACE jen co bylo v souboru. */
    TEST_ASSERT_EQUAL_INT ( 2, (int) bp_vars_count ( ) );
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "should_be_wiped" ) );

    remove ( tmpfile );
}


/**
 * MERGE_OVERWRITE: file vyhrává konflikt jména.
 */
void test_load_mode_merge_overwrite_file_wins ( void ) {
    const char *tmpfile = "test_bp_vars_merge_ow.vars";

    /* Připrav file s "shared" = 100, "from_file" = 200. */
    bp_var_set ( "shared",     100 );
    bp_var_set ( "from_file",  200 );
    char err[256] = {0};
    bp_vars_save_to_filepath ( tmpfile, err, sizeof ( err ) );

    /* Reset + naplň storage tím, co se má merge. */
    bp_vars_clear_storage ( );
    bp_var_set ( "shared",     999 );  /* konflikt - file vyhrává */
    bp_var_set ( "from_storage", 50 );

    bp_vars_load_from_filepath ( tmpfile, BP_VARS_LOAD_MERGE_OVERWRITE,
                                  err, sizeof ( err ) );

    TEST_ASSERT_EQUAL_INT32 ( 100, bp_var_get ( "shared" ) );      /* file */
    TEST_ASSERT_EQUAL_INT32 ( 200, bp_var_get ( "from_file" ) );   /* nová z file */
    TEST_ASSERT_EQUAL_INT32 ( 50,  bp_var_get ( "from_storage" ) ); /* zachována */

    remove ( tmpfile );
}


/**
 * MERGE_SKIP: existing vyhrává konflikt jména.
 */
void test_load_mode_merge_skip_existing_wins ( void ) {
    const char *tmpfile = "test_bp_vars_merge_skip.vars";

    bp_var_set ( "shared",     100 );
    bp_var_set ( "from_file",  200 );
    char err[256] = {0};
    bp_vars_save_to_filepath ( tmpfile, err, sizeof ( err ) );

    bp_vars_clear_storage ( );
    bp_var_set ( "shared",     999 );  /* konflikt - existing vyhrává */
    bp_var_set ( "from_storage", 50 );

    bp_vars_load_from_filepath ( tmpfile, BP_VARS_LOAD_MERGE_SKIP,
                                  err, sizeof ( err ) );

    TEST_ASSERT_EQUAL_INT32 ( 999, bp_var_get ( "shared" ) );      /* existing */
    TEST_ASSERT_EQUAL_INT32 ( 200, bp_var_get ( "from_file" ) );   /* nová z file */
    TEST_ASSERT_EQUAL_INT32 ( 50,  bp_var_get ( "from_storage" ) );

    remove ( tmpfile );
}


/* ========================================================================= */
/*  V1.5.B - Default per-arch filepath                                       */
/* ========================================================================= */


void test_default_filepath_returns_nonempty ( void ) {
    char *path = bp_vars_default_filepath ( );
    if ( path ) {
        /* Cfg dir může být dostupný - jen ověř že path je sane. */
        TEST_ASSERT_TRUE ( strlen ( path ) > 0 );
        /* Per-arch filename na konci cesty. */
        const char *expected_suffix =
#if MZARCH == 1500
            "mz1500.vars"
#elif MZARCH == 700
            "mz700.vars"
#else
            "mz800.vars"
#endif
            ;
        const char *found = strstr ( path, expected_suffix );
        TEST_ASSERT_NOT_NULL ( found );
        free ( path );
    } else {
        /* Cfg dir neaccessible v test env - akceptujeme. */
        TEST_PASS_MESSAGE ( "cfg dir nedostupný v testu, default_filepath = NULL" );
    };
}


/* ========================================================================= */
/*  MAIN                                                                     */
/* ========================================================================= */


int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    /* V1 - basic CRUD */
    RUN_TEST ( test_default_zero_for_missing );
    RUN_TEST ( test_set_get_basic );
    RUN_TEST ( test_set_multiple );
    RUN_TEST ( test_set_overwrite_existing );
    RUN_TEST ( test_set_invalid_name_noop );
    RUN_TEST ( test_set_case_sensitive );
    RUN_TEST ( test_clear_all_resets_values );
    RUN_TEST ( test_clear_storage_removes_entries );
    RUN_TEST ( test_get_by_index );
    RUN_TEST ( test_expr_reads_var );
    RUN_TEST ( test_expr_unknown_var_zero );
    RUN_TEST ( test_init_idempotent );

    /* V1.5.B - Name regex validation */
    RUN_TEST ( test_name_empty_invalid );
    RUN_TEST ( test_name_starts_with_letter );
    RUN_TEST ( test_name_starts_with_underscore );
    RUN_TEST ( test_name_starts_with_digit_invalid );
    RUN_TEST ( test_name_contains_invalid_chars );
    RUN_TEST ( test_name_with_digits_after_first_char );
    RUN_TEST ( test_name_max_length_31 );
    RUN_TEST ( test_name_keyword_allowed );
    RUN_TEST ( test_set_invalid_name_via_storage );

    /* V1.5.B - Comment field */
    RUN_TEST ( test_comment_default_null );
    RUN_TEST ( test_comment_set_basic );
    RUN_TEST ( test_comment_set_overwrites );
    RUN_TEST ( test_comment_set_null_clears );
    RUN_TEST ( test_comment_set_empty_clears );
    RUN_TEST ( test_comment_unknown_var );
    RUN_TEST ( test_comment_preserved_by_set );

    /* V1.5.B - Persist value flag */
    RUN_TEST ( test_persist_default_true );
    RUN_TEST ( test_persist_toggle );
    RUN_TEST ( test_persist_unknown_var );
    RUN_TEST ( test_persist_preserved_by_set );

    /* V1.5.B - bp_var_set_full */
    RUN_TEST ( test_set_full_creates_with_metadata );
    RUN_TEST ( test_set_full_overwrites_existing );
    RUN_TEST ( test_set_full_with_null_comment );
    RUN_TEST ( test_set_full_invalid_name_noop );

    /* V1.5.B - bp_var_unset */
    RUN_TEST ( test_unset_removes_record );
    RUN_TEST ( test_unset_unknown_returns_false );
    RUN_TEST ( test_unset_null_or_empty_noop );

    /* V1.5.B - clear_all preserves metadata */
    RUN_TEST ( test_clear_all_preserves_comment_and_persist );

    /* V1.5.B - File ops .vars */
    RUN_TEST ( test_file_save_load_round_trip );
    RUN_TEST ( test_file_persist_false_resets_value_on_load );
    RUN_TEST ( test_file_load_missing_persist_defaults_true );

    /* V1.5.B - Load modes */
    RUN_TEST ( test_load_mode_replace_wipes_existing );
    RUN_TEST ( test_load_mode_merge_overwrite_file_wins );
    RUN_TEST ( test_load_mode_merge_skip_existing_wins );

    /* V1.5.B - Default per-arch filepath */
    RUN_TEST ( test_default_filepath_returns_nonempty );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
