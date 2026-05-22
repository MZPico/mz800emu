/*
 * test_breakpoints_v2.c — testy smart BP V1 (post fáze D.1)
 *
 * Doplňuje existující test_breakpoints.c o pokrytí nových polí st_BPT,
 * jejich setterů, string konverzí typů/zón a JSON round-trip persistence.
 *
 * Existující test_breakpoints.c běží v plné podobě dál (= regrese pro
 * legacy PC_EXEC chování).
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include "debugger/breakpoints.h"
#include "debugger/bp_expr.h"
#include "debugger/bp_vars.h"
#include "debugger/bptmap.h"

#include <glib.h>
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <string.h>


/* ========================================================================= */
/*  setUp / tearDown                                                         */
/* ========================================================================= */


void setUp ( void ) {
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
}


void tearDown ( void ) {
}


/* ========================================================================= */
/*  Default values pro nový BP                                               */
/* ========================================================================= */


void test_bpt_v2_defaults_after_add ( void ) {
    int id = breakpoints_add ( 0x1234, "Test", -1 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );

    /* Smart BP V1 defaulty */
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_PC_EXEC, bpt->type );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1234, bpt->addr_end );  /* single-point default */
    TEST_ASSERT_EQUAL_INT ( BP_ZONE_CPU_VIEW, bpt->zone );
    TEST_ASSERT_EQUAL_UINT8 ( 0, bpt->bank_id );
    TEST_ASSERT_EQUAL_HEX16 ( 0, bpt->port );
    TEST_ASSERT_NULL ( bpt->event_name );
    TEST_ASSERT_EQUAL_HEX16 ( 0, bpt->sp_threshold );
    TEST_ASSERT_NULL ( bpt->expr );
    TEST_ASSERT_NULL ( bpt->action );
    TEST_ASSERT_EQUAL_UINT32 ( 0, bpt->hit_count );
    TEST_ASSERT_EQUAL_UINT32 ( 0, bpt->skip_count );
    TEST_ASSERT_FALSE ( bpt->edge_triggered );
}


void test_bpt_v2_defaults_after_add_auto ( void ) {
    int id = breakpoints_add_auto ( 0xABCD, "AutoTest", -1 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );

    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_PC_EXEC, bpt->type );
    TEST_ASSERT_EQUAL_HEX16 ( 0xABCD, bpt->addr_end );
    TEST_ASSERT_EQUAL_INT ( BP_ZONE_CPU_VIEW, bpt->zone );
}


/* ========================================================================= */
/*  Setters pro nová pole                                                    */
/* ========================================================================= */


void test_set_type ( void ) {
    int id = breakpoints_add ( 0x1000, "T", -1 );
    TEST_ASSERT_TRUE ( breakpoints_set_type ( id, BPT_TYPE_MEM_W ) );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_MEM_W, bpt->type );

    /* Out-of-range type rejected */
    TEST_ASSERT_FALSE ( breakpoints_set_type ( id, BPT_TYPE_COUNT ) );
    TEST_ASSERT_FALSE ( breakpoints_set_type ( id, (en_BPT_TYPE) -1 ) );

    /* Hodnota nezměněna po neúspěšném setteru */
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_MEM_W, bpt->type );
}


void test_set_zone ( void ) {
    int id = breakpoints_add ( 0x1000, "Z", -1 );
    TEST_ASSERT_TRUE ( breakpoints_set_zone ( id, BP_ZONE_MMEXT_BANK ) );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_INT ( BP_ZONE_MMEXT_BANK, bpt->zone );

    TEST_ASSERT_FALSE ( breakpoints_set_zone ( id, BP_ZONE_COUNT ) );
}


void test_set_bank_id_port_sp ( void ) {
    int id = breakpoints_add ( 0x1000, "X", -1 );

    TEST_ASSERT_TRUE ( breakpoints_set_bank_id ( id, 9 ) );
    TEST_ASSERT_TRUE ( breakpoints_set_port ( id, 0x00CE ) );
    TEST_ASSERT_TRUE ( breakpoints_set_sp_threshold ( id, 0xFE82 ) );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT8 ( 9, bpt->bank_id );
    TEST_ASSERT_EQUAL_HEX16 ( 0x00CE, bpt->port );
    TEST_ASSERT_EQUAL_HEX16 ( 0xFE82, bpt->sp_threshold );
}


void test_set_addr_range ( void ) {
    int id = breakpoints_add ( 0x4000, "R", -1 );
    TEST_ASSERT_TRUE ( breakpoints_set_addr_range ( id, 0x4000, 0x40FF ) );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_HEX16 ( 0x4000, bpt->addr );
    TEST_ASSERT_EQUAL_HEX16 ( 0x40FF, bpt->addr_end );
}


void test_set_addr_end_independent ( void ) {
    int id = breakpoints_add ( 0x4000, "R", -1 );
    /* Bez addr_range - jen end */
    TEST_ASSERT_TRUE ( breakpoints_set_addr_end ( id, 0x40FF ) );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_HEX16 ( 0x4000, bpt->addr );
    TEST_ASSERT_EQUAL_HEX16 ( 0x40FF, bpt->addr_end );
}


void test_set_addr_keeps_addr_end_for_range ( void ) {
    /* Pro range BP (addr_end > addr) musí set_addr ponechat addr_end. */
    int id = breakpoints_add ( 0x4000, "R", -1 );
    breakpoints_set_addr_range ( id, 0x4000, 0x40FF );

    breakpoints_set_addr ( id, 0x5000 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_HEX16 ( 0x5000, bpt->addr );
    TEST_ASSERT_EQUAL_HEX16 ( 0x40FF, bpt->addr_end );  /* nezměněno */
}


void test_set_addr_syncs_addr_end_for_single_point ( void ) {
    /* Pro single-point BP (addr_end == addr) musí set_addr addr_end syncnout. */
    int id = breakpoints_add ( 0x4000, "S", -1 );
    /* addr_end automaticky == 0x4000 */

    breakpoints_set_addr ( id, 0x5000 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_HEX16 ( 0x5000, bpt->addr );
    TEST_ASSERT_EQUAL_HEX16 ( 0x5000, bpt->addr_end );
}


void test_set_event_name ( void ) {
    int id = breakpoints_add ( 0x1000, "E", -1 );

    TEST_ASSERT_TRUE ( breakpoints_set_event_name ( id, "vsync" ) );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_STRING ( "vsync", bpt->event_name );

    /* Re-set kopíruje (nesmí padat na původním stringu) */
    TEST_ASSERT_TRUE ( breakpoints_set_event_name ( id, "ctc:zc0" ) );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_STRING ( "ctc:zc0", bpt->event_name );

    /* NULL = vyčistí */
    TEST_ASSERT_TRUE ( breakpoints_set_event_name ( id, NULL ) );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NULL ( bpt->event_name );
}


void test_set_expr_action ( void ) {
    int id = breakpoints_add ( 0x1000, "EA", -1 );

    TEST_ASSERT_TRUE ( breakpoints_set_expr ( id, "A == 0x42 && PC > 0x4000" ) );
    TEST_ASSERT_TRUE ( breakpoints_set_action ( id, "log \"hit\"; continue" ) );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_STRING ( "A == 0x42 && PC > 0x4000", bpt->expr );
    TEST_ASSERT_EQUAL_STRING ( "log \"hit\"; continue", bpt->action );

    /* NULL = vyčistí */
    TEST_ASSERT_TRUE ( breakpoints_set_expr ( id, NULL ) );
    TEST_ASSERT_TRUE ( breakpoints_set_action ( id, NULL ) );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NULL ( bpt->expr );
    TEST_ASSERT_NULL ( bpt->action );
}


void test_set_hit_skip_edge ( void ) {
    int id = breakpoints_add ( 0x1000, "HSE", -1 );

    TEST_ASSERT_TRUE ( breakpoints_set_hit_count ( id, 7 ) );
    TEST_ASSERT_TRUE ( breakpoints_set_skip_count ( id, 3 ) );
    TEST_ASSERT_TRUE ( breakpoints_set_edge_triggered ( id, true ) );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT32 ( 7, bpt->hit_count );
    TEST_ASSERT_EQUAL_UINT32 ( 3, bpt->skip_count );
    TEST_ASSERT_TRUE ( bpt->edge_triggered );
}


void test_setters_on_nonexistent_id_fail ( void ) {
    TEST_ASSERT_FALSE ( breakpoints_set_type ( 9999, BPT_TYPE_MEM_R ) );
    TEST_ASSERT_FALSE ( breakpoints_set_zone ( 9999, BP_ZONE_RAM ) );
    TEST_ASSERT_FALSE ( breakpoints_set_port ( 9999, 0xCE ) );
    TEST_ASSERT_FALSE ( breakpoints_set_event_name ( 9999, "vsync" ) );
    TEST_ASSERT_FALSE ( breakpoints_set_expr ( 9999, "1" ) );
    TEST_ASSERT_FALSE ( breakpoints_set_action ( 9999, "continue" ) );
    TEST_ASSERT_FALSE ( breakpoints_set_hit_count ( 9999, 1 ) );
    TEST_ASSERT_FALSE ( breakpoints_set_skip_count ( 9999, 1 ) );
    TEST_ASSERT_FALSE ( breakpoints_set_edge_triggered ( 9999, true ) );
    TEST_ASSERT_FALSE ( breakpoints_set_addr_end ( 9999, 0xFFFF ) );
    TEST_ASSERT_FALSE ( breakpoints_set_addr_range ( 9999, 0, 0xFFFF ) );
    TEST_ASSERT_FALSE ( breakpoints_set_bank_id ( 9999, 1 ) );
    TEST_ASSERT_FALSE ( breakpoints_set_sp_threshold ( 9999, 0 ) );
}


/* ========================================================================= */
/*  String konverze typ a zóna                                               */
/* ========================================================================= */


void test_type_to_string_roundtrip ( void ) {
    int t;
    for ( t = 0; t < BPT_TYPE_COUNT; t++ ) {
        const char *s = bpt_type_to_string ( (en_BPT_TYPE) t );
        TEST_ASSERT_NOT_NULL ( s );
        TEST_ASSERT_TRUE ( s[0] != '\0' );

        en_BPT_TYPE back;
        TEST_ASSERT_TRUE ( bpt_type_from_string ( s, &back ) );
        TEST_ASSERT_EQUAL_INT ( t, back );
    };
}


void test_type_from_string_unknown ( void ) {
    en_BPT_TYPE out = BPT_TYPE_HW_EVENT; /* sentinel jiný než PC_EXEC */
    TEST_ASSERT_FALSE ( bpt_type_from_string ( "BOGUS", &out ) );
    /* Při chybě out zůstává nezměněné */
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_HW_EVENT, out );

    TEST_ASSERT_FALSE ( bpt_type_from_string ( NULL, &out ) );
    TEST_ASSERT_FALSE ( bpt_type_from_string ( "PC_EXEC", NULL ) );
}


void test_type_to_string_specific ( void ) {
    /* Stabilní řetězce - kontrakt s JSON formátem */
    TEST_ASSERT_EQUAL_STRING ( "PC_EXEC", bpt_type_to_string ( BPT_TYPE_PC_EXEC ) );
    TEST_ASSERT_EQUAL_STRING ( "MEM_R", bpt_type_to_string ( BPT_TYPE_MEM_R ) );
    TEST_ASSERT_EQUAL_STRING ( "MEM_W", bpt_type_to_string ( BPT_TYPE_MEM_W ) );
    TEST_ASSERT_EQUAL_STRING ( "IORQ_R", bpt_type_to_string ( BPT_TYPE_IORQ_R ) );
    TEST_ASSERT_EQUAL_STRING ( "IORQ_W", bpt_type_to_string ( BPT_TYPE_IORQ_W ) );
    TEST_ASSERT_EQUAL_STRING ( "IRQ", bpt_type_to_string ( BPT_TYPE_IRQ ) );
    TEST_ASSERT_EQUAL_STRING ( "HW_EVENT", bpt_type_to_string ( BPT_TYPE_HW_EVENT ) );
    TEST_ASSERT_EQUAL_STRING ( "SP_THRESHOLD", bpt_type_to_string ( BPT_TYPE_SP_THRESHOLD ) );
    TEST_ASSERT_EQUAL_STRING ( "GLOBAL", bpt_type_to_string ( BPT_TYPE_GLOBAL ) );
}


void test_zone_to_string_roundtrip ( void ) {
    int z;
    for ( z = 0; z < BP_ZONE_COUNT; z++ ) {
        const char *s = bp_zone_to_string ( (en_BP_ZONE) z );
        TEST_ASSERT_NOT_NULL ( s );
        TEST_ASSERT_TRUE ( s[0] != '\0' );

        en_BP_ZONE back;
        TEST_ASSERT_TRUE ( bp_zone_from_string ( s, &back ) );
        TEST_ASSERT_EQUAL_INT ( z, back );
    };
}


void test_zone_to_string_specific ( void ) {
    TEST_ASSERT_EQUAL_STRING ( "CPU_VIEW", bp_zone_to_string ( BP_ZONE_CPU_VIEW ) );
    TEST_ASSERT_EQUAL_STRING ( "ROM_LOWER", bp_zone_to_string ( BP_ZONE_ROM_LOWER ) );
    TEST_ASSERT_EQUAL_STRING ( "ROM_UPPER", bp_zone_to_string ( BP_ZONE_ROM_UPPER ) );
    TEST_ASSERT_EQUAL_STRING ( "RAM", bp_zone_to_string ( BP_ZONE_RAM ) );
    TEST_ASSERT_EQUAL_STRING ( "VRAM_FB", bp_zone_to_string ( BP_ZONE_VRAM_FB ) );
    TEST_ASSERT_EQUAL_STRING ( "PCG", bp_zone_to_string ( BP_ZONE_PCG ) );
    TEST_ASSERT_EQUAL_STRING ( "MMEXT_BANK", bp_zone_to_string ( BP_ZONE_MMEXT_BANK ) );
}


void test_zone_from_string_unknown ( void ) {
    en_BP_ZONE out = BP_ZONE_RAM;
    TEST_ASSERT_FALSE ( bp_zone_from_string ( "BOGUS", &out ) );
    TEST_ASSERT_EQUAL_INT ( BP_ZONE_RAM, out );

    TEST_ASSERT_FALSE ( bp_zone_from_string ( NULL, &out ) );
    TEST_ASSERT_FALSE ( bp_zone_from_string ( "RAM", NULL ) );
}


/* ========================================================================= */
/*  JSON round-trip persistence                                              */
/* ========================================================================= */


void test_json_roundtrip_smart_fields ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_v2_smart.bpt";

    /* Vytvořit BP každého typu se specifickými per-typ daty. */

    /* PC_EXEC s expr/action */
    int b_pc = breakpoints_add ( 0x1000, "PC", -1 );
    breakpoints_set_expr ( b_pc, "A == 0x42" );
    breakpoints_set_action ( b_pc, "log \"hit %X\", PC; continue" );
    breakpoints_set_hit_count ( b_pc, 5 );
    breakpoints_set_skip_count ( b_pc, 2 );

    /* MEM_R range + zone */
    /* Adresa 0x2000 nesmí kolidovat s b_pc na CPU view; rozdílná =>
     * bptmap zatím nezná zone, ale 0x2000 != 0x1000 takže OK pro
     * legacy bptmap PC enforcement. */
    int b_mr = breakpoints_add ( 0x2000, "MR", -1 );
    breakpoints_set_type ( b_mr, BPT_TYPE_MEM_R );
    breakpoints_set_addr_range ( b_mr, 0x2000, 0x2FFF );
    breakpoints_set_zone ( b_mr, BP_ZONE_VRAM_FB );
    breakpoints_set_edge_triggered ( b_mr, true );

    /* IORQ_W na port */
    int b_iow = breakpoints_add ( 0x3000, "IOW", -1 );
    breakpoints_set_type ( b_iow, BPT_TYPE_IORQ_W );
    breakpoints_set_port ( b_iow, 0x00CE );

    /* HW_EVENT */
    int b_evt = breakpoints_add ( 0x4000, "EVT", -1 );
    breakpoints_set_type ( b_evt, BPT_TYPE_HW_EVENT );
    breakpoints_set_event_name ( b_evt, "vsync" );

    /* SP_THRESHOLD */
    int b_sp = breakpoints_add ( 0x5000, "SP", -1 );
    breakpoints_set_type ( b_sp, BPT_TYPE_SP_THRESHOLD );
    breakpoints_set_sp_threshold ( b_sp, 0xFE82 );

    /* GLOBAL s bank_id */
    int b_gl = breakpoints_add ( 0x6000, "GL", -1 );
    breakpoints_set_type ( b_gl, BPT_TYPE_GLOBAL );
    breakpoints_set_zone ( b_gl, BP_ZONE_MMEXT_BANK );
    breakpoints_set_bank_id ( b_gl, 9 );

    /* Save */
    breakpoints_save_to_filepath ( tmpfile );

    /* Wipe a načíst */
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    TEST_ASSERT_EQUAL_UINT ( 6, breakpoints_count ( ) );

    /* Najít po adrese a ověřit klíčové fields */
    st_BPT *pc = breakpoints_find_by_addr ( 0x1000 );
    TEST_ASSERT_NOT_NULL ( pc );
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_PC_EXEC, pc->type );
    TEST_ASSERT_EQUAL_STRING ( "A == 0x42", pc->expr );
    TEST_ASSERT_EQUAL_STRING ( "log \"hit %X\", PC; continue", pc->action );
    TEST_ASSERT_EQUAL_UINT32 ( 5, pc->hit_count );
    TEST_ASSERT_EQUAL_UINT32 ( 2, pc->skip_count );

    st_BPT *mr = breakpoints_find_by_addr ( 0x2000 );
    TEST_ASSERT_NOT_NULL ( mr );
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_MEM_R, mr->type );
    TEST_ASSERT_EQUAL_HEX16 ( 0x2FFF, mr->addr_end );
    TEST_ASSERT_EQUAL_INT ( BP_ZONE_VRAM_FB, mr->zone );
    TEST_ASSERT_TRUE ( mr->edge_triggered );

    st_BPT *iow = breakpoints_find_by_addr ( 0x3000 );
    TEST_ASSERT_NOT_NULL ( iow );
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_IORQ_W, iow->type );
    TEST_ASSERT_EQUAL_HEX16 ( 0x00CE, iow->port );

    st_BPT *evt = breakpoints_find_by_addr ( 0x4000 );
    TEST_ASSERT_NOT_NULL ( evt );
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_HW_EVENT, evt->type );
    TEST_ASSERT_NOT_NULL ( evt->event_name );
    TEST_ASSERT_EQUAL_STRING ( "vsync", evt->event_name );

    st_BPT *sp = breakpoints_find_by_addr ( 0x5000 );
    TEST_ASSERT_NOT_NULL ( sp );
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_SP_THRESHOLD, sp->type );
    TEST_ASSERT_EQUAL_HEX16 ( 0xFE82, sp->sp_threshold );

    st_BPT *gl = breakpoints_find_by_addr ( 0x6000 );
    TEST_ASSERT_NOT_NULL ( gl );
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_GLOBAL, gl->type );
    TEST_ASSERT_EQUAL_INT ( BP_ZONE_MMEXT_BANK, gl->zone );
    TEST_ASSERT_EQUAL_UINT8 ( 9, gl->bank_id );

    remove ( tmpfile );
}


void test_json_roundtrip_groups_hierarchy ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_v2_groups.bpt";

    int g1 = breakpoints_group_add ( "ROM rutiny", -1 );
    breakpoints_group_set_colors ( g1, 0x112233, 0xAABBCC );

    int g2 = breakpoints_group_add ( "Wide BP", g1 );
    breakpoints_group_set_enabled ( g2, false );

    int b1 = breakpoints_add ( 0x1000, "InG2", g2 );
    (void) b1;

    breakpoints_save_to_filepath ( tmpfile );
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    TEST_ASSERT_EQUAL_UINT ( 2, breakpoints_group_count ( ) );
    TEST_ASSERT_EQUAL_UINT ( 1, breakpoints_count ( ) );

    /* Najít skupinu Wide BP a ověřit parent + enabled=false */
    unsigned i;
    int wide_id = -1;
    for ( i = 0; i < g_breakpoints.groups->len; i++ ) {
        st_BPTGROUP *grp = &g_array_index ( g_breakpoints.groups, st_BPTGROUP, i );
        if ( strcmp ( grp->name, "Wide BP" ) == 0 ) {
            wide_id = grp->id;
            TEST_ASSERT_GREATER_OR_EQUAL ( 0, grp->parent );
            TEST_ASSERT_FALSE ( grp->enabled );
            break;
        };
    };
    TEST_ASSERT_GREATER_OR_EQUAL ( 0, wide_id );

    /* BP musí mít parent = wide_id */
    st_BPT *bpt = breakpoints_find_by_addr ( 0x1000 );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_INT ( wide_id, bpt->parent );

    remove ( tmpfile );
}


void test_json_roundtrip_null_strings ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_v2_null.bpt";

    /* BP s NULL expr/action/event_name (= default) */
    int id = breakpoints_add ( 0x1000, "NullStrings", -1 );
    (void) id;

    breakpoints_save_to_filepath ( tmpfile );
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_addr ( 0x1000 );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_NULL ( bpt->expr );
    TEST_ASSERT_NULL ( bpt->action );
    TEST_ASSERT_NULL ( bpt->event_name );

    remove ( tmpfile );
}


void test_json_load_missing_file_noop ( void ) {
    /* Neexistující soubor = no-op, next_id zůstane validní */
    breakpoints_load_from_filepath ( "test_breakpoints_v2_does_not_exist_XYZ.bpt" );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, g_breakpoints.next_id );
    TEST_ASSERT_EQUAL_UINT ( 0, breakpoints_count ( ) );

    int id = breakpoints_add ( 0x1234, "OK", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id );
}


void test_json_load_unknown_type_falls_back ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_v2_bogus_type.bpt";

    /* Připravíme JSON ručně s neznámým typem - parser musí spadnout
     * do PC_EXEC fallbacku. */
    const char *content =
        "{\n"
        "  \"version\": 1,\n"
        "  \"groups\": [],\n"
        "  \"breakpoints\": [\n"
        "    {\n"
        "      \"id\": 5,\n"
        "      \"parent_id\": -1,\n"
        "      \"type\": \"BOGUS_TYPE\",\n"
        "      \"addr\": 4096,\n"
        "      \"addr_end\": 4096,\n"
        "      \"zone\": \"BOGUS_ZONE\",\n"
        "      \"name\": \"Bogus\",\n"
        "      \"enabled\": true\n"
        "    }\n"
        "  ]\n"
        "}\n";
    FILE *fp = fopen ( tmpfile, "wb" );
    TEST_ASSERT_NOT_NULL ( fp );
    fwrite ( content, 1, strlen ( content ), fp );
    fclose ( fp );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    TEST_ASSERT_EQUAL_UINT ( 1, breakpoints_count ( ) );
    st_BPT *bpt = breakpoints_find_by_addr ( 0x1000 );
    TEST_ASSERT_NOT_NULL ( bpt );
    /* Fallback PC_EXEC + CPU_VIEW */
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_PC_EXEC, bpt->type );
    TEST_ASSERT_EQUAL_INT ( BP_ZONE_CPU_VIEW, bpt->zone );

    remove ( tmpfile );
}


void test_json_load_missing_optional_keys ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_v2_minimal.bpt";

    /* Minimální JSON BP - jen id + addr; všechno ostatní default. */
    const char *content =
        "{\n"
        "  \"version\": 1,\n"
        "  \"breakpoints\": [\n"
        "    { \"id\": 7, \"addr\": 8192 }\n"
        "  ]\n"
        "}\n";
    FILE *fp = fopen ( tmpfile, "wb" );
    TEST_ASSERT_NOT_NULL ( fp );
    fwrite ( content, 1, strlen ( content ), fp );
    fclose ( fp );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_addr ( 0x2000 );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_INT ( BPT_TYPE_PC_EXEC, bpt->type );
    TEST_ASSERT_EQUAL_INT ( BP_ZONE_CPU_VIEW, bpt->zone );
    TEST_ASSERT_EQUAL_HEX16 ( 0x2000, bpt->addr_end );  /* default = addr */
    TEST_ASSERT_TRUE ( bpt->enabled );                  /* default true */

    remove ( tmpfile );
}


/* ========================================================================= */
/*  Condition eval helper (D.1.2)                                            */
/* ========================================================================= */


void test_eval_condition_no_expr_fires ( void ) {
    int id = breakpoints_add ( 0x1000, "no_expr", -1 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );

    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );

    /* Bez expr = vždy fire (legacy) */
    TEST_ASSERT_TRUE ( breakpoints_eval_condition ( bpt, &ctx ) );
}


void test_eval_condition_with_const_expr ( void ) {
    int id = breakpoints_add ( 0x1000, "const_expr", -1 );
    breakpoints_set_expr ( id, "1 == 1" );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_NOT_NULL ( bpt->parsed_expr );  /* parsováno */

    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );
    TEST_ASSERT_TRUE ( breakpoints_eval_condition ( bpt, &ctx ) );

    breakpoints_set_expr ( id, "0" );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_FALSE ( breakpoints_eval_condition ( bpt, &ctx ) );
}


void test_eval_condition_with_context ( void ) {
    int id = breakpoints_add ( 0x1000, "ctx_expr", -1 );
    breakpoints_set_expr ( id, "Address == 0x4242 && IsWrite" );
    st_BPT *bpt = breakpoints_find_by_id ( id );

    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );

    /* Match */
    ctx.Address = 0x4242;
    ctx.IsWrite = true;
    TEST_ASSERT_TRUE ( breakpoints_eval_condition ( bpt, &ctx ) );

    /* No match */
    ctx.IsWrite = false;
    TEST_ASSERT_FALSE ( breakpoints_eval_condition ( bpt, &ctx ) );

    ctx.IsWrite = true;
    ctx.Address = 0x1111;
    TEST_ASSERT_FALSE ( breakpoints_eval_condition ( bpt, &ctx ) );
}


void test_eval_condition_invalid_expr_fires ( void ) {
    int id = breakpoints_add ( 0x1000, "bad_expr", -1 );
    /* Syntax error - parser selže, ale string se uloží. */
    breakpoints_set_expr ( id, "1 +" );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_NOT_NULL ( bpt->expr );      /* string drží */
    TEST_ASSERT_NULL ( bpt->parsed_expr );   /* AST není */

    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );
    /* Conservative fire - ať uživatel chybu vidí. */
    TEST_ASSERT_TRUE ( breakpoints_eval_condition ( bpt, &ctx ) );
}


void test_eval_condition_clear_expr_resets_cache ( void ) {
    int id = breakpoints_add ( 0x1000, "reset", -1 );
    breakpoints_set_expr ( id, "0" );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt->parsed_expr );

    /* Vyčistit */
    breakpoints_set_expr ( id, NULL );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NULL ( bpt->expr );
    TEST_ASSERT_NULL ( bpt->parsed_expr );

    /* Po vyčištění zase fire */
    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );
    TEST_ASSERT_TRUE ( breakpoints_eval_condition ( bpt, &ctx ) );
}


/* ========================================================================= */
/*  Action helpers + vars persist (D.1.3)                                    */
/* ========================================================================= */


/**
 * NULL action → run_action vrací false (= STOP).
 */
void test_run_action_null_stops ( void ) {
    int id = breakpoints_add ( 0x1000, "noact", -1 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );
    TEST_ASSERT_FALSE ( breakpoints_run_action ( bpt, &ctx ) );
}


/**
 * Empty action (= "") → run_action vrací false (= STOP).
 */
void test_run_action_empty_stops ( void ) {
    int id = breakpoints_add ( 0x1000, "noact", -1 );
    breakpoints_set_action ( id, "" );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );
    TEST_ASSERT_FALSE ( breakpoints_run_action ( bpt, &ctx ) );
}


/**
 * Non-empty action → run_action vrací true (= CONTINUE).
 */
void test_run_action_with_var_continues ( void ) {
    bp_vars_clear_storage ( );

    int id = breakpoints_add ( 0x1000, "withact", -1 );
    breakpoints_set_action ( id, "$hits += 1" );
    st_BPT *bpt = breakpoints_find_by_id ( id );

    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );

    TEST_ASSERT_TRUE ( breakpoints_run_action ( bpt, &ctx ) );
    TEST_ASSERT_EQUAL_INT32 ( 1, bp_var_get ( "hits" ) );

    /* Druhý hit. */
    TEST_ASSERT_TRUE ( breakpoints_run_action ( bpt, &ctx ) );
    TEST_ASSERT_EQUAL_INT32 ( 2, bp_var_get ( "hits" ) );
}


/**
 * Invalid action source → parsed_action zůstane NULL → run_action stop.
 */
void test_run_action_parse_error_stops ( void ) {
    int id = breakpoints_add ( 0x1000, "broken", -1 );
    breakpoints_set_action ( id, "fubar 42 garbage" );  /* unknown stmt */
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NULL ( bpt->parsed_action );

    bp_expr_ctx_t ctx;
    bp_expr_ctx_zero ( &ctx );
    TEST_ASSERT_FALSE ( breakpoints_run_action ( bpt, &ctx ) );
}


/**
 * Set_action invaliduje předchozí cache.
 */
void test_set_action_invalidates_cache ( void ) {
    int id = breakpoints_add ( 0x1000, "rebind", -1 );
    breakpoints_set_action ( id, "$x = 1" );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt->parsed_action );

    breakpoints_set_action ( id, NULL );
    TEST_ASSERT_NULL ( bpt->parsed_action );
    TEST_ASSERT_NULL ( bpt->action );
}


/**
 * Round-trip persist vars přes save/load .bpt JSON.
 */
void test_json_roundtrip_vars ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_v2_vars.bpt";

    /* Naplň vars + jeden BP. */
    bp_vars_clear_storage ( );
    bp_var_set ( "score", 1234 );
    bp_var_set ( "phase", -7 );
    bp_var_set ( "flag",  1 );

    int id = breakpoints_add ( 0x1000, "PCBP", -1 );
    breakpoints_set_action ( id, "$score += 1" );

    breakpoints_save_to_filepath ( tmpfile );

    /* Wipe a načíst zpět. */
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    bp_vars_clear_storage ( );
    breakpoints_load_from_filepath ( tmpfile );

    TEST_ASSERT_EQUAL_INT ( 3, (int) bp_vars_count ( ) );
    TEST_ASSERT_EQUAL_INT32 ( 1234, bp_var_get ( "score" ) );
    TEST_ASSERT_EQUAL_INT32 ( -7,   bp_var_get ( "phase" ) );
    TEST_ASSERT_EQUAL_INT32 ( 1,    bp_var_get ( "flag" ) );

    /* BP musí mít cache parsovanou (= action načtená a zparsovaná). */
    st_BPT *bpt = breakpoints_find_by_addr ( 0x1000 );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_NOT_NULL ( bpt->parsed_action );

    remove ( tmpfile );
}


/**
 * Load souboru bez "vars" sekce → storage prázdná.
 */
void test_json_load_no_vars_section ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_v2_novars.bpt";

    /* Save bez žádných vars. */
    bp_vars_clear_storage ( );
    int id = breakpoints_add ( 0x1000, "X", -1 );
    (void) id;
    breakpoints_save_to_filepath ( tmpfile );

    /* Po loadu storage prázdná. */
    bp_var_set ( "ghost", 99 );  /* tato by měla být wipnuta loadem */
    breakpoints_load_from_filepath ( tmpfile );
    TEST_ASSERT_EQUAL_INT ( 0, (int) bp_vars_count ( ) );

    remove ( tmpfile );
}


/* ========================================================================= */
/*  V1.5.E - Match modes round-trip                                          */
/* ========================================================================= */


/**
 * PC_EXEC RANGE: addr_match_mode + addr_end persist.
 */
void test_persist_pc_exec_range ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_pc_range.bpt";

    int id = breakpoints_add ( 0x4000, "PCR", -1 );
    breakpoints_set_type ( id, BPT_TYPE_PC_EXEC );
    breakpoints_set_addr_match_mode ( id, BP_MATCH_RANGE );
    breakpoints_set_addr_range ( id, 0x4000, 0x40FF );

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_addr ( 0x4000 );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_RANGE, bpt->addr_match_mode );
    TEST_ASSERT_EQUAL_HEX16 ( 0x40FF, bpt->addr_end );

    remove ( tmpfile );
}


/**
 * MEM_W MASK: addr_mask persist.
 */
void test_persist_mem_w_mask ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_mem_mask.bpt";

    int id = breakpoints_add ( 0x4042, "MM", -1 );
    breakpoints_set_type ( id, BPT_TYPE_MEM_W );
    breakpoints_set_addr_match_mode ( id, BP_MATCH_MASK );
    breakpoints_set_addr_mask ( id, 0x00FF );

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_addr ( 0x4042 );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_MASK, bpt->addr_match_mode );
    TEST_ASSERT_EQUAL_HEX16 ( 0x00FF, bpt->addr_mask );

    remove ( tmpfile );
}


/**
 * IORQ port_match_mode + port_end + port_mask round-trip.
 */
void test_persist_iorq_match_mode ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_iorq_mm.bpt";

    int id = breakpoints_add ( 0xC0, "IO", -1 );
    breakpoints_set_type ( id, BPT_TYPE_IORQ_W );
    breakpoints_set_port ( id, 0xC0 );
    breakpoints_set_port_end ( id, 0xCF );
    breakpoints_set_port_match_mode ( id, BP_MATCH_RANGE );

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_addr ( 0xC0 );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_RANGE, bpt->port_match_mode );
    TEST_ASSERT_EQUAL_HEX16 ( 0xCF, bpt->port_end );

    remove ( tmpfile );
}


/* ========================================================================= */
/*  V1.5.A7 - IORQ port_mode (8/16-bit) round-trip                           */
/* ========================================================================= */


void test_persist_iorq_port_mode_16bit ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_port_mode.bpt";

    int id = breakpoints_add ( 0xFFCE, "IO16", -1 );
    breakpoints_set_type ( id, BPT_TYPE_IORQ_W );
    breakpoints_set_port ( id, 0xFFCE );
    breakpoints_set_port_mode ( id, BP_PORT_16BIT );

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_addr ( 0xFFCE );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_INT ( BP_PORT_16BIT, bpt->port_mode );

    remove ( tmpfile );
}


/* ========================================================================= */
/*  V1.5.A8 + A8.5 - IRQ filter round-trip                                   */
/* ========================================================================= */


void test_persist_irq_im_modes ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_irq_im.bpt";

    int id = breakpoints_add ( 0x0000, "IRQ", -1 );
    breakpoints_set_type ( id, BPT_TYPE_IRQ );
    breakpoints_set_im_enabled ( id, 0, true );
    breakpoints_set_im_enabled ( id, 1, false );
    breakpoints_set_im_enabled ( id, 2, true );

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_TRUE  ( bpt->im0_enabled );
    TEST_ASSERT_FALSE ( bpt->im1_enabled );
    TEST_ASSERT_TRUE  ( bpt->im2_enabled );

    remove ( tmpfile );
}


void test_persist_irq_im0_rst_mask ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_irq_rst.bpt";

    int id = breakpoints_add ( 0x0000, "IRQ", -1 );
    breakpoints_set_type ( id, BPT_TYPE_IRQ );
    breakpoints_set_im0_rst_mask ( id, 0xA5 );  /* RST 00, 10, 20, 38 */

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_HEX8 ( 0xA5, bpt->im0_rst_mask );

    remove ( tmpfile );
}


void test_persist_irq_im2_filters ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_irq_im2.bpt";

    int id = breakpoints_add ( 0x0000, "IRQ", -1 );
    breakpoints_set_type ( id, BPT_TYPE_IRQ );
    breakpoints_set_im2_vector_filter ( id, true, 0xFE10 );
    breakpoints_set_im2_isr_filter   ( id, true, 0x1234 );

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_TRUE ( bpt->im2_vector_enabled );
    TEST_ASSERT_EQUAL_HEX16 ( 0xFE10, bpt->im2_vector_addr );
    TEST_ASSERT_TRUE ( bpt->im2_isr_enabled );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1234, bpt->im2_isr_addr );
    /* Default match modes po legacy save (V1.5 settery zachovavaji SINGLE). */
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_SINGLE, bpt->im2_vector_match_mode );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_SINGLE, bpt->im2_isr_match_mode );

    remove ( tmpfile );
}


/**
 * V1.6+ TODO 4.4: IM2 vector + ISR Match modes (RANGE/MASK + addr_end + mask)
 * round-trip persist test.
 */
void test_persist_irq_im2_match_modes ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v16_irq_im2_modes.bpt";

    int id = breakpoints_add ( 0x0000, "IRQ_MM", -1 );
    breakpoints_set_type ( id, BPT_TYPE_IRQ );
    breakpoints_set_im2_vector_filter ( id, true, 0xFE00 );
    breakpoints_set_im2_vector_match_mode ( id, BP_MATCH_RANGE );
    breakpoints_set_im2_vector_addr_end ( id, 0xFE3F );
    breakpoints_set_im2_isr_filter ( id, true, 0x1000 );
    breakpoints_set_im2_isr_match_mode ( id, BP_MATCH_MASK );
    breakpoints_set_im2_isr_mask ( id, 0xF000 );

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );

    TEST_ASSERT_TRUE ( bpt->im2_vector_enabled );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_RANGE, bpt->im2_vector_match_mode );
    TEST_ASSERT_EQUAL_HEX16 ( 0xFE00, bpt->im2_vector_addr );
    TEST_ASSERT_EQUAL_HEX16 ( 0xFE3F, bpt->im2_vector_addr_end );

    TEST_ASSERT_TRUE ( bpt->im2_isr_enabled );
    TEST_ASSERT_EQUAL_INT ( BP_MATCH_MASK, bpt->im2_isr_match_mode );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1000, bpt->im2_isr_addr );
    TEST_ASSERT_EQUAL_HEX16 ( 0xF000, bpt->im2_isr_mask );

    remove ( tmpfile );
}


void test_persist_irq_sig_source_mask ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_irq_sig.bpt";

    int id = breakpoints_add ( 0x0000, "SIG", -1 );
    breakpoints_set_type ( id, BPT_TYPE_IRQ_SIG );
    /* Mask: PIO_A | CTC2 | FDC. */
    uint8_t mask = (uint8_t) ( BP_IRQ_SIG_PIOZ80_PORT_A
                              | BP_IRQ_SIG_CTC2
                              | BP_IRQ_SIG_FDC );
    breakpoints_set_irq_sig_source_mask ( id, mask );

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_HEX8 ( mask, bpt->irq_sig_source_mask );

    remove ( tmpfile );
}


/* ========================================================================= */
/*  V1.5 HWE - Trigger condition + raster:N round-trip                       */
/* ========================================================================= */


void test_persist_hwe_with_trigger ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_hwe_trig.bpt";

    int id = breakpoints_add ( 0x0000, "HWE", -1 );
    breakpoints_set_type ( id, BPT_TYPE_HW_EVENT );
    breakpoints_set_event_name ( id, "vsync" );
    breakpoints_set_event_trigger ( id, BP_EVT_TRIG_FALLING );

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_INT ( BP_EVT_TRIG_FALLING, bpt->event_trigger );
    TEST_ASSERT_EQUAL_STRING ( "vsync", bpt->event_name );

    remove ( tmpfile );
}


void test_persist_hwe_raster_param ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_raster_param.bpt";

    int id = breakpoints_add ( 0x0000, "RAS", -1 );
    breakpoints_set_type ( id, BPT_TYPE_HW_EVENT );
    breakpoints_set_event_name ( id, "raster:192" );

    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_STRING ( "raster:192", bpt->event_name );
    TEST_ASSERT_EQUAL_INT ( BP_EVENT_GDG_RASTER, bpt->parsed_event );
    TEST_ASSERT_EQUAL ( 192, bpt->event_param );

    remove ( tmpfile );
}


/* ========================================================================= */
/*  V1.5.B - Vars sekce comment + persist_value round-trip                   */
/* ========================================================================= */


void test_persist_vars_with_comment ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );
    const char *tmpfile = "test_v15_vars_comment.bpt";

    bp_vars_clear_storage ( );
    bp_var_set_full ( "score",   100, "hra body",        true );
    bp_var_set_full ( "lives",   3,   NULL,              true );
    bp_var_set_full ( "runtime", 999, "ephemeral runtime", false );

    breakpoints_save_to_filepath ( tmpfile );

    /* Wipe a load. */
    bp_vars_clear_storage ( );
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    TEST_ASSERT_EQUAL_INT ( 3, (int) bp_vars_count ( ) );

    /* score: persist=true + comment. */
    TEST_ASSERT_EQUAL_INT32 ( 100, bp_var_get ( "score" ) );
    TEST_ASSERT_EQUAL_STRING ( "hra body", bp_var_get_comment ( "score" ) );
    TEST_ASSERT_TRUE ( bp_var_get_persist ( "score" ) );

    /* lives: persist=true bez comment. */
    TEST_ASSERT_EQUAL_INT32 ( 3, bp_var_get ( "lives" ) );
    TEST_ASSERT_NULL ( bp_var_get_comment ( "lives" ) );

    /* runtime: persist_value=false → value se neukládala, po loadu = 0. */
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "runtime" ) );
    TEST_ASSERT_EQUAL_STRING ( "ephemeral runtime", bp_var_get_comment ( "runtime" ) );
    TEST_ASSERT_FALSE ( bp_var_get_persist ( "runtime" ) );

    remove ( tmpfile );
}


/* ========================================================================= */
/*  V1.6+ TODO 4.7 - Schema versioning                                       */
/* ========================================================================= */


/**
 * Po save .bpt JSON musí obsahovat klíč "schema_version" s hodnotou
 * BREAKPOINTS_SCHEMA_VERSION_CURRENT v root objektu.
 *
 * Round-trip test: save → reparse JSON → assert klíč + hodnota.
 */
void test_persist_schema_version_present ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_v15_schema_version.bpt";

    /* Naplň minimální obsah a zapiš. */
    int id = breakpoints_add ( 0x1234, "SV", -1 );
    (void) id;
    breakpoints_save_to_filepath ( tmpfile );

    /* Reparse soubor přes JsonParser - ověř klíč + hodnota. */
    JsonParser *parser = json_parser_new ( );
    GError *err = NULL;
    gboolean ok = json_parser_load_from_file ( parser, tmpfile, &err );
    TEST_ASSERT_TRUE ( ok );
    TEST_ASSERT_NULL ( err );

    JsonNode *root = json_parser_get_root ( parser );
    TEST_ASSERT_NOT_NULL ( root );
    TEST_ASSERT_EQUAL_INT ( JSON_NODE_OBJECT, json_node_get_node_type ( root ) );

    JsonObject *obj = json_node_get_object ( root );
    TEST_ASSERT_TRUE ( json_object_has_member ( obj, "schema_version" ) );

    const char *ver = json_object_get_string_member ( obj, "schema_version" );
    TEST_ASSERT_NOT_NULL ( ver );
    TEST_ASSERT_EQUAL_STRING ( BREAKPOINTS_SCHEMA_VERSION_CURRENT, ver );

    g_object_unref ( parser );
    remove ( tmpfile );
}


/**
 * Round-trip save → load funguje (= save zapíše schema_version, load ho
 * tiše akceptuje + nehlásí warning na current verzi).
 *
 * Ověření že load nepoškodí state: BP zůstane po round-trip.
 */
void test_persist_schema_version_roundtrip ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_v15_schema_rt.bpt";

    int id = breakpoints_add ( 0xABCD, "RT", -1 );
    breakpoints_set_type ( id, BPT_TYPE_PC_EXEC );
    breakpoints_save_to_filepath ( tmpfile );

    /* Wipe + load. */
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    st_BPT *bpt = breakpoints_find_by_addr ( 0xABCD );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_STRING ( "RT", bpt->name );

    remove ( tmpfile );
}


/* ========================================================================= */
/*  MAIN                                                                     */
/* ========================================================================= */


int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    /* Default values po add */
    RUN_TEST ( test_bpt_v2_defaults_after_add );
    RUN_TEST ( test_bpt_v2_defaults_after_add_auto );

    /* Setters */
    RUN_TEST ( test_set_type );
    RUN_TEST ( test_set_zone );
    RUN_TEST ( test_set_bank_id_port_sp );
    RUN_TEST ( test_set_addr_range );
    RUN_TEST ( test_set_addr_end_independent );
    RUN_TEST ( test_set_addr_keeps_addr_end_for_range );
    RUN_TEST ( test_set_addr_syncs_addr_end_for_single_point );
    RUN_TEST ( test_set_event_name );
    RUN_TEST ( test_set_expr_action );
    RUN_TEST ( test_set_hit_skip_edge );
    RUN_TEST ( test_setters_on_nonexistent_id_fail );

    /* String konverze */
    RUN_TEST ( test_type_to_string_roundtrip );
    RUN_TEST ( test_type_from_string_unknown );
    RUN_TEST ( test_type_to_string_specific );
    RUN_TEST ( test_zone_to_string_roundtrip );
    RUN_TEST ( test_zone_to_string_specific );
    RUN_TEST ( test_zone_from_string_unknown );

    /* JSON round-trip */
    RUN_TEST ( test_json_roundtrip_smart_fields );
    RUN_TEST ( test_json_roundtrip_groups_hierarchy );
    RUN_TEST ( test_json_roundtrip_null_strings );
    RUN_TEST ( test_json_load_missing_file_noop );
    RUN_TEST ( test_json_load_unknown_type_falls_back );
    RUN_TEST ( test_json_load_missing_optional_keys );

    /* Condition eval helper (D.1.2) */
    RUN_TEST ( test_eval_condition_no_expr_fires );
    RUN_TEST ( test_eval_condition_with_const_expr );
    RUN_TEST ( test_eval_condition_with_context );
    RUN_TEST ( test_eval_condition_invalid_expr_fires );
    RUN_TEST ( test_eval_condition_clear_expr_resets_cache );

    /* Action helpers + vars persist (D.1.3) */
    RUN_TEST ( test_run_action_null_stops );
    RUN_TEST ( test_run_action_empty_stops );
    RUN_TEST ( test_run_action_with_var_continues );
    RUN_TEST ( test_run_action_parse_error_stops );
    RUN_TEST ( test_set_action_invalidates_cache );
    RUN_TEST ( test_json_roundtrip_vars );
    RUN_TEST ( test_json_load_no_vars_section );

    /* V1.5.E - Match modes round-trip */
    RUN_TEST ( test_persist_pc_exec_range );
    RUN_TEST ( test_persist_mem_w_mask );
    RUN_TEST ( test_persist_iorq_match_mode );

    /* V1.5.A7 - IORQ port_mode round-trip */
    RUN_TEST ( test_persist_iorq_port_mode_16bit );

    /* V1.5.A8 + A8.5 - IRQ filter round-trip */
    RUN_TEST ( test_persist_irq_im_modes );
    RUN_TEST ( test_persist_irq_im0_rst_mask );
    RUN_TEST ( test_persist_irq_im2_filters );
    RUN_TEST ( test_persist_irq_im2_match_modes );
    RUN_TEST ( test_persist_irq_sig_source_mask );

    /* V1.5 HWE - Trigger condition + raster:N round-trip */
    RUN_TEST ( test_persist_hwe_with_trigger );
    RUN_TEST ( test_persist_hwe_raster_param );

    /* V1.5.B - Vars sekce comment + persist_value */
    RUN_TEST ( test_persist_vars_with_comment );

    /* V1.6+ TODO 4.7 - Schema versioning */
    RUN_TEST ( test_persist_schema_version_present );
    RUN_TEST ( test_persist_schema_version_roundtrip );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
