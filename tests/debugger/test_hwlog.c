/*
 * test_hwlog.c - unit testy pro trace-suite HW Log
 *
 * Testuje:
 *  - lifecycle
 *  - per-event 24 B layout (pxclk + chip + sub + payload[6])
 *  - hwlog_record / hwlog_record_byte / hwlog_record_2byte
 *  - GDG_MODE / GDG_HWSCROLL / GDG_COLORS_BORDER / GDG_COLORS_PALGRP / GDG_COLORS_PAL emit
 *  - per-chip enum hodnoty rezervovány pro PIO8255, CTC8253, PSG, atd.
 *  - meta.json subsys_header (event_record_size = 24, v1_chip_coverage)
 *
 * GDG hooky jsou skutečně volané z gdg_write_byte() (bez integration testu
 * to nelze projet, ale enum hodnoty + emit cesta jdou unit testovat).
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "debugger/trace/hwlog.h"
#include "debugger/trace/tlog_common.h"
#include "hw-generic/pio8255/pio8255.h"
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
    s_test_dir = g_dir_make_tmp ( "mztest-hwlog-XXXXXX", &err );
    if ( !s_test_dir ) {
        if ( err ) g_error_free ( err );
        TEST_FAIL_MESSAGE ( "Nepodařilo se vytvořit tmpdir" );
        return;
    }

    g_free ( g_hwlog_config.dir );
    g_free ( g_hwlog_config.name );
    g_hwlog_config.dir = g_strdup ( s_test_dir );
    g_hwlog_config.name = g_strdup ( "hw" );
    g_hwlog_config.chunk_mb = 1;
    g_hwlog_config.max_total_mb = 0;
    g_hwlog_config.save_on_exit = 1;
    g_hwlog_config.mode = TLOG_MODE_OFF;
    g_hwlog_config.hs_decimation = 0;
    g_hwlog_active = 0;
}


void tearDown ( void )
{
    if ( g_hwlog_active ) {
        hwlog_stop ( );
        g_hwlog_active = 0;
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
    uint8_t  chip;
    uint8_t  sub;
    uint8_t  payload[ 6 ];
} parsed_hw_t;


static void parse_hw ( const uint8_t *buf, parsed_hw_t *e )
{
    e->pxclk_total = 0;
    for ( int i = 0; i < 8; i++ ) e->pxclk_total |= (uint64_t) buf[ i ] << ( i * 8 );
    e->screens = 0;
    for ( int i = 0; i < 4; i++ ) e->screens |= (uint32_t) buf[ 8 + i ] << ( i * 8 );
    e->pxclk_in_screen = 0;
    for ( int i = 0; i < 4; i++ ) e->pxclk_in_screen |= (uint32_t) buf[ 12 + i ] << ( i * 8 );
    e->chip = buf[ 16 ];
    e->sub = buf[ 17 ];
    memcpy ( e->payload, buf + 18, 6 );
}


static int activate_hwlog ( void )
{
    int rc = hwlog_start ( );
    if ( rc == 0 ) g_hwlog_active = 1;
    return rc;
}


/**
 * @brief Vrátí velikost souboru @p filename v @p dir v bajtech.
 *
 * @return Velikost v bajtech, nebo -1 pokud soubor neexistuje / nelze
 *         získat stat.
 */
static gint64 file_size ( const char *dir, const char *filename )
{
    char *path = g_build_filename ( dir, filename, NULL );
    GStatBuf st;
    int rc = g_stat ( path, &st );
    g_free ( path );
    if ( rc != 0 ) return -1;
    return (gint64) st.st_size;
}


/* ================================================================
 * Lifecycle
 * ================================================================ */

void test_hwlog_start_stop ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0, activate_hwlog ( ) );
    hwlog_stop ( );
    g_hwlog_active = 0;

    char *p = g_build_filename ( s_test_dir, "hw.json", NULL );
    TEST_ASSERT_TRUE ( g_file_test ( p, G_FILE_TEST_IS_REGULAR ) );
    g_free ( p );
}


/* ================================================================
 * Per-event layout
 * ================================================================ */

void test_hwlog_record_full_payload ( void )
{
    activate_hwlog ( );

    uint8_t payload[ 6 ] = { 0xCE, 0x00, 0x05, 0x11, 0x22, 0x33 };
    hwlog_record ( HWLOG_CHIP_GDG_MODE, 0, payload );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 24, fsize );

    parsed_hw_t e;
    parse_hw ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_GDG_MODE, e.chip );
    TEST_ASSERT_EQUAL_HEX8 ( 0, e.sub );
    for ( int i = 0; i < 6; i++ ) {
        TEST_ASSERT_EQUAL_HEX8 ( payload[ i ], e.payload[ i ] );
    }

    g_free ( buf );
}


void test_hwlog_record_byte_helper ( void )
{
    activate_hwlog ( );

    /* hwlog_record_byte(value=0x42) - payload[0]=0x42, ostatní 0. */
    hwlog_record_byte ( HWLOG_CHIP_GDG_COLORS, HWLOG_GDG_COLORS_BORDER, 0x42 );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_hw_t e;
    parse_hw ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_GDG_COLORS, e.chip );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_GDG_COLORS_BORDER, e.sub );
    TEST_ASSERT_EQUAL_HEX8 ( 0x42, e.payload[ 0 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0, e.payload[ 1 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0, e.payload[ 5 ] );

    g_free ( buf );
}


void test_hwlog_record_2byte_helper ( void )
{
    activate_hwlog ( );

    hwlog_record_2byte ( HWLOG_CHIP_PSG, 0x10, 0xAB, 0xCD );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_hw_t e;
    parse_hw ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_PSG, e.chip );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAB, e.payload[ 0 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xCD, e.payload[ 1 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0,    e.payload[ 2 ] );

    g_free ( buf );
}


void test_hwlog_record_null_payload ( void )
{
    activate_hwlog ( );

    /* hwlog_record(payload=NULL) - musí dát 6 nul. */
    hwlog_record ( HWLOG_CHIP_GDG_VIDEO, 1, NULL );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_hw_t e;
    parse_hw ( buf, &e );
    for ( int i = 0; i < 6; i++ ) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE ( 0, e.payload[ i ],
                                         "NULL payload musí dát 6 nul" );
    }

    g_free ( buf );
}


/* ================================================================
 * GDG cluster events (pattern z gdg_write_byte)
 * ================================================================ */

void test_hwlog_gdg_mode_event ( void )
{
    activate_hwlog ( );

    /* Simulace OUT 0xCE, value=0x08 (= MZ-800 mode 320x200x16). */
    uint8_t payload[ 6 ] = { 0xCE, 0x00, 0x08, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_GDG_MODE, 0, payload );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_hw_t e;
    parse_hw ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_GDG_MODE, e.chip );
    TEST_ASSERT_EQUAL_HEX8 ( 0xCE, e.payload[ 0 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x08, e.payload[ 2 ] );
    g_free ( buf );
}


void test_hwlog_gdg_border_event ( void )
{
    activate_hwlog ( );

    /* Simulace OUT 0x06CF (= BORDER), value=0x05. */
    uint8_t payload[ 6 ] = { 0xCF, 0x06, 0x05, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_GDG_COLORS, HWLOG_GDG_COLORS_BORDER, payload );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_hw_t e;
    parse_hw ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_GDG_COLORS, e.chip );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_GDG_COLORS_BORDER, e.sub );
    g_free ( buf );
}


void test_hwlog_gdg_palette_palgrp_pal ( void )
{
    activate_hwlog ( );

    /* OUT 0xF0 s value 0x40 = PALGRP (bit 6). */
    uint8_t pal_grp_payload[ 6 ] = { 0xF0, 0x00, 0x40, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_GDG_COLORS, HWLOG_GDG_COLORS_PALGRP, pal_grp_payload );

    /* OUT 0xF0 s value 0x05 = PAL update bez palgrp bit. */
    uint8_t pal_payload[ 6 ] = { 0xF0, 0x00, 0x05, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_GDG_COLORS, HWLOG_GDG_COLORS_PAL, pal_payload );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 2 * 24, fsize );

    parsed_hw_t e0, e1;
    parse_hw ( buf, &e0 );
    parse_hw ( buf + 24, &e1 );

    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_GDG_COLORS_PALGRP, e0.sub );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_GDG_COLORS_PAL, e1.sub );

    g_free ( buf );
}


void test_hwlog_gdg_hwscroll_event ( void )
{
    activate_hwlog ( );

    /* OUT 0x02CF (= HWSCROLL reg 2), value=0x10. */
    uint8_t payload[ 6 ] = { 0xCF, 0x02, 0x10, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_GDG_HWSCROLL, 0x02, payload );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_hw_t e;
    parse_hw ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_GDG_HWSCROLL, e.chip );
    TEST_ASSERT_EQUAL_HEX8 ( 0x02, e.sub );
    g_free ( buf );
}


/* ================================================================
 * Per-chip enum hodnoty rezervovány
 * ================================================================ */

void test_hwlog_chip_enum_reservations ( void )
{
    /* Per PLAN.md: GDG-* (0x01-0x06), PIO8255 (0x10), CTC8253 (0x11),
     * PIOZ80 (0x12), PSG (0x20), QD (0x30), FDC (0x31), MEMEXT (0x40),
     * RD (0x41). Test ověřuje stabilitu hodnot pro binární formát. */
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_CHIP_GDG_MODE );
    TEST_ASSERT_EQUAL_INT ( 0x02, HWLOG_CHIP_GDG_BANKING );
    TEST_ASSERT_EQUAL_INT ( 0x03, HWLOG_CHIP_GDG_HWSCROLL );
    TEST_ASSERT_EQUAL_INT ( 0x04, HWLOG_CHIP_GDG_COLORS );
    TEST_ASSERT_EQUAL_INT ( 0x05, HWLOG_CHIP_GDG_WFRF );
    TEST_ASSERT_EQUAL_INT ( 0x06, HWLOG_CHIP_GDG_VIDEO );
    TEST_ASSERT_EQUAL_INT ( 0x10, HWLOG_CHIP_PIO8255 );
    TEST_ASSERT_EQUAL_INT ( 0x11, HWLOG_CHIP_CTC8253 );
    TEST_ASSERT_EQUAL_INT ( 0x12, HWLOG_CHIP_PIOZ80 );
    TEST_ASSERT_EQUAL_INT ( 0x20, HWLOG_CHIP_PSG );
    TEST_ASSERT_EQUAL_INT ( 0x30, HWLOG_CHIP_QD );
    TEST_ASSERT_EQUAL_INT ( 0x31, HWLOG_CHIP_FDC );
    TEST_ASSERT_EQUAL_INT ( 0x40, HWLOG_CHIP_MEMEXT );
    TEST_ASSERT_EQUAL_INT ( 0x41, HWLOG_CHIP_RD );
}


void test_hwlog_gdg_colors_sub_enum ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_GDG_COLORS_BORDER );
    TEST_ASSERT_EQUAL_INT ( 0x02, HWLOG_GDG_COLORS_PALGRP );
    TEST_ASSERT_EQUAL_INT ( 0x03, HWLOG_GDG_COLORS_PAL );
    TEST_ASSERT_EQUAL_INT ( 0x04, HWLOG_GDG_COLORS_PCG );
    TEST_ASSERT_EQUAL_INT ( 0x05, HWLOG_GDG_COLORS_PACKETGROUP );
}


void test_hwlog_psg_sub_enum ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_PSG_REGISTER_WRITE );
}


void test_hwlog_psg_event_format ( void )
{
    activate_hwlog ( );

    /* Simulace write 0x9F (latch CS=0, attn=1, value=0xF = OFF) v mono. */
    uint8_t payload[ 6 ] = { 0x9F, 0x01, 0x00, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_PSG, HWLOG_PSG_REGISTER_WRITE, payload );

    /* Simulace write v stereo na PSG_CH_LEFT|RIGHT (broadcast). */
    uint8_t payload2[ 6 ] = { 0x80, 0x03, 0x01, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_PSG, HWLOG_PSG_REGISTER_WRITE, payload2 );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 2 * 24, fsize );

    parsed_hw_t e0, e1;
    parse_hw ( buf, &e0 );
    parse_hw ( buf + 24, &e1 );

    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_PSG, e0.chip );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_PSG_REGISTER_WRITE, e0.sub );
    TEST_ASSERT_EQUAL_HEX8 ( 0x9F, e0.payload[ 0 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x01, e0.payload[ 1 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x00, e0.payload[ 2 ] );

    TEST_ASSERT_EQUAL_HEX8 ( 0x80, e1.payload[ 0 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x03, e1.payload[ 1 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x01, e1.payload[ 2 ] );

    g_free ( buf );
}


void test_hwlog_gdg_banking_sub_enum ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0xE0, HWLOG_GDG_BANKING_E0 );
    TEST_ASSERT_EQUAL_INT ( 0xE1, HWLOG_GDG_BANKING_E1 );
    TEST_ASSERT_EQUAL_INT ( 0xE2, HWLOG_GDG_BANKING_E2 );
    TEST_ASSERT_EQUAL_INT ( 0xE3, HWLOG_GDG_BANKING_E3 );
    TEST_ASSERT_EQUAL_INT ( 0xE4, HWLOG_GDG_BANKING_E4 );
    TEST_ASSERT_EQUAL_INT ( 0xE5, HWLOG_GDG_BANKING_E5 );
    TEST_ASSERT_EQUAL_INT ( 0xE6, HWLOG_GDG_BANKING_E6 );
}


void test_hwlog_qd_fdc_rd_sub_enum ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_QD_REGISTER_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_FDC_REGISTER_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_RD_STD_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x02, HWLOG_RD_PEZIK_WRITE );
}


void test_hwlog_gdg_video_sub_enum ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_GDG_VIDEO_VBLN_START );
    TEST_ASSERT_EQUAL_INT ( 0x02, HWLOG_GDG_VIDEO_VBLN_END );
    TEST_ASSERT_EQUAL_INT ( 0x03, HWLOG_GDG_VIDEO_VS_START );
    TEST_ASSERT_EQUAL_INT ( 0x04, HWLOG_GDG_VIDEO_VS_END );
    /* HS/HBLN edges - decimovany emit přes hwlog_record_gdg_video_hs_edge. */
    TEST_ASSERT_EQUAL_INT ( 0x05, HWLOG_GDG_VIDEO_HBLN_START );
    TEST_ASSERT_EQUAL_INT ( 0x06, HWLOG_GDG_VIDEO_HBLN_END );
    TEST_ASSERT_EQUAL_INT ( 0x07, HWLOG_GDG_VIDEO_HS_START );
    TEST_ASSERT_EQUAL_INT ( 0x08, HWLOG_GDG_VIDEO_HS_END );
}


/* ================================================================
 * GDG_VIDEO HS/HBLN decimace (hs_decimation)
 * ================================================================ */

void test_hwlog_hs_decimation_zero_suppresses_emit ( void )
{
    /* Default hs_decimation = 0 => HS/HBLN edges nevolají emit
     * (counter sice bude růst, ale nepoušt eventy). */
    activate_hwlog ( );
    g_hwlog_config.hs_decimation = 0;

    for ( int i = 0; i < 100; i++ ) {
        hwlog_record_gdg_video_hs_edge ( HWLOG_GDG_VIDEO_HBLN_START );
    }

    hwlog_stop ( );
    g_hwlog_active = 0;

    /* Žádný chunk soubor by neměl mít HS/HBLN events. Otevřeme chunk
     * a ověříme že je bud prázdný nebo neobsahuje sub_event v rozsahu
     * 0x05..0x08 pro CHIP_GDG_VIDEO. */
    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    /* může i neexistovat - pokud writer flush neudělal, je to OK. */
    if ( buf ) {
        for ( size_t off = 0; off + 24 <= fsize; off += 24 ) {
            if ( buf[ off + 16 ] == HWLOG_CHIP_GDG_VIDEO ) {
                uint8_t sub = buf[ off + 17 ];
                TEST_ASSERT_TRUE_MESSAGE (
                    sub != HWLOG_GDG_VIDEO_HBLN_START
                    && sub != HWLOG_GDG_VIDEO_HBLN_END
                    && sub != HWLOG_GDG_VIDEO_HS_START
                    && sub != HWLOG_GDG_VIDEO_HS_END,
                    "Při hs_decimation=0 nesmí být žádný HS/HBLN event v chunku"
                );
            }
        }
        g_free ( buf );
    }
}


void test_hwlog_hs_decimation_emits_every_nth ( void )
{
    /* hs_decimation = 100 => každý 100. event emitnut. 1000 volání =
     * 10 events. */
    activate_hwlog ( );
    g_hwlog_config.hs_decimation = 100;

    for ( int i = 0; i < 1000; i++ ) {
        hwlog_record_gdg_video_hs_edge ( HWLOG_GDG_VIDEO_HS_START );
    }

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL_MESSAGE ( buf, "chunk by měl existovat" );

    /* Spočítej kolik HS_START events je v chunku. */
    int hs_count = 0;
    for ( size_t off = 0; off + 24 <= fsize; off += 24 ) {
        if ( buf[ off + 16 ] == HWLOG_CHIP_GDG_VIDEO
             && buf[ off + 17 ] == HWLOG_GDG_VIDEO_HS_START ) {
            hs_count++;
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE ( 10, hs_count,
        "1000 volání s decimation=100 => přesně 10 emitů" );

    g_free ( buf );
}


void test_hwlog_hs_decimation_emits_first_event_when_n_set ( void )
{
    /* Counter začíná na 0 => první volání s decimation=N emituje
     * (0 % N == 0). To je úmyslné: aby start recordingu měl konzistentní
     * baseline anchor v čase. */
    activate_hwlog ( );
    g_hwlog_config.hs_decimation = 5;

    hwlog_record_gdg_video_hs_edge ( HWLOG_GDG_VIDEO_HBLN_END );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    int hbln_end_count = 0;
    for ( size_t off = 0; off + 24 <= fsize; off += 24 ) {
        if ( buf[ off + 16 ] == HWLOG_CHIP_GDG_VIDEO
             && buf[ off + 17 ] == HWLOG_GDG_VIDEO_HBLN_END ) {
            hbln_end_count++;
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE ( 1, hbln_end_count,
        "1. volání s decimation=N emit (counter=0 % N == 0)" );

    g_free ( buf );
}


void test_hwlog_pio8255_sub_enum ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_PIO8255_PORT_A_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x02, HWLOG_PIO8255_PORT_B_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x03, HWLOG_PIO8255_PORT_C_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x04, HWLOG_PIO8255_CONTROL_WRITE );
}


void test_hwlog_pio8255_event_format ( void )
{
    activate_hwlog ( );

    /* PORT_A: column 5, JOY1 strobe enabled. */
    uint8_t pa[ 6 ] = { 0x00, 0x05, 0, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_PIO8255, HWLOG_PIO8255_PORT_A_WRITE, pa );

    /* PORT_C: blokovani audio. */
    uint8_t pc[ 6 ] = { 0x02, 0x01, 0, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_PIO8255, HWLOG_PIO8255_PORT_C_WRITE, pc );

    /* CONTROL: 0x90 (= mode 0 init, PA=in, PB+PC=out). */
    uint8_t cw[ 6 ] = { 0x03, 0x90, 0, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_PIO8255, HWLOG_PIO8255_CONTROL_WRITE, cw );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 3 * 24, fsize );

    parsed_hw_t e0, e1, e2;
    parse_hw ( buf,      &e0 );
    parse_hw ( buf + 24, &e1 );
    parse_hw ( buf + 48, &e2 );

    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_PIO8255, e0.chip );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_PIO8255_PORT_A_WRITE,  e0.sub );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_PIO8255_PORT_C_WRITE,  e1.sub );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_PIO8255_CONTROL_WRITE, e2.sub );
    TEST_ASSERT_EQUAL_HEX8 ( 0x90, e2.payload[ 1 ] );

    g_free ( buf );
}


void test_hwlog_ctc8253_sub_enum ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_CTC8253_CONTROL_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x02, HWLOG_CTC8253_COUNTER_WRITE );
}


void test_hwlog_ctc8253_event_format ( void )
{
    activate_hwlog ( );

    /* Simulace CONTROL_WRITE: addr=3, value=0x36 (= CTC0, RL=LSB+MSB,
     * MODE3, BCD=0). */
    uint8_t cw[ 6 ] = { 0x03, 0x36, 0x00, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_CTC8253, HWLOG_CTC8253_CONTROL_WRITE, cw );

    /* Simulace COUNTER_WRITE: addr=0, value=0x10, pre-rl_byte=0. */
    uint8_t cnt[ 6 ] = { 0x00, 0x10, 0x00, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_CTC8253, HWLOG_CTC8253_COUNTER_WRITE, cnt );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 2 * 24, fsize );

    parsed_hw_t e0, e1;
    parse_hw ( buf, &e0 );
    parse_hw ( buf + 24, &e1 );

    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_CTC8253, e0.chip );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CTC8253_CONTROL_WRITE, e0.sub );
    TEST_ASSERT_EQUAL_HEX8 ( 0x03, e0.payload[ 0 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x36, e0.payload[ 1 ] );

    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CTC8253_COUNTER_WRITE, e1.sub );
    TEST_ASSERT_EQUAL_HEX8 ( 0x00, e1.payload[ 0 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x10, e1.payload[ 1 ] );

    g_free ( buf );
}


void test_hwlog_memext_sub_enum ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_MEMEXT_BANK_SWITCH );
}


void test_hwlog_memext_event_format ( void )
{
    activate_hwlog ( );

    /* Simulace BANK_SWITCH: bus page 5 -> raw bank 0x42, LUFTNER. */
    uint8_t payload[ 6 ] = { 0x05, 0x42, 0x00, 0, 0, 0 };
    hwlog_record ( HWLOG_CHIP_MEMEXT, HWLOG_MEMEXT_BANK_SWITCH, payload );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 24, fsize );

    parsed_hw_t e;
    parse_hw ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_MEMEXT, e.chip );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_MEMEXT_BANK_SWITCH, e.sub );
    TEST_ASSERT_EQUAL_HEX8 ( 0x05, e.payload[ 0 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x42, e.payload[ 1 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x00, e.payload[ 2 ] );

    g_free ( buf );
}


void test_hwlog_initial_state_file_created ( void )
{
    activate_hwlog ( );
    hwlog_stop ( );
    g_hwlog_active = 0;

    /* Ověř že soubor existuje a má alespoň 1 TLV record + EOF marker. */
    char *p = g_build_filename ( s_test_dir, "hw_initial_state.bin", NULL );
    TEST_ASSERT_TRUE_MESSAGE ( g_file_test ( p, G_FILE_TEST_IS_REGULAR ),
                               "initial_state.bin musí vzniknout" );

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw_initial_state.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    /* Musí být alespoň header velikosti 5 B (PIO8255 TLV) + EOF 5 B. */
    TEST_ASSERT_TRUE_MESSAGE ( fsize >= 10, "TLV soubor minimální velikost" );
    /* První byte = HWLOG_CHIP_PIO8255 = 0x10. */
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_PIO8255, buf[ 0 ] );

    /* Poslední 5 bytů = EOF marker (0xFF + 4× 0). */
    TEST_ASSERT_EQUAL_HEX8 ( 0xFF, buf[ fsize - 5 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x00, buf[ fsize - 4 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x00, buf[ fsize - 1 ] );

    g_free ( p );
    g_free ( buf );
}


/* ================================================================
 * PIOZ80 sub-event enum + payload format
 * ================================================================ */

void test_hwlog_pioz80_sub_enum_reservations ( void )
{
    /* Stabilita hodnot pro binární formát - parser je závazně používá. */
    TEST_ASSERT_EQUAL_INT ( 0x01, HWLOG_PIOZ80_MODE_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x02, HWLOG_PIOZ80_VECTOR_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x03, HWLOG_PIOZ80_INT_CTRL_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x04, HWLOG_PIOZ80_MASK_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x05, HWLOG_PIOZ80_IO_SELECT_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x06, HWLOG_PIOZ80_DATA_WRITE );
    TEST_ASSERT_EQUAL_INT ( 0x07, HWLOG_PIOZ80_DATA_READ );
    TEST_ASSERT_EQUAL_INT ( 0x08, HWLOG_PIOZ80_BUS_INPUT_CHANGE );
    TEST_ASSERT_EQUAL_INT ( 0x09, HWLOG_PIOZ80_IRQ_ACK_M2 );
    TEST_ASSERT_EQUAL_INT ( 0x0A, HWLOG_PIOZ80_RETI_APPLIED );
}


void test_hwlog_pioz80_event_format ( void )
{
    activate_hwlog ( );

    /* Per sub-event simulace s konkretním payloadem (= 24 B layout
     * verifikace, ne reálné hooky - ty vyžadují živý emu). */

    /* MODE_WRITE: port A, addr=0xFC, value=0xCF (Mode 3), delta = MODE bit. */
    uint8_t p_mode[ 6 ] = { 0x00, 0xFC, 0xCF,
                            (uint8_t) ( HWLOG_PIOZ80_DELTA_MODE & 0xff ), 0x00, 0x00 };
    hwlog_record ( HWLOG_CHIP_PIOZ80, HWLOG_PIOZ80_MODE_WRITE, p_mode );

    /* DATA_WRITE: port B, addr=0xFF, value=0x55 (data), delta = DATA_OUT. */
    uint8_t p_data[ 6 ] = { 0x01, 0xFF, 0x55,
                            (uint8_t) ( HWLOG_PIOZ80_DELTA_DATA_OUT & 0xff ), 0x00, 0x00 };
    hwlog_record ( HWLOG_CHIP_PIOZ80, HWLOG_PIOZ80_DATA_WRITE, p_data );

    /* IRQ_ACK_M2: port A, vector=0x40. */
    uint8_t p_ack[ 6 ] = { 0x00, 0x40, 0x40,
                           (uint8_t) ( HWLOG_PIOZ80_DELTA_PORT_INT & 0xff ),
                           (uint8_t) ( ( HWLOG_PIOZ80_DELTA_INT_GLOBAL >> 8 ) & 0xff ),
                           0x00 };
    hwlog_record ( HWLOG_CHIP_PIOZ80, HWLOG_PIOZ80_IRQ_ACK_M2, p_ack );

    /* BUS_INPUT_CHANGE: port A, pin=5 (PA5/VBLN), value=1 = rising. */
    uint8_t p_bus[ 6 ] = { 0x00, 0x05, 0x01,
                           (uint8_t) ( HWLOG_PIOZ80_DELTA_MASKED_IN & 0xff ),
                           0x00, 0x00 };
    hwlog_record ( HWLOG_CHIP_PIOZ80, HWLOG_PIOZ80_BUS_INPUT_CHANGE, p_bus );

    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 4 * 24, fsize );

    parsed_hw_t e0, e1, e2, e3;
    parse_hw ( buf,      &e0 );
    parse_hw ( buf + 24, &e1 );
    parse_hw ( buf + 48, &e2 );
    parse_hw ( buf + 72, &e3 );

    /* Všechny eventy chip = PIOZ80 (0x12). */
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_PIOZ80, e0.chip );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_PIOZ80, e1.chip );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_PIOZ80, e2.chip );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_PIOZ80, e3.chip );

    /* Sub-event types. */
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_PIOZ80_MODE_WRITE,       e0.sub );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_PIOZ80_DATA_WRITE,       e1.sub );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_PIOZ80_IRQ_ACK_M2,       e2.sub );
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_PIOZ80_BUS_INPUT_CHANGE, e3.sub );

    /* Payload byte 0 = port_id. */
    TEST_ASSERT_EQUAL_HEX8 ( 0x00, e0.payload[ 0 ] ); /* port A */
    TEST_ASSERT_EQUAL_HEX8 ( 0x01, e1.payload[ 0 ] ); /* port B */

    /* Payload byte 1 = addr / sub_addr (0xFC pro PA control). */
    TEST_ASSERT_EQUAL_HEX8 ( 0xFC, e0.payload[ 1 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xFF, e1.payload[ 1 ] );

    /* Payload byte 2 = value. */
    TEST_ASSERT_EQUAL_HEX8 ( 0xCF, e0.payload[ 2 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x55, e1.payload[ 2 ] );

    /* Decoded delta bits ([3..5] = 24 b LE). */
    uint32_t delta0 = (uint32_t) e0.payload[ 3 ]
                    | ( (uint32_t) e0.payload[ 4 ] << 8 )
                    | ( (uint32_t) e0.payload[ 5 ] << 16 );
    TEST_ASSERT_BITS_HIGH ( HWLOG_PIOZ80_DELTA_MODE, delta0 );

    uint32_t delta1 = (uint32_t) e1.payload[ 3 ]
                    | ( (uint32_t) e1.payload[ 4 ] << 8 )
                    | ( (uint32_t) e1.payload[ 5 ] << 16 );
    TEST_ASSERT_BITS_HIGH ( HWLOG_PIOZ80_DELTA_DATA_OUT, delta1 );

    uint32_t delta2 = (uint32_t) e2.payload[ 3 ]
                    | ( (uint32_t) e2.payload[ 4 ] << 8 )
                    | ( (uint32_t) e2.payload[ 5 ] << 16 );
    TEST_ASSERT_BITS_HIGH ( HWLOG_PIOZ80_DELTA_INT_GLOBAL, delta2 );

    g_free ( buf );
}


void test_hwlog_initial_state_contains_pioz80_tlv ( void )
{
    activate_hwlog ( );
    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw_initial_state.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    /* Hledej TLV s chip_id = HWLOG_CHIP_PIOZ80 (0x12). Procházíme TLV
     * záznamy (chip_id 1B + length 4B LE + payload). */
    int found_pioz80 = 0;
    size_t off = 0;
    while ( off + 5 <= fsize ) {
        uint8_t chip_id = buf[ off ];
        uint32_t len = (uint32_t) buf[ off + 1 ]
                     | ( (uint32_t) buf[ off + 2 ] << 8 )
                     | ( (uint32_t) buf[ off + 3 ] << 16 )
                     | ( (uint32_t) buf[ off + 4 ] << 24 );
        if ( chip_id == 0xFF && len == 0 ) break; /* EOF */
        if ( chip_id == HWLOG_CHIP_PIOZ80 ) {
            found_pioz80 = 1;
            /* PIOZ80 payload má per port (12 polí + bus_input = 13 B)
             * krát 2 + 2 globální = 28 B minimum. */
            TEST_ASSERT_TRUE_MESSAGE ( len >= 28,
                "PIOZ80 TLV payload >= 28 B (2 ports + globals)" );
            break;
        }
        off += 5 + len;
    }
    TEST_ASSERT_TRUE_MESSAGE ( found_pioz80,
        "Initial state musí obsahovat PIOZ80 TLV (chip_id=0x12)" );

    g_free ( buf );
}


/* ================================================================
 * Field-by-field initial state layout
 * ================================================================
 *
 * Refactor V1.1: per-chip serializery zapisují konkrétní hodnoty bez
 * závislosti na compiler ABI. Test ověří první pole každého chipu na
 * očekávaných offsetech.
 */

void test_hwlog_initial_state_field_layout ( void )
{
    /* Mockujeme známé hodnoty do globálních struktur tak, aby parser
     * dokázal na konkrétní offset najít konkrétní byte. Test ověří
     * jen PIO8255 začátek (signal_PA = u32 LE) - ostatní chipy by
     * vyžadovaly link-time globální přepis (těžké testovat headless). */
    uint8_t saved_keyboard[ 10 ];
    memcpy ( saved_keyboard, g_pio8255.keyboard_matrix, 10 );
    unsigned saved_signal_PA = g_pio8255.signal_PA;

    g_pio8255.signal_PA = 0xDEADBEEFu;
    /* Vyplníme keyboard_matrix sekvencí 0x10..0x19 ať poznáme offset. */
    for ( int i = 0; i < 10; i++ ) g_pio8255.keyboard_matrix[ i ] = (uint8_t) ( 0x10 + i );

    activate_hwlog ( );
    hwlog_stop ( );
    g_hwlog_active = 0;

    /* Restore. */
    g_pio8255.signal_PA = saved_signal_PA;
    memcpy ( g_pio8255.keyboard_matrix, saved_keyboard, 10 );

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw_initial_state.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    /* První TLV = PIO8255 (chip_id=0x10). */
    TEST_ASSERT_EQUAL_HEX8 ( HWLOG_CHIP_PIO8255, buf[ 0 ] );
    /* Délka payloadu (4 B LE). */
    uint32_t pio_len = (uint32_t) buf[ 1 ]
                     | ( (uint32_t) buf[ 2 ] << 8 )
                     | ( (uint32_t) buf[ 3 ] << 16 )
                     | ( (uint32_t) buf[ 4 ] << 24 );
    /* 10× u32 (40 B) + 10 B keyboard_matrix = 50 B. */
    TEST_ASSERT_EQUAL_INT_MESSAGE ( 50, (int) pio_len,
        "PIO8255 field-by-field payload = 50 B" );

    /* Payload start na offset 5. signal_PA = u32 LE na offset 5..8. */
    TEST_ASSERT_EQUAL_HEX8 ( 0xEF, buf[ 5 ] );    /* LSB */
    TEST_ASSERT_EQUAL_HEX8 ( 0xBE, buf[ 6 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAD, buf[ 7 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0xDE, buf[ 8 ] );    /* MSB */

    /* Po 10× u32 (= +40 B = offset 5+40 = 45) následuje keyboard_matrix
     * 10 B (0x10..0x19). */
    TEST_ASSERT_EQUAL_HEX8 ( 0x10, buf[ 45 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x11, buf[ 46 ] );
    TEST_ASSERT_EQUAL_HEX8 ( 0x19, buf[ 54 ] );

    g_free ( buf );
}


void test_hwlog_meta_psg_dividers ( void )
{
    activate_hwlog ( );
    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.json", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    const char *s = (const char *) buf;
    TEST_ASSERT_TRUE_MESSAGE ( json_key_present ( s, "psgclk_divider_cpuclk" ),
                                   "PSGCLK divider musí být v subsys_header" );
    TEST_ASSERT_TRUE_MESSAGE ( json_key_present ( s, "tempo_divider_screen_rows" ),
                                   "TEMPO divider musí být v subsys_header" );
    g_free ( buf );
}


/* ================================================================
 * meta.json
 * ================================================================ */

void test_hwlog_meta_subsys_header ( void )
{
    activate_hwlog ( );
    hwlog_stop ( );
    g_hwlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "hw.json", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    const char *s = (const char *) buf;

    TEST_ASSERT_TRUE_MESSAGE ( json_kv_present ( s, "subsys", "\"hwlog\"" ),
                                   "subsys = hwlog" );
    TEST_ASSERT_TRUE_MESSAGE ( json_kv_present ( s, "event_record_size", "24" ),
                                   "event_record_size = 24" );
    TEST_ASSERT_TRUE_MESSAGE ( json_key_present ( s, "v1_chip_coverage" ),
                                   "v1_chip_coverage musí být v subsys_header" );
    TEST_ASSERT_TRUE_MESSAGE ( json_key_present ( s, "v1_chip_pending" ),
                                   "v1_chip_pending musí být v subsys_header" );

    g_free ( buf );
}


/* ================================================================
 * Inactive writer = no-op
 * ================================================================ */

void test_hwlog_record_when_not_active_is_noop ( void )
{
    /* Bez hwlog_start: hwlog_record musí být no-op (žádný crash, žádný
     * soubor). */
    hwlog_record_byte ( HWLOG_CHIP_GDG_MODE, 0, 0xFF );

    char *p = g_build_filename ( s_test_dir, "hw.000.bin", NULL );
    TEST_ASSERT_FALSE_MESSAGE ( g_file_test ( p, G_FILE_TEST_EXISTS ),
                                "Bez aktivace nesmí vzniknout chunk" );
    g_free ( p );
}


/* ================================================================
 * I19HWLOG - byte backstop accounting hwlog disk zápisů
 * ================================================================
 *
 * I19BYTE zařídil, že byte backstop countuje trace_save disk footprint,
 * ale hwlog měl vlastní raw fwrite cesty (write_chip_tlv_buf / _empty
 * volané z dump_initial_state), které NEšly přes
 * tlog_common_disk_bytes_add -> initial-state dump (~stovky B až KB)
 * unikal accountingu a auto-pauza ho neviděla.
 *
 * Test prokazuje, že po hwlog_start() (= reálný zápis hw_initial_state.bin
 * na disk + meta hw.json) narostl kumulativní disk čítač přesně o součet
 * velikostí zapsaných souborů. Před fixem by delta postrádala velikost
 * initial-state souboru.
 *
 * Test je deterministický nezávisle na časování (čítač je čistá
 * aritmetika disk bajtů, ne clock-domain stav).
 */
void test_hwlog_initial_state_counted_in_disk_backstop ( void )
{
    tlog_common_disk_bytes_reset ( );
    TEST_ASSERT_EQUAL_UINT64 ( 0, tlog_common_disk_bytes_total ( ) );

    /* hwlog_start() zapíše: hw_initial_state.bin (raw fwrite cesta, jádro
     * fixu) + meta hw.json (přes tlog_common writer). Měříme deltu jen
     * kolem startu, abychom izolovali od second meta write v hwlog_stop. */
    uint64_t before = tlog_common_disk_bytes_total ( );
    TEST_ASSERT_EQUAL_INT ( 0, hwlog_start ( ) );
    g_hwlog_active = 1;
    uint64_t after = tlog_common_disk_bytes_total ( );

    gint64 sz_initial = file_size ( s_test_dir, "hw_initial_state.bin" );
    gint64 sz_meta    = file_size ( s_test_dir, "hw.json" );

    /* Oba soubory musí existovat a být neprázdné. */
    TEST_ASSERT_TRUE_MESSAGE ( sz_initial > 0,
        "hw_initial_state.bin musí vzniknout a být neprázdný" );
    TEST_ASSERT_TRUE_MESSAGE ( sz_meta > 0,
        "hw.json (meta) musí vzniknout a být neprázdný" );

    /* Klíčové tvrzení gapu: delta čítače = initial_state + meta. Bez fixu
     * by delta postrádala sz_initial (raw fwrite necountoval). */
    TEST_ASSERT_EQUAL_UINT64_MESSAGE (
        (uint64_t) ( sz_initial + sz_meta ),
        after - before,
        "disk byte backstop musí započítat initial-state dump i meta hwlogu" );

    hwlog_stop ( );
    g_hwlog_active = 0;
}


/* ================================================================
 * MAIN
 * ================================================================ */

int main ( int argc, char *argv[] )
{
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    RUN_TEST ( test_hwlog_start_stop );

    /* Per-event */
    RUN_TEST ( test_hwlog_record_full_payload );
    RUN_TEST ( test_hwlog_record_byte_helper );
    RUN_TEST ( test_hwlog_record_2byte_helper );
    RUN_TEST ( test_hwlog_record_null_payload );

    /* GDG cluster */
    RUN_TEST ( test_hwlog_gdg_mode_event );
    RUN_TEST ( test_hwlog_gdg_border_event );
    RUN_TEST ( test_hwlog_gdg_palette_palgrp_pal );
    RUN_TEST ( test_hwlog_gdg_hwscroll_event );

    /* Enum reservations */
    RUN_TEST ( test_hwlog_chip_enum_reservations );
    RUN_TEST ( test_hwlog_gdg_colors_sub_enum );
    RUN_TEST ( test_hwlog_psg_sub_enum );
    RUN_TEST ( test_hwlog_memext_sub_enum );
    RUN_TEST ( test_hwlog_ctc8253_sub_enum );
    RUN_TEST ( test_hwlog_pio8255_sub_enum );
    RUN_TEST ( test_hwlog_gdg_banking_sub_enum );
    RUN_TEST ( test_hwlog_gdg_video_sub_enum );
    RUN_TEST ( test_hwlog_hs_decimation_zero_suppresses_emit );
    RUN_TEST ( test_hwlog_hs_decimation_emits_every_nth );
    RUN_TEST ( test_hwlog_hs_decimation_emits_first_event_when_n_set );
    RUN_TEST ( test_hwlog_qd_fdc_rd_sub_enum );

    /* PSG */
    RUN_TEST ( test_hwlog_psg_event_format );

    /* MEMEXT */
    RUN_TEST ( test_hwlog_memext_event_format );

    /* CTC8253 */
    RUN_TEST ( test_hwlog_ctc8253_event_format );

    /* PIO8255 */
    RUN_TEST ( test_hwlog_pio8255_event_format );

    /* Meta */
    RUN_TEST ( test_hwlog_meta_subsys_header );
    RUN_TEST ( test_hwlog_meta_psg_dividers );

    /* Initial state */
    RUN_TEST ( test_hwlog_initial_state_file_created );
    RUN_TEST ( test_hwlog_initial_state_field_layout );
    RUN_TEST ( test_hwlog_initial_state_contains_pioz80_tlv );
    RUN_TEST ( test_hwlog_initial_state_counted_in_disk_backstop );

    /* PIOZ80 */
    RUN_TEST ( test_hwlog_pioz80_sub_enum_reservations );
    RUN_TEST ( test_hwlog_pioz80_event_format );

    /* Inactive */
    RUN_TEST ( test_hwlog_record_when_not_active_is_noop );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
