/*
 * test_io_activity.c - unit testy pro IO ports activity tracker (V1.5).
 *
 * Testuje:
 *  - init vynuluje vsechny porty
 *  - record_hit inkrementuje hits_per_frame[current_bucket] + total_hits
 *  - get_hits_per_sec sčítá posledních IO_ACTIVITY_WINDOW_BUCKETS
 *    bucketu (= sliding window ~1s)
 *  - advance_frame rotuje current_bucket modulo 60 + nuluje novy bucket
 *  - reset_port vynuluje jeden port (= ostatní zachovány)
 *  - reset_all vynuluje všech portů (16-bit prostor)
 *  - tracking flag default OFF
 *  - tracking flag respect: advance_frame neudělá nic při OFF
 *  - V1.5 fix #1: 16-bit indexing - 0xCF01 vs 0xCF06 separate counters
 *  - V1.5 fix #1: 8-bit aggregating helpers
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "debugger/io_activity.h"


void setUp ( void )
{
    /* Vždy fresh state - vynuluj a tracking ON pro většinu testů. */
    io_activity_init ( );
    g_io_window_tracking_active = 1;
}


void tearDown ( void )
{
    g_io_window_tracking_active = 0;
}


/* ================================================================
 * Init
 * ================================================================ */

void test_io_activity_init_clears_all ( void )
{
    /* Manualne polni nejake hodnoty (V1.5.D fix #2: per smer). */
    g_io_activity[ 0xCE ].total_hits_in = 12345;
    g_io_activity[ 0xCE ].total_hits_out = 7890;
    g_io_activity[ 0x42 ].hits_per_frame_in[ 5 ] = 99;
    g_io_activity[ 0x42 ].hits_per_frame_out[ 5 ] = 77;

    io_activity_init ( );

    for ( int p = 0; p < 256; p++ ) {
        TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ p ].total_hits_in );
        TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ p ].total_hits_out );
        TEST_ASSERT_EQUAL_UINT8 ( 0, g_io_activity[ p ].current_bucket );
        for ( int b = 0; b < IO_ACTIVITY_BUCKET_COUNT; b++ ) {
            TEST_ASSERT_EQUAL_UINT16 ( 0, g_io_activity[ p ].hits_per_frame_in[ b ] );
            TEST_ASSERT_EQUAL_UINT16 ( 0, g_io_activity[ p ].hits_per_frame_out[ b ] );
        }
    }
    TEST_ASSERT_EQUAL_UINT8 ( 0, g_io_window_tracking_active );
}


/* ================================================================
 * record_hit
 * ================================================================ */

void test_io_activity_record_hit_basic ( void )
{
    io_activity_record_hit ( 0xCE, 0x11, false /* OUT */, 0 );
    io_activity_record_hit ( 0xCE, 0x22, false, 0 );
    io_activity_record_hit ( 0xCE, 0x33, false, 0 );

    /* V1.5.D fix #2: OUT inkrementuje total_hits_out a hits_per_frame_out. */
    TEST_ASSERT_EQUAL_UINT64 ( 3, g_io_activity[ 0xCE ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0xCE ].total_hits_in );
    TEST_ASSERT_EQUAL_UINT16 ( 3, g_io_activity[ 0xCE ].hits_per_frame_out[ 0 ] );
    TEST_ASSERT_EQUAL_UINT16 ( 0, g_io_activity[ 0xCE ].hits_per_frame_in[ 0 ] );

    /* Ostatní porty zůstávají 0. */
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0xCD ].total_hits_out );
}


void test_io_activity_record_100_hits ( void )
{
    for ( int i = 0; i < 100; i++ ) {
        io_activity_record_hit ( 0x42, (uint8_t) i, false, 0 );
    }
    TEST_ASSERT_EQUAL_UINT64 ( 100, g_io_activity[ 0x42 ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0x42 ].total_hits_in );
}


/* ================================================================
 * advance_frame
 * ================================================================ */

void test_io_activity_advance_frame_rotates_bucket ( void )
{
    /* Po 1 frame = current_bucket == 1 */
    io_activity_advance_frame ( );
    TEST_ASSERT_EQUAL_UINT8 ( 1, g_io_activity[ 0xCE ].current_bucket );

    /* Po 60 frames = wrap zpet na 0 (= 60 modulo 60 = 0). Uz jsme udelali
     * 1, takze potrebujeme 59 dalsich. */
    for ( int i = 0; i < 59; i++ ) io_activity_advance_frame ( );
    TEST_ASSERT_EQUAL_UINT8 ( 0, g_io_activity[ 0xCE ].current_bucket );
}


void test_io_activity_advance_frame_clears_new_bucket ( void )
{
    /* Naplň aktuální bucket 5 hity (OUT smer, V1.5.D fix #2). */
    for ( int i = 0; i < 5; i++ ) io_activity_record_hit ( 0xCE, 0, false, 0 );
    TEST_ASSERT_EQUAL_UINT16 ( 5, g_io_activity[ 0xCE ].hits_per_frame_out[ 0 ] );

    /* Advance - bucket 1 musi byt 0 v obou smerech. */
    io_activity_advance_frame ( );
    TEST_ASSERT_EQUAL_UINT16 ( 0, g_io_activity[ 0xCE ].hits_per_frame_out[ 1 ] );
    TEST_ASSERT_EQUAL_UINT16 ( 0, g_io_activity[ 0xCE ].hits_per_frame_in[ 1 ] );
    /* Bucket 0 zachovan */
    TEST_ASSERT_EQUAL_UINT16 ( 5, g_io_activity[ 0xCE ].hits_per_frame_out[ 0 ] );
}


void test_io_activity_advance_frame_skip_when_disabled ( void )
{
    g_io_window_tracking_active = 0;
    io_activity_advance_frame ( );
    /* Bucket nezmenen */
    TEST_ASSERT_EQUAL_UINT8 ( 0, g_io_activity[ 0xCE ].current_bucket );
}


/* ================================================================
 * get_hits_per_sec - sliding window
 * ================================================================ */

void test_io_activity_hits_per_sec_simple ( void )
{
    /* 10 OUT hitů ve frame 0, advance, 20 OUT hitů ve frame 1, advance.
     * get_hits_per_sec(OUT) po 2 advance vidí bucket 0=10, bucket 1=20,
     * window 50 buckets ale jen 2 mají hodnotu = sum 30. */
    for ( int i = 0; i < 10; i++ ) io_activity_record_hit ( 0xCE, 0, false, 0 );
    io_activity_advance_frame ( );
    for ( int i = 0; i < 20; i++ ) io_activity_record_hit ( 0xCE, 0, false, 1 );
    io_activity_advance_frame ( );
    /* Aktualni bucket = 2, sumujeme posledni 50 (= bucket 1, 0, 59, 58, ...).
     * Plne hodnoty jsou jen v 0 (10) a 1 (20) = 30. */
    uint32_t hps = io_activity_get_hits_per_sec ( 0xCE, false /* OUT */ );
    TEST_ASSERT_EQUAL_UINT32 ( 30, hps );
    /* IN counter je nedotcen. */
    TEST_ASSERT_EQUAL_UINT32 ( 0, io_activity_get_hits_per_sec ( 0xCE, true ) );
}


void test_io_activity_hits_per_sec_excludes_current_bucket ( void )
{
    /* Hit bez advance = hodnota je v current_bucket = 0.
     * sliding window suma exkluduje current_bucket = vraci 0. */
    for ( int i = 0; i < 100; i++ ) io_activity_record_hit ( 0xCE, 0, false, 0 );
    uint32_t hps = io_activity_get_hits_per_sec ( 0xCE, false );
    TEST_ASSERT_EQUAL_UINT32 ( 0, hps );

    /* Po jednom advance current_bucket=1, sliding window vidi bucket 0. */
    io_activity_advance_frame ( );
    hps = io_activity_get_hits_per_sec ( 0xCE, false );
    TEST_ASSERT_EQUAL_UINT32 ( 100, hps );
}


/* ================================================================
 * Reset
 * ================================================================ */

void test_io_activity_reset_port_isolates ( void )
{
    io_activity_record_hit ( 0xCE, 0, false, 0 );
    io_activity_record_hit ( 0xCE, 0, false, 0 );
    io_activity_record_hit ( 0xF0, 0, false, 0 );

    io_activity_reset_port ( 0xCE );

    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0xCE ].total_hits_out );
    /* 0xF0 zachovan */
    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0xF0 ].total_hits_out );
}


void test_io_activity_reset_all ( void )
{
    io_activity_record_hit ( 0xCE, 0, false, 0 );
    io_activity_record_hit ( 0xF0, 0, false, 0 );
    io_activity_record_hit ( 0x42, 0, false, 0 );

    io_activity_reset_all ( );

    for ( int p = 0; p < 256; p++ ) {
        TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ p ].total_hits_in );
        TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ p ].total_hits_out );
    }
}


/* ================================================================
 * Last value cache (fix #10)
 * ================================================================ */

void test_io_activity_last_value_no_data ( void )
{
    /* Po init nemame zadny zaznam - get vraci false. */
    uint8_t v;
    uint32_t age;
    bool ok = io_activity_get_last_value ( 0xCE, false, 100, &v, &age );
    TEST_ASSERT_FALSE ( ok );
}


void test_io_activity_last_value_write_cache ( void )
{
    /* OUT 0xCE = 0x42 ve frame 50. */
    io_activity_record_hit ( 0xCE, 0x42, false /* OUT */, 50 );

    uint8_t v = 0;
    uint32_t age = 0xFFFFFFFFu;
    bool ok = io_activity_get_last_value ( 0xCE, false, 100, &v, &age );
    TEST_ASSERT_TRUE ( ok );
    TEST_ASSERT_EQUAL_HEX8 ( 0x42, v );
    TEST_ASSERT_EQUAL_UINT32 ( 50, age );  /* 100 - 50 = 50 frames */

    /* Read cache zustava prazdny (= jine smery oddelene). */
    ok = io_activity_get_last_value ( 0xCE, true /* IN */, 100, &v, &age );
    TEST_ASSERT_FALSE ( ok );
}


void test_io_activity_last_value_read_cache_distinct ( void )
{
    /* Mix: OUT + IN na stejnem portu - kazda cache zvlast. */
    io_activity_record_hit ( 0xCE, 0xAA, false, 10 );  /* OUT 0xAA @ 10 */
    io_activity_record_hit ( 0xCE, 0xBB, true,  20 );  /* IN  0xBB @ 20 */

    uint8_t v;
    uint32_t age;
    TEST_ASSERT_TRUE ( io_activity_get_last_value ( 0xCE, false, 100, &v, &age ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAA, v );
    TEST_ASSERT_EQUAL_UINT32 ( 90, age );

    TEST_ASSERT_TRUE ( io_activity_get_last_value ( 0xCE, true, 100, &v, &age ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0xBB, v );
    TEST_ASSERT_EQUAL_UINT32 ( 80, age );
}


void test_io_activity_last_value_frame_zero ( void )
{
    /* Frame 0 (= start emu) je validni - inkrement +1 sentinel zaruci
     * rozliseni "no data" vs "first hit at 0". */
    io_activity_record_hit ( 0xCE, 0x55, false, 0 );

    uint8_t v;
    uint32_t age;
    bool ok = io_activity_get_last_value ( 0xCE, false, 0, &v, &age );
    TEST_ASSERT_TRUE ( ok );
    TEST_ASSERT_EQUAL_HEX8 ( 0x55, v );
    TEST_ASSERT_EQUAL_UINT32 ( 0, age );
}


void test_io_activity_last_value_overwrite ( void )
{
    /* Vícero zápisů - cache vždy nejnovější. */
    io_activity_record_hit ( 0xCE, 0x11, false, 1 );
    io_activity_record_hit ( 0xCE, 0x22, false, 2 );
    io_activity_record_hit ( 0xCE, 0x33, false, 3 );

    uint8_t v;
    uint32_t age;
    TEST_ASSERT_TRUE ( io_activity_get_last_value ( 0xCE, false, 10, &v, &age ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0x33, v );
    TEST_ASSERT_EQUAL_UINT32 ( 7, age );  /* 10 - 3 */
}


/* ================================================================
 * Tracking flag default OFF (Michal rozhodnuti #4)
 * ================================================================ */

void test_io_activity_tracking_flag_default_off_after_init ( void )
{
    io_activity_init ( );
    TEST_ASSERT_EQUAL_UINT8 ( 0, g_io_window_tracking_active );
}


/* ================================================================
 * V1.5 fix #1: 16-bit indexing
 * ================================================================ */

void test_io_activity_16bit_separate_counters ( void )
{
    /* Unicard 0xCF01 (bus 0x01CF) vs 0xCF06 (bus 0x06CF) - dříve sdílely
     * 8-bit slot 0xCF, nyní mají oddělené 16-bit counters. */
    io_activity_record_hit ( 0x01CF, 0x11, false, 0 );
    io_activity_record_hit ( 0x01CF, 0x22, false, 0 );
    io_activity_record_hit ( 0x06CF, 0x99, false, 0 );

    TEST_ASSERT_EQUAL_UINT64 ( 2, g_io_activity[ 0x01CF ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0x06CF ].total_hits_out );
    /* Low byte 0xCF (slot 0x00CF) sám nedostává nic. */
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0x00CF ].total_hits_out );
}


void test_io_activity_8bit_8bit_distinct_from_16bit ( void )
{
    /* 8-bit port 0xCE (catalog 0x00CE) vs 16-bit 0xCFCE (= bus 0xCFCE).
     * High byte se liší, oba ve 16-bit indexingu. */
    io_activity_record_hit ( 0x00CE, 0x10, false, 0 );  /* B=0, C=0xCE */
    io_activity_record_hit ( 0xCFCE, 0x20, false, 0 );  /* B=0xCF, C=0xCE */

    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0x00CE ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0xCFCE ].total_hits_out );
}


void test_io_activity_8bit_aggregator ( void )
{
    /* Pro 8-bit port (low byte 0xCE) UI agreguje přes 256 high-byte slotů.
     * Hot path zaznamenává s různými B reg hodnotami. */
    io_activity_record_hit ( 0x00CE, 0x10, false, 0 );  /* B=0 */
    io_activity_record_hit ( 0x42CE, 0x20, false, 0 );  /* B=0x42 */
    io_activity_record_hit ( 0xFFCE, 0x30, false, 0 );  /* B=0xFF */

    /* Po jednom advance, sliding window vidi vsechny 3 hits (OUT smer). */
    io_activity_advance_frame ( );
    uint32_t agg = io_activity_get_hits_per_sec_8bit ( 0xCE, false /* OUT */ );
    TEST_ASSERT_EQUAL_UINT32 ( 3, agg );
    /* IN smer je 0. */
    TEST_ASSERT_EQUAL_UINT32 ( 0,
        io_activity_get_hits_per_sec_8bit ( 0xCE, true /* IN */ ) );

    /* Direct lookup na konkretni slot vraci jen 1. */
    uint32_t single = io_activity_get_hits_per_sec ( 0x00CE, false );
    TEST_ASSERT_EQUAL_UINT32 ( 1, single );
}


void test_io_activity_8bit_last_value_aggregator ( void )
{
    /* Aggregator vrací nejmladší cache napříč 256 high-byte sloty. */
    io_activity_record_hit ( 0x00CE, 0xAA, false, 10 );  /* B=0,    OUT 0xAA */
    io_activity_record_hit ( 0x42CE, 0xBB, false, 20 );  /* B=0x42, OUT 0xBB */
    io_activity_record_hit ( 0xFFCE, 0xCC, false, 30 );  /* B=0xFF, OUT 0xCC */

    uint8_t v = 0;
    uint32_t age = 0xFFFFFFFFu;
    bool ok = io_activity_get_last_value_8bit ( 0xCE, false, 100, &v, &age );
    TEST_ASSERT_TRUE ( ok );
    /* Nejmladší = age 70 (frame 100 - 30). */
    TEST_ASSERT_EQUAL_HEX8 ( 0xCC, v );
    TEST_ASSERT_EQUAL_UINT32 ( 70, age );
}


void test_io_activity_reset_port_8bit_clears_all_high ( void )
{
    /* reset_port_8bit musi vynulovat vsech 256 high-byte slotu pro low byte. */
    io_activity_record_hit ( 0x00CE, 0, false, 0 );
    io_activity_record_hit ( 0x42CE, 0, false, 0 );
    io_activity_record_hit ( 0xFFCE, 0, false, 0 );
    /* Jiny low byte zustava netknuty. */
    io_activity_record_hit ( 0x42CD, 0, false, 0 );

    io_activity_reset_port_8bit ( 0xCE );

    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0x00CE ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0x42CE ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0xFFCE ].total_hits_out );
    /* CD zachovan */
    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0x42CD ].total_hits_out );
}


/* ================================================================
 * V1.5.D fix #2: separate IN vs OUT counters
 * ================================================================ */

void test_io_activity_separate_in_out_counters ( void )
{
    /* Port 0xF0 ma R=JOY0 a W=GDG Palette - drive sdileli counter,
     * ted distinct per smer. */
    io_activity_record_hit ( 0xF0, 0xAA, false /* OUT palette */, 0 );
    io_activity_record_hit ( 0xF0, 0xBB, false, 0 );
    io_activity_record_hit ( 0xF0, 0xCC, false, 0 );
    io_activity_record_hit ( 0xF0, 0x11, true  /* IN JOY0 */, 0 );

    TEST_ASSERT_EQUAL_UINT64 ( 3, g_io_activity[ 0xF0 ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0xF0 ].total_hits_in );
    TEST_ASSERT_EQUAL_UINT16 ( 3, g_io_activity[ 0xF0 ].hits_per_frame_out[ 0 ] );
    TEST_ASSERT_EQUAL_UINT16 ( 1, g_io_activity[ 0xF0 ].hits_per_frame_in[ 0 ] );

    /* Po advance: get_hits_per_sec per smer vraci jen prislusny smer. */
    io_activity_advance_frame ( );
    uint32_t out_hps = io_activity_get_hits_per_sec ( 0xF0, false );
    uint32_t in_hps  = io_activity_get_hits_per_sec ( 0xF0, true );
    TEST_ASSERT_EQUAL_UINT32 ( 3, out_hps );
    TEST_ASSERT_EQUAL_UINT32 ( 1, in_hps );
}


void test_io_activity_get_last_value_per_direction ( void )
{
    /* OUT 0x42 a IN 0x99 na stejnem portu - oddelene cache.
     * Tento test je redundantni s test_io_activity_last_value_read_cache_distinct
     * ale validuje fix #2 explicitne (= dokumentuje pozadavek). */
    io_activity_record_hit ( 0xCE, 0x42, false /* OUT */, 50 );
    io_activity_record_hit ( 0xCE, 0x99, true  /* IN  */, 60 );

    uint8_t v;
    uint32_t age;
    /* OUT cache */
    TEST_ASSERT_TRUE ( io_activity_get_last_value ( 0xCE, false, 100, &v, &age ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0x42, v );
    TEST_ASSERT_EQUAL_UINT32 ( 50, age );
    /* IN cache - jine value */
    TEST_ASSERT_TRUE ( io_activity_get_last_value ( 0xCE, true, 100, &v, &age ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0x99, v );
    TEST_ASSERT_EQUAL_UINT32 ( 40, age );
}


/* ================================================================
 * Main
 * ================================================================ */

int main ( int argc, char *argv[] )
{
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    RUN_TEST ( test_io_activity_init_clears_all );
    RUN_TEST ( test_io_activity_record_hit_basic );
    RUN_TEST ( test_io_activity_record_100_hits );
    RUN_TEST ( test_io_activity_advance_frame_rotates_bucket );
    RUN_TEST ( test_io_activity_advance_frame_clears_new_bucket );
    RUN_TEST ( test_io_activity_advance_frame_skip_when_disabled );
    RUN_TEST ( test_io_activity_hits_per_sec_simple );
    RUN_TEST ( test_io_activity_hits_per_sec_excludes_current_bucket );
    RUN_TEST ( test_io_activity_reset_port_isolates );
    RUN_TEST ( test_io_activity_reset_all );
    RUN_TEST ( test_io_activity_last_value_no_data );
    RUN_TEST ( test_io_activity_last_value_write_cache );
    RUN_TEST ( test_io_activity_last_value_read_cache_distinct );
    RUN_TEST ( test_io_activity_last_value_frame_zero );
    RUN_TEST ( test_io_activity_last_value_overwrite );
    RUN_TEST ( test_io_activity_tracking_flag_default_off_after_init );
    RUN_TEST ( test_io_activity_16bit_separate_counters );
    RUN_TEST ( test_io_activity_8bit_8bit_distinct_from_16bit );
    RUN_TEST ( test_io_activity_8bit_aggregator );
    RUN_TEST ( test_io_activity_8bit_last_value_aggregator );
    RUN_TEST ( test_io_activity_reset_port_8bit_clears_all_high );
    RUN_TEST ( test_io_activity_separate_in_out_counters );
    RUN_TEST ( test_io_activity_get_last_value_per_direction );

    return UNITY_END ( );
}
