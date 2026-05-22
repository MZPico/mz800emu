/*
 * test_bp_sp_threshold.c - testy SP threshold BP (D.4)
 *
 * Pokrytí:
 *   - bptmap registrace SP_THRESHOLD BP do BPTMAP_IDX_SP_THRESHOLD
 *   - edge crossing dolů (old >= th, new < th) = hit
 *   - "už pod prahem" (= prev < th) = no hit
 *   - "stoupá" (new > old) = no hit
 *   - sustained pod prahem = jen 1 hit (ne každé volání)
 *   - více SP_THRESHOLD BP s různými prahy = nezávislý fire
 *   - condition + skip/hit_count + action continue
 *   - JSON persist sp_threshold round-trip
 *
 * Test mockuje MSG dispatcher a volá enforce_sp_threshold přímo
 * (= unit test bez živého CPU). Hot loop integraci ověří manual smoke
 * (mzdos kernel stack overflow scénář, viz IMPLEMENTATION-NOTES.md).
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include "debugger/breakpoints.h"
#include "debugger/bptmap.h"
#include "debugger/bp_expr.h"
#include "debugger/bp_action.h"
#include "debugger/bp_vars.h"
#include "debugger/debugger.h"
#include "debugger/dbgapi_emu.h"
#include "debugger/dbgapi_msg.h"
#include "emulator/emulator.h"

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>


/* ========================================================================= */
/*  Mock MSG dispatcher                                                       */
/* ========================================================================= */


typedef struct {
    int call_count;
    en_DBGAPI_MSG last_msg;
    int last_id;
    uint16_t last_addr;
} mock_msg_state_t;

static mock_msg_state_t s_mock_state;


static void mock_msg_dispatcher ( en_DBGAPI_MSG msg,
                                   st_DBGAPI_MSG_DATA *data,
                                   void *user_data ) {
    (void) user_data;
    s_mock_state.call_count++;
    s_mock_state.last_msg = msg;
    if ( data ) {
        s_mock_state.last_id = data->id;
        s_mock_state.last_addr = data->addr;
        g_free ( data );
    };
}


static void mock_msg_reset ( void ) {
    memset ( &s_mock_state, 0, sizeof ( s_mock_state ) );
}


/* ========================================================================= */
/*  setUp / tearDown                                                          */
/* ========================================================================= */


void setUp ( void ) {
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    bp_vars_clear_storage ( );
    g_emulator.paused = false;
    mock_msg_reset ( );
    dbgapi_emu_register_msg_dispatcher ( mock_msg_dispatcher, NULL );
}


void tearDown ( void ) {
    dbgapi_emu_register_msg_dispatcher ( NULL, NULL );
}


/* ========================================================================= */
/*  Helper                                                                    */
/* ========================================================================= */


/**
 * Vytvoří SP_THRESHOLD BP s daným threshold.
 */
static int make_sp_bp ( uint16_t threshold ) {
    int id = breakpoints_add ( 0, "T", -1 );
    breakpoints_set_type ( id, BPT_TYPE_SP_THRESHOLD );
    breakpoints_set_sp_threshold ( id, threshold );
    return id;
}


/* ========================================================================= */
/*  bptmap registrace                                                         */
/* ========================================================================= */


/**
 * SP_THRESHOLD BP se registruje do BPTMAP_IDX_SP_THRESHOLD slotu.
 * per_type_active flag = true.
 */
void test_bptmap_register_sp_threshold ( void ) {
    int id = make_sp_bp ( 0xFE82 );

    GArray *list = bptmap_get_list ( BPTMAP_IDX_SP_THRESHOLD );
    TEST_ASSERT_NOT_NULL ( list );
    TEST_ASSERT_EQUAL ( 1, list->len );
    TEST_ASSERT_EQUAL ( id, g_array_index ( list, int, 0 ) );
    TEST_ASSERT_TRUE ( g_bptmap.per_type_active[ BPTMAP_IDX_SP_THRESHOLD ] );
    TEST_ASSERT_TRUE ( g_bptmap.any_active );
}


/**
 * Disable BP -> unregister, per_type_active = false.
 */
void test_bptmap_unregister_on_disable ( void ) {
    int id = make_sp_bp ( 0xFE82 );
    TEST_ASSERT_TRUE ( g_bptmap.per_type_active[ BPTMAP_IDX_SP_THRESHOLD ] );

    breakpoints_set_enabled ( id, false );
    TEST_ASSERT_FALSE ( g_bptmap.per_type_active[ BPTMAP_IDX_SP_THRESHOLD ] );
    TEST_ASSERT_FALSE ( g_bptmap.any_active );

    breakpoints_set_enabled ( id, true );
    TEST_ASSERT_TRUE ( g_bptmap.per_type_active[ BPTMAP_IDX_SP_THRESHOLD ] );
}


/* ========================================================================= */
/*  Edge crossing semantika                                                   */
/* ========================================================================= */


/**
 * Klasický edge dolů: old=FF00, new=FE80, threshold=FE82 -> hit.
 */
void test_edge_crossing_down_hit ( void ) {
    int id = make_sp_bp ( 0xFE82 );

    breakpoints_enforce_sp_threshold ( 0xFF00, 0xFE80 );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 1, bpt->hits );
    TEST_ASSERT_TRUE ( g_emulator.paused );
    TEST_ASSERT_EQUAL_INT ( 1, s_mock_state.call_count );
    TEST_ASSERT_EQUAL_INT ( DBGAPI_MSG_BREAKPOINT_HIT, s_mock_state.last_msg );
    TEST_ASSERT_EQUAL_INT ( id, s_mock_state.last_id );
    /* MSG addr payload = bpt->addr (= legacy D.2 chování, PC EXEC
     * sémantika). Pro SP_THRESHOLD BP je bpt->addr nepoužitý a zůstává
     * 0. V1 omezení - V1.5 zvážit přepnutí na ctx->Address pro
     * smart MSG payload (= new_sp pro stack overflow detekci). */
    TEST_ASSERT_EQUAL_HEX16 ( 0x0000, s_mock_state.last_addr );
}


/**
 * Edge přesně na hranici: old=FE82, new=FE81, threshold=FE82.
 * Old >= threshold (= rovno), new < threshold -> hit.
 */
void test_edge_exactly_at_threshold ( void ) {
    int id = make_sp_bp ( 0xFE82 );

    breakpoints_enforce_sp_threshold ( 0xFE82, 0xFE81 );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 1, bpt->hits );
    TEST_ASSERT_TRUE ( g_emulator.paused );
}


/**
 * Už pod prahem: old=FE80 (< threshold), new=FE70 -> no hit.
 * Nesplňuje "old >= threshold" část edge podmínky.
 */
void test_already_below_no_hit ( void ) {
    int id = make_sp_bp ( 0xFE82 );

    breakpoints_enforce_sp_threshold ( 0xFE80, 0xFE70 );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 0, bpt->hits );
    TEST_ASSERT_FALSE ( g_emulator.paused );
    TEST_ASSERT_EQUAL_INT ( 0, s_mock_state.call_count );
}


/**
 * Stoupá nad: old=FE70 (< threshold), new=FF00 (> threshold) -> no hit.
 * Žádné crossing dolů.
 */
void test_rising_no_hit ( void ) {
    int id = make_sp_bp ( 0xFE82 );

    breakpoints_enforce_sp_threshold ( 0xFE70, 0xFF00 );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 0, bpt->hits );
    TEST_ASSERT_FALSE ( g_emulator.paused );
}


/**
 * Sustained pod prahem - simulace více push instrukcí pod prahem
 * fires jen 1x (= když SP poprvé klesne pod threshold).
 *
 * Scénář:
 *   1. SP=FF00 -> SP=FE80 (= edge crossing) ... 1. hit
 *   2. SP=FE80 -> SP=FE7E (= další push pod prahem) ... no hit
 *   3. SP=FE7E -> SP=FE7C (= a další) ... no hit
 *   4. SP=FE7C -> SP=FF00 (= ret zpět nahoru) ... no hit
 *   5. SP=FF00 -> SP=FE80 (= znovu klesne pod) ... 2. hit
 *
 * Mezi 1. a 4. krokem clearujeme paused (= simulace continue přes
 * action). Action 'continue' by toto dělala automaticky, ale my zde
 * testujeme jen sémantiku enforce_sp_threshold.
 */
void test_sustained_below_single_hit ( void ) {
    int id = make_sp_bp ( 0xFE82 );

    /* 1. - hit */
    breakpoints_enforce_sp_threshold ( 0xFF00, 0xFE80 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 1, bpt->hits );

    /* clear pause pro další iteraci */
    g_emulator.paused = false;

    /* 2. - no hit */
    breakpoints_enforce_sp_threshold ( 0xFE80, 0xFE7E );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 1, bpt->hits );
    TEST_ASSERT_FALSE ( g_emulator.paused );

    /* 3. - no hit */
    breakpoints_enforce_sp_threshold ( 0xFE7E, 0xFE7C );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 1, bpt->hits );

    /* 4. - no hit (jdeme nahoru) */
    breakpoints_enforce_sp_threshold ( 0xFE7C, 0xFF00 );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 1, bpt->hits );

    /* 5. - znovu klesne pod -> hit */
    breakpoints_enforce_sp_threshold ( 0xFF00, 0xFE80 );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 2, bpt->hits );
    TEST_ASSERT_TRUE ( g_emulator.paused );
}


/* ========================================================================= */
/*  Více BP, různé thresholdy                                                 */
/* ========================================================================= */


/**
 * Dva BP s různými prahy.
 *   BP1 threshold=FF00, BP2 threshold=FE82.
 *   Crossing FF20 -> FEFF: BP1 fires (FE82 < FEFF, ale FE82 ne, FF00 ano).
 *   Crossing FEFF -> FE70: BP2 fires (FE82 ano, FF00 nesedí - prev už pod).
 */
void test_multiple_bps_independent_fire ( void ) {
    int id_high = make_sp_bp ( 0xFF00 );
    int id_low  = make_sp_bp ( 0xFE82 );

    /* FF20 -> FEFF: prev >= FF00 (true, FF20 >= FF00), curr < FF00 (true, FEFF < FF00) -> BP1 hit
     *               prev >= FE82 (true), curr < FE82 (false, FEFF >= FE82) -> BP2 no hit */
    breakpoints_enforce_sp_threshold ( 0xFF20, 0xFEFF );
    st_BPT *b1 = breakpoints_find_by_id ( id_high );
    st_BPT *b2 = breakpoints_find_by_id ( id_low );
    TEST_ASSERT_EQUAL_UINT64 ( 1, b1->hits );
    TEST_ASSERT_EQUAL_UINT64 ( 0, b2->hits );

    /* První fire spustil pause - ostatní BP v listu se ale stejně iterují
     * dokud nezablokuje EMULATOR_TEST_PAUSED. Test zde nedává jasné
     * pořadí - kontrolujeme jen finální hit count. Reset paused pro
     * druhou iteraci. */
    g_emulator.paused = false;

    /* FEFF -> FE70: BP1 prev=FEFF, FEFF < FF00 -> už pod, no hit.
     *               BP2 prev=FEFF >= FE82, curr=FE70 < FE82 -> hit. */
    breakpoints_enforce_sp_threshold ( 0xFEFF, 0xFE70 );
    b1 = breakpoints_find_by_id ( id_high );
    b2 = breakpoints_find_by_id ( id_low );
    TEST_ASSERT_EQUAL_UINT64 ( 1, b1->hits );
    TEST_ASSERT_EQUAL_UINT64 ( 1, b2->hits );
}


/* ========================================================================= */
/*  Condition / action                                                        */
/* ========================================================================= */


/**
 * Condition false -> no hit i přes edge crossing.
 *
 * V V1 jsou globální vars Frame=0 (TODO doplnit z emu state). Condition
 * "Frame > 5" tedy false -> condition NEpovolí hit.
 */
void test_condition_false_no_hit ( void ) {
    int id = make_sp_bp ( 0xFE82 );
    breakpoints_set_expr ( id, "Frame > 5" );

    breakpoints_enforce_sp_threshold ( 0xFF00, 0xFE80 );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 0, bpt->hits );
    TEST_ASSERT_FALSE ( g_emulator.paused );
}


/**
 * Action 'continue' = fire bez pause, jen log/hits inkrement.
 */
void test_action_continue_no_pause ( void ) {
    int id = make_sp_bp ( 0xFE82 );
    breakpoints_set_action ( id, "continue" );

    breakpoints_enforce_sp_threshold ( 0xFF00, 0xFE80 );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 1, bpt->hits );
    TEST_ASSERT_FALSE ( g_emulator.paused );
    TEST_ASSERT_EQUAL_INT ( 0, s_mock_state.call_count );
}


/* ========================================================================= */
/*  Žádný BP                                                                  */
/* ========================================================================= */


/**
 * Volání enforce_sp_threshold bez registrovaného BP = no-op.
 * (V hot loop tomu předchází fast-skip přes per_type_active, ale i přímé
 * volání musí být safe.)
 */
void test_no_bp_no_op ( void ) {
    breakpoints_enforce_sp_threshold ( 0xFF00, 0xFE80 );
    TEST_ASSERT_FALSE ( g_emulator.paused );
    TEST_ASSERT_EQUAL_INT ( 0, s_mock_state.call_count );
}


/**
 * Volání s old_sp == new_sp = no-op (= sanity, nemělo by nastat z hot
 * loop ale enforce funkce by neměla padat).
 */
void test_equal_sp_no_op ( void ) {
    int id = make_sp_bp ( 0xFE82 );

    breakpoints_enforce_sp_threshold ( 0xFE80, 0xFE80 );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 0, bpt->hits );
    TEST_ASSERT_FALSE ( g_emulator.paused );
}


/* ========================================================================= */
/*  Persist - JSON round-trip sp_threshold                                    */
/* ========================================================================= */


/**
 * Save BP s sp_threshold=0xFE82 do tmp souboru, clear, load, ověř že
 * sp_threshold = 0xFE82 a typ = SP_THRESHOLD.
 */
void test_persist_sp_threshold_roundtrip ( void ) {
    int id = make_sp_bp ( 0xFE82 );
    breakpoints_set_action ( id, "continue" );

    gchar *tmpfile = g_build_filename ( g_get_tmp_dir ( ),
                                         "test_bp_sp_threshold.bpt", NULL );
    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    TEST_ASSERT_FALSE ( g_bptmap.per_type_active[ BPTMAP_IDX_SP_THRESHOLD ] );

    breakpoints_load_from_filepath ( tmpfile );
    TEST_ASSERT_EQUAL_INT ( 1, g_breakpoints.breakpoints->len );

    st_BPT *bpt = &g_array_index ( g_breakpoints.breakpoints, st_BPT, 0 );
    TEST_ASSERT_EQUAL ( BPT_TYPE_SP_THRESHOLD, bpt->type );
    TEST_ASSERT_EQUAL_HEX16 ( 0xFE82, bpt->sp_threshold );
    TEST_ASSERT_TRUE ( g_bptmap.per_type_active[ BPTMAP_IDX_SP_THRESHOLD ] );

    g_unlink ( tmpfile );
    g_free ( tmpfile );
}


/* ========================================================================= */
/*  Test runner                                                               */
/* ========================================================================= */


int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    /* bptmap registrace */
    RUN_TEST ( test_bptmap_register_sp_threshold );
    RUN_TEST ( test_bptmap_unregister_on_disable );

    /* Edge sémantika */
    RUN_TEST ( test_edge_crossing_down_hit );
    RUN_TEST ( test_edge_exactly_at_threshold );
    RUN_TEST ( test_already_below_no_hit );
    RUN_TEST ( test_rising_no_hit );
    RUN_TEST ( test_sustained_below_single_hit );

    /* Více BP */
    RUN_TEST ( test_multiple_bps_independent_fire );

    /* Condition + action */
    RUN_TEST ( test_condition_false_no_hit );
    RUN_TEST ( test_action_continue_no_pause );

    /* Sanity */
    RUN_TEST ( test_no_bp_no_op );
    RUN_TEST ( test_equal_sp_no_op );

    /* Persist */
    RUN_TEST ( test_persist_sp_threshold_roundtrip );

    return UNITY_END ( );
}
