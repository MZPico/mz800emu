/*
 * test_intlog.c - unit testy pro trace-suite Interrupt Log
 *
 * Testuje:
 *  - lifecycle
 *  - per-event 24 B layout (pxclk + class + chip + pin + edge + state_bits)
 *  - intlog_record_pin_edge - emit eventu s edge info
 *  - intlog_record_cpu_int_state - emit s INTLOG_CHIP_CPU + bity
 *  - intlog_record_pio_state - emit s INTLOG_CHIP_PIOZ80
 *  - intlog_record_irq_ack_im2 - encoded payload (vector_table_addr | isr_addr<<16)
 *  - intlog_poll_interrupt_state - rising/falling detekce z bitmasky
 *  - per-pin enum hodnoty rezervované pro PIOZ80@PA0..PA7, PB0..PB7
 *  - meta.json subsys_header (event_record_size = 24, initial_interrupt_bus)
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "debugger/trace/intlog.h"
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
    s_test_dir = g_dir_make_tmp ( "mztest-intlog-XXXXXX", &err );
    if ( !s_test_dir ) {
        if ( err ) g_error_free ( err );
        TEST_FAIL_MESSAGE ( "Nepodařilo se vytvořit tmpdir" );
        return;
    }

    g_free ( g_intlog_config.dir );
    g_free ( g_intlog_config.name );
    g_intlog_config.dir = g_strdup ( s_test_dir );
    g_intlog_config.name = g_strdup ( "intl" );
    g_intlog_config.chunk_mb = 1;
    g_intlog_config.max_total_mb = 0;
    g_intlog_config.save_on_exit = 1;
    g_intlog_config.mode = TLOG_MODE_OFF;
    g_intlog_active = 0;
}


void tearDown ( void )
{
    if ( g_intlog_active ) {
        intlog_stop ( );
        g_intlog_active = 0;
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
    uint8_t  event_class;
    uint8_t  source_chip;
    uint8_t  source_pin;
    uint8_t  edge;
    uint32_t state_bits;
} parsed_int_t;


static void parse_int ( const uint8_t *buf, parsed_int_t *e )
{
    e->pxclk_total = 0;
    for ( int i = 0; i < 8; i++ ) e->pxclk_total |= (uint64_t) buf[ i ] << ( i * 8 );
    e->screens = 0;
    for ( int i = 0; i < 4; i++ ) e->screens |= (uint32_t) buf[ 8 + i ] << ( i * 8 );
    e->pxclk_in_screen = 0;
    for ( int i = 0; i < 4; i++ ) e->pxclk_in_screen |= (uint32_t) buf[ 12 + i ] << ( i * 8 );
    e->event_class = buf[ 16 ];
    e->source_chip = buf[ 17 ];
    e->source_pin = buf[ 18 ];
    e->edge = buf[ 19 ];
    e->state_bits = 0;
    for ( int i = 0; i < 4; i++ ) e->state_bits |= (uint32_t) buf[ 20 + i ] << ( i * 8 );
}


static int activate_intlog ( void )
{
    int rc = intlog_start ( );
    if ( rc == 0 ) g_intlog_active = 1;
    return rc;
}


/* ================================================================
 * Lifecycle
 * ================================================================ */

void test_intlog_start_stop ( void )
{
    TEST_ASSERT_EQUAL_INT ( 0, activate_intlog ( ) );
    intlog_stop ( );
    g_intlog_active = 0;

    char *p = g_build_filename ( s_test_dir, "intl.json", NULL );
    TEST_ASSERT_TRUE ( g_file_test ( p, G_FILE_TEST_IS_REGULAR ) );
    g_free ( p );
}


/* ================================================================
 * Pin edge events
 * ================================================================ */

void test_intlog_pin_edge_rising ( void )
{
    activate_intlog ( );
    intlog_record_pin_edge ( INTLOG_CHIP_CTC2, 0, INTLOG_EDGE_RISING );
    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 24, fsize );

    parsed_int_t e;
    parse_int ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EVENT_PIN_EDGE, e.event_class );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_CHIP_CTC2, e.source_chip );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EDGE_RISING, e.edge );
    TEST_ASSERT_EQUAL_HEX8 ( 0, e.source_pin );
    g_free ( buf );
}


void test_intlog_pin_edge_falling ( void )
{
    activate_intlog ( );
    intlog_record_pin_edge ( INTLOG_CHIP_PIOZ80, 0, INTLOG_EDGE_FALLING );
    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_int_t e;
    parse_int ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EDGE_FALLING, e.edge );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_CHIP_PIOZ80, e.source_chip );
    g_free ( buf );
}


/* ================================================================
 * CPU INT state events
 * ================================================================ */

void test_intlog_cpu_int_state_ei ( void )
{
    activate_intlog ( );

    /* EI event s IM=2, IFF1=1, IFF2=1. */
    uint32_t state = INTLOG_STATE_BIT_EI | INTLOG_STATE_BIT_IM2
                   | INTLOG_STATE_BIT_IFF1 | INTLOG_STATE_BIT_IFF2;
    intlog_record_cpu_int_state ( state );

    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_int_t e;
    parse_int ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( INTLOG_EVENT_CPU_INT_STATE, e.event_class,
                                     "event_class = CPU_INT_STATE" );
    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( INTLOG_CHIP_CPU, e.source_chip,
                                     "source_chip = CPU" );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EDGE_NONE, e.edge );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE ( state, e.state_bits,
                                      "state_bits musí být zachované" );

    /* Bit-level kontrola */
    TEST_ASSERT_TRUE ( e.state_bits & INTLOG_STATE_BIT_EI );
    TEST_ASSERT_TRUE ( e.state_bits & INTLOG_STATE_BIT_IM2 );
    TEST_ASSERT_TRUE ( e.state_bits & INTLOG_STATE_BIT_IFF1 );
    TEST_ASSERT_TRUE ( e.state_bits & INTLOG_STATE_BIT_IFF2 );

    g_free ( buf );
}


void test_intlog_cpu_int_state_reti ( void )
{
    activate_intlog ( );
    intlog_record_cpu_int_state ( INTLOG_STATE_BIT_RETI );
    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_int_t e;
    parse_int ( buf, &e );
    TEST_ASSERT_TRUE_MESSAGE ( e.state_bits & INTLOG_STATE_BIT_RETI,
                               "RETI bit musí být v state_bits" );
    g_free ( buf );
}


/* ================================================================
 * PIO state events
 * ================================================================ */

void test_intlog_pio_state ( void )
{
    activate_intlog ( );
    intlog_record_pio_state ( INTLOG_STATE_BIT_PIO_READY | INTLOG_STATE_BIT_PIO_ARMED );
    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );

    parsed_int_t e;
    parse_int ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EVENT_PIO_STATE, e.event_class );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_CHIP_PIOZ80, e.source_chip );
    TEST_ASSERT_TRUE ( e.state_bits & INTLOG_STATE_BIT_PIO_READY );
    TEST_ASSERT_TRUE ( e.state_bits & INTLOG_STATE_BIT_PIO_ARMED );

    g_free ( buf );
}


/* ================================================================
 * Polling interrupt state - rising/falling detekce
 * ================================================================ */

void test_intlog_poll_no_change_no_event ( void )
{
    activate_intlog ( );

    /* Stejný stav 0 -> 0: žádná změna, žádný event. */
    intlog_poll_interrupt_state ( 0 );
    intlog_stop ( );
    g_intlog_active = 0;

    char *p = g_build_filename ( s_test_dir, "intl.000.bin", NULL );
    /* Pokud žádný event, soubor nemusí vzniknout (writer.buffer_used=0 = no flush). */
    if ( g_file_test ( p, G_FILE_TEST_IS_REGULAR ) ) {
        size_t fsize = 0;
        uint8_t *b = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
        TEST_ASSERT_EQUAL_size_t_MESSAGE ( 0, fsize,
                                           "Bez change polling nesmí emitovat" );
        g_free ( b );
    }
    g_free ( p );
}


void test_intlog_poll_ctc2_rising ( void )
{
    activate_intlog ( );

    /* 0 -> bit 0 set: CTC2 rising. */
    intlog_poll_interrupt_state ( 0x01 );

    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 24, fsize );

    parsed_int_t e;
    parse_int ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_CHIP_CTC2, e.source_chip );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EDGE_RISING, e.edge );

    g_free ( buf );
}


void test_intlog_poll_multiple_pins_change ( void )
{
    activate_intlog ( );

    /* 0 -> 0x07: CTC2 + PIOZ80 + FDC simultaneously rising. */
    intlog_poll_interrupt_state ( 0x07 );

    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t_MESSAGE ( 3 * 24, fsize,
                                       "3 simultánní pin changes = 3 events" );

    /* Ověř že každý chip má 1 event s RISING. */
    int found_ctc = 0, found_pio = 0, found_fdc = 0;
    for ( int i = 0; i < 3; i++ ) {
        parsed_int_t e;
        parse_int ( buf + i * 24, &e );
        TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EDGE_RISING, e.edge );
        if ( e.source_chip == INTLOG_CHIP_CTC2 ) found_ctc = 1;
        else if ( e.source_chip == INTLOG_CHIP_PIOZ80 ) found_pio = 1;
        else if ( e.source_chip == INTLOG_CHIP_FDC ) found_fdc = 1;
    }
    TEST_ASSERT_TRUE_MESSAGE ( found_ctc, "CTC2 pin event" );
    TEST_ASSERT_TRUE_MESSAGE ( found_pio, "PIOZ80 pin event" );
    TEST_ASSERT_TRUE_MESSAGE ( found_fdc, "FDC pin event" );

    g_free ( buf );
}


void test_intlog_poll_falling ( void )
{
    activate_intlog ( );

    /* 0 -> 0x01 (rising), pak 0x01 -> 0 (falling). */
    intlog_poll_interrupt_state ( 0x01 );
    intlog_poll_interrupt_state ( 0x00 );

    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 2 * 24, fsize );

    parsed_int_t e0, e1;
    parse_int ( buf, &e0 );
    parse_int ( buf + 24, &e1 );
    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( INTLOG_EDGE_RISING, e0.edge,
                                     "0->1 = RISING" );
    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( INTLOG_EDGE_FALLING, e1.edge,
                                     "1->0 = FALLING" );

    g_free ( buf );
}


/* ================================================================
 * Per-pin enum hodnoty rezervované
 * ================================================================ */

void test_intlog_chip_enum_reservations ( void )
{
    /* Per PLAN: PIOZ80@PA[0..7] = 3..10, PB[0..7] = 11..18.
     * Po Fáze 3 follow-up jsou všechny hodnoty definované jako enum members,
     * ne jen rezervované v komentáři. */
    TEST_ASSERT_EQUAL_INT ( 0,  INTLOG_CHIP_NONE );
    TEST_ASSERT_EQUAL_INT ( 1,  INTLOG_CHIP_CTC2 );
    TEST_ASSERT_EQUAL_INT ( 2,  INTLOG_CHIP_PIOZ80 );
    TEST_ASSERT_EQUAL_INT ( 3,  INTLOG_CHIP_PIOZ80_PA0 );
    TEST_ASSERT_EQUAL_INT ( 4,  INTLOG_CHIP_PIOZ80_PA1 );
    TEST_ASSERT_EQUAL_INT ( 5,  INTLOG_CHIP_PIOZ80_PA2 );
    TEST_ASSERT_EQUAL_INT ( 6,  INTLOG_CHIP_PIOZ80_PA3 );
    TEST_ASSERT_EQUAL_INT ( 7,  INTLOG_CHIP_PIOZ80_PA4 );
    TEST_ASSERT_EQUAL_INT ( 8,  INTLOG_CHIP_PIOZ80_PA5 );
    TEST_ASSERT_EQUAL_INT ( 9,  INTLOG_CHIP_PIOZ80_PA6 );
    TEST_ASSERT_EQUAL_INT ( 10, INTLOG_CHIP_PIOZ80_PA7 );
    TEST_ASSERT_EQUAL_INT ( 11, INTLOG_CHIP_PIOZ80_PB0 );
    TEST_ASSERT_EQUAL_INT ( 12, INTLOG_CHIP_PIOZ80_PB1 );
    TEST_ASSERT_EQUAL_INT ( 13, INTLOG_CHIP_PIOZ80_PB2 );
    TEST_ASSERT_EQUAL_INT ( 14, INTLOG_CHIP_PIOZ80_PB3 );
    TEST_ASSERT_EQUAL_INT ( 15, INTLOG_CHIP_PIOZ80_PB4 );
    TEST_ASSERT_EQUAL_INT ( 16, INTLOG_CHIP_PIOZ80_PB5 );
    TEST_ASSERT_EQUAL_INT ( 17, INTLOG_CHIP_PIOZ80_PB6 );
    TEST_ASSERT_EQUAL_INT ( 18, INTLOG_CHIP_PIOZ80_PB7 );
    TEST_ASSERT_EQUAL_INT ( 19, INTLOG_CHIP_FDC );
    TEST_ASSERT_EQUAL_INT ( 20, INTLOG_CHIP_CPU );
    TEST_ASSERT_EQUAL_INT ( 21, INTLOG_CHIP_PIOZ80_PORT_A );
    TEST_ASSERT_EQUAL_INT ( 22, INTLOG_CHIP_PIOZ80_PORT_B );
    TEST_ASSERT_EQUAL_INT ( 23, INTLOG_CHIP_VECTOR_BUS_LATCH );

    TEST_ASSERT_EQUAL_INT ( 0, INTLOG_EVENT_PIN_EDGE );
    TEST_ASSERT_EQUAL_INT ( 1, INTLOG_EVENT_CPU_INT_STATE );
    TEST_ASSERT_EQUAL_INT ( 2, INTLOG_EVENT_PIO_STATE );
    TEST_ASSERT_EQUAL_INT ( 3, INTLOG_EVENT_IRQ_ACK_IM2 );

    TEST_ASSERT_EQUAL_INT ( 0, INTLOG_EDGE_NONE );
    TEST_ASSERT_EQUAL_INT ( 1, INTLOG_EDGE_RISING );
    TEST_ASSERT_EQUAL_INT ( 2, INTLOG_EDGE_FALLING );
}


/* ================================================================
 * Per-pin PIOZ80 detekce - emit přes API (poll vyžaduje real g_pioz80
 * state, takže testujeme jen direct emit funkcí intlog_record_pin_edge
 * pro každou PA/PB pin pozici).
 * ================================================================ */

void test_intlog_pioz80_per_pin_emit ( void )
{
    activate_intlog ( );

    /* Emit eventy pro všech 16 pinů PA[0..7] + PB[0..7] - simulace
     * polling layeru pro ověření, že každý pin má unique source_chip. */
    for ( uint8_t i = 0; i < 8; i++ ) {
        intlog_record_pin_edge ( (en_INTLOG_SOURCE_CHIP)( INTLOG_CHIP_PIOZ80_PA0 + i ),
                                 i, INTLOG_EDGE_RISING );
    }
    for ( uint8_t i = 0; i < 8; i++ ) {
        intlog_record_pin_edge ( (en_INTLOG_SOURCE_CHIP)( INTLOG_CHIP_PIOZ80_PB0 + i ),
                                 i, INTLOG_EDGE_FALLING );
    }

    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t_MESSAGE ( 16 * 24, fsize, "16 PIOZ80 pinů = 16 events" );

    /* Ověř že each pin má svůj unique source_chip + edge. */
    for ( int i = 0; i < 8; i++ ) {
        parsed_int_t e;
        parse_int ( buf + i * 24, &e );
        TEST_ASSERT_EQUAL_HEX8 ( INTLOG_CHIP_PIOZ80_PA0 + i, e.source_chip );
        TEST_ASSERT_EQUAL_HEX8 ( i, e.source_pin );
        TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EDGE_RISING, e.edge );
    }
    for ( int i = 0; i < 8; i++ ) {
        parsed_int_t e;
        parse_int ( buf + ( 8 + i ) * 24, &e );
        TEST_ASSERT_EQUAL_HEX8 ( INTLOG_CHIP_PIOZ80_PB0 + i, e.source_chip );
        TEST_ASSERT_EQUAL_HEX8 ( i, e.source_pin );
        TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EDGE_FALLING, e.edge );
    }

    g_free ( buf );
}


/* ================================================================
 * IRQ ack IM 2 events
 * ================================================================ */

void test_intlog_irq_ack_im2_event_format ( void )
{
    activate_intlog ( );

    /* Mock vector_table_addr = 0xFE34 (= I=0xFE, vector=0x34),
     * isr_addr = 0xABCD (16-bit ISR adresa nahrávaná do PC). */
    uint16_t vector_table_addr = 0xFE34;
    uint16_t isr_addr = 0xABCD;
    intlog_record_irq_ack_im2 ( INTLOG_CHIP_PIOZ80_PORT_A,
                                vector_table_addr, isr_addr );

    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( 24, fsize );

    parsed_int_t e;
    parse_int ( buf, &e );
    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( INTLOG_EVENT_IRQ_ACK_IM2, e.event_class,
                                     "event_class = IRQ_ACK_IM2" );
    TEST_ASSERT_EQUAL_HEX8_MESSAGE ( INTLOG_CHIP_PIOZ80_PORT_A, e.source_chip,
                                     "source_chip = PIOZ80_PORT_A" );
    TEST_ASSERT_EQUAL_HEX8 ( 0, e.source_pin );
    TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EDGE_NONE, e.edge );

    /* Encoded payload: low 16 bit = vector_table_addr, high 16 bit = isr_addr. */
    uint32_t expected = (uint32_t) vector_table_addr | ( (uint32_t) isr_addr << 16 );
    TEST_ASSERT_EQUAL_HEX32_MESSAGE ( expected, e.state_bits,
                                      "encoded payload = vector_table_addr | (isr_addr << 16)" );

    g_free ( buf );
}


void test_intlog_irq_ack_im2_decode_roundtrip ( void )
{
    activate_intlog ( );

    /* 4 různé hodnoty - boundary values. */
    struct { uint16_t vta; uint16_t isr; en_INTLOG_SOURCE_CHIP src; } cases[] = {
        { 0x0000, 0x0000, INTLOG_CHIP_PIOZ80_PORT_A },
        { 0xFFFE, 0xFFFF, INTLOG_CHIP_PIOZ80_PORT_B },
        { 0x1234, 0x5678, INTLOG_CHIP_PIOZ80_PORT_A },
        { 0xDEAD, 0xBEEF, INTLOG_CHIP_PIOZ80_PORT_B },
    };
    int N = (int) ( sizeof ( cases ) / sizeof ( cases[ 0 ] ) );

    for ( int i = 0; i < N; i++ ) {
        intlog_record_irq_ack_im2 ( cases[ i ].src, cases[ i ].vta, cases[ i ].isr );
    }

    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.000.bin", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    TEST_ASSERT_EQUAL_size_t ( (size_t) ( N * 24 ), fsize );

    for ( int i = 0; i < N; i++ ) {
        parsed_int_t e;
        parse_int ( buf + i * 24, &e );
        TEST_ASSERT_EQUAL_HEX8 ( INTLOG_EVENT_IRQ_ACK_IM2, e.event_class );
        TEST_ASSERT_EQUAL_HEX8 ( cases[ i ].src, e.source_chip );

        /* Decode přes shift/mask, hodnoty match. */
        uint16_t decoded_vta = (uint16_t) ( e.state_bits & 0xFFFF );
        uint16_t decoded_isr = (uint16_t) ( ( e.state_bits >> 16 ) & 0xFFFF );
        TEST_ASSERT_EQUAL_HEX16_MESSAGE ( cases[ i ].vta, decoded_vta,
                                          "decoded vector_table_addr roundtrip" );
        TEST_ASSERT_EQUAL_HEX16_MESSAGE ( cases[ i ].isr, decoded_isr,
                                          "decoded isr_addr roundtrip" );
    }

    g_free ( buf );
}


/* ================================================================
 * meta.json
 * ================================================================ */

void test_intlog_meta_subsys_header ( void )
{
    activate_intlog ( );
    intlog_stop ( );
    g_intlog_active = 0;

    size_t fsize = 0;
    uint8_t *buf = read_whole_file ( s_test_dir, "intl.json", &fsize );
    TEST_ASSERT_NOT_NULL ( buf );
    const char *s = (const char *) buf;

    TEST_ASSERT_TRUE_MESSAGE ( json_kv_present ( s, "subsys", "\"intlog\"" ),
                                   "subsys = intlog" );
    TEST_ASSERT_TRUE_MESSAGE ( json_kv_present ( s, "event_record_size", "24" ),
                                   "event_record_size = 24" );
    TEST_ASSERT_TRUE_MESSAGE ( json_key_present ( s, "initial_interrupt_bus" ),
                                   "initial_interrupt_bus musí být v subsys_header" );

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

    RUN_TEST ( test_intlog_start_stop );

    /* Pin edge */
    RUN_TEST ( test_intlog_pin_edge_rising );
    RUN_TEST ( test_intlog_pin_edge_falling );

    /* CPU INT state */
    RUN_TEST ( test_intlog_cpu_int_state_ei );
    RUN_TEST ( test_intlog_cpu_int_state_reti );

    /* PIO state */
    RUN_TEST ( test_intlog_pio_state );

    /* Polling */
    RUN_TEST ( test_intlog_poll_no_change_no_event );
    RUN_TEST ( test_intlog_poll_ctc2_rising );
    RUN_TEST ( test_intlog_poll_multiple_pins_change );
    RUN_TEST ( test_intlog_poll_falling );

    /* Enum hodnoty */
    RUN_TEST ( test_intlog_chip_enum_reservations );

    /* Per-pin PIOZ80 emit */
    RUN_TEST ( test_intlog_pioz80_per_pin_emit );

    /* IRQ ack IM 2 */
    RUN_TEST ( test_intlog_irq_ack_im2_event_format );
    RUN_TEST ( test_intlog_irq_ack_im2_decode_roundtrip );

    /* meta.json */
    RUN_TEST ( test_intlog_meta_subsys_header );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
