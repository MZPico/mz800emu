/*
 * test_io_history.c - unit testy pro IO ports history ring buffer (V1.5).
 *
 * Testuje:
 *  - init s default capacity
 *  - record + count + head ring
 *  - overflow flag po wrap-around
 *  - get(idx) - oldest first ordering
 *  - clear nuluje stav (ne capacity)
 *  - set_capacity zahodi data + zmeni size
 *  - capacity clamping do [MIN, MAX]
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <string.h>
#include <stdio.h>

#include "debugger/io_history.h"


void setUp ( void )
{
    io_history_init ( IO_HISTORY_DEFAULT_CAPACITY );
}


void tearDown ( void )
{
    io_history_destroy ( );
}


/* ================================================================
 * Init / destroy
 * ================================================================ */

void test_io_history_init_default ( void )
{
    TEST_ASSERT_NOT_NULL ( g_io_history.events );
    TEST_ASSERT_EQUAL_size_t ( IO_HISTORY_DEFAULT_CAPACITY, g_io_history.capacity );
    TEST_ASSERT_EQUAL_size_t ( 0, g_io_history.count );
    TEST_ASSERT_EQUAL_size_t ( 0, g_io_history.head );
    TEST_ASSERT_FALSE ( g_io_history.overflow );
}


void test_io_history_init_custom_capacity ( void )
{
    io_history_init ( 5000 );
    TEST_ASSERT_EQUAL_size_t ( 5000, g_io_history.capacity );
}


void test_io_history_init_clamps_below_min ( void )
{
    io_history_init ( 100 );  /* < MIN_CAPACITY */
    TEST_ASSERT_EQUAL_size_t ( IO_HISTORY_MIN_CAPACITY, g_io_history.capacity );
}


void test_io_history_init_clamps_above_max ( void )
{
    io_history_init ( 100000 );  /* > MAX_CAPACITY */
    TEST_ASSERT_EQUAL_size_t ( IO_HISTORY_MAX_CAPACITY, g_io_history.capacity );
}


/* ================================================================
 * Record
 * ================================================================ */

void test_io_history_record_basic ( void )
{
    io_history_record ( true /* IN */, 0x00CE, 0xAA, 0x4042, 12345,
                        128 /* scanline */, 256 /* px */,
                        4567890 /* cpu_cycle, V1.5.D fix #3 */ );

    TEST_ASSERT_EQUAL_size_t ( 1, g_io_history.count );
    TEST_ASSERT_EQUAL_size_t ( 1, g_io_history.head );
    TEST_ASSERT_FALSE ( g_io_history.overflow );

    const st_IO_HISTORY_EVENT *e = io_history_get ( 0 );
    TEST_ASSERT_NOT_NULL ( e );
    TEST_ASSERT_EQUAL_HEX16 ( 0x00CE, e->port );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAA, e->value );
    TEST_ASSERT_EQUAL_HEX16 ( 0x4042, e->pc );
    TEST_ASSERT_EQUAL_UINT32 ( 12345, e->frame );
    TEST_ASSERT_EQUAL_UINT8 ( IO_HISTORY_FLAG_READ, e->flags );
    TEST_ASSERT_EQUAL_UINT16 ( 128, e->scanline );
    TEST_ASSERT_EQUAL_UINT16 ( 256, e->px );
    TEST_ASSERT_EQUAL_UINT32 ( 4567890, e->cpu_cycle );
}


void test_io_history_record_100_events ( void )
{
    for ( int i = 0; i < 100; i++ ) {
        io_history_record ( ( i & 1 ) ? false : true,
                            (uint16_t)( 0x00CE + i ),
                            (uint8_t)( i & 0xFF ),
                            (uint16_t)( 0x4000 + i ),
                            (uint32_t) i,
                            (uint16_t)( i % 312 ) /* scanline */,
                            (uint16_t)( i % 768 ) /* px */,
                            (uint32_t)( i * 100 ) /* cpu_cycle */ );
    }

    TEST_ASSERT_EQUAL_size_t ( 100, g_io_history.count );
    TEST_ASSERT_FALSE ( g_io_history.overflow );

    /* Idx 0 = oldest = i=0, idx 99 = newest = i=99 */
    const st_IO_HISTORY_EVENT *e0 = io_history_get ( 0 );
    const st_IO_HISTORY_EVENT *e99 = io_history_get ( 99 );
    TEST_ASSERT_EQUAL_UINT32 ( 0, e0->frame );
    TEST_ASSERT_EQUAL_UINT32 ( 99, e99->frame );
    TEST_ASSERT_EQUAL_HEX16 ( 0x00CE, e0->port );
    TEST_ASSERT_EQUAL_HEX16 ( 0x00CE + 99, e99->port );
}


void test_io_history_get_out_of_range ( void )
{
    io_history_record ( true, 0x00CE, 0x00, 0x4000, 0, 0, 0, 0 );
    TEST_ASSERT_NULL ( io_history_get ( 1 ) );
    TEST_ASSERT_NULL ( io_history_get ( 100 ) );
}


/* ================================================================
 * Overflow / wrap-around
 * ================================================================ */

void test_io_history_overflow ( void )
{
    /* Smaller capacity pro test rychlosti */
    io_history_set_capacity ( 1000 );

    /* 1500 eventu = wrap-around 500x */
    for ( int i = 0; i < 1500; i++ ) {
        io_history_record ( true, (uint16_t)i, (uint8_t)( i & 0xFF ),
                            0x0000, (uint32_t) i, 0, 0, (uint32_t) i );
    }

    TEST_ASSERT_EQUAL_size_t ( 1000, g_io_history.count );
    TEST_ASSERT_TRUE ( g_io_history.overflow );

    /* Po wrap: oldest = i=500 (= prvnich 500 bylo prepsano), newest = i=1499. */
    const st_IO_HISTORY_EVENT *oldest = io_history_get ( 0 );
    const st_IO_HISTORY_EVENT *newest = io_history_get ( 999 );
    TEST_ASSERT_EQUAL_UINT32 ( 500, oldest->frame );
    TEST_ASSERT_EQUAL_UINT32 ( 1499, newest->frame );
}


/* ================================================================
 * Clear
 * ================================================================ */

void test_io_history_clear ( void )
{
    io_history_record ( true, 0x00CE, 0xAA, 0x4042, 0, 0, 0, 0 );
    io_history_record ( false, 0x00F0, 0x55, 0x4044, 0, 0, 0, 0 );

    io_history_clear ( );

    TEST_ASSERT_EQUAL_size_t ( 0, g_io_history.count );
    TEST_ASSERT_EQUAL_size_t ( 0, g_io_history.head );
    TEST_ASSERT_FALSE ( g_io_history.overflow );

    /* Capacity zachovana */
    TEST_ASSERT_EQUAL_size_t ( IO_HISTORY_DEFAULT_CAPACITY,
                                g_io_history.capacity );
}


/* ================================================================
 * Set capacity
 * ================================================================ */

void test_io_history_set_capacity_resizes_and_clears ( void )
{
    io_history_record ( true, 0x00CE, 0xAA, 0x4042, 0, 0, 0, 0 );
    TEST_ASSERT_EQUAL_size_t ( 1, g_io_history.count );

    io_history_set_capacity ( 5000 );

    TEST_ASSERT_EQUAL_size_t ( 5000, g_io_history.capacity );
    TEST_ASSERT_EQUAL_size_t ( 0, g_io_history.count );
    TEST_ASSERT_FALSE ( g_io_history.overflow );
}


/* ================================================================
 * IN / OUT direction
 * ================================================================ */

void test_io_history_in_out_distinction ( void )
{
    io_history_record ( true,  0x00CE, 0xAA, 0x4042, 0, 0, 0, 0 );
    io_history_record ( false, 0x00CE, 0x55, 0x4044, 0, 0, 0, 0 );

    const st_IO_HISTORY_EVENT *e0 = io_history_get ( 0 );
    const st_IO_HISTORY_EVENT *e1 = io_history_get ( 1 );

    TEST_ASSERT_EQUAL_UINT8 ( IO_HISTORY_FLAG_READ, e0->flags );  /* IN */
    TEST_ASSERT_EQUAL_UINT8 ( 0, e1->flags );                     /* OUT */
    TEST_ASSERT_EQUAL_HEX8 ( 0xAA, e0->value );
    TEST_ASSERT_EQUAL_HEX8 ( 0x55, e1->value );
}


/* ================================================================
 * V1.5.E - memory-mapped event tests
 * ================================================================ */


void test_io_history_record_mem_basic ( void )
{
    io_history_record_mem ( true /* is_read */, 0xE008, 0x42,
                             0x4100, 12345, 200, 50, 1000000 );

    TEST_ASSERT_EQUAL_size_t ( 1, g_io_history.count );

    const st_IO_HISTORY_EVENT *e = io_history_get ( 0 );
    TEST_ASSERT_NOT_NULL ( e );
    TEST_ASSERT_EQUAL_HEX16 ( 0xE008, e->port );
    TEST_ASSERT_EQUAL_HEX8 ( 0x42, e->value );
    TEST_ASSERT_EQUAL_HEX16 ( 0x4100, e->pc );
    /* MR event - flags = READ | MEMORY */
    TEST_ASSERT_EQUAL_UINT8 (
        IO_HISTORY_FLAG_READ | IO_HISTORY_FLAG_MEMORY, e->flags );
}


void test_io_history_record_mem_write ( void )
{
    /* MW event - is_read=false, flags = MEMORY (no READ bit) */
    io_history_record_mem ( false /* is_read */, 0xE000, 0xAA,
                             0x4200, 0, 0, 0, 0 );

    const st_IO_HISTORY_EVENT *e = io_history_get ( 0 );
    TEST_ASSERT_NOT_NULL ( e );
    TEST_ASSERT_EQUAL_HEX16 ( 0xE000, e->port );
    TEST_ASSERT_EQUAL_UINT8 ( IO_HISTORY_FLAG_MEMORY, e->flags );
}


void test_io_history_iorq_vs_mmio_in_same_ring ( void )
{
    /* Mix IORQ + MMIO eventy v jednom ringu - rozliseni pres flags. */
    io_history_record ( true,  0x00CE, 0x11, 0x4000, 0, 0, 0, 0 );
    io_history_record_mem ( true,  0xE008, 0x22, 0x4002, 0, 0, 0, 0 );
    io_history_record ( false, 0x00CE, 0x33, 0x4004, 0, 0, 0, 0 );
    io_history_record_mem ( false, 0xE008, 0x44, 0x4006, 0, 0, 0, 0 );

    TEST_ASSERT_EQUAL_size_t ( 4, g_io_history.count );

    /* idx 0 = IORQ IN */
    const st_IO_HISTORY_EVENT *e0 = io_history_get ( 0 );
    TEST_ASSERT_EQUAL_UINT8 ( IO_HISTORY_FLAG_READ, e0->flags );

    /* idx 1 = MMIO MR */
    const st_IO_HISTORY_EVENT *e1 = io_history_get ( 1 );
    TEST_ASSERT_EQUAL_UINT8 (
        IO_HISTORY_FLAG_READ | IO_HISTORY_FLAG_MEMORY, e1->flags );

    /* idx 2 = IORQ OUT */
    const st_IO_HISTORY_EVENT *e2 = io_history_get ( 2 );
    TEST_ASSERT_EQUAL_UINT8 ( 0, e2->flags );

    /* idx 3 = MMIO MW */
    const st_IO_HISTORY_EVENT *e3 = io_history_get ( 3 );
    TEST_ASSERT_EQUAL_UINT8 ( IO_HISTORY_FLAG_MEMORY, e3->flags );
}


/* ================================================================
 * Main
 * ================================================================ */

int main ( int argc, char *argv[] )
{
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    RUN_TEST ( test_io_history_init_default );
    RUN_TEST ( test_io_history_init_custom_capacity );
    RUN_TEST ( test_io_history_init_clamps_below_min );
    RUN_TEST ( test_io_history_init_clamps_above_max );
    RUN_TEST ( test_io_history_record_basic );
    RUN_TEST ( test_io_history_record_100_events );
    RUN_TEST ( test_io_history_get_out_of_range );
    RUN_TEST ( test_io_history_overflow );
    RUN_TEST ( test_io_history_clear );
    RUN_TEST ( test_io_history_set_capacity_resizes_and_clears );
    RUN_TEST ( test_io_history_in_out_distinction );
    RUN_TEST ( test_io_history_record_mem_basic );
    RUN_TEST ( test_io_history_record_mem_write );
    RUN_TEST ( test_io_history_iorq_vs_mmio_in_same_ring );

    return UNITY_END ( );
}
