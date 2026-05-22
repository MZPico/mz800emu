/*
 * test_io_history_filter.c - unit testy pro io_history_filter parser+match.
 *
 * V1.5.F refactor: filter je opaque AST + arena, testy musí používat
 * jen new/parse/match/free API. Žádný přístup ke struct fieldům.
 *
 * Pokrývá:
 *  - parse_basic_token (port:CE, pc:4042) - sémantika ověřena match()
 *  - parse_range_token (pc:4000-40FF)
 *  - parse_compare_token (frame:>100, frame:<100)
 *  - parse_multiple_tokens (= AND combine)
 *  - parse_invalid (bad hex, missing value, range min > max)
 *  - in/out keyword tokens
 *  - plain text → name substring match
 *  - match_basic (event proti filtru)
 *  - match_name_substr (case-insensitive)
 *  - match_empty_filter (= match all)
 *  - per-token negace ('!') invertuje raw match
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <string.h>
#include <stdint.h>

#include "debugger/io_history.h"
#include "debugger/io_history_filter.h"


/* Sdílený filter pro každý test - alokován v setUp, free v tearDown.
 * Tím se ověřuje že arena se správně reset()-uje mezi parse() voláními.
 * Pozn.: globální static je v testu OK, není to thread-safety případ. */
static st_IO_HISTORY_FILTER *g_f = NULL;


void setUp ( void )
{
    g_f = io_history_filter_new ( );
    TEST_ASSERT_NOT_NULL ( g_f );
}


void tearDown ( void )
{
    io_history_filter_free ( g_f );
    g_f = NULL;
}


/* ================================================================
 * Helpery
 * ================================================================ */

static st_IO_HISTORY_EVENT mk_event ( uint32_t frame, uint16_t port,
                                       uint16_t pc, uint8_t value, bool is_in )
{
    st_IO_HISTORY_EVENT e;
    memset ( &e, 0, sizeof ( e ) );
    e.frame  = frame;
    e.port   = port;
    e.pc     = pc;
    e.value  = value;
    e.flags  = is_in ? IO_HISTORY_FLAG_READ : 0u;
    return e;
}


/**
 * Helper - V1.5.E memory-mapped event (= flags has FLAG_MEMORY bit).
 */
static st_IO_HISTORY_EVENT mk_event_mem ( uint32_t frame, uint16_t addr,
                                           uint16_t pc, uint8_t value,
                                           bool is_read )
{
    st_IO_HISTORY_EVENT e;
    memset ( &e, 0, sizeof ( e ) );
    e.frame  = frame;
    e.port   = addr;     /* MMIO addr ulozeno do `port` field */
    e.pc     = pc;
    e.value  = value;
    e.flags  = ( is_read ? IO_HISTORY_FLAG_READ : 0u ) | IO_HISTORY_FLAG_MEMORY;
    return e;
}


/* ================================================================
 * Parse: basic single tokens
 * ================================================================ */

void test_filter_parse_empty ( void )
{
    /* empty filter = match all */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e = mk_event ( 100, 0xCE, 0x4042, 0xAA, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );
    st_IO_HISTORY_EVENT e2 = mk_event ( 9999, 0xFFFF, 0xFFFF, 0xFF, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e2, NULL ) );
}


void test_filter_parse_null ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( NULL, g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e = mk_event ( 100, 0xCE, 0x4042, 0xAA, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_parse_port_basic ( void )
{
    /* port: = low byte 8-bit match. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0x00CD, 0, 0, false );
    /* low byte match → 0x20CE musí take projít. */
    st_IO_HISTORY_EVENT hi   = mk_event ( 0, 0x20CE, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hi,   NULL ) );
}


void test_filter_parse_port16_full ( void )
{
    /* port16: = full 16-bit. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port16:CF06", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 0, 0xCF06, 0, 0, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0x0F06, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss, NULL ) );
}


void test_filter_parse_port_lowercase ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:ce", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e = mk_event ( 0, 0x00CE, 0, 0, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_parse_pc_basic ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "pc:4042", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 0, 0, 0x4042, 0, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0, 0x4043, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss, NULL ) );
}


/* ================================================================
 * Parse: ranges
 * ================================================================ */

void test_filter_parse_pc_range ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "pc:4000-40FF", g_f,
                                                   err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT lo   = mk_event ( 0, 0, 0x4000, 0, false );
    st_IO_HISTORY_EVENT hi   = mk_event ( 0, 0, 0x40FF, 0, false );
    st_IO_HISTORY_EVENT out  = mk_event ( 0, 0, 0x4100, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &lo,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hi,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &out, NULL ) );
}


void test_filter_parse_port_range ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:C0-CF", g_f,
                                                   err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT lo  = mk_event ( 0, 0x00C0, 0, 0, false );
    st_IO_HISTORY_EVENT hi  = mk_event ( 0, 0x00CF, 0, 0, false );
    st_IO_HISTORY_EVENT out = mk_event ( 0, 0x00D0, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &lo,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hi,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &out, NULL ) );
}


/* ================================================================
 * Parse: compare frame:> frame:<
 * ================================================================ */

void test_filter_parse_frame_gt ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "frame:>100", g_f,
                                                   err, sizeof ( err ) ) );
    /* frame > 100 → 100 miss, 101 hit. */
    st_IO_HISTORY_EVENT e100 = mk_event ( 100, 0, 0, 0, false );
    st_IO_HISTORY_EVENT e101 = mk_event ( 101, 0, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e100, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e101, NULL ) );
}


void test_filter_parse_frame_lt ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "frame:<100", g_f,
                                                   err, sizeof ( err ) ) );
    /* frame < 100 → 99 hit, 100 miss. */
    st_IO_HISTORY_EVENT e99  = mk_event ( 99,  0, 0, 0, false );
    st_IO_HISTORY_EVENT e100 = mk_event ( 100, 0, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e99,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e100, NULL ) );
}


void test_filter_parse_frame_range ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "frame:50-150", g_f,
                                                   err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT lo  = mk_event ( 50,  0, 0, 0, false );
    st_IO_HISTORY_EVENT hi  = mk_event ( 150, 0, 0, 0, false );
    st_IO_HISTORY_EVENT mid = mk_event ( 100, 0, 0, 0, false );
    st_IO_HISTORY_EVENT lo2 = mk_event ( 49,  0, 0, 0, false );
    st_IO_HISTORY_EVENT hi2 = mk_event ( 151, 0, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &lo,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hi,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &mid, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &lo2, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &hi2, NULL ) );
}


void test_filter_parse_frame_equality ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "frame:42", g_f,
                                                   err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 42, 0, 0, 0, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 43, 0, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss, NULL ) );
}


/* ================================================================
 * Parse: in/out keywords + name_substr
 * ================================================================ */

void test_filter_parse_in_keyword ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "in", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e_in  = mk_event ( 0, 0, 0, 0, true );
    st_IO_HISTORY_EVENT e_out = mk_event ( 0, 0, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_in,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_out, NULL ) );
}


void test_filter_parse_out_keyword ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "out", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e_in  = mk_event ( 0, 0, 0, 0, true );
    st_IO_HISTORY_EVENT e_out = mk_event ( 0, 0, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_in,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_out, NULL ) );
}


void test_filter_parse_plain_text ( void )
{
    /* Plain text → name substring match (case-insensitive). */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "GDG", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e = mk_event ( 0, 0, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e, "GDG - WF (W)" ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, "8255 PPI - Port A" ) );
}


/* ================================================================
 * Parse: multiple tokens (AND combination)
 * ================================================================ */

void test_filter_parse_multiple_tokens ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE pc:4042", g_f,
                                                   err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT both = mk_event ( 0, 0x00CE, 0x4042, 0, false );
    st_IO_HISTORY_EVENT only_port = mk_event ( 0, 0x00CE, 0x4000, 0, false );
    st_IO_HISTORY_EVENT only_pc   = mk_event ( 0, 0x00CD, 0x4042, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &both,      NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &only_port, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &only_pc,   NULL ) );
}


void test_filter_parse_complex ( void )
{
    /* "port:CE pc:4000-40FF frame:>100 out" - vše AND. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse (
        "port:CE pc:4000-40FF frame:>100 out", g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT hit       = mk_event ( 200, 0x00CE, 0x4042, 0, false );
    st_IO_HISTORY_EVENT miss_port = mk_event ( 200, 0x00CD, 0x4042, 0, false );
    st_IO_HISTORY_EVENT miss_pc   = mk_event ( 200, 0x00CE, 0x5000, 0, false );
    st_IO_HISTORY_EVENT miss_frm  = mk_event (  50, 0x00CE, 0x4042, 0, false );
    st_IO_HISTORY_EVENT miss_type = mk_event ( 200, 0x00CE, 0x4042, 0, true );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hit,       NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss_port, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss_pc,   NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss_frm,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss_type, NULL ) );
}


/* ================================================================
 * Parse: invalid syntax
 * ================================================================ */

void test_filter_parse_invalid_hex ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "port:XYZ", g_f,
                                                    err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );  /* err message present */
}


void test_filter_parse_missing_value ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "port:", g_f,
                                                    err, sizeof ( err ) ) );
}


void test_filter_parse_invalid_range ( void )
{
    char err[ 64 ] = "";
    /* lo > hi */
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "pc:8000-4000", g_f,
                                                    err, sizeof ( err ) ) );
}


void test_filter_parse_unknown_prefix ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "foo:bar", g_f,
                                                    err, sizeof ( err ) ) );
}


/* ================================================================
 * Match: jednoduchy event vs filter
 * ================================================================ */

void test_filter_match_empty_filter ( void )
{
    /* reset bez parse = match all */
    io_history_filter_reset ( g_f );
    st_IO_HISTORY_EVENT e = mk_event ( 100, 0xCE, 0x4042, 0xAA, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_port_hit ( void )
{
    char err[ 64 ] = "";
    io_history_filter_parse ( "port:CE", g_f, err, sizeof ( err ) );
    st_IO_HISTORY_EVENT e = mk_event ( 100, 0xCE, 0x4042, 0xAA, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_port_miss ( void )
{
    char err[ 64 ] = "";
    io_history_filter_parse ( "port:CE", g_f, err, sizeof ( err ) );
    st_IO_HISTORY_EVENT e = mk_event ( 100, 0xCD, 0x4042, 0xAA, true );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_pc_range_hit ( void )
{
    char err[ 64 ] = "";
    io_history_filter_parse ( "pc:4000-40FF", g_f, err, sizeof ( err ) );
    st_IO_HISTORY_EVENT e = mk_event ( 100, 0xCE, 0x4042, 0xAA, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_pc_range_miss ( void )
{
    char err[ 64 ] = "";
    io_history_filter_parse ( "pc:4000-40FF", g_f, err, sizeof ( err ) );
    st_IO_HISTORY_EVENT e = mk_event ( 100, 0xCE, 0x5000, 0xAA, true );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_frame_gt ( void )
{
    char err[ 64 ] = "";
    io_history_filter_parse ( "frame:>100", g_f, err, sizeof ( err ) );
    st_IO_HISTORY_EVENT e1 = mk_event ( 50, 0xCE, 0x4042, 0xAA, true );
    st_IO_HISTORY_EVENT e2 = mk_event ( 150, 0xCE, 0x4042, 0xAA, true );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e1, NULL ) );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e2, NULL ) );
}


void test_filter_match_in_only ( void )
{
    char err[ 64 ] = "";
    io_history_filter_parse ( "in", g_f, err, sizeof ( err ) );
    st_IO_HISTORY_EVENT in_ev  = mk_event ( 100, 0xCE, 0x4042, 0xAA, true );
    st_IO_HISTORY_EVENT out_ev = mk_event ( 100, 0xCE, 0x4042, 0xAA, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &in_ev, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &out_ev, NULL ) );
}


void test_filter_match_combined_and ( void )
{
    char err[ 64 ] = "";
    io_history_filter_parse ( "port:CE pc:4042", g_f, err, sizeof ( err ) );
    st_IO_HISTORY_EVENT both = mk_event ( 100, 0xCE, 0x4042, 0xAA, true );
    st_IO_HISTORY_EVENT only_port = mk_event ( 100, 0xCE, 0x5000, 0xAA, true );
    st_IO_HISTORY_EVENT only_pc = mk_event ( 100, 0xCD, 0x4042, 0xAA, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &both, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &only_port, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &only_pc, NULL ) );
}


void test_filter_match_name_substr_ci ( void )
{
    char err[ 64 ] = "";
    io_history_filter_parse ( "gdg", g_f, err, sizeof ( err ) );
    st_IO_HISTORY_EVENT e = mk_event ( 100, 0xCE, 0x4042, 0xAA, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, "GDG - DMD (W)" ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, "8255 PPI - Port A" ) );
    /* NULL port_name = no match */
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
}


/* ================================================================
 * V1.5 fix #4: negace + value: prefix
 * ================================================================ */

void test_filter_parse_negate_port ( void )
{
    /* !port:CE - event s portem 0xCE NESMÍ projít, ostatní ANO. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!port:CE", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e_ce = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT e_d0 = mk_event ( 0, 0x00D0, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_ce, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_d0, NULL ) );
}


void test_filter_parse_negate_pc ( void )
{
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!pc:4042", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 0, 0, 0x4042, 0, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0, 0x4000, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &miss, NULL ) );
}


void test_filter_parse_negate_frame ( void )
{
    /* !frame:>100 → negace "> 100" = match frames <= 100. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!frame:>100", g_f,
                                                   err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e_le = mk_event ( 100, 0, 0, 0, false );
    st_IO_HISTORY_EVENT e_gt = mk_event ( 200, 0, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_le, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_gt, NULL ) );
}


void test_filter_parse_negate_in ( void )
{
    /* !in = match only OUT. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!in", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT in_ev  = mk_event ( 0, 0, 0, 0, true );
    st_IO_HISTORY_EVENT out_ev = mk_event ( 0, 0, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &in_ev,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &out_ev, NULL ) );
}


void test_filter_parse_negate_text ( void )
{
    /* !gdg → event s port name containing "gdg" NESMÍ projít. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!gdg", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e = mk_event ( 0, 0, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, "GDG - WF (W)" ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e, "8255 PPI - Port A" ) );
}


void test_filter_parse_value_basic ( void )
{
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "value:42", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 0, 0, 0, 0x42, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0, 0, 0x43, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss, NULL ) );
}


void test_filter_parse_value_range ( void )
{
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "value:00-7F", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT lo  = mk_event ( 0, 0, 0, 0x00, false );
    st_IO_HISTORY_EVENT hi  = mk_event ( 0, 0, 0, 0x7F, false );
    st_IO_HISTORY_EVENT out = mk_event ( 0, 0, 0, 0x80, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &lo,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hi,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &out, NULL ) );
}


void test_filter_parse_negate_value ( void )
{
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!value:42", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 0, 0, 0, 0x42, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0, 0, 0x55, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &miss, NULL ) );
}


void test_filter_parse_negate_alone_invalid ( void )
{
    /* Samotný '!' bez tokenu = chyba. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "!", g_f, err, sizeof ( err ) ) );
}


void test_filter_match_negate_port_excludes ( void )
{
    /* !port:CE → event s port=0xCE NESMI projit. */
    char err[ 80 ] = "";
    io_history_filter_parse ( "!port:CE", g_f, err, sizeof ( err ) );

    st_IO_HISTORY_EVENT e_ce = mk_event ( 100, 0x00CE, 0x4000, 0xAA, false );
    st_IO_HISTORY_EVENT e_d0 = mk_event ( 100, 0x00D0, 0x4000, 0xAA, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_ce, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_d0, NULL ) );
}


void test_filter_match_negate_pc_excludes ( void )
{
    char err[ 80 ] = "";
    io_history_filter_parse ( "!pc:4042", g_f, err, sizeof ( err ) );

    st_IO_HISTORY_EVENT e_match = mk_event ( 100, 0xCE, 0x4042, 0xAA, false );
    st_IO_HISTORY_EVENT e_other = mk_event ( 100, 0xCE, 0x4000, 0xAA, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_match, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_other, NULL ) );
}


void test_filter_match_negate_value_excludes ( void )
{
    char err[ 80 ] = "";
    io_history_filter_parse ( "!value:42", g_f, err, sizeof ( err ) );

    st_IO_HISTORY_EVENT e_42 = mk_event ( 100, 0xCE, 0x4000, 0x42, false );
    st_IO_HISTORY_EVENT e_55 = mk_event ( 100, 0xCE, 0x4000, 0x55, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_42, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_55, NULL ) );
}


void test_filter_match_combined_negate ( void )
{
    /* "port:CE !pc:4042 frame:>100" - port=0xCE AND pc!=0x4042 AND frame>100. */
    char err[ 80 ] = "";
    io_history_filter_parse ( "port:CE !pc:4042 frame:>100", g_f, err, sizeof ( err ) );

    st_IO_HISTORY_EVENT e1 = mk_event ( 200, 0x00CE, 0x4000, 0xAA, false );
    /* port=0xCE OK, pc!=0x4042 OK, frame>100 OK -> match */
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e1, NULL ) );

    st_IO_HISTORY_EVENT e2 = mk_event ( 200, 0x00CE, 0x4042, 0xAA, false );
    /* pc==4042 -> negate vyradi */
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e2, NULL ) );

    st_IO_HISTORY_EVENT e3 = mk_event ( 50, 0x00CE, 0x4000, 0xAA, false );
    /* frame=50 nesplnuje >100 */
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e3, NULL ) );

    st_IO_HISTORY_EVENT e4 = mk_event ( 200, 0x00D0, 0x4000, 0xAA, false );
    /* port!=CE */
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e4, NULL ) );
}


void test_filter_match_negate_in_means_out ( void )
{
    /* !in = exclude IN = match jen OUT. */
    char err[ 80 ] = "";
    io_history_filter_parse ( "!in", g_f, err, sizeof ( err ) );

    st_IO_HISTORY_EVENT e_in  = mk_event ( 100, 0xCE, 0x4000, 0xAA, true );
    st_IO_HISTORY_EVENT e_out = mk_event ( 100, 0xCE, 0x4000, 0xBB, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_in,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_out, NULL ) );
}


void test_filter_match_value_basic ( void )
{
    char err[ 80 ] = "";
    io_history_filter_parse ( "value:42", g_f, err, sizeof ( err ) );

    st_IO_HISTORY_EVENT e_42 = mk_event ( 100, 0xCE, 0x4000, 0x42, false );
    st_IO_HISTORY_EVENT e_43 = mk_event ( 100, 0xCE, 0x4000, 0x43, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_43, NULL ) );
}


/* ================================================================
 * V1.5.D fix #3: cycle: prefix
 * ================================================================ */

void test_filter_parse_cycle_eq ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "cycle:1234567",
                                                  g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e = mk_event ( 0, 0, 0, 0, false );
    e.cpu_cycle = 1234567;
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e, NULL ) );
    e.cpu_cycle = 1234568;
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_parse_cycle_gt ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "cycle:>1000000",
                                                  g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e = mk_event ( 0, 0, 0, 0, false );
    e.cpu_cycle = 1000000;
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
    e.cpu_cycle = 1000001;
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_parse_cycle_lt ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "cycle:<500000",
                                                  g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e = mk_event ( 0, 0, 0, 0, false );
    e.cpu_cycle = 499999;
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e, NULL ) );
    e.cpu_cycle = 500000;
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_parse_cycle_range ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "cycle:1000-2000",
                                                  g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e = mk_event ( 0, 0, 0, 0, false );
    e.cpu_cycle = 1000;
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e, NULL ) );
    e.cpu_cycle = 2000;
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e, NULL ) );
    e.cpu_cycle = 999;
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
    e.cpu_cycle = 2001;
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_parse_negate_cycle ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!cycle:1000",
                                                  g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e = mk_event ( 0, 0, 0, 0, false );
    e.cpu_cycle = 1000;
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
    e.cpu_cycle = 1001;
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_cycle_eq ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "cycle:5000",
                                                  g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e = mk_event ( 0, 0, 0, 0, false );
    e.cpu_cycle = 5000;
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );

    e.cpu_cycle = 5001;
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_cycle_range ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "cycle:1000-2000",
                                                  g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e = mk_event ( 0, 0, 0, 0, false );
    e.cpu_cycle = 1500;
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );

    e.cpu_cycle = 999;
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );

    e.cpu_cycle = 2001;
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );

    /* Inclusive boundaries. */
    e.cpu_cycle = 1000;
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );
    e.cpu_cycle = 2000;
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_negate_cycle ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!cycle:5000",
                                                  g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e = mk_event ( 0, 0, 0, 0, false );
    e.cpu_cycle = 5000;
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );

    e.cpu_cycle = 4999;
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );
}


/* ================================================================
 * V1.5.D fix: port: 8-bit low byte vs port16: full 16-bit
 * ================================================================ */

void test_filter_parse_port_8bit_low_byte_match ( void )
{
    /* port:CE = low byte match: 0x20CE projde, 0x00CE projde, 0x00CD nikoliv. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hi  = mk_event ( 0, 0x20CE, 0, 0, false );
    st_IO_HISTORY_EVENT lo  = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT bad = mk_event ( 0, 0x00CD, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hi,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &lo,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &bad, NULL ) );
}


void test_filter_parse_port16_full_match ( void )
{
    /* port16:20CE = full 16-bit: 0x20CE projde, 0x00CE NE. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port16:20CE", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 0, 0x20CE, 0, 0, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0x00CE, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss, NULL ) );
}


void test_filter_parse_port16_range ( void )
{
    /* port16:CF00-CF0F = range full 16-bit. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port16:CF00-CF0F", g_f,
                                                  err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT lo  = mk_event ( 0, 0xCF00, 0, 0, false );
    st_IO_HISTORY_EVENT hi  = mk_event ( 0, 0xCF0F, 0, 0, false );
    st_IO_HISTORY_EVENT out = mk_event ( 0, 0xCF10, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &lo,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hi,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &out, NULL ) );
}


void test_filter_parse_port_8bit_too_large ( void )
{
    /* port:CE06 musi selhat - low byte je max 0xFF, 4 hex chars > 0xFF. */
    char err[ 64 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "port:CE06", g_f,
                                                    err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_filter_parse_port16_too_large ( void )
{
    /* port16:1FFFF (5 hex chars > 0xFFFF) musi selhat. */
    char err[ 64 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "port16:1FFFF", g_f,
                                                    err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_filter_match_port_8bit_low_byte_match ( void )
{
    /* port:CE proti event.port=0x20CE -> match (low byte se shoduje). */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE", g_f,
                                                  err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e1 = mk_event ( 0, 0x20CE, 0x4000, 0xAA, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e1, NULL ) );

    st_IO_HISTORY_EVENT e2 = mk_event ( 0, 0x40CE, 0x4000, 0xAA, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e2, NULL ) );

    st_IO_HISTORY_EVENT e3 = mk_event ( 0, 0x00CE, 0x4000, 0xAA, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e3, NULL ) );
}


void test_filter_match_port_8bit_low_byte_miss ( void )
{
    /* port:CE proti event.port=0x20CF -> no match (low byte 0xCF != 0xCE). */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE", g_f,
                                                  err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e = mk_event ( 0, 0x20CF, 0x4000, 0xAA, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_port16_full_match ( void )
{
    /* port16:00CE proti event.port=0x00CE -> match. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port16:00CE", g_f,
                                                  err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e = mk_event ( 0, 0x00CE, 0x4000, 0xAA, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_port16_full_miss ( void )
{
    /* port16:00CE proti event.port=0x20CE -> NO match (high byte se lisi). */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port16:00CE", g_f,
                                                  err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e = mk_event ( 0, 0x20CE, 0x4000, 0xAA, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_filter_match_port16_range ( void )
{
    /* port16:CF00-CF0F matchuje 0xCF06, ne 0xCF10. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port16:CF00-CF0F", g_f,
                                                  err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e_in  = mk_event ( 0, 0xCF06, 0x4000, 0xAA, false );
    st_IO_HISTORY_EVENT e_out = mk_event ( 0, 0xCF10, 0x4000, 0xAA, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_in,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_out, NULL ) );
}


void test_filter_match_negate_port16 ( void )
{
    /* !port16:00CE - exclude full 16-bit 0x00CE. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!port16:00CE", g_f,
                                                  err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e_match = mk_event ( 0, 0x00CE, 0x4000, 0xAA, false );
    st_IO_HISTORY_EVENT e_other = mk_event ( 0, 0x20CE, 0x4000, 0xAA, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_match, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_other, NULL ) );
}


/* ================================================================
 * V1.5.E - addr: prefix, mr/mw keywords, sjednocena in/out
 * ================================================================ */


void test_filter_parse_addr_basic ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "addr:E008", g_f, err, sizeof ( err ) ) );
    /* addr: matchuje JEN MMIO eventy. */
    st_IO_HISTORY_EVENT mmio_hit  = mk_event_mem ( 0, 0xE008, 0, 0, true );
    st_IO_HISTORY_EVENT mmio_miss = mk_event_mem ( 0, 0xE000, 0, 0, true );
    st_IO_HISTORY_EVENT iorq      = mk_event ( 0, 0xE008, 0, 0, true );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &mmio_hit,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &mmio_miss, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &iorq,      NULL ) );
}


void test_filter_parse_addr_range ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "addr:E000-E008", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT lo  = mk_event_mem ( 0, 0xE000, 0, 0, true );
    st_IO_HISTORY_EVENT hi  = mk_event_mem ( 0, 0xE008, 0, 0, true );
    st_IO_HISTORY_EVENT out = mk_event_mem ( 0, 0xE009, 0, 0, true );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &lo,  NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hi,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &out, NULL ) );
}


void test_filter_parse_addr_negate ( void )
{
    /* !addr:E008 - exclude MMIO 0xE008. Ostatní MMIO projdou. IORQ event
     * proti addr leaf v non-negate stavu vrací false (raw match=false);
     * po negaci raw → !false = true → IORQ event projde. To je v souladu
     * s předchozí flat sémantikou (per-token negate invertuje raw match). */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!addr:E008", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT mmio_match = mk_event_mem ( 0, 0xE008, 0, 0, true );
    st_IO_HISTORY_EVENT mmio_other = mk_event_mem ( 0, 0xE000, 0, 0, true );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &mmio_match, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &mmio_other, NULL ) );
}


void test_filter_match_addr_hit ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "addr:E008", g_f, err, sizeof ( err ) ) );

    /* MMIO event 0xE008 - match. */
    st_IO_HISTORY_EVENT em = mk_event_mem ( 0, 0xE008, 0x4000, 0x42, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &em, NULL ) );

    /* MMIO event 0xE000 - mimo range. */
    st_IO_HISTORY_EVENT em2 = mk_event_mem ( 0, 0xE000, 0x4000, 0x42, true );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &em2, NULL ) );
}


void test_filter_match_addr_iorq_rejected ( void )
{
    /* addr: prefix matchuje JEN MMIO eventy. IORQ event s portem 0xE008
     * (= teoreticky stejna 16-bit hodnota) musi byt rejected. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "addr:E008", g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT iorq = mk_event ( 0, 0xE008, 0x4000, 0x42, true );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &iorq, NULL ) );
}


void test_filter_match_addr_range ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "addr:E000-E004", g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT em0 = mk_event_mem ( 0, 0xE000, 0, 0, true );
    st_IO_HISTORY_EVENT em4 = mk_event_mem ( 0, 0xE004, 0, 0, true );
    st_IO_HISTORY_EVENT em8 = mk_event_mem ( 0, 0xE008, 0, 0, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &em0, NULL ) );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &em4, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &em8, NULL ) );
}


void test_filter_parse_mr_keyword ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "mr", g_f, err, sizeof ( err ) ) );
    /* mr matchuje JEN memory READ event. */
    st_IO_HISTORY_EVENT mr = mk_event_mem ( 0, 0xE008, 0, 0, true );
    st_IO_HISTORY_EVENT mw = mk_event_mem ( 0, 0xE008, 0, 0, false );
    st_IO_HISTORY_EVENT iorq = mk_event ( 0, 0x00CE, 0, 0, true );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &mr,   NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &mw,   NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &iorq, NULL ) );
}


void test_filter_parse_mw_keyword ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "mw", g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT mr = mk_event_mem ( 0, 0xE008, 0, 0, true );
    st_IO_HISTORY_EVENT mw = mk_event_mem ( 0, 0xE008, 0, 0, false );
    st_IO_HISTORY_EVENT iorq = mk_event ( 0, 0x00CE, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &mr,   NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &mw,   NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &iorq, NULL ) );
}


void test_filter_match_mr_only ( void )
{
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "mr", g_f, err, sizeof ( err ) ) );

    /* MR event - match. */
    st_IO_HISTORY_EVENT em_r = mk_event_mem ( 0, 0xE008, 0, 0, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &em_r, NULL ) );

    /* MW event - reject (= jiny direction). */
    st_IO_HISTORY_EVENT em_w = mk_event_mem ( 0, 0xE008, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &em_w, NULL ) );

    /* IORQ IN event - reject (= ne memory). */
    st_IO_HISTORY_EVENT iorq_in = mk_event ( 0, 0x00CE, 0, 0, true );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &iorq_in, NULL ) );
}


void test_filter_match_in_unified_iorq_and_mmio ( void )
{
    /* V1.5.E sjednocena semantika: "in" matchuje IN i MR. */
    char err[ 64 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "in", g_f, err, sizeof ( err ) ) );

    /* IORQ IN - match. */
    st_IO_HISTORY_EVENT iorq_in = mk_event ( 0, 0x00CE, 0, 0, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &iorq_in, NULL ) );

    /* MMIO MR - take match (= univerzalni in). */
    st_IO_HISTORY_EVENT mmio_r = mk_event_mem ( 0, 0xE008, 0, 0, true );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &mmio_r, NULL ) );

    /* IORQ OUT - reject. */
    st_IO_HISTORY_EVENT iorq_out = mk_event ( 0, 0x00CE, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &iorq_out, NULL ) );

    /* MMIO MW - reject. */
    st_IO_HISTORY_EVENT mmio_w = mk_event_mem ( 0, 0xE008, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &mmio_w, NULL ) );
}


/* ================================================================
 * Fáze 2 - OR operátor ("|" / "OR") bez závorek
 *
 * Gramatika v F2:
 *   expr     = or_expr
 *   or_expr  = and_expr ( ( "|" | "OR" ) and_expr )*
 *   and_expr = unary    ( ( "&" | "AND" | implicit-WS ) unary )*
 *
 * "OR" / "AND" jsou case-sensitive uppercase keywordy. Lowercase
 * "or" / "and" zůstává plain-text name match (= backward compat).
 * ================================================================ */


void test_or_single ( void )
{
    /* "port:CE | port:CF" - match 0xCE i 0xCF, ne 0xD0. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE | port:CF",
                                                   g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e_ce = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT e_cf = mk_event ( 0, 0x00CF, 0, 0, false );
    st_IO_HISTORY_EVENT e_d0 = mk_event ( 0, 0x00D0, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_ce, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_cf, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_d0, NULL ) );
}


void test_or_multiple ( void )
{
    /* "port:CE | port:CF | port:D0" - tři ORované varianty. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE | port:CF | port:D0",
                                                   g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e_ce = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT e_cf = mk_event ( 0, 0x00CF, 0, 0, false );
    st_IO_HISTORY_EVENT e_d0 = mk_event ( 0, 0x00D0, 0, 0, false );
    st_IO_HISTORY_EVENT e_d1 = mk_event ( 0, 0x00D1, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_ce, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_cf, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_d0, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_d1, NULL ) );
}


void test_or_symbol_and_keyword_equivalent ( void )
{
    /* "port:CE OR port:CF" musí být sémanticky shodné s "port:CE | port:CF". */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE OR port:CF",
                                                   g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e_ce = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT e_cf = mk_event ( 0, 0x00CF, 0, 0, false );
    st_IO_HISTORY_EVENT e_d0 = mk_event ( 0, 0x00D0, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_ce, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_cf, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_d0, NULL ) );
}


void test_or_keyword_lowercase_is_name_match ( void )
{
    /* "or" lowercase NENÍ keyword - je to plain-text name substring match.
     * Backward compat: lowercase keyword nikdy nezavádíme.
     *
     * "port:CE or port:CF" parsuje jako:
     *   port:CE  AND  or  AND  port:CF
     * Kde "or" je name leaf. Filter pak vyžaduje: port==CE && (name obsahuje
     * "or") && port==CF - to nikdy nevyjde (port nemůže být současně CE
     * i CF), pokud port_name není relevantní (a stejně to padne na první
     * AND větvi). Klíčové ověření: parse PROJDE (= žádný parse error)
     * a chování NENÍ OR (= e_cf při NULL port_name nemůže match). */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE or port:CF",
                                                   g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e_cf = mk_event ( 0, 0x00CF, 0, 0, false );
    /* Pro OR by mělo match=true; pro AND-chain s "or" name leaf NULL ⇒
     * name leaf vrací false ⇒ AND false ⇒ filtr neprojde. */
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_cf, NULL ) );
}


void test_or_precedence_with_and ( void )
{
    /* "port:CE pc:42 | port:CF pc:43"
     * = (port==0xCE && pc==0x42) OR (port==0xCF && pc==0x43). */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse (
        "port:CE pc:42 | port:CF pc:43", g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e_ce42 = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT e_ce99 = mk_event ( 0, 0x00CE, 0x0099, 0, false );
    st_IO_HISTORY_EVENT e_cf43 = mk_event ( 0, 0x00CF, 0x0043, 0, false );
    st_IO_HISTORY_EVENT e_cf99 = mk_event ( 0, 0x00CF, 0x0099, 0, false );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_ce42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_ce99, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_cf43, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_cf99, NULL ) );
}


void test_or_with_explicit_and_keyword ( void )
{
    /* "port:CE AND pc:42 | port:CF" = (port:CE && pc:42) | port:CF */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse (
        "port:CE AND pc:42 | port:CF", g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e_ce42 = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT e_ce99 = mk_event ( 0, 0x00CE, 0x0099, 0, false );
    st_IO_HISTORY_EVENT e_cf99 = mk_event ( 0, 0x00CF, 0x0099, 0, false );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_ce42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_ce99, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_cf99, NULL ) );
}


void test_or_with_explicit_and_symbol ( void )
{
    /* "port:CE & pc:42 | port:CF" = (port:CE && pc:42) | port:CF */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse (
        "port:CE & pc:42 | port:CF", g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e_ce42 = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT e_ce99 = mk_event ( 0, 0x00CE, 0x0099, 0, false );
    st_IO_HISTORY_EVENT e_cf99 = mk_event ( 0, 0x00CF, 0x0099, 0, false );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_ce42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_ce99, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_cf99, NULL ) );
}


void test_or_with_token_negate ( void )
{
    /* "port:CE | !pc:4042" - OR s per-token negate.
     * Match když port==CE NEBO pc!=0x4042. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE | !pc:4042",
                                                   g_f, err, sizeof ( err ) ) );

    /* port==CE → match (left OR větev). */
    st_IO_HISTORY_EVENT e_ce_42 = mk_event ( 0, 0x00CE, 0x4042, 0, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e_ce_42, NULL ) );

    /* port!=CE && pc!=4042 → match (right OR větev). */
    st_IO_HISTORY_EVENT e_d0_99 = mk_event ( 0, 0x00D0, 0x9999, 0, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e_d0_99, NULL ) );

    /* port!=CE && pc==4042 → ani jedna větev (port miss + negate miss). */
    st_IO_HISTORY_EVENT e_d0_42 = mk_event ( 0, 0x00D0, 0x4042, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_d0_42, NULL ) );
}


void test_or_empty_lhs_error ( void )
{
    /* "| port:CE" musí dát parse error. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "| port:CE",
                                                    g_f, err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_or_empty_rhs_error ( void )
{
    /* "port:CE |" musí dát parse error. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "port:CE |",
                                                    g_f, err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_or_double_pipe_error ( void )
{
    /* "port:CE || port:CF" = empty mezi dvěma OR ⇒ parse error. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "port:CE || port:CF",
                                                    g_f, err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_and_symbol_equivalent_to_whitespace ( void )
{
    /* "port:CE & pc:42" ≡ "port:CE pc:42" - explicit AND symbol jako WS. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE & pc:42",
                                                   g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT both = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT one  = mk_event ( 0, 0x00CE, 0x0099, 0, false );
    st_IO_HISTORY_EVENT none = mk_event ( 0, 0x00CD, 0x0099, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &both, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &one,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &none, NULL ) );
}


void test_or_three_pairs_precedence ( void )
{
    /* "port:CE pc:42 | port:CF pc:43 | port:D0 pc:44"
     * = (CE && 42) | (CF && 43) | (D0 && 44). */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse (
        "port:CE pc:42 | port:CF pc:43 | port:D0 pc:44",
        g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT a = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT b = mk_event ( 0, 0x00CF, 0x0043, 0, false );
    st_IO_HISTORY_EVENT c = mk_event ( 0, 0x00D0, 0x0044, 0, false );
    st_IO_HISTORY_EVENT x = mk_event ( 0, 0x00CE, 0x9999, 0, false );
    st_IO_HISTORY_EVENT y = mk_event ( 0, 0x00D1, 0x0044, 0, false );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &a, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &b, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &c, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &x, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &y, NULL ) );
}


void test_or_long_filter_no_overflow ( void )
{
    /* Mix AND/OR ~120 znaků - blízko 128 B filter buffer limitu.
     * Arena 4 KB musí pojmout všechny uzly bez OOM. */
    const char *src =
        "port:CE pc:4000 | port:CF pc:4001 | port:D0 pc:4002 | "
        "port:D1 pc:4003 | port:D2 pc:4004 | port:D3 pc:4005";
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( src, g_f, err, sizeof ( err ) ) );

    /* Quick sanity match - 4. dvojice. */
    st_IO_HISTORY_EVENT e = mk_event ( 0, 0x00D1, 0x4003, 0, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e, NULL ) );

    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0x00FF, 0x9999, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss, NULL ) );
}


void test_or_amp_amp_error ( void )
{
    /* "port:CE && port:CF" - druhý "&" je empty expression mezi & a &. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "port:CE && port:CF",
                                                    g_f, err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_and_trailing_error ( void )
{
    /* "port:CE AND" / "port:CE &" - trailing AND. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "port:CE AND",
                                                    g_f, err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );

    char err2[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "port:CE &",
                                                    g_f, err2, sizeof ( err2 ) ) );
    TEST_ASSERT_TRUE ( err2[ 0 ] != '\0' );
}


void test_or_lhs_leading_pipe_only_error ( void )
{
    /* Samotný "|" musí selhat. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "|", g_f, err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_or_uppercase_keyword_only ( void )
{
    /* "Or" (mixed case) NENÍ keyword - test ostré case sensitivity.
     * Parsuje jako name leaf "Or" + AND chain s ostatními. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "port:CE Or port:CF",
                                                   g_f, err, sizeof ( err ) ) );
    /* "Or" jako name match proti NULL port_name = false ⇒ AND false. */
    st_IO_HISTORY_EVENT e_cf = mk_event ( 0, 0x00CF, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_cf, NULL ) );
}


/* ================================================================
 * Fáze 3 - závorky "(" ")" + NOT nad podstromem
 * ================================================================ */

void test_paren_single ( void )
{
    /* "(port:CE)" ≡ "port:CE" - závorky jsou passthrough. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "(port:CE)", g_f,
                                                   err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0x00CD, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss, NULL ) );
}


void test_paren_redundant_nested ( void )
{
    /* "((port:CE))" ≡ "port:CE". */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "((port:CE))", g_f,
                                                   err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0x00CD, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss, NULL ) );
}


void test_paren_around_or ( void )
{
    /* "(port:CE | port:CF) pc:42" = (CE OR CF) AND pc==0x42. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse (
        "(port:CE | port:CF) pc:42", g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e_ce_42 = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT e_cf_42 = mk_event ( 0, 0x00CF, 0x0042, 0, false );
    st_IO_HISTORY_EVENT e_d0_42 = mk_event ( 0, 0x00D0, 0x0042, 0, false );
    st_IO_HISTORY_EVENT e_ce_99 = mk_event ( 0, 0x00CE, 0x0099, 0, false );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_ce_42, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_cf_42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_d0_42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_ce_99, NULL ) );
}


void test_paren_nested_or_and ( void )
{
    /* "((port:CE | port:CF) & pc:42) | port:D0"
     * = ((CE OR CF) AND pc==42) OR D0 */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse (
        "((port:CE | port:CF) & pc:42) | port:D0",
        g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT a = mk_event ( 0, 0x00CE, 0x0042, 0, false ); /* hit left */
    st_IO_HISTORY_EVENT b = mk_event ( 0, 0x00CF, 0x0042, 0, false ); /* hit left */
    st_IO_HISTORY_EVENT c = mk_event ( 0, 0x00D0, 0x9999, 0, false ); /* hit right */
    st_IO_HISTORY_EVENT d = mk_event ( 0, 0x00CE, 0x9999, 0, false ); /* miss */
    st_IO_HISTORY_EVENT e = mk_event ( 0, 0x00D1, 0x0042, 0, false ); /* miss */

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &a, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &b, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &c, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &d, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e, NULL ) );
}


void test_paren_groups_or_at_top ( void )
{
    /* "(port:CE pc:42) | (port:CF pc:43)"
     * = (CE AND 42) OR (CF AND 43) */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse (
        "(port:CE pc:42) | (port:CF pc:43)",
        g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT a = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT b = mk_event ( 0, 0x00CF, 0x0043, 0, false );
    st_IO_HISTORY_EVENT c = mk_event ( 0, 0x00CE, 0x0043, 0, false );
    st_IO_HISTORY_EVENT d = mk_event ( 0, 0x00CF, 0x0042, 0, false );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &a, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &b, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &c, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &d, NULL ) );
}


void test_not_paren_group ( void )
{
    /* "!(port:CE pc:42)" = NOT (CE AND 42).
     *   {CE, 42} = NOT(true) = false (= nematchne)
     *   {CE, 99} = NOT(false) = true
     *   {CF, 42} = NOT(false) = true */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!(port:CE pc:42)",
                                                   g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e_ce_42 = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT e_ce_99 = mk_event ( 0, 0x00CE, 0x0099, 0, false );
    st_IO_HISTORY_EVENT e_cf_42 = mk_event ( 0, 0x00CF, 0x0042, 0, false );

    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_ce_42, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_ce_99, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_cf_42, NULL ) );
}


void test_not_paren_or ( void )
{
    /* "!(port:CE | port:CF)" = NOT (CE OR CF) = nikoliv CE a nikoliv CF. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!(port:CE | port:CF)",
                                                   g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT e_ce = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT e_cf = mk_event ( 0, 0x00CF, 0, 0, false );
    st_IO_HISTORY_EVENT e_d0 = mk_event ( 0, 0x00D0, 0, 0, false );
    st_IO_HISTORY_EVENT e_42 = mk_event ( 0, 0x0042, 0, 0, false );

    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_ce, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_cf, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_d0, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_42, NULL ) );
}


void test_double_not_leaf ( void )
{
    /* "!!port:CE" ≡ "port:CE" (dvojitá negace přes LEAF collapse). */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!!port:CE",
                                                   g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT hit  = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT miss = mk_event ( 0, 0x00CD, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &hit,  NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &miss, NULL ) );
}


void test_double_not_paren ( void )
{
    /* "!!(port:CE | port:CF)" ≡ "(port:CE | port:CF)". */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!!(port:CE | port:CF)",
                                                   g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e_ce = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT e_cf = mk_event ( 0, 0x00CF, 0, 0, false );
    st_IO_HISTORY_EVENT e_d0 = mk_event ( 0, 0x00D0, 0, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_ce, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_cf, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_d0, NULL ) );
}


void test_not_collapse_into_leaf_legacy ( void )
{
    /* "!port:CE" parsuje jako single LEAF s negate=true (collapse cesta).
     * Funkční ekvivalent existujícího F1/F2 test_filter_match_negate_port_excludes:
     * matchne všechny porty kromě 0xCE. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "!port:CE",
                                                   g_f, err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT e_ce = mk_event ( 0, 0x00CE, 0, 0, false );
    st_IO_HISTORY_EVENT e_cd = mk_event ( 0, 0x00CD, 0, 0, false );
    st_IO_HISTORY_EVENT e_42 = mk_event ( 0, 0x0042, 0, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_ce, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_cd, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &e_42, NULL ) );
}


void test_paren_mismatched_open_error ( void )
{
    /* "(port:CE" - chybí ')'. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "(port:CE",
                                                    g_f, err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_paren_mismatched_close_error ( void )
{
    /* "port:CE )" - ')' bez '('. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "port:CE )",
                                                    g_f, err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_paren_empty_error ( void )
{
    /* "()" - prázdné závorky. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "()",
                                                    g_f, err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_paren_not_empty_error ( void )
{
    /* "!()" - NOT před prázdnými závorkami. */
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( "!()",
                                                    g_f, err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
}


void test_paren_max_depth_overflow ( void )
{
    /* Sestavit AND-chain s ~70 leaf tokeny - musí dát "Expression too
     * complex". Reálně potřebujeme >64 AST uzlů. Každý leaf token = 1 LEAF
     * uzel + při řetězení AND vznikají AND uzly: N tokenů → 2N-1 uzlů.
     * Stačí tedy ~33 tokenů aby přepadli 64.
     *
     * Vyhneme se však nutnosti přepad přes string filter buffer (= test
     * pracuje s plain char *), takže můžeme dát 40 tokenů "a b c ..." -
     * plain text name match. */
    char buf[ 512 ];
    buf[ 0 ] = '\0';
    /* 40 tokenů "x ": 40 LEAF + 39 AND = 79 uzlů → > IOFILT_MAX_NODES=64. */
    for ( int i = 0; i < 40; i++ ) {
        strncat ( buf, "x ", sizeof ( buf ) - strlen ( buf ) - 1 );
    }
    char err[ 80 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_parse ( buf, g_f,
                                                    err, sizeof ( err ) ) );
    TEST_ASSERT_TRUE ( err[ 0 ] != '\0' );
    /* Error musí obsahovat "complex" - sanity check. */
    TEST_ASSERT_TRUE ( strstr ( err, "complex" ) != NULL );
}


void test_complex_real_world_1 ( void )
{
    /* "(port:CE pc:4000-40FF) | (port16:CF06 frame:>100)" - z plánu. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse (
        "(port:CE pc:4000-40FF) | (port16:CF06 frame:>100)",
        g_f, err, sizeof ( err ) ) );

    /* Left branch hit: port=CE, pc=4080. */
    st_IO_HISTORY_EVENT a = mk_event ( 50, 0x00CE, 0x4080, 0, false );
    /* Right branch hit: port16=CF06, frame=200. */
    st_IO_HISTORY_EVENT b = mk_event ( 200, 0xCF06, 0x9999, 0, false );
    /* Žádná větev: port=CE ale pc out of range, frame OK ale port jiný. */
    st_IO_HISTORY_EVENT c = mk_event ( 200, 0x00CE, 0x5000, 0, false );
    st_IO_HISTORY_EVENT d = mk_event ( 50, 0xCF06, 0x9999, 0, false );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &a, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &b, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &c, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &d, NULL ) );
}


void test_complex_real_world_2 ( void )
{
    /* "pc:4000-40FF & (port:CE | port:CF)"
     * = pc range AND (CE OR CF). */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse (
        "pc:4000-40FF & (port:CE | port:CF)",
        g_f, err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT a = mk_event ( 0, 0x00CE, 0x4080, 0, false ); /* hit */
    st_IO_HISTORY_EVENT b = mk_event ( 0, 0x00CF, 0x4080, 0, false ); /* hit */
    st_IO_HISTORY_EVENT c = mk_event ( 0, 0x00D0, 0x4080, 0, false ); /* port miss */
    st_IO_HISTORY_EVENT d = mk_event ( 0, 0x00CE, 0x5000, 0, false ); /* pc miss */

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &a, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &b, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &c, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &d, NULL ) );
}


void test_or_token_in_paren_does_not_apply_outside ( void )
{
    /* "(port:CE | port:CF) pc:42" - OR jen uvnitř, vně implicit AND.
     * Druh testu: pc:42 NEsmí být ORované s port:CF. */
    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( "(port:CE | port:CF) pc:42",
                                                   g_f, err, sizeof ( err ) ) );

    /* port:CF AND pc:99 NESMÍ projít (pc nesplňuje). */
    st_IO_HISTORY_EVENT e_cf_99 = mk_event ( 0, 0x00CF, 0x0099, 0, false );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &e_cf_99, NULL ) );
    /* port:CF AND pc:42 musí projít. */
    st_IO_HISTORY_EVENT e_cf_42 = mk_event ( 0, 0x00CF, 0x0042, 0, false );
    TEST_ASSERT_TRUE ( io_history_filter_match ( g_f, &e_cf_42, NULL ) );
}


/* ================================================================
 * Main
 * ================================================================ */

int main ( int argc, char *argv[] )
{
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    /* Parse */
    RUN_TEST ( test_filter_parse_empty );
    RUN_TEST ( test_filter_parse_null );
    RUN_TEST ( test_filter_parse_port_basic );
    RUN_TEST ( test_filter_parse_port16_full );
    RUN_TEST ( test_filter_parse_port_lowercase );
    RUN_TEST ( test_filter_parse_pc_basic );
    RUN_TEST ( test_filter_parse_pc_range );
    RUN_TEST ( test_filter_parse_port_range );
    RUN_TEST ( test_filter_parse_frame_gt );
    RUN_TEST ( test_filter_parse_frame_lt );
    RUN_TEST ( test_filter_parse_frame_range );
    RUN_TEST ( test_filter_parse_frame_equality );
    RUN_TEST ( test_filter_parse_in_keyword );
    RUN_TEST ( test_filter_parse_out_keyword );
    RUN_TEST ( test_filter_parse_plain_text );
    RUN_TEST ( test_filter_parse_multiple_tokens );
    RUN_TEST ( test_filter_parse_complex );
    RUN_TEST ( test_filter_parse_invalid_hex );
    RUN_TEST ( test_filter_parse_missing_value );
    RUN_TEST ( test_filter_parse_invalid_range );
    RUN_TEST ( test_filter_parse_unknown_prefix );

    /* Match */
    RUN_TEST ( test_filter_match_empty_filter );
    RUN_TEST ( test_filter_match_port_hit );
    RUN_TEST ( test_filter_match_port_miss );
    RUN_TEST ( test_filter_match_pc_range_hit );
    RUN_TEST ( test_filter_match_pc_range_miss );
    RUN_TEST ( test_filter_match_frame_gt );
    RUN_TEST ( test_filter_match_in_only );
    RUN_TEST ( test_filter_match_combined_and );
    RUN_TEST ( test_filter_match_name_substr_ci );

    /* V1.5.D fix #3: cycle: prefix */
    RUN_TEST ( test_filter_parse_cycle_eq );
    RUN_TEST ( test_filter_parse_cycle_gt );
    RUN_TEST ( test_filter_parse_cycle_lt );
    RUN_TEST ( test_filter_parse_cycle_range );
    RUN_TEST ( test_filter_parse_negate_cycle );
    RUN_TEST ( test_filter_match_cycle_eq );
    RUN_TEST ( test_filter_match_cycle_range );
    RUN_TEST ( test_filter_match_negate_cycle );

    /* V1.5 fix #4: negace + value: prefix */
    RUN_TEST ( test_filter_parse_negate_port );
    RUN_TEST ( test_filter_parse_negate_pc );
    RUN_TEST ( test_filter_parse_negate_frame );
    RUN_TEST ( test_filter_parse_negate_in );
    RUN_TEST ( test_filter_parse_negate_text );
    RUN_TEST ( test_filter_parse_value_basic );
    RUN_TEST ( test_filter_parse_value_range );
    RUN_TEST ( test_filter_parse_negate_value );
    RUN_TEST ( test_filter_parse_negate_alone_invalid );
    RUN_TEST ( test_filter_match_negate_port_excludes );
    RUN_TEST ( test_filter_match_negate_pc_excludes );
    RUN_TEST ( test_filter_match_negate_value_excludes );
    RUN_TEST ( test_filter_match_combined_negate );
    RUN_TEST ( test_filter_match_negate_in_means_out );
    RUN_TEST ( test_filter_match_value_basic );

    /* V1.5.D fix: port: 8-bit low byte vs port16: full 16-bit */
    RUN_TEST ( test_filter_parse_port_8bit_low_byte_match );
    RUN_TEST ( test_filter_parse_port16_full_match );
    RUN_TEST ( test_filter_parse_port16_range );
    RUN_TEST ( test_filter_parse_port_8bit_too_large );
    RUN_TEST ( test_filter_parse_port16_too_large );
    RUN_TEST ( test_filter_match_port_8bit_low_byte_match );
    RUN_TEST ( test_filter_match_port_8bit_low_byte_miss );
    RUN_TEST ( test_filter_match_port16_full_match );
    RUN_TEST ( test_filter_match_port16_full_miss );
    RUN_TEST ( test_filter_match_port16_range );
    RUN_TEST ( test_filter_match_negate_port16 );

    /* V1.5.E: addr: prefix + mr/mw + sjednocena in/out */
    RUN_TEST ( test_filter_parse_addr_basic );
    RUN_TEST ( test_filter_parse_addr_range );
    RUN_TEST ( test_filter_parse_addr_negate );
    RUN_TEST ( test_filter_match_addr_hit );
    RUN_TEST ( test_filter_match_addr_iorq_rejected );
    RUN_TEST ( test_filter_match_addr_range );
    RUN_TEST ( test_filter_parse_mr_keyword );
    RUN_TEST ( test_filter_parse_mw_keyword );
    RUN_TEST ( test_filter_match_mr_only );
    RUN_TEST ( test_filter_match_in_unified_iorq_and_mmio );

    /* Fáze 2 - OR operátor (| / OR) bez závorek */
    RUN_TEST ( test_or_single );
    RUN_TEST ( test_or_multiple );
    RUN_TEST ( test_or_symbol_and_keyword_equivalent );
    RUN_TEST ( test_or_keyword_lowercase_is_name_match );
    RUN_TEST ( test_or_precedence_with_and );
    RUN_TEST ( test_or_with_explicit_and_keyword );
    RUN_TEST ( test_or_with_explicit_and_symbol );
    RUN_TEST ( test_or_with_token_negate );
    RUN_TEST ( test_or_empty_lhs_error );
    RUN_TEST ( test_or_empty_rhs_error );
    RUN_TEST ( test_or_double_pipe_error );
    RUN_TEST ( test_and_symbol_equivalent_to_whitespace );
    RUN_TEST ( test_or_three_pairs_precedence );
    RUN_TEST ( test_or_long_filter_no_overflow );
    RUN_TEST ( test_or_amp_amp_error );
    RUN_TEST ( test_and_trailing_error );
    RUN_TEST ( test_or_lhs_leading_pipe_only_error );
    RUN_TEST ( test_or_uppercase_keyword_only );

    /* Fáze 3 - závorky "(" ")" + NOT nad podstromem */
    RUN_TEST ( test_paren_single );
    RUN_TEST ( test_paren_redundant_nested );
    RUN_TEST ( test_paren_around_or );
    RUN_TEST ( test_paren_nested_or_and );
    RUN_TEST ( test_paren_groups_or_at_top );
    RUN_TEST ( test_not_paren_group );
    RUN_TEST ( test_not_paren_or );
    RUN_TEST ( test_double_not_leaf );
    RUN_TEST ( test_double_not_paren );
    RUN_TEST ( test_not_collapse_into_leaf_legacy );
    RUN_TEST ( test_paren_mismatched_open_error );
    RUN_TEST ( test_paren_mismatched_close_error );
    RUN_TEST ( test_paren_empty_error );
    RUN_TEST ( test_paren_not_empty_error );
    RUN_TEST ( test_paren_max_depth_overflow );
    RUN_TEST ( test_complex_real_world_1 );
    RUN_TEST ( test_complex_real_world_2 );
    RUN_TEST ( test_or_token_in_paren_does_not_apply_outside );

    return UNITY_END ( );
}
