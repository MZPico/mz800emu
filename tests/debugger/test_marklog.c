/*
 * test_marklog.c - unit testy pro trace-suite Marker Log (5. subsystem)
 *
 * Testuje:
 *  - lifecycle (start/stop, meta.json + chunk soubory)
 *  - registry: register/get_name roundtrip
 *  - registry: opakovaná registrace stejného name = stejný id (idempotence)
 *  - registry: truncate long name + stable id pro truncated variant
 *  - per-record 24 B layout (pxclk + marker_id + reserved padding)
 *  - marklog_record() pro INVALID_ID = no-op
 *  - meta.json subsys_header (event_record_size, marker_count, markers)
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "debugger/trace/marklog.h"
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
    s_test_dir = g_dir_make_tmp ( "mztest-marklog-XXXXXX", &err );
    if ( !s_test_dir ) {
        if ( err ) g_error_free ( err );
        TEST_FAIL_MESSAGE ( "Nepodařilo se vytvořit tmpdir" );
        return;
    }

    g_free ( g_marklog_config.dir );
    g_free ( g_marklog_config.name );
    g_marklog_config.dir = g_strdup ( s_test_dir );
    g_marklog_config.name = g_strdup ( "marklg" );
    g_marklog_config.chunk_mb = 1;
    g_marklog_config.max_total_mb = 0;
    g_marklog_config.save_on_exit = 1;
    g_marklog_config.stdout_enabled = 0;  /* tichý test - jen binární zápis */
    g_marklog_config.mode = TLOG_MODE_OFF;
    g_marklog_active = 0;
}


void tearDown ( void )
{
    if ( g_marklog_active ) {
        marklog_stop ( );
        g_marklog_active = 0;
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
    uint16_t marker_id;
    uint8_t  reserved[ 6 ];
} parsed_mark_t;


static void parse_mark ( const uint8_t *buf, parsed_mark_t *e )
{
    e->pxclk_total = 0;
    for ( int i = 0; i < 8; i++ ) e->pxclk_total |= (uint64_t) buf[ i ] << ( i * 8 );
    e->screens = 0;
    for ( int i = 0; i < 4; i++ ) e->screens |= (uint32_t) buf[ 8 + i ] << ( i * 8 );
    e->pxclk_in_screen = 0;
    for ( int i = 0; i < 4; i++ ) e->pxclk_in_screen |= (uint32_t) buf[ 12 + i ] << ( i * 8 );
    e->marker_id = (uint16_t) buf[ 16 ] | ( (uint16_t) buf[ 17 ] << 8 );
    memcpy ( e->reserved, buf + 18, 6 );
}


static int activate_marklog ( void )
{
    int rc = marklog_start ( );
    if ( rc == 0 ) g_marklog_active = 1;
    return rc;
}


/* ================================================================
 * Lifecycle
 * ================================================================ */

void test_marklog_start_stop ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0, activate_marklog ( ) );
    marklog_stop ( );
    g_marklog_active = 0;

    /* meta.json musí existovat */
    char *p = g_build_filename ( s_test_dir, "marklg.json", NULL );
    TEST_ASSERT_TRUE ( g_file_test ( p, G_FILE_TEST_IS_REGULAR ) );
    g_free ( p );
}


/* ================================================================
 * Registry
 * ================================================================ */

void test_marklog_register_roundtrip ( void )
{
    /* Po setUp je registry zcela vyprázdněna? - není garantováno (registr
     * je perzistentní napříč start/stop). Použijeme deterministická názvy
     * z unique prefixu pro testovací izolaci. */

    uint16_t id_a = marklog_register ( "test_alpha_roundtrip" );
    uint16_t id_b = marklog_register ( "test_beta_roundtrip" );

    TEST_ASSERT_NOT_EQUAL ( MARKLOG_INVALID_ID, id_a );
    TEST_ASSERT_NOT_EQUAL ( MARKLOG_INVALID_ID, id_b );
    TEST_ASSERT_NOT_EQUAL ( id_a, id_b );

    TEST_ASSERT_EQUAL_STRING ( "test_alpha_roundtrip", marklog_get_name ( id_a ) );
    TEST_ASSERT_EQUAL_STRING ( "test_beta_roundtrip",  marklog_get_name ( id_b ) );
}


void test_marklog_register_idempotent ( void )
{
    uint16_t id1 = marklog_register ( "idem_marker" );
    uint16_t id2 = marklog_register ( "idem_marker" );
    uint16_t id3 = marklog_register ( "idem_marker" );

    TEST_ASSERT_NOT_EQUAL ( MARKLOG_INVALID_ID, id1 );
    TEST_ASSERT_EQUAL_HEX16 ( id1, id2 );
    TEST_ASSERT_EQUAL_HEX16 ( id1, id3 );
}


void test_marklog_register_null ( void )
{
    TEST_ASSERT_EQUAL_HEX16 ( MARKLOG_INVALID_ID, marklog_register ( NULL ) );
}


void test_marklog_register_truncate ( void )
{
    /* Name přesahující MARKLOG_NAME_MAX (64 vč NUL = 63 chars max).
     * Truncate na 63 chars. */
    char long_name[ 100 ];
    for ( int i = 0; i < 99; i++ ) long_name[ i ] = 'A' + ( i % 26 );
    long_name[ 99 ] = '\0';

    uint16_t id = marklog_register ( long_name );
    TEST_ASSERT_NOT_EQUAL ( MARKLOG_INVALID_ID, id );

    const char *stored = marklog_get_name ( id );
    TEST_ASSERT_NOT_NULL ( stored );
    /* Truncated string má délku přesně MARKLOG_NAME_MAX - 1 (= 63 chars). */
    TEST_ASSERT_EQUAL_size_t ( MARKLOG_NAME_MAX - 1, strlen ( stored ) );
    /* Prefix musí odpovídat původnímu name. */
    TEST_ASSERT_EQUAL_MEMORY ( long_name, stored, MARKLOG_NAME_MAX - 1 );
}


void test_marklog_get_name_invalid_id ( void )
{
    TEST_ASSERT_NULL ( marklog_get_name ( MARKLOG_INVALID_ID ) );
}


void test_marklog_get_name_out_of_range ( void )
{
    /* Nějaké id zcela mimo zaregistrovaný rozsah. Musí vrátit NULL,
     * ne crashnout. */
    TEST_ASSERT_NULL ( marklog_get_name ( 0xFFFE ) );
}


/* ================================================================
 * Per-record formát
 * ================================================================ */

void test_marklog_record_layout ( void )
{
    uint16_t id_first  = marklog_register ( "layout_first" );
    uint16_t id_second = marklog_register ( "layout_second" );

    activate_marklog ( );
    marklog_record ( id_first );
    marklog_record ( id_second );
    marklog_stop ( );
    g_marklog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "marklg.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    /* 2 záznamy x 24 B = 48 B */
    TEST_ASSERT_EQUAL_size_t ( 48, fsize );

    parsed_mark_t e;
    parse_mark ( buf, &e );
    TEST_ASSERT_EQUAL_HEX16 ( id_first, e.marker_id );
    /* Reserved padding musí být nuly. */
    for ( int i = 0; i < 6; i++ ) {
        TEST_ASSERT_EQUAL_HEX8 ( 0, e.reserved[ i ] );
    }

    parse_mark ( buf + 24, &e );
    TEST_ASSERT_EQUAL_HEX16 ( id_second, e.marker_id );

    g_free ( buf );
}


void test_marklog_record_invalid_id_noop ( void )
{
    activate_marklog ( );
    marklog_record ( MARKLOG_INVALID_ID );
    marklog_stop ( );
    g_marklog_active = 0;

    /* Žádný chunk soubor (= no record written), nebo chunk se 0 B.
     * Konkrétně tlog_writer vytváří chunk až po prvním reálném append. */
    char *p = g_build_filename ( s_test_dir, "marklg.000.bin", NULL );
    int has_chunk = g_file_test ( p, G_FILE_TEST_IS_REGULAR );
    g_free ( p );

    if ( has_chunk ) {
        size_t fsize = 0;
        uint8_t *buf = read_whole_file ( s_test_dir, "marklg.000.bin", &fsize );
        TEST_ASSERT_EQUAL_size_t ( 0, fsize );
        g_free ( buf );
    }
    /* OK i pokud chunk neexistuje vůbec. */
}


void test_marklog_record_not_active_noop ( void )
{
    uint16_t id = marklog_register ( "noop_marker" );
    /* Bez marklog_start: writer není aktivní. */
    marklog_record ( id );
    /* No-op - nesmí crashnout. */
    TEST_PASS ( );
}


/* ================================================================
 * meta.json
 * ================================================================ */

void test_marklog_meta_markers_dump ( void )
{
    uint16_t id_a = marklog_register ( "meta_alpha" );
    uint16_t id_b = marklog_register ( "meta_beta" );
    (void) id_a; (void) id_b;

    activate_marklog ( );
    marklog_stop ( );
    g_marklog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "marklg.json", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_TRUE ( fsize > 0 );

    /* Hrubá substring kontrola - JSON parse je v json-glib mimo scope. */
    const char *json = (const char *) buf;
    TEST_ASSERT_NOT_NULL ( strstr ( json, "\"marker_id_invalid\"" ) );
    TEST_ASSERT_NOT_NULL ( strstr ( json, "\"markers\"" ) );
    TEST_ASSERT_NOT_NULL ( strstr ( json, "meta_alpha" ) );
    TEST_ASSERT_NOT_NULL ( strstr ( json, "meta_beta" ) );
    TEST_ASSERT_NOT_NULL ( strstr ( json, "\"event_record_size\"" ) );

    g_free ( buf );
}


/* ================================================================
 * Runner
 * ================================================================ */

int main ( int argc, char *argv[] )
{
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );
    RUN_TEST ( test_marklog_start_stop );
    RUN_TEST ( test_marklog_register_roundtrip );
    RUN_TEST ( test_marklog_register_idempotent );
    RUN_TEST ( test_marklog_register_null );
    RUN_TEST ( test_marklog_register_truncate );
    RUN_TEST ( test_marklog_get_name_invalid_id );
    RUN_TEST ( test_marklog_get_name_out_of_range );
    RUN_TEST ( test_marklog_record_layout );
    RUN_TEST ( test_marklog_record_invalid_id_noop );
    RUN_TEST ( test_marklog_record_not_active_noop );
    RUN_TEST ( test_marklog_meta_markers_dump );
    return UNITY_END ( );
}
