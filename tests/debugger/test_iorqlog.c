/*
 * test_iorqlog.c - unit testy pro trace-suite IORQ Log
 *
 * Testuje:
 *  - lifecycle (start/stop/idempotent)
 *  - per-event 24 B layout (pxclk_total / screens / pxclk_in / type / dir / addr / value)
 *  - IN events mají správnou source_addr (regBC)
 *  - OUT events mají hodnotu portu
 *  - IORQ_UNCONNECTED enum hodnota rezervovaná
 *  - meta.json subsys_header (event_record_size = 24)
 *
 * Test volá iorqlog_record() přímo s ručně sestavenými argumenty,
 * ne přes IORQ dispatch CPU smyčky.
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "debugger/trace/iorqlog.h"
#include "debugger/trace/tlog_common.h"
#include "json_test_helpers.h"


/* ================================================================
 * setUp / tearDown
 * ================================================================ */

static char *s_test_dir = NULL;


static int rmtree ( const char *path )
{
    if ( !path || !g_file_test ( path, G_FILE_TEST_EXISTS ) ) return 0;
    if ( g_file_test ( path, G_FILE_TEST_IS_DIR ) && !g_file_test ( path, G_FILE_TEST_IS_SYMLINK ) ) {
        GError *err = NULL;
        GDir *d = g_dir_open ( path, 0, &err );
        if ( !d ) {
            if ( err ) g_error_free ( err );
            return -1;
        }
        const gchar *name;
        while ( ( name = g_dir_read_name ( d ) ) != NULL ) {
            char *child = g_build_filename ( path, name, NULL );
            rmtree ( child );
            g_free ( child );
        }
        g_dir_close ( d );
        return g_rmdir ( path );
    }
    return g_remove ( path );
}


void setUp ( void )
{
    GError *err = NULL;
    s_test_dir = g_dir_make_tmp ( "mztest-iorqlog-XXXXXX", &err );
    if ( !s_test_dir ) {
        if ( err ) g_error_free ( err );
        TEST_FAIL_MESSAGE ( "Nepodařilo se vytvořit tmpdir" );
        return;
    }

    g_free ( g_iorqlog_config.dir );
    g_free ( g_iorqlog_config.name );
    g_iorqlog_config.dir = g_strdup ( s_test_dir );
    g_iorqlog_config.name = g_strdup ( "iorq" );
    g_iorqlog_config.chunk_mb = 1;
    g_iorqlog_config.max_total_mb = 0;
    g_iorqlog_config.save_on_exit = 1;
    g_iorqlog_config.mode = TLOG_MODE_OFF;
    g_iorqlog_active = 0;
}


void tearDown ( void )
{
    if ( g_iorqlog_active ) {
        iorqlog_stop ( );
        g_iorqlog_active = 0;
    }
    if ( s_test_dir ) {
        rmtree ( s_test_dir );
        g_free ( s_test_dir );
        s_test_dir = NULL;
    }
}


/* ================================================================
 * Helpery
 * ================================================================ */

static uint8_t *read_whole_file ( const char *dir, const char *filename, size_t *out_size )
{
    char *path = g_build_filename ( dir, filename, NULL );
    FILE *fp = g_fopen ( path, "rb" );
    g_free ( path );
    if ( !fp ) {
        if ( out_size ) *out_size = 0;
        return NULL;
    }
    fseek ( fp, 0, SEEK_END );
    long size = ftell ( fp );
    fseek ( fp, 0, SEEK_SET );
    uint8_t *buf = (uint8_t *) g_malloc ( (size_t) size + 1 );
    size_t n = fread ( buf, 1, (size_t) size, fp );
    buf[ n ] = 0;
    fclose ( fp );
    if ( out_size ) *out_size = n;
    return buf;
}


typedef struct {
    uint64_t pxclk_total;
    uint32_t screens;
    uint32_t pxclk_in_screen;
    uint8_t  event_type;
    uint8_t  direction;
    uint16_t source_addr;
    uint16_t port_or_addr;
    uint8_t  value;
    uint8_t  pulse_duration;
} parsed_iorq_t;


static void parse_iorq ( const uint8_t *buf, parsed_iorq_t *e )
{
    e->pxclk_total = 0;
    for ( int i = 0; i < 8; i++ ) e->pxclk_total |= (uint64_t) buf[ i ] << ( i * 8 );
    e->screens = 0;
    for ( int i = 0; i < 4; i++ ) e->screens |= (uint32_t) buf[ 8 + i ] << ( i * 8 );
    e->pxclk_in_screen = 0;
    for ( int i = 0; i < 4; i++ ) e->pxclk_in_screen |= (uint32_t) buf[ 12 + i ] << ( i * 8 );
    e->event_type = buf[ 16 ];
    e->direction = buf[ 17 ];
    e->source_addr = (uint16_t) buf[ 18 ] | ( (uint16_t) buf[ 19 ] << 8 );
    e->port_or_addr = (uint16_t) buf[ 20 ] | ( (uint16_t) buf[ 21 ] << 8 );
    e->value = buf[ 22 ];
    e->pulse_duration = buf[ 23 ];
}


static int activate_iorqlog ( void )
{
    int rc = iorqlog_start ( );
    if ( rc == 0 ) g_iorqlog_active = 1;
    return rc;
}


/* ================================================================
 * Lifecycle
 * ================================================================ */

void test_iorqlog_start_stop ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0, activate_iorqlog ( ) );
    iorqlog_stop ( );
    g_iorqlog_active = 0;

    char *meta = g_build_filename ( s_test_dir, "iorq.json", NULL );
    TEST_ASSERT_TRUE ( g_file_test ( meta, G_FILE_TEST_IS_REGULAR ) );
    g_free ( meta );
}


void test_iorqlog_start_idempotent ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0, activate_iorqlog ( ) );
    TEST_ASSERT_EQUAL_INT ( 0, iorqlog_start ( ) );
    iorqlog_stop ( );
    g_iorqlog_active = 0;
}


/* ================================================================
 * Per-event 24 B layout
 * ================================================================ */

void test_iorqlog_event_layout_in ( void )
{
    activate_iorqlog ( );

    /* IN z portu 0xCE (DMD), source = regBC = 0x12CE, value = 0xAA */
    iorqlog_record ( IORQLOG_EVENT_IORQ, IORQLOG_DIR_IN,
                     0x12CE, 0x00CE, 0xAA, 4 );

    iorqlog_stop ( );
    g_iorqlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "iorq.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t_MESSAGE ( 24, fsize, "Jeden event = 24 B" );

    parsed_iorq_t e;
    parse_iorq ( buf, &e );

    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( IORQLOG_EVENT_IORQ, e.event_type,
                                     "event_type = IORQ" );
    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( IORQLOG_DIR_IN, e.direction,
                                     "direction = IN" );
    TEST_ASSERT_EQUAL_HEX16_MESSAGE ( 0x12CE, e.source_addr,
                                      "source_addr (regBC) = 0x12CE" );
    TEST_ASSERT_EQUAL_HEX16_MESSAGE ( 0x00CE, e.port_or_addr,
                                      "port_or_addr = 0xCE" );
    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( 0xAA, e.value, "value = 0xAA" );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE ( 4, e.pulse_duration,
                                      "pulse_duration = 4" );

    g_free ( buf );
}


void test_iorqlog_event_layout_out ( void )
{
    activate_iorqlog ( );

    /* OUT na port 0xF0 (PALGRP), value = 0x40 */
    iorqlog_record ( IORQLOG_EVENT_IORQ, IORQLOG_DIR_OUT,
                     0x40F0, 0x00F0, 0x40, 0 );

    iorqlog_stop ( );
    g_iorqlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "iorq.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_iorq_t e;
    parse_iorq ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( IORQLOG_DIR_OUT, e.direction );
    TEST_ASSERT_EQUAL_HEX16 ( 0x00F0, e.port_or_addr );
    TEST_ASSERT_EQUAL_HEX8 ( 0x40, e.value );

    g_free ( buf );
}


void test_iorqlog_multiple_events_sequence ( void )
{
    activate_iorqlog ( );

    /* 5 různých eventů s různými hodnotami pro ověření sekvence. */
    iorqlog_record ( IORQLOG_EVENT_IORQ, IORQLOG_DIR_OUT, 0x0001, 0x00CE, 0x01, 0 );
    iorqlog_record ( IORQLOG_EVENT_IORQ, IORQLOG_DIR_OUT, 0x0002, 0x00CF, 0x02, 0 );
    iorqlog_record ( IORQLOG_EVENT_IORQ, IORQLOG_DIR_IN,  0x0003, 0x00D0, 0x03, 1 );
    iorqlog_record ( IORQLOG_EVENT_IORQ, IORQLOG_DIR_OUT, 0x0004, 0x00F0, 0x04, 0 );
    iorqlog_record ( IORQLOG_EVENT_IORQ, IORQLOG_DIR_IN,  0x0005, 0x00FE, 0x05, 0 );

    iorqlog_stop ( );
    g_iorqlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "iorq.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t_MESSAGE ( 5 * 24, fsize, "5 eventů = 120 B" );

    /* Ověř pořadí - hodnoty 0x01..0x05. */
    for ( int i = 0; i < 5; i++ ) {
        parsed_iorq_t e;
        parse_iorq ( buf + i * 24, &e );
        TEST_ASSERT_EQUAL_HEX8 ( i + 1, e.value );
        TEST_ASSERT_EQUAL_HEX16 ( i + 1, e.source_addr );
    }

    g_free ( buf );
}


/* ================================================================
 * IORQ_UNCONNECTED + MREQ_MAPPED enum hodnoty rezervovány
 * ================================================================ */

void test_iorqlog_event_type_enum_values ( void )
{
    /* Compile-time test - enum hodnoty musí mít konkrétní pořadí
     * pro stabilitu binárního formátu. */
    TEST_ASSERT_EQUAL_INT ( 0, IORQLOG_EVENT_IORQ );
    TEST_ASSERT_EQUAL_INT ( 1, IORQLOG_EVENT_MREQ_MAPPED );
    TEST_ASSERT_EQUAL_INT ( 2, IORQLOG_EVENT_IORQ_UNCONNECTED );

    TEST_ASSERT_EQUAL_INT ( 0, IORQLOG_DIR_IN );
    TEST_ASSERT_EQUAL_INT ( 1, IORQLOG_DIR_OUT );
}


void test_iorqlog_unconnected_event_emit ( void )
{
    /* Test že IORQ_UNCONNECTED se uloží správně, i když ho zatím
     * nikdo emit nehookuje (=ručně). */
    activate_iorqlog ( );
    iorqlog_record ( IORQLOG_EVENT_IORQ_UNCONNECTED, IORQLOG_DIR_IN,
                     0x1234, 0x00BF, 0xFF, 0 );
    iorqlog_stop ( );
    g_iorqlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "iorq.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_iorq_t e;
    parse_iorq ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( IORQLOG_EVENT_IORQ_UNCONNECTED, e.event_type );

    g_free ( buf );
}


void test_iorqlog_mreq_mapped_event_emit ( void )
{
    /* MREQ_MAPPED event = CPU instrukce přistupuje na 0xE000-0xE008
     * v MZ-700 mode (CTC/PIO/GDG namapovaný do RAM region).
     * source_addr = PC instrukce, port_or_addr = E00x cílová adresa. */
    activate_iorqlog ( );
    iorqlog_record ( IORQLOG_EVENT_MREQ_MAPPED, IORQLOG_DIR_OUT,
                     0x1234, 0xE004, 0x80, 0 );
    iorqlog_record ( IORQLOG_EVENT_MREQ_MAPPED, IORQLOG_DIR_IN,
                     0x1235, 0xE008, 0x40, 0 );
    iorqlog_stop ( );
    g_iorqlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "iorq.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t_MESSAGE ( 2 * 24, fsize, "2 MREQ_MAPPED events" );

    parsed_iorq_t e0, e1;
    parse_iorq ( buf, &e0 );
    parse_iorq ( buf + 24, &e1 );

    TEST_ASSERT_EQUAL_HEX8 ( IORQLOG_EVENT_MREQ_MAPPED, e0.event_type );
    TEST_ASSERT_EQUAL_HEX8 ( IORQLOG_DIR_OUT, e0.direction );
    TEST_ASSERT_EQUAL_HEX16_MESSAGE ( 0xE004, e0.port_or_addr,
                                      "MREQ adresa = E004 (CTC bank)" );
    TEST_ASSERT_EQUAL_HEX16_MESSAGE ( 0x1234, e0.source_addr,
                                      "source_addr = PC instrukce" );

    TEST_ASSERT_EQUAL_HEX8 ( IORQLOG_EVENT_MREQ_MAPPED, e1.event_type );
    TEST_ASSERT_EQUAL_HEX8 ( IORQLOG_DIR_IN, e1.direction );
    TEST_ASSERT_EQUAL_HEX16_MESSAGE ( 0xE008, e1.port_or_addr,
                                      "MREQ adresa = E008 (DMD)" );

    g_free ( buf );
}


void test_iorqlog_unconnected_flag_default_zero ( void )
{
    /* g_tracelog_iorq_unconnected musí být v initial stavu 0 - jinak by
     * každý IORQ event byl falsely označen jako ghost. */
    TEST_ASSERT_EQUAL_HEX8 ( 0, g_tracelog_iorq_unconnected );
}


void test_iorqlog_unconnected_macros_work ( void )
{
    /* Test že makra TRACELOG_IORQ_MARK_UNCONNECTED / _HANDLED přepínají
     * flag jak slíbeno. */
    g_tracelog_iorq_unconnected = 0;
    TRACELOG_IORQ_MARK_UNCONNECTED();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( 1, g_tracelog_iorq_unconnected,
                                     "MARK_UNCONNECTED nastaví na 1" );

    TRACELOG_IORQ_MARK_HANDLED();
    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( 0, g_tracelog_iorq_unconnected,
                                     "MARK_HANDLED nastaví na 0" );
}


/* ================================================================
 * meta.json
 * ================================================================ */

void test_iorqlog_meta_event_record_size ( void )
{
    activate_iorqlog ( );
    iorqlog_stop ( );
    g_iorqlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "iorq.json", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    const char *s = (const char *) buf;

    TEST_ASSERT_TRUE_MESSAGE ( json_kv_present ( s, "subsys", "\"iorqlog\"" ),
                                   "subsys = iorqlog" );
    TEST_ASSERT_TRUE_MESSAGE ( json_kv_present ( s, "event_record_size", "24" ),
                                   "event_record_size = 24" );
    TEST_ASSERT_TRUE_MESSAGE ( json_key_present ( s, "start_pxclk" ),
                                   "subsys_header musí mít start_pxclk" );

    g_free ( buf );
}


/* ================================================================
 * Pxclk timestamp roste mezi events
 * ================================================================ */

void test_iorqlog_pxclk_present_in_event ( void )
{
    activate_iorqlog ( );

    iorqlog_record ( IORQLOG_EVENT_IORQ, IORQLOG_DIR_OUT, 0, 0, 0, 0 );

    iorqlog_stop ( );
    g_iorqlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "iorq.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_iorq_t e;
    parse_iorq ( buf, &e );
    /* Po init je pxclk obvykle 0 nebo malé - jen ověříme že číslo je
     * konzistentní (v rozumném rozsahu pro headless test, žádný overflow). */
    TEST_ASSERT_TRUE_MESSAGE ( e.pxclk_total < 0x100000000ULL,
                               "pxclk po init musí být v rozumném rozsahu" );

    g_free ( buf );
}


/* ================================================================
 * MAIN
 * ================================================================ */

int main ( int argc, char *argv[] )
{
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    RUN_TEST ( test_iorqlog_start_stop );
    RUN_TEST ( test_iorqlog_start_idempotent );

    RUN_TEST ( test_iorqlog_event_layout_in );
    RUN_TEST ( test_iorqlog_event_layout_out );
    RUN_TEST ( test_iorqlog_multiple_events_sequence );

    RUN_TEST ( test_iorqlog_event_type_enum_values );
    RUN_TEST ( test_iorqlog_unconnected_event_emit );
    RUN_TEST ( test_iorqlog_mreq_mapped_event_emit );
    RUN_TEST ( test_iorqlog_unconnected_flag_default_zero );
    RUN_TEST ( test_iorqlog_unconnected_macros_work );

    RUN_TEST ( test_iorqlog_meta_event_record_size );
    RUN_TEST ( test_iorqlog_pxclk_present_in_event );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
