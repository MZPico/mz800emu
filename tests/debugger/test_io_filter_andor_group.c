/*
 * test_io_filter_andor_group.c - unit testy pro
 * io_history_filter_string_group_wrap() (V1.7+ položka 5.1).
 *
 * Pokrývá:
 *  - empty buffer + AND/OR -> bare token
 *  - jednolist + AND/OR group -> "(leaf) <op> token"
 *  - OR řetězec + AND group -> sémantika v parseru ověřena round-trip
 *  - existující závorky -> dvojitý wrap (parser je tolerantně sní)
 *  - buffer overflow -> buf zůstává intaktní, vrací false
 *  - invalid args (NULL buf, NULL token, empty token, neznámý op)
 *  - round-trip parsing s match sémantikou pro všechny non-trivial případy
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <string.h>
#include <stdint.h>

#include "debugger/io_history.h"
#include "debugger/io_history_filter.h"


/* Sdílený filter pro round-trip testy (= zda string projde parserem). */
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
 * Helper: event constructor (sdílený s test_io_history_filter.c).
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


/* ================================================================
 * String transformace - empty buffer
 * ================================================================ */

void test_group_wrap_empty_buf_and ( void )
{
    /* Empty filter + AND group -> bare token (= jako Add AND). */
    char buf[ 128 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "pc:42" ) );
    TEST_ASSERT_EQUAL_STRING ( "pc:42", buf );
}


void test_group_wrap_empty_buf_or ( void )
{
    char buf[ 128 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_OR, "port:CE" ) );
    TEST_ASSERT_EQUAL_STRING ( "port:CE", buf );
}


/* ================================================================
 * String transformace - non-empty buffer
 * ================================================================ */

void test_group_wrap_single_leaf_and ( void )
{
    /* "port:CE" + AND group "pc:42" -> "(port:CE) & pc:42". */
    char buf[ 128 ] = "port:CE";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "pc:42" ) );
    TEST_ASSERT_EQUAL_STRING ( "(port:CE) & pc:42", buf );
}


void test_group_wrap_single_leaf_or ( void )
{
    /* "port:CE" + OR group "pc:42" -> "(port:CE) | pc:42". */
    char buf[ 128 ] = "port:CE";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_OR, "pc:42" ) );
    TEST_ASSERT_EQUAL_STRING ( "(port:CE) | pc:42", buf );
}


void test_group_wrap_or_chain_and ( void )
{
    /* OR řetězec + AND group - klíčový use case z CHECKLIST 5.1. */
    char buf[ 128 ] = "port:CE | port:CF";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "pc:42" ) );
    TEST_ASSERT_EQUAL_STRING ( "(port:CE | port:CF) & pc:42", buf );
}


void test_group_wrap_existing_parens ( void )
{
    /* "(a | b)" + AND group "c" -> "((a | b)) & c" - dvojité závorky
     * jsou syntakticky validní (parser je sní jako passthrough). */
    char buf[ 128 ] = "(port:CE | port:CF)";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "pc:42" ) );
    TEST_ASSERT_EQUAL_STRING ( "((port:CE | port:CF)) & pc:42", buf );
}


void test_group_wrap_complex_chain ( void )
{
    /* Komplexnější vstup s AND i OR + NOT + závorky uvnitř. */
    char buf[ 128 ] = "port:CE pc:4000-40FF | port:CF";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_OR, "frame:>100" ) );
    TEST_ASSERT_EQUAL_STRING (
        "(port:CE pc:4000-40FF | port:CF) | frame:>100", buf );
}


/* ================================================================
 * Round-trip: výsledný string projde parserem + sémantika je správně
 * ================================================================ */

void test_group_wrap_and_roundtrip_semantics ( void )
{
    /* "port:CE | port:CF" + AND group "pc:42"
     *  -> "(port:CE | port:CF) & pc:42"
     * Sémantika: (CE OR CF) AND pc==42.
     *  - CE + pc:42 -> hit
     *  - CF + pc:42 -> hit
     *  - D0 + pc:42 -> miss (port mimo)
     *  - CE + pc:99 -> miss (pc mimo) */
    char buf[ 128 ] = "port:CE | port:CF";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "pc:42" ) );

    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( buf, g_f,
                                                  err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT ce_42 = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT cf_42 = mk_event ( 0, 0x00CF, 0x0042, 0, false );
    st_IO_HISTORY_EVENT d0_42 = mk_event ( 0, 0x00D0, 0x0042, 0, false );
    st_IO_HISTORY_EVENT ce_99 = mk_event ( 0, 0x00CE, 0x0099, 0, false );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &ce_42, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &cf_42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &d0_42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &ce_99, NULL ) );
}


void test_group_wrap_demonstrates_precedence_problem ( void )
{
    /* Tento test ukazuje ROZDÍL proti dnešnímu "Add AND:" helperu.
     *
     * Bez group wrap (= Add AND: pc:42 na string "port:CE | port:CF"):
     *   "port:CE | port:CF pc:42"
     *   Operator precedence "&" > "|" => parser zgrupuje jako
     *   "port:CE | (port:CF AND pc:42)" - tj. CE projde i bez pc:42.
     * S group wrap:
     *   "(port:CE | port:CF) & pc:42"
     *   CE bez pc:42 NEPROJDE.
     *
     * Test ověřuje, že group-wrap variantou CE bez pc:42 nematchne -
     * což je očekávané (= nový helper opravuje uživatelský intent). */
    char buf[ 128 ] = "port:CE | port:CF";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "pc:42" ) );

    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( buf, g_f,
                                                  err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT ce_99 = mk_event ( 0, 0x00CE, 0x0099, 0, false );
    /* S group wrap: CE bez pc:42 -> NEMATCH (= správný uživatelský intent). */
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &ce_99, NULL ) );
}


void test_group_wrap_or_roundtrip_semantics ( void )
{
    /* "port:CE pc:42" + OR group "port:D0"
     *  -> "(port:CE pc:42) | port:D0"
     * Sémantika: (CE AND pc:42) OR D0. */
    char buf[ 128 ] = "port:CE pc:42";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_OR, "port:D0" ) );

    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( buf, g_f,
                                                  err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT ce_42 = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT ce_99 = mk_event ( 0, 0x00CE, 0x0099, 0, false );
    st_IO_HISTORY_EVENT d0_99 = mk_event ( 0, 0x00D0, 0x0099, 0, false );
    st_IO_HISTORY_EVENT cf_42 = mk_event ( 0, 0x00CF, 0x0042, 0, false );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &ce_42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &ce_99, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &d0_99, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &cf_42, NULL ) );
}


void test_group_wrap_double_paren_roundtrip ( void )
{
    /* "(a | b)" + AND group "c" -> "((a | b)) & c", parser pro
     * dvojité závorky NESMÍ házet error. Sémantika: (a OR b) AND c. */
    char buf[ 128 ] = "(port:CE | port:CF)";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "pc:42" ) );

    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( buf, g_f,
                                                  err, sizeof ( err ) ) );

    st_IO_HISTORY_EVENT ce_42 = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT cf_42 = mk_event ( 0, 0x00CF, 0x0042, 0, false );
    st_IO_HISTORY_EVENT ce_99 = mk_event ( 0, 0x00CE, 0x0099, 0, false );

    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &ce_42, NULL ) );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &cf_42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &ce_99, NULL ) );
}


/* ================================================================
 * Buffer overflow - buf zůstává intaktní, vrací false
 * ================================================================ */

void test_group_wrap_overflow_keeps_buffer ( void )
{
    /* Tight buffer: existing "port:CE" (7 chars) + "() & X" rámec
     * by potřeboval 1+7+1 + 3 + 1 + 1 = 14 znaků. Buffer 12 je
     * příliš malý. Funkce musí vrátit false a buffer NEMĚNIT. */
    char buf[ 12 ] = "port:CE";
    TEST_ASSERT_FALSE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "X" ) );
    /* Buffer beze změny. */
    TEST_ASSERT_EQUAL_STRING ( "port:CE", buf );
}


void test_group_wrap_overflow_empty_buf_too ( void )
{
    /* Empty buffer + token příliš dlouhý. */
    char buf[ 4 ] = "";
    TEST_ASSERT_FALSE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "pc:1234" ) );
    /* Empty buffer zůstává empty. */
    TEST_ASSERT_EQUAL_STRING ( "", buf );
}


void test_group_wrap_exact_fit ( void )
{
    /* Hraniční velikost: výsledek "(a) & b" = 7 znaků + '\0' = 8 bytů.
     * Buf 8 musí projít, buf 7 ne. */
    char buf8[ 8 ] = "a";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf8, sizeof ( buf8 ), IOFILT_GROUP_AND, "b" ) );
    TEST_ASSERT_EQUAL_STRING ( "(a) & b", buf8 );

    char buf7[ 7 ] = "a";
    TEST_ASSERT_FALSE ( io_history_filter_string_group_wrap (
        buf7, sizeof ( buf7 ), IOFILT_GROUP_AND, "b" ) );
    TEST_ASSERT_EQUAL_STRING ( "a", buf7 );
}


/* ================================================================
 * Invalid args
 * ================================================================ */

void test_group_wrap_null_buf ( void )
{
    TEST_ASSERT_FALSE ( io_history_filter_string_group_wrap (
        NULL, 128, IOFILT_GROUP_AND, "X" ) );
}


void test_group_wrap_zero_bufsz ( void )
{
    char dummy[ 4 ] = "x";
    TEST_ASSERT_FALSE ( io_history_filter_string_group_wrap (
        dummy, 0, IOFILT_GROUP_AND, "X" ) );
}


void test_group_wrap_null_token ( void )
{
    char buf[ 64 ] = "port:CE";
    TEST_ASSERT_FALSE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, NULL ) );
    /* Buffer beze změny. */
    TEST_ASSERT_EQUAL_STRING ( "port:CE", buf );
}


void test_group_wrap_empty_token ( void )
{
    char buf[ 64 ] = "port:CE";
    TEST_ASSERT_FALSE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "" ) );
    TEST_ASSERT_EQUAL_STRING ( "port:CE", buf );
}


void test_group_wrap_invalid_op ( void )
{
    /* Hodnota mimo enum - sanity, ne sémantická. */
    char buf[ 64 ] = "port:CE";
    TEST_ASSERT_FALSE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), (en_IOFILT_GROUP_OP) 99, "X" ) );
    TEST_ASSERT_EQUAL_STRING ( "port:CE", buf );
}


/* ================================================================
 * Idempotence pre-existujícího jednolist obalu (= "(X)" je legal vstup)
 * ================================================================ */

void test_group_wrap_paren_around_single_leaf ( void )
{
    /* "(port:CE)" + AND group "pc:42" -> "((port:CE)) & pc:42".
     * Parser passthrough závorky -> ekvivalent "port:CE & pc:42". */
    char buf[ 128 ] = "(port:CE)";
    TEST_ASSERT_TRUE ( io_history_filter_string_group_wrap (
        buf, sizeof ( buf ), IOFILT_GROUP_AND, "pc:42" ) );
    TEST_ASSERT_EQUAL_STRING ( "((port:CE)) & pc:42", buf );

    char err[ 80 ] = "";
    TEST_ASSERT_TRUE ( io_history_filter_parse ( buf, g_f,
                                                  err, sizeof ( err ) ) );
    st_IO_HISTORY_EVENT ce_42 = mk_event ( 0, 0x00CE, 0x0042, 0, false );
    st_IO_HISTORY_EVENT ce_99 = mk_event ( 0, 0x00CE, 0x0099, 0, false );
    TEST_ASSERT_TRUE  ( io_history_filter_match ( g_f, &ce_42, NULL ) );
    TEST_ASSERT_FALSE ( io_history_filter_match ( g_f, &ce_99, NULL ) );
}


/* ================================================================
 * Main
 * ================================================================ */

int main ( int argc, char *argv[] )
{
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    /* Empty buffer */
    RUN_TEST ( test_group_wrap_empty_buf_and );
    RUN_TEST ( test_group_wrap_empty_buf_or );

    /* Non-empty - basic */
    RUN_TEST ( test_group_wrap_single_leaf_and );
    RUN_TEST ( test_group_wrap_single_leaf_or );
    RUN_TEST ( test_group_wrap_or_chain_and );
    RUN_TEST ( test_group_wrap_existing_parens );
    RUN_TEST ( test_group_wrap_complex_chain );

    /* Round-trip parser + semantics */
    RUN_TEST ( test_group_wrap_and_roundtrip_semantics );
    RUN_TEST ( test_group_wrap_demonstrates_precedence_problem );
    RUN_TEST ( test_group_wrap_or_roundtrip_semantics );
    RUN_TEST ( test_group_wrap_double_paren_roundtrip );
    RUN_TEST ( test_group_wrap_paren_around_single_leaf );

    /* Overflow */
    RUN_TEST ( test_group_wrap_overflow_keeps_buffer );
    RUN_TEST ( test_group_wrap_overflow_empty_buf_too );
    RUN_TEST ( test_group_wrap_exact_fit );

    /* Invalid args */
    RUN_TEST ( test_group_wrap_null_buf );
    RUN_TEST ( test_group_wrap_zero_bufsz );
    RUN_TEST ( test_group_wrap_null_token );
    RUN_TEST ( test_group_wrap_empty_token );
    RUN_TEST ( test_group_wrap_invalid_op );

    return UNITY_END ( );
}
