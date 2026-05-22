/*
 * test_eventlog_filter.c - unit testy pro eventlog_filter parser/matcher.
 *
 * Pokrývá Tier 1 syntax z Vlna 1 Commit 6:
 *  - cat:NAME[,NAME]*  per-kategorie bulk
 *  - sub:N[,N]*        per-subtype bulk
 *  - pc:HEX[-HEX]      PC exact / range
 *  - frame:N / >N / <N frame compare
 *  - cycle:N s k/M suffixem
 *  - sline:N-N         scanline range (dekódováno z pxclk_in_screen)
 *  - px:N-N            pixel column range
 *  - payload:HEX       exact uint32 payload
 *  - !TOKEN            per-token negace
 *  - whitespace AND    default mezi tokeny
 *  - "( a or b )"      OR group (jen v závorkách)
 *  - round-trip        parse -> to_string -> parse (sémanticky shodné)
 *  - error reporting   syntax error vrátí non-NULL handle s err msg
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "debugger/eventlog_filter.h"
#include "debugger/trace/eventlog.h"
#include "debugger/symbols/sym_db.h"


void setUp ( void )
{
    /* Default screen width pro sline/px dekódování. */
    eventlog_filter_set_screen_width ( 320 );

    /* Symbol DB čistá pro každý test. Init je idempotentní, clear vrátí
     * DB do prázdného stavu pokud byla naplněna v předchozím testu. */
    sym_db_init ( );
    sym_db_clear ( );
}


void tearDown ( void )
{
    /* Sym DB destroy aby nezůstaly heap allocs mezi testy. */
    sym_db_destroy ( );
}


/* ================================================================
 * Helpery
 * ================================================================ */


/**
 * @brief Postaví testovací event.
 */
static st_EVENTLOG_EVENT mk_event ( uint8_t cat, uint8_t sub, uint16_t pc,
                                      uint32_t payload, uint32_t screens,
                                      uint64_t pxclk_total, uint32_t pxclk_in_screen )
{
    st_EVENTLOG_EVENT e;
    memset ( &e, 0, sizeof ( e ) );
    e.category = cat;
    e.subtype = sub;
    e.pc = pc;
    e.payload = payload;
    e.screens_total = screens;
    e.pxclk_total = pxclk_total;
    e.pxclk_in_screen = pxclk_in_screen;
    return e;
}


/* ================================================================
 * Parse - basic tokens
 * ================================================================ */


void test_eventlog_filter_parse_empty ( void )
{
    /* Empty / NULL = match all. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT e = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0x1234, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f, &e ) );
    eventlog_filter_free ( f );

    f = eventlog_filter_parse ( NULL );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f, &e ) );
    eventlog_filter_free ( f );

    f = eventlog_filter_parse ( "   " );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f, &e ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_cat_single ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT match = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT miss  = mk_event ( EVENTLOG_CAT_GDG_MODE, 0, 0, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &match ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &miss ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_cat_bulk ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "cat:cpu_int,gdg_mode" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( EVENTLOG_CAT_GDG_MODE, 0, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT c = mk_event ( EVENTLOG_CAT_PSG,      0, 0, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &c ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_sub_bulk ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "sub:0,1,5" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( EVENTLOG_CAT_CPU_INT, 1, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT c = mk_event ( EVENTLOG_CAT_CPU_INT, 5, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT d = mk_event ( EVENTLOG_CAT_CPU_INT, 2, 0, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &c ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &d ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_pc_exact ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "pc:4042" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0x4042, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0x4043, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_pc_range ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "pc:4000-40FF" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0x4000, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0x40FF, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT c = mk_event ( 0, 0, 0x4080, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT d = mk_event ( 0, 0, 0x4100, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &c ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &d ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_frame_gt ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "frame:>100" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0, 0,  50, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0, 0, 100, 0, 0 );
    st_EVENTLOG_EVENT c = mk_event ( 0, 0, 0, 0, 101, 0, 0 );
    st_EVENTLOG_EVENT d = mk_event ( 0, 0, 0, 0, 200, 0, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &c ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &d ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_frame_lt ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "frame:<100" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0, 0, 99,  0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0, 0, 100, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_cycle_M_suffix ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "cycle:>1M" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0, 0, 0, 999999u, 0 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0, 0, 0, 1000000u, 0 );
    st_EVENTLOG_EVENT c = mk_event ( 0, 0, 0, 0, 0, 1000001u, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &c ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_cycle_k_suffix ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "cycle:>5k" );
    TEST_ASSERT_NOT_NULL ( f );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0, 0, 0, 4999u, 0 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0, 0, 0, 5000u, 0 );
    st_EVENTLOG_EVENT c = mk_event ( 0, 0, 0, 0, 0, 5001u, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &c ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_sline_range ( void )
{
    /* sline:50-150 + screen width 320 => pxclk_in_screen 50*320 .. 150*320+319. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "sline:50-150" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0, 0, 0, 0,  49 * 320 + 100 ); /* sline 49 */
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0, 0, 0, 0,  50 * 320 );        /* sline 50 */
    st_EVENTLOG_EVENT c = mk_event ( 0, 0, 0, 0, 0, 0, 100 * 320 + 50 );   /* sline 100 */
    st_EVENTLOG_EVENT d = mk_event ( 0, 0, 0, 0, 0, 0, 151 * 320 );        /* sline 151 */
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &c ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &d ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_px_range ( void )
{
    /* px:160-200 + width 320 => pxclk_in_screen mod 320 v [160..200]. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "px:160-200" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0, 0, 0, 0, 50 * 320 + 159 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0, 0, 0, 0, 50 * 320 + 160 );
    st_EVENTLOG_EVENT c = mk_event ( 0, 0, 0, 0, 0, 0, 50 * 320 + 200 );
    st_EVENTLOG_EVENT d = mk_event ( 0, 0, 0, 0, 0, 0, 50 * 320 + 201 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &c ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &d ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_payload_eq ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "payload:0xCE" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0, 0xCE, 0, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0, 0xCF, 0, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );
    eventlog_filter_free ( f );

    /* Bez 0x prefixu taky funguje. */
    f = eventlog_filter_parse ( "payload:CE" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_negation ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "!cat:gdg_video" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( EVENTLOG_CAT_GDG_VIDEO, 0, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( EVENTLOG_CAT_GDG_MODE,  0, 0, 0, 0, 0, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &b ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_and ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "cat:cpu_int pc:4000-40FF" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT m  = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0x4080, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT nc = mk_event ( EVENTLOG_CAT_GDG_MODE, 0, 0x4080, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT np = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0x5000, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &m  ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &nc ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &np ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_or_group ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "( cat:cpu_int or cat:bp_fire )" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( EVENTLOG_CAT_BP_FIRE, 0, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT c = mk_event ( EVENTLOG_CAT_PSG,     0, 0, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &c ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_parse_complex ( void )
{
    /* cat:cpu_int ( pc:4000-40FF or pc:5000 ) !frame:<100. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse (
        "cat:cpu_int ( pc:4000-40FF or pc:5000 ) !frame:<100" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    /* Match: cpu_int, pc=0x4080, frame=200 (= !<100 ok) */
    st_EVENTLOG_EVENT ok = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0x4080, 0,
                                       200, 0, 0 );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f, &ok ) );

    /* Miss: pc out of both ranges. */
    st_EVENTLOG_EVENT miss_pc = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0x6000, 0,
                                            200, 0, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &miss_pc ) );

    /* Miss: frame < 100 (negace flipne -> false). */
    st_EVENTLOG_EVENT miss_frame = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0x4080, 0,
                                               50, 0, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &miss_frame ) );

    /* Match přes druhou OR větev: pc=0x5000. */
    st_EVENTLOG_EVENT or_branch = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0x5000, 0,
                                              200, 0, 0 );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f, &or_branch ) );

    eventlog_filter_free ( f );
}


/* ================================================================
 * Match scenarios
 * ================================================================ */


void test_eventlog_filter_match_simple ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    st_EVENTLOG_EVENT e = mk_event ( EVENTLOG_CAT_CPU_INT, 7, 0x1234, 0xDEAD,
                                      42, 1000, 800 );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f, &e ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_match_no_match ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "cat:gdg_mode" );
    TEST_ASSERT_NOT_NULL ( f );
    st_EVENTLOG_EVENT e = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0, 0, 0, 0, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &e ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_match_complex ( void )
{
    /* cat:bp_fire pc:4000-40FF: 4 scénáře (match / no_cat / no_pc / no_both). */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "cat:bp_fire pc:4000-40FF" );
    TEST_ASSERT_NOT_NULL ( f );

    st_EVENTLOG_EVENT match    = mk_event ( EVENTLOG_CAT_BP_FIRE, 0, 0x4080, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT no_cat   = mk_event ( EVENTLOG_CAT_PSG,     0, 0x4080, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT no_pc    = mk_event ( EVENTLOG_CAT_BP_FIRE, 0, 0x5000, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT no_both  = mk_event ( EVENTLOG_CAT_PSG,     0, 0x5000, 0, 0, 0, 0 );

    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &match   ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &no_cat  ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &no_pc   ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &no_both ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_match_negation ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "!cat:gdg_video" );
    TEST_ASSERT_NOT_NULL ( f );

    st_EVENTLOG_EVENT mode  = mk_event ( EVENTLOG_CAT_GDG_MODE,  0, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT video = mk_event ( EVENTLOG_CAT_GDG_VIDEO, 0, 0, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &mode  ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &video ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_match_or ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse (
        "( cat:bp_fire or cat:user_mark )" );
    TEST_ASSERT_NOT_NULL ( f );

    st_EVENTLOG_EVENT bp  = mk_event ( EVENTLOG_CAT_BP_FIRE,   0, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT um  = mk_event ( EVENTLOG_CAT_USER_MARK, 0, 0, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT gdg = mk_event ( EVENTLOG_CAT_GDG_MODE,  0, 0, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &bp  ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &um  ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &gdg ) );
    eventlog_filter_free ( f );
}


/* ================================================================
 * Round trip
 * ================================================================ */


/**
 * @brief Pomocná funkce: parse expr A -> dump -> parse dump B,
 * test že obě dají shodný match na sadě sondových eventů.
 */
static void assert_roundtrip ( const char *expr )
{
    st_EVENTLOG_FILTER *a = eventlog_filter_parse ( expr );
    TEST_ASSERT_NOT_NULL ( a );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( a ) );

    char *dumped = eventlog_filter_to_string ( a );
    TEST_ASSERT_NOT_NULL ( dumped );

    st_EVENTLOG_FILTER *b = eventlog_filter_parse ( dumped );
    TEST_ASSERT_NOT_NULL ( b );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( b ) );

    /* Sonda na různých kategoriích a hodnotách. */
    st_EVENTLOG_EVENT probes[] = {
        mk_event ( EVENTLOG_CAT_CPU_INT,    0, 0x4000, 0,     0, 0,      0 ),
        mk_event ( EVENTLOG_CAT_GDG_MODE,   1, 0x4080, 0x10,  50, 500,   320 ),
        mk_event ( EVENTLOG_CAT_BP_FIRE,    2, 0x5000, 0xFF, 100, 1000,  640 ),
        mk_event ( EVENTLOG_CAT_USER_MARK,  3, 0x40FF, 0xCE, 150, 5000,  100 ),
        mk_event ( EVENTLOG_CAT_PSG,        7, 0x6000, 0,    200, 2000000, 200 ),
    };
    for ( size_t i = 0; i < sizeof ( probes ) / sizeof ( probes[ 0 ] ); i++ ) {
        bool ra = eventlog_filter_match ( a, &probes[ i ] );
        bool rb = eventlog_filter_match ( b, &probes[ i ] );
        TEST_ASSERT_EQUAL_INT ( (int) ra, (int) rb );
    }

    free ( dumped );
    eventlog_filter_free ( a );
    eventlog_filter_free ( b );
}


void test_eventlog_filter_roundtrip ( void )
{
    assert_roundtrip ( "cat:cpu_int" );
    assert_roundtrip ( "cat:cpu_int,gdg_mode" );
    assert_roundtrip ( "pc:4000-40FF" );
    assert_roundtrip ( "pc:4042" );
    assert_roundtrip ( "frame:>100" );
    assert_roundtrip ( "cycle:>1M" );
    assert_roundtrip ( "sline:50-150" );
    assert_roundtrip ( "px:160-200" );
    assert_roundtrip ( "payload:CE" );
    assert_roundtrip ( "!cat:gdg_video" );
    assert_roundtrip ( "cat:cpu_int pc:4000-40FF" );
    assert_roundtrip ( "( cat:bp_fire or cat:user_mark )" );
}


/* ================================================================
 * Invalid syntax
 * ================================================================ */


void test_eventlog_filter_invalid_syntax ( void )
{
    /* cat: bez hodnoty */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "cat:" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    /* match vrací false při error */
    st_EVENTLOG_EVENT e = mk_event ( EVENTLOG_CAT_CPU_INT, 0, 0, 0, 0, 0, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &e ) );
    eventlog_filter_free ( f );

    /* pc s neplatným hexem */
    f = eventlog_filter_parse ( "pc:zzz" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* frame:> bez hodnoty */
    f = eventlog_filter_parse ( "frame:>" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* Neznámá kategorie */
    f = eventlog_filter_parse ( "cat:foobar" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* "or" mimo závorky */
    f = eventlog_filter_parse ( "cat:cpu_int or cat:psg" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* Nezavřená závorka */
    f = eventlog_filter_parse ( "( cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* Neznámý prefix */
    f = eventlog_filter_parse ( "xxx:42" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );
}


/* ================================================================
 * Cat name table sanity
 * ================================================================ */


void test_eventlog_filter_cat_name_table ( void )
{
    /* Spot-check: round-trip jména přes cat_from/cat_to. */
    TEST_ASSERT_EQUAL_INT ( EVENTLOG_CAT_CPU_INT,
                             eventlog_filter_cat_from_name ( "cpu_int" ) );
    TEST_ASSERT_EQUAL_INT ( EVENTLOG_CAT_GDG_MODE,
                             eventlog_filter_cat_from_name ( "gdg_mode" ) );
    TEST_ASSERT_EQUAL_INT ( EVENTLOG_CAT_CPU_CTRL,
                             eventlog_filter_cat_from_name ( "cpu_ctrl" ) );
    TEST_ASSERT_EQUAL_INT ( -1, eventlog_filter_cat_from_name ( "unknown" ) );
    TEST_ASSERT_EQUAL_INT ( -1, eventlog_filter_cat_from_name ( NULL ) );
    TEST_ASSERT_EQUAL_INT ( -1, eventlog_filter_cat_from_name ( "" ) );

    TEST_ASSERT_EQUAL_STRING ( "cpu_int",
        eventlog_filter_cat_to_name ( EVENTLOG_CAT_CPU_INT ) );
    TEST_ASSERT_EQUAL_STRING ( "bp_fire",
        eventlog_filter_cat_to_name ( EVENTLOG_CAT_BP_FIRE ) );
    TEST_ASSERT_NULL ( eventlog_filter_cat_to_name ( 99 ) );
}


/* ================================================================
 * Symbol-aware filtry (Vlna 3 Commit 18)
 * ================================================================ */


/**
 * Helper: naplní sym_db pár fixními symboly pro sym filter testy.
 */
static void seed_sym_db_for_filter_tests ( void )
{
    sym_db_add_user_label ( 0x4042, "isr_handler",     NULL );
    sym_db_add_user_label ( 0x4050, "isr_timer",       NULL );
    sym_db_add_user_label ( 0x4060, "isr_kbd",         NULL );
    sym_db_add_user_label ( 0x5000, "main_loop",       NULL );
    sym_db_add_user_label ( 0x5100, "main_loop_end",   NULL );
    sym_db_add_user_label ( 0x6000, "video_init",      NULL );
}


void test_eventlog_filter_sym_exact_match ( void )
{
    seed_sym_db_for_filter_tests ( );

    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "sym:isr_handler" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    /* PC == addr symbolu -> match */
    st_EVENTLOG_EVENT e = mk_event ( 0, 0, 0x4042, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f, &e ) );

    eventlog_filter_free ( f );
}


void test_eventlog_filter_sym_exact_no_match ( void )
{
    seed_sym_db_for_filter_tests ( );

    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "sym:isr_handler" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    /* PC mimo addr symbolu -> no match */
    st_EVENTLOG_EVENT e1 = mk_event ( 0, 0, 0x4043, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT e2 = mk_event ( 0, 0, 0x4041, 0, 0, 0, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &e1 ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &e2 ) );

    eventlog_filter_free ( f );
}


void test_eventlog_filter_sym_prefix_match ( void )
{
    seed_sym_db_for_filter_tests ( );

    /* "isr_*" matchuje isr_handler (0x4042), isr_timer (0x4050),
     * isr_kbd (0x4060). */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "sym:isr_*" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0x4042, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0x4050, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT c = mk_event ( 0, 0, 0x4060, 0, 0, 0, 0 );
    /* main_loop má jinou prefix -> no match */
    st_EVENTLOG_EVENT d = mk_event ( 0, 0, 0x5000, 0, 0, 0, 0 );
    /* PC mimo známé symboly -> no match */
    st_EVENTLOG_EVENT e = mk_event ( 0, 0, 0x1234, 0, 0, 0, 0 );

    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &c ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &d ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &e ) );

    eventlog_filter_free ( f );
}


void test_eventlog_filter_sym_prefix_no_match ( void )
{
    seed_sym_db_for_filter_tests ( );

    /* Žádný symbol s prefixem "xxx_" -> cache empty, match vždy false. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "sym:xxx_*" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0x4042, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0x5000, 0, 0, 0, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );

    eventlog_filter_free ( f );
}


void test_eventlog_filter_sym_range_match ( void )
{
    seed_sym_db_for_filter_tests ( );

    /* [main_loop=0x5000, main_loop_end=0x5100] inclusive. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse (
        "from_sym:main_loop to_sym:main_loop_end" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0x5000, 0, 0, 0, 0 );  /* lo bound */
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0x5050, 0, 0, 0, 0 );  /* uvnitř */
    st_EVENTLOG_EVENT c = mk_event ( 0, 0, 0x5100, 0, 0, 0, 0 );  /* hi bound */
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f, &c ) );

    eventlog_filter_free ( f );
}


void test_eventlog_filter_sym_range_outside ( void )
{
    seed_sym_db_for_filter_tests ( );

    st_EVENTLOG_FILTER *f = eventlog_filter_parse (
        "from_sym:main_loop to_sym:main_loop_end" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0x4FFF, 0, 0, 0, 0 );  /* pod lo */
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0x5101, 0, 0, 0, 0 );  /* nad hi */
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );

    eventlog_filter_free ( f );
}


void test_eventlog_filter_sym_unknown_name ( void )
{
    seed_sym_db_for_filter_tests ( );

    /* Neznámé jméno = parse OK (= jméno se uloží), match vždy false. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "sym:nonexistent" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0x4042, 0, 0, 0, 0 );
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0x0000, 0, 0, 0, 0 );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );

    eventlog_filter_free ( f );

    /* SYM_RANGE s neznámým from/to = také match false. */
    f = eventlog_filter_parse ( "from_sym:nonexist_a to_sym:nonexist_b" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_sym_negate ( void )
{
    seed_sym_db_for_filter_tests ( );

    /* "!sym:isr_*" = vše KROMĚ adres isr_* symbolů. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "!sym:isr_*" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    /* PC == addr isr_handler -> negace -> false */
    st_EVENTLOG_EVENT a = mk_event ( 0, 0, 0x4042, 0, 0, 0, 0 );
    /* PC == addr main_loop -> nematchuje isr_*, negace -> true */
    st_EVENTLOG_EVENT b = mk_event ( 0, 0, 0x5000, 0, 0, 0, 0 );
    /* PC úplně mimo -> nematchuje isr_*, negace -> true */
    st_EVENTLOG_EVENT c = mk_event ( 0, 0, 0x1234, 0, 0, 0, 0 );

    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &c ) );

    eventlog_filter_free ( f );
}


void test_eventlog_filter_sym_syntax_errors ( void )
{
    /* from_sym: bez to_sym:. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "from_sym:foo" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* Standalone to_sym:. */
    f = eventlog_filter_parse ( "to_sym:foo" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* sym: bez identifieru. */
    f = eventlog_filter_parse ( "sym:" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* sym:* sám hvězdička = invalid. */
    f = eventlog_filter_parse ( "sym:*" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );
}


void test_eventlog_filter_sym_roundtrip ( void )
{
    seed_sym_db_for_filter_tests ( );

    /* sym:NAME round-trip. */
    st_EVENTLOG_FILTER *f1 = eventlog_filter_parse ( "sym:isr_handler" );
    TEST_ASSERT_NOT_NULL ( f1 );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f1 ) );
    char *s1 = eventlog_filter_to_string ( f1 );
    TEST_ASSERT_NOT_NULL ( s1 );
    TEST_ASSERT_EQUAL_STRING ( "sym:isr_handler", s1 );

    /* Sémantická ekvivalence po re-parse. */
    st_EVENTLOG_FILTER *f2 = eventlog_filter_parse ( s1 );
    TEST_ASSERT_NOT_NULL ( f2 );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f2 ) );
    st_EVENTLOG_EVENT e = mk_event ( 0, 0, 0x4042, 0, 0, 0, 0 );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f1, &e ) );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( f2, &e ) );

    free ( s1 );
    eventlog_filter_free ( f1 );
    eventlog_filter_free ( f2 );

    /* sym:prefix_* round-trip. */
    f1 = eventlog_filter_parse ( "sym:isr_*" );
    TEST_ASSERT_NOT_NULL ( f1 );
    s1 = eventlog_filter_to_string ( f1 );
    TEST_ASSERT_EQUAL_STRING ( "sym:isr_*", s1 );
    free ( s1 );
    eventlog_filter_free ( f1 );

    /* from_sym/to_sym round-trip. */
    f1 = eventlog_filter_parse ( "from_sym:main_loop to_sym:main_loop_end" );
    TEST_ASSERT_NOT_NULL ( f1 );
    s1 = eventlog_filter_to_string ( f1 );
    TEST_ASSERT_EQUAL_STRING ( "from_sym:main_loop to_sym:main_loop_end", s1 );
    free ( s1 );
    eventlog_filter_free ( f1 );
}


/* ================================================================
 * Vlna 4 Commit 25 - state-aware filtry (if iff1/im/reason/banking)
 * ================================================================ */


/**
 * @brief Pomocný event builder s explicitní ambient hodnotou.
 *
 * @c mk_event() nastavuje ambient na @c 0 (= IFF1=0, IM=0, reason=RESET,
 * banking=DEFAULT). Pro state-aware testy potřebujeme volně skládat
 * ambient bity z @c EVENTLOG_AMBIENT_* maker.
 */
static st_EVENTLOG_EVENT mk_event_amb ( uint8_t cat, uint16_t ambient )
{
    st_EVENTLOG_EVENT e = mk_event ( cat, 0, 0, 0, 0, 0, 0 );
    e.ambient = ambient;
    return e;
}


void test_filter_state_aware_iff1 ( void )
{
    /* if iff1:1 - eventy s IFF1=1. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "if iff1:1" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT iff_on  = mk_event_amb ( 0, EVENTLOG_AMBIENT_IFF1 );
    st_EVENTLOG_EVENT iff_off = mk_event_amb ( 0, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &iff_on ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &iff_off ) );
    eventlog_filter_free ( f );
}


void test_filter_state_aware_iff1_zero ( void )
{
    /* if iff1:0 - eventy s IFF1=0. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "if iff1:0" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT iff_off = mk_event_amb ( 0, 0 );
    st_EVENTLOG_EVENT iff_on  = mk_event_amb ( 0, EVENTLOG_AMBIENT_IFF1 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &iff_off ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &iff_on ) );
    eventlog_filter_free ( f );
}


void test_filter_state_aware_im_mode ( void )
{
    /* if im:2 - jen eventy s IM=2. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "if im:2" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT im0 = mk_event_amb ( 0, 0u << EVENTLOG_AMBIENT_IM_SHIFT );
    st_EVENTLOG_EVENT im1 = mk_event_amb ( 0, 1u << EVENTLOG_AMBIENT_IM_SHIFT );
    st_EVENTLOG_EVENT im2 = mk_event_amb ( 0, 2u << EVENTLOG_AMBIENT_IM_SHIFT );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &im0 ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &im1 ) );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &im2 ) );
    eventlog_filter_free ( f );

    /* Hodnota mimo rozsah 0..2 je parse error. */
    st_EVENTLOG_FILTER *bad = eventlog_filter_parse ( "if im:3" );
    TEST_ASSERT_NOT_NULL ( bad );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( bad ) );
    eventlog_filter_free ( bad );
}


void test_filter_state_aware_reason ( void )
{
    /* if reason:int_ack - eventy s ambient reason code 3 (= INT_ACK). */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "if reason:int_ack" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT e_int_ack = mk_event_amb (
        0, EVENTLOG_AMBIENT_REASON_IFF_INT_ACK << EVENTLOG_AMBIENT_REASON_SHIFT );
    st_EVENTLOG_EVENT e_nmi_ack = mk_event_amb (
        0, EVENTLOG_AMBIENT_REASON_IFF_NMI_ACK << EVENTLOG_AMBIENT_REASON_SHIFT );
    st_EVENTLOG_EVENT e_none    = mk_event_amb (
        0, EVENTLOG_AMBIENT_REASON_NONE << EVENTLOG_AMBIENT_REASON_SHIFT );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &e_int_ack ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &e_nmi_ack ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &e_none ) );
    eventlog_filter_free ( f );

    /* Všechny known reason jména musí parse OK. */
    static const char *reasons[] = {
        "reset", "ei", "di", "int_ack", "nmi_ack", "reti", "retn", "none"
    };
    for ( size_t i = 0; i < sizeof ( reasons ) / sizeof ( reasons[ 0 ] ); i++ ) {
        char buf[ 32 ];
        snprintf ( buf, sizeof ( buf ), "if reason:%s", reasons[ i ] );
        st_EVENTLOG_FILTER *fr = eventlog_filter_parse ( buf );
        TEST_ASSERT_NOT_NULL ( fr );
        TEST_ASSERT_NULL ( eventlog_filter_get_error ( fr ) );
        eventlog_filter_free ( fr );
    }
}


void test_filter_state_aware_banking ( void )
{
    /* if banking:ram (= ALL_RAM = kód 1). */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "if banking:ram" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT e_ram = mk_event_amb (
        0, EVENTLOG_AMBIENT_BANKING_ALL_RAM << EVENTLOG_AMBIENT_BANKING_SHIFT );
    st_EVENTLOG_EVENT e_default = mk_event_amb (
        0, EVENTLOG_AMBIENT_BANKING_DEFAULT << EVENTLOG_AMBIENT_BANKING_SHIFT );
    st_EVENTLOG_EVENT e_other = mk_event_amb (
        0, EVENTLOG_AMBIENT_BANKING_OTHER << EVENTLOG_AMBIENT_BANKING_SHIFT );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &e_ram ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &e_default ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &e_other ) );
    eventlog_filter_free ( f );

    /* Kanonická banking jména. */
    static const char *banks[] = {
        "default", "all_ram", "rom_low_off", "rom_high_off",
        "cgrom", "vram_640", "pcg_high", "other"
    };
    for ( size_t i = 0; i < sizeof ( banks ) / sizeof ( banks[ 0 ] ); i++ ) {
        char buf[ 32 ];
        snprintf ( buf, sizeof ( buf ), "if banking:%s", banks[ i ] );
        st_EVENTLOG_FILTER *fb = eventlog_filter_parse ( buf );
        TEST_ASSERT_NOT_NULL ( fb );
        TEST_ASSERT_NULL ( eventlog_filter_get_error ( fb ) );
        eventlog_filter_free ( fb );
    }

    /* Synonyma. */
    static const char *aliases[] = {
        "ram", "vram", "vram_low", "pcg_1", "pcg_2", "pcg_3"
    };
    for ( size_t i = 0; i < sizeof ( aliases ) / sizeof ( aliases[ 0 ] ); i++ ) {
        char buf[ 32 ];
        snprintf ( buf, sizeof ( buf ), "if banking:%s", aliases[ i ] );
        st_EVENTLOG_FILTER *fa = eventlog_filter_parse ( buf );
        TEST_ASSERT_NOT_NULL ( fa );
        TEST_ASSERT_NULL ( eventlog_filter_get_error ( fa ) );
        eventlog_filter_free ( fa );
    }
}


void test_filter_state_aware_negation ( void )
{
    /* !if iff1:1 - eventy s IFF1=0 match (negace BITu). */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "!if iff1:1" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT iff_off = mk_event_amb ( 0, 0 );
    st_EVENTLOG_EVENT iff_on  = mk_event_amb ( 0, EVENTLOG_AMBIENT_IFF1 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &iff_off ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &iff_on ) );
    eventlog_filter_free ( f );
}


void test_filter_state_aware_combined ( void )
{
    /* cat:cpu_int if iff1:0 - AND s ostatními tokens. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "cat:cpu_int if iff1:0" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    /* CPU_INT s IFF1=0 -> match. */
    st_EVENTLOG_EVENT a = mk_event_amb ( EVENTLOG_CAT_CPU_INT, 0 );
    /* CPU_INT s IFF1=1 -> no match (AND). */
    st_EVENTLOG_EVENT b = mk_event_amb ( EVENTLOG_CAT_CPU_INT, EVENTLOG_AMBIENT_IFF1 );
    /* GDG_MODE s IFF1=0 -> no match (cat se neshoduje). */
    st_EVENTLOG_EVENT c = mk_event_amb ( EVENTLOG_CAT_GDG_MODE, 0 );
    TEST_ASSERT_TRUE  ( eventlog_filter_match ( f, &a ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &b ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &c ) );
    eventlog_filter_free ( f );
}


void test_filter_state_aware_unknown_name ( void )
{
    /* Neznámé banking jméno = parse error. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "if banking:nonexistent" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* Neznámé reason jméno = parse error. */
    f = eventlog_filter_parse ( "if reason:foobar" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* Neznámé field jméno = parse error. */
    f = eventlog_filter_parse ( "if hello:1" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* "if" bez následujícího tokenu = parse error. */
    f = eventlog_filter_parse ( "if" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* "if" + bare token bez ':' = parse error. */
    f = eventlog_filter_parse ( "if iff1" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );
}


void test_filter_state_aware_roundtrip ( void )
{
    /* parse -> to_string -> parse -> match shodný. */
    static const char *exprs[] = {
        "if iff1:0",
        "if iff1:1",
        "if im:0",
        "if im:1",
        "if im:2",
        "if reason:int_ack",
        "if reason:nmi_ack",
        "if reason:none",
        "if banking:default",
        "if banking:all_ram",
        "if banking:rom_low_off",
        "if banking:cgrom",
    };
    for ( size_t i = 0; i < sizeof ( exprs ) / sizeof ( exprs[ 0 ] ); i++ ) {
        st_EVENTLOG_FILTER *f1 = eventlog_filter_parse ( exprs[ i ] );
        TEST_ASSERT_NOT_NULL ( f1 );
        TEST_ASSERT_NULL ( eventlog_filter_get_error ( f1 ) );

        char *s1 = eventlog_filter_to_string ( f1 );
        TEST_ASSERT_NOT_NULL ( s1 );
        TEST_ASSERT_EQUAL_STRING ( exprs[ i ], s1 );

        /* Re-parse a porovnání proti několika ambient stavům. */
        st_EVENTLOG_FILTER *f2 = eventlog_filter_parse ( s1 );
        TEST_ASSERT_NOT_NULL ( f2 );
        TEST_ASSERT_NULL ( eventlog_filter_get_error ( f2 ) );

        uint16_t test_ambients[] = {
            0u,
            (uint16_t) EVENTLOG_AMBIENT_IFF1,
            (uint16_t) ( 2u << EVENTLOG_AMBIENT_IM_SHIFT ),
            (uint16_t) ( EVENTLOG_AMBIENT_REASON_IFF_INT_ACK << EVENTLOG_AMBIENT_REASON_SHIFT ),
            (uint16_t) ( EVENTLOG_AMBIENT_BANKING_ALL_RAM << EVENTLOG_AMBIENT_BANKING_SHIFT ),
            (uint16_t) ( EVENTLOG_AMBIENT_IFF1
                         | ( 2u << EVENTLOG_AMBIENT_IM_SHIFT ) ),
        };
        for ( size_t k = 0; k < sizeof ( test_ambients )
                                  / sizeof ( test_ambients[ 0 ] ); k++ ) {
            st_EVENTLOG_EVENT e = mk_event_amb ( 0, test_ambients[ k ] );
            bool m1 = eventlog_filter_match ( f1, &e );
            bool m2 = eventlog_filter_match ( f2, &e );
            TEST_ASSERT_EQUAL_INT ( (int) m1, (int) m2 );
        }
        free ( s1 );
        eventlog_filter_free ( f1 );
        eventlog_filter_free ( f2 );
    }
}


/* ================================================================
 * Vlna 4 Commit 26 - temporal filtry (before/after/near)
 * ================================================================ */


/**
 * @brief Pomocný builder eventu s explicitní @c pxclk_total + @c category.
 */
static st_EVENTLOG_EVENT mk_event_t ( uint8_t cat, uint64_t pxclk_total )
{
    return mk_event ( cat, 0, 0, 0, 0, pxclk_total, 0 );
}


/**
 * @brief Postaví ctx s explicitním polem eventů.
 *
 * Test path používá @c ctx->events != NULL (= prosté pole), aby
 * nevyžadoval inicializaci globálního @c eventlog ringu.
 */
static st_EVENTLOG_FILTER_CTX mk_ctx ( const st_EVENTLOG_EVENT *events,
                                        size_t count )
{
    st_EVENTLOG_FILTER_CTX c;
    c.events = events;
    c.count  = count;
    return c;
}


void test_temporal_before_match ( void )
{
    /* before(1000) cat:cpu_int - eventy v posledních 1000 pxclk před INT. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "before(1000) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );
    TEST_ASSERT_TRUE ( eventlog_filter_has_temporal ( f ) );

    /* Ring: [e0 at 1500, r at 2000 (CPU_INT)]. */
    st_EVENTLOG_EVENT ring[2];
    ring[0] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 1500 );
    ring[1] = mk_event_t ( EVENTLOG_CAT_CPU_INT,  2000 );
    st_EVENTLOG_FILTER_CTX ctx = mk_ctx ( ring, 2 );

    /* e0 splňuje: e<=r (1500<=2000), diff 500 <= 1000. */
    TEST_ASSERT_TRUE ( eventlog_filter_match_ctx ( f, &ring[0], &ctx ) );
    /* r samotné splňuje: r<=r, diff 0. */
    TEST_ASSERT_TRUE ( eventlog_filter_match_ctx ( f, &ring[1], &ctx ) );

    eventlog_filter_free ( f );
}


void test_temporal_before_no_match_too_far ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "before(1000) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );

    /* e0 at 500, r at 2000 -> diff 1500 > 1000 -> no match. */
    st_EVENTLOG_EVENT ring[2];
    ring[0] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 500 );
    ring[1] = mk_event_t ( EVENTLOG_CAT_CPU_INT,  2000 );
    st_EVENTLOG_FILTER_CTX ctx = mk_ctx ( ring, 2 );

    TEST_ASSERT_FALSE ( eventlog_filter_match_ctx ( f, &ring[0], &ctx ) );

    eventlog_filter_free ( f );
}


void test_temporal_before_no_match_no_reference ( void )
{
    /* before(1000) cat:cpu_int - ring bez CPU_INT eventu. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "before(1000) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );

    st_EVENTLOG_EVENT ring[2];
    ring[0] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 500 );
    ring[1] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 1500 );
    st_EVENTLOG_FILTER_CTX ctx = mk_ctx ( ring, 2 );

    TEST_ASSERT_FALSE ( eventlog_filter_match_ctx ( f, &ring[0], &ctx ) );
    TEST_ASSERT_FALSE ( eventlog_filter_match_ctx ( f, &ring[1], &ctx ) );

    eventlog_filter_free ( f );
}


void test_temporal_after_match ( void )
{
    /* after(500) cat:cpu_int - eventy v 500 pxclk po INT. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "after(500) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );

    /* Ring: [r at 1000 (CPU_INT), e1 at 1400, e2 at 1600]. */
    st_EVENTLOG_EVENT ring[3];
    ring[0] = mk_event_t ( EVENTLOG_CAT_CPU_INT,  1000 );
    ring[1] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 1400 );
    ring[2] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 1600 );
    st_EVENTLOG_FILTER_CTX ctx = mk_ctx ( ring, 3 );

    /* r sám: diff 0 <= 500. */
    TEST_ASSERT_TRUE  ( eventlog_filter_match_ctx ( f, &ring[0], &ctx ) );
    /* e1: 1400 >= 1000, diff 400 <= 500. */
    TEST_ASSERT_TRUE  ( eventlog_filter_match_ctx ( f, &ring[1], &ctx ) );
    /* e2: 1600 >= 1000, diff 600 > 500 -> no match. */
    TEST_ASSERT_FALSE ( eventlog_filter_match_ctx ( f, &ring[2], &ctx ) );

    eventlog_filter_free ( f );
}


void test_temporal_near_match ( void )
{
    /* near(500) cat:cpu_int - eventy v okně +-500 kolem INT. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "near(500) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );

    st_EVENTLOG_EVENT ring[3];
    ring[0] = mk_event_t ( EVENTLOG_CAT_GDG_MODE,  600 );  /* diff 400 - in */
    ring[1] = mk_event_t ( EVENTLOG_CAT_CPU_INT,  1000 );  /* ref */
    ring[2] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 1400 );  /* diff 400 - in */
    st_EVENTLOG_FILTER_CTX ctx = mk_ctx ( ring, 3 );

    TEST_ASSERT_TRUE ( eventlog_filter_match_ctx ( f, &ring[0], &ctx ) );
    TEST_ASSERT_TRUE ( eventlog_filter_match_ctx ( f, &ring[1], &ctx ) );
    TEST_ASSERT_TRUE ( eventlog_filter_match_ctx ( f, &ring[2], &ctx ) );

    eventlog_filter_free ( f );
}


void test_temporal_near_no_match ( void )
{
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "near(100) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );

    st_EVENTLOG_EVENT ring[2];
    ring[0] = mk_event_t ( EVENTLOG_CAT_GDG_MODE,  500 );  /* diff 500 - out */
    ring[1] = mk_event_t ( EVENTLOG_CAT_CPU_INT,  1000 );
    st_EVENTLOG_FILTER_CTX ctx = mk_ctx ( ring, 2 );

    TEST_ASSERT_FALSE ( eventlog_filter_match_ctx ( f, &ring[0], &ctx ) );

    eventlog_filter_free ( f );
}


void test_temporal_combined_and ( void )
{
    /* cat:gdg_mode before(1000) cat:cpu_int
     * = e musí mít cat=GDG_MODE A zároveň být před nějakým CPU_INT (<=1000). */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse (
        "cat:gdg_mode before(1000) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT ring[3];
    ring[0] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 1500 );  /* GDG + before INT (diff 500) */
    ring[1] = mk_event_t ( EVENTLOG_CAT_CPU_INT,  2000 );
    ring[2] = mk_event_t ( EVENTLOG_CAT_PSG,       800 );  /* PSG, ne GDG -> ne */
    st_EVENTLOG_FILTER_CTX ctx = mk_ctx ( ring, 3 );

    TEST_ASSERT_TRUE  ( eventlog_filter_match_ctx ( f, &ring[0], &ctx ) );
    /* INT sám: cat != gdg_mode -> AND selže. */
    TEST_ASSERT_FALSE ( eventlog_filter_match_ctx ( f, &ring[1], &ctx ) );
    /* PSG: cat != gdg_mode -> AND selže (i kdyby temporal seděl). */
    TEST_ASSERT_FALSE ( eventlog_filter_match_ctx ( f, &ring[2], &ctx ) );

    eventlog_filter_free ( f );
}


void test_temporal_negation ( void )
{
    /* !before(1000) cat:cpu_int = eventy které NEJSOU before CPU_INT.
     * Parser: '!' před atomem; před temporal expression vyrobí NOT wrapper. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "!before(1000) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );

    st_EVENTLOG_EVENT ring[2];
    ring[0] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 1500 );  /* before INT -> match raw -> negace = false */
    ring[1] = mk_event_t ( EVENTLOG_CAT_CPU_INT,  2000 );
    st_EVENTLOG_FILTER_CTX ctx = mk_ctx ( ring, 2 );

    TEST_ASSERT_FALSE ( eventlog_filter_match_ctx ( f, &ring[0], &ctx ) );

    /* Event mimo okno: ring[0] @ 100 -> diff 1900 > 1000 -> raw=false -> negace=true. */
    st_EVENTLOG_EVENT far_e = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 100 );
    st_EVENTLOG_EVENT ring2[2];
    ring2[0] = far_e;
    ring2[1] = ring[1];
    st_EVENTLOG_FILTER_CTX ctx2 = mk_ctx ( ring2, 2 );
    TEST_ASSERT_TRUE ( eventlog_filter_match_ctx ( f, &ring2[0], &ctx2 ) );

    eventlog_filter_free ( f );
}


void test_temporal_unit_suffix ( void )
{
    /* before(1k) = 1000, before(1M) = 1000000. */
    st_EVENTLOG_FILTER *f1 = eventlog_filter_parse ( "before(1k) cat:cpu_int" );
    st_EVENTLOG_FILTER *f2 = eventlog_filter_parse ( "before(1M) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f1 );
    TEST_ASSERT_NOT_NULL ( f2 );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f1 ) );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f2 ) );

    /* f1: 1k = 1000 - event diff 800 matchne. */
    st_EVENTLOG_EVENT ring1[2];
    ring1[0] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 1200 );
    ring1[1] = mk_event_t ( EVENTLOG_CAT_CPU_INT,  2000 );  /* diff 800 < 1000 */
    st_EVENTLOG_FILTER_CTX c1 = mk_ctx ( ring1, 2 );
    TEST_ASSERT_TRUE ( eventlog_filter_match_ctx ( f1, &ring1[0], &c1 ) );

    /* f2: 1M = 1000000 - event diff 500000 matchne. */
    st_EVENTLOG_EVENT ring2[2];
    ring2[0] = mk_event_t ( EVENTLOG_CAT_GDG_MODE,  500000 );
    ring2[1] = mk_event_t ( EVENTLOG_CAT_CPU_INT,  1000000 );  /* diff 500000 < 1M */
    st_EVENTLOG_FILTER_CTX c2 = mk_ctx ( ring2, 2 );
    TEST_ASSERT_TRUE ( eventlog_filter_match_ctx ( f2, &ring2[0], &c2 ) );

    eventlog_filter_free ( f1 );
    eventlog_filter_free ( f2 );
}


void test_temporal_nested_two_levels ( void )
{
    /* before(1000) before(500) cat:cpu_int = 2 úrovně - povoleno. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse (
        "before(1000) before(500) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NULL ( eventlog_filter_get_error ( f ) );
    TEST_ASSERT_TRUE ( eventlog_filter_has_temporal ( f ) );

    eventlog_filter_free ( f );
}


void test_temporal_nested_three_levels ( void )
{
    /* before(1k) before(500) before(100) cat:X = 3 úrovně - error. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse (
        "before(1k) before(500) before(100) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );

    eventlog_filter_free ( f );
}


void test_temporal_roundtrip ( void )
{
    /* parse -> to_string -> parse - sémanticky shodné. */
    static const char *exprs[] = {
        "before(1000) cat:cpu_int",
        "after(500) cat:cpu_int",
        "near(200) cat:cpu_int",
        "before(1000) before(500) cat:cpu_int",
    };
    for ( size_t i = 0; i < sizeof ( exprs ) / sizeof ( exprs[ 0 ] ); i++ ) {
        st_EVENTLOG_FILTER *f1 = eventlog_filter_parse ( exprs[ i ] );
        TEST_ASSERT_NOT_NULL ( f1 );
        TEST_ASSERT_NULL ( eventlog_filter_get_error ( f1 ) );

        char *s1 = eventlog_filter_to_string ( f1 );
        TEST_ASSERT_NOT_NULL ( s1 );

        st_EVENTLOG_FILTER *f2 = eventlog_filter_parse ( s1 );
        TEST_ASSERT_NOT_NULL ( f2 );
        TEST_ASSERT_NULL ( eventlog_filter_get_error ( f2 ) );

        /* Match equivalence proti jednoduchému scénáři. */
        st_EVENTLOG_EVENT ring[2];
        ring[0] = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 1500 );
        ring[1] = mk_event_t ( EVENTLOG_CAT_CPU_INT,  2000 );
        st_EVENTLOG_FILTER_CTX ctx = mk_ctx ( ring, 2 );

        for ( size_t k = 0; k < 2; k++ ) {
            bool m1 = eventlog_filter_match_ctx ( f1, &ring[ k ], &ctx );
            bool m2 = eventlog_filter_match_ctx ( f2, &ring[ k ], &ctx );
            TEST_ASSERT_EQUAL_INT ( (int) m1, (int) m2 );
        }

        free ( s1 );
        eventlog_filter_free ( f1 );
        eventlog_filter_free ( f2 );
    }
}


void test_temporal_match_null_context ( void )
{
    /* Filter s temporal node-em volaný přes legacy shim (ctx=NULL) =
     * fallback na false. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "before(1000) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_TRUE ( eventlog_filter_has_temporal ( f ) );

    st_EVENTLOG_EVENT e = mk_event_t ( EVENTLOG_CAT_GDG_MODE, 1500 );

    /* Shim API bez ctx -> false (temporal vyžaduje ctx). */
    TEST_ASSERT_FALSE ( eventlog_filter_match ( f, &e ) );
    /* Explicit ctx=NULL přes ctx API -> taky false. */
    TEST_ASSERT_FALSE ( eventlog_filter_match_ctx ( f, &e, NULL ) );

    /* Non-temporal filter pres shim funguje stejně jako predtim. */
    st_EVENTLOG_FILTER *g = eventlog_filter_parse ( "cat:gdg_mode" );
    TEST_ASSERT_NOT_NULL ( g );
    TEST_ASSERT_FALSE ( eventlog_filter_has_temporal ( g ) );
    TEST_ASSERT_TRUE ( eventlog_filter_match ( g, &e ) );

    eventlog_filter_free ( f );
    eventlog_filter_free ( g );
}


void test_temporal_syntax_errors ( void )
{
    /* Chybějící ')'. */
    st_EVENTLOG_FILTER *f = eventlog_filter_parse ( "before(1000 cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* Empty window. */
    f = eventlog_filter_parse ( "before() cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* Empty reference. */
    f = eventlog_filter_parse ( "before(1000)" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );

    /* Invalid window value. */
    f = eventlog_filter_parse ( "before(abc) cat:cpu_int" );
    TEST_ASSERT_NOT_NULL ( f );
    TEST_ASSERT_NOT_NULL ( eventlog_filter_get_error ( f ) );
    eventlog_filter_free ( f );
}


/* ================================================================
 * Test runner
 * ================================================================ */


int main ( void )
{
    UNITY_BEGIN ( );

    RUN_TEST ( test_eventlog_filter_parse_empty );
    RUN_TEST ( test_eventlog_filter_parse_cat_single );
    RUN_TEST ( test_eventlog_filter_parse_cat_bulk );
    RUN_TEST ( test_eventlog_filter_parse_sub_bulk );
    RUN_TEST ( test_eventlog_filter_parse_pc_exact );
    RUN_TEST ( test_eventlog_filter_parse_pc_range );
    RUN_TEST ( test_eventlog_filter_parse_frame_gt );
    RUN_TEST ( test_eventlog_filter_parse_frame_lt );
    RUN_TEST ( test_eventlog_filter_parse_cycle_M_suffix );
    RUN_TEST ( test_eventlog_filter_parse_cycle_k_suffix );
    RUN_TEST ( test_eventlog_filter_parse_sline_range );
    RUN_TEST ( test_eventlog_filter_parse_px_range );
    RUN_TEST ( test_eventlog_filter_parse_payload_eq );
    RUN_TEST ( test_eventlog_filter_parse_negation );
    RUN_TEST ( test_eventlog_filter_parse_and );
    RUN_TEST ( test_eventlog_filter_parse_or_group );
    RUN_TEST ( test_eventlog_filter_parse_complex );

    RUN_TEST ( test_eventlog_filter_match_simple );
    RUN_TEST ( test_eventlog_filter_match_no_match );
    RUN_TEST ( test_eventlog_filter_match_complex );
    RUN_TEST ( test_eventlog_filter_match_negation );
    RUN_TEST ( test_eventlog_filter_match_or );

    RUN_TEST ( test_eventlog_filter_roundtrip );
    RUN_TEST ( test_eventlog_filter_invalid_syntax );
    RUN_TEST ( test_eventlog_filter_cat_name_table );

    /* Vlna 3 Commit 18 - symbol-aware filtry. */
    RUN_TEST ( test_eventlog_filter_sym_exact_match );
    RUN_TEST ( test_eventlog_filter_sym_exact_no_match );
    RUN_TEST ( test_eventlog_filter_sym_prefix_match );
    RUN_TEST ( test_eventlog_filter_sym_prefix_no_match );
    RUN_TEST ( test_eventlog_filter_sym_range_match );
    RUN_TEST ( test_eventlog_filter_sym_range_outside );
    RUN_TEST ( test_eventlog_filter_sym_unknown_name );
    RUN_TEST ( test_eventlog_filter_sym_negate );
    RUN_TEST ( test_eventlog_filter_sym_syntax_errors );
    RUN_TEST ( test_eventlog_filter_sym_roundtrip );

    /* Vlna 4 Commit 25 - state-aware filtry (if iff1/im/reason/banking). */
    RUN_TEST ( test_filter_state_aware_iff1 );
    RUN_TEST ( test_filter_state_aware_iff1_zero );
    RUN_TEST ( test_filter_state_aware_im_mode );
    RUN_TEST ( test_filter_state_aware_reason );
    RUN_TEST ( test_filter_state_aware_banking );
    RUN_TEST ( test_filter_state_aware_negation );
    RUN_TEST ( test_filter_state_aware_combined );
    RUN_TEST ( test_filter_state_aware_unknown_name );
    RUN_TEST ( test_filter_state_aware_roundtrip );

    /* Vlna 4 Commit 26 - temporal filtry (before/after/near). */
    RUN_TEST ( test_temporal_before_match );
    RUN_TEST ( test_temporal_before_no_match_too_far );
    RUN_TEST ( test_temporal_before_no_match_no_reference );
    RUN_TEST ( test_temporal_after_match );
    RUN_TEST ( test_temporal_near_match );
    RUN_TEST ( test_temporal_near_no_match );
    RUN_TEST ( test_temporal_combined_and );
    RUN_TEST ( test_temporal_negation );
    RUN_TEST ( test_temporal_unit_suffix );
    RUN_TEST ( test_temporal_nested_two_levels );
    RUN_TEST ( test_temporal_nested_three_levels );
    RUN_TEST ( test_temporal_roundtrip );
    RUN_TEST ( test_temporal_match_null_context );
    RUN_TEST ( test_temporal_syntax_errors );

    return UNITY_END ( );
}
