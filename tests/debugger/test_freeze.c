/*
 * test_freeze.c - testy Freeze Bytes subsystému (V1 Memory Browser feature).
 *
 * Pokrývá:
 *   - lifecycle (init / clear)
 *   - CRUD (add / remove / is_frozen / count / get_slot)
 *   - update path (add stejné adresy 2× → update value, ne new entry)
 *   - storage limit (FREEZE_MAX_ENTRIES)
 *   - g_freeze_active flag sync s count
 *
 * Type: EMU (interní storage modulu, sdílí debugger build).
 * Pozn.: freeze_apply_all() vyžaduje dbgapi_regions backend - separátní
 * integration test by potřeboval emu setup. Tady testujeme čistou storage
 * logiku.
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include "debugger/freeze/freeze.h"

#include <string.h>


void setUp ( void )
{
    freeze_init ( );
}


void tearDown ( void )
{
    freeze_clear ( );
}


/* T1: po init je storage prázdný a g_freeze_active = 0. */
static void test_init_empty ( void )
{
    TEST_ASSERT_EQUAL_size_t ( 0, freeze_count ( ) );
    TEST_ASSERT_EQUAL_UINT ( 0u, g_freeze_active );
    TEST_ASSERT_FALSE ( freeze_is_frozen ( 0, 0, 0x1234, NULL ) );
    TEST_ASSERT_NULL ( freeze_get_slot ( 0 ) );
}


/* T2: add jeden entry inkrementuje count a aktivuje flag. */
static void test_add_one ( void )
{
    TEST_ASSERT_TRUE ( freeze_add ( 0, 0, 0x1234, 0xAB ) );
    TEST_ASSERT_EQUAL_size_t ( 1, freeze_count ( ) );
    TEST_ASSERT_EQUAL_UINT ( 1u, g_freeze_active );

    uint8_t v = 0;
    TEST_ASSERT_TRUE ( freeze_is_frozen ( 0, 0, 0x1234, &v ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAB, v );
}


/* T3: add stejné (kind, sub_id, offset) podruhé NEPŘIDÁ nový slot,
 *     ale update value. count zůstává 1. */
static void test_add_duplicate_updates ( void )
{
    freeze_add ( 0, 0, 0x1234, 0xAB );
    TEST_ASSERT_TRUE ( freeze_add ( 0, 0, 0x1234, 0xCD ) );

    TEST_ASSERT_EQUAL_size_t ( 1, freeze_count ( ) );
    uint8_t v = 0;
    TEST_ASSERT_TRUE ( freeze_is_frozen ( 0, 0, 0x1234, &v ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0xCD, v );
}


/* T4: add více entries s různými (kind, sub_id, offset) - vše distinct. */
static void test_add_distinct ( void )
{
    freeze_add ( 0, 0, 0x1000, 0x11 );
    freeze_add ( 0, 0, 0x2000, 0x22 );
    freeze_add ( 1, 5, 0x1000, 0x33 );  /* jiný kind+sub_id */
    TEST_ASSERT_EQUAL_size_t ( 3, freeze_count ( ) );

    uint8_t v = 0;
    TEST_ASSERT_TRUE ( freeze_is_frozen ( 0, 0, 0x1000, &v ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0x11, v );
    TEST_ASSERT_TRUE ( freeze_is_frozen ( 1, 5, 0x1000, &v ) );
    TEST_ASSERT_EQUAL_HEX8 ( 0x33, v );
}


/* T5: remove existující entry. */
static void test_remove ( void )
{
    freeze_add ( 0, 0, 0x1234, 0xAB );
    freeze_add ( 0, 0, 0x5678, 0xCD );
    TEST_ASSERT_EQUAL_size_t ( 2, freeze_count ( ) );

    TEST_ASSERT_TRUE ( freeze_remove ( 0, 0, 0x1234 ) );
    TEST_ASSERT_EQUAL_size_t ( 1, freeze_count ( ) );
    TEST_ASSERT_FALSE ( freeze_is_frozen ( 0, 0, 0x1234, NULL ) );
    TEST_ASSERT_TRUE  ( freeze_is_frozen ( 0, 0, 0x5678, NULL ) );

    /* Remove neexistujícího vrátí false. */
    TEST_ASSERT_FALSE ( freeze_remove ( 0, 0, 0xDEAD ) );
    TEST_ASSERT_EQUAL_size_t ( 1, freeze_count ( ) );
}


/* T6: remove poslední -> g_freeze_active znovu 0. */
static void test_remove_last_deactivates ( void )
{
    freeze_add ( 0, 0, 0x1234, 0xAB );
    TEST_ASSERT_EQUAL_UINT ( 1u, g_freeze_active );

    freeze_remove ( 0, 0, 0x1234 );
    TEST_ASSERT_EQUAL_UINT ( 0u, g_freeze_active );
    TEST_ASSERT_EQUAL_size_t ( 0, freeze_count ( ) );
}


/* T7: clear vrací do empty stavu. */
static void test_clear ( void )
{
    freeze_add ( 0, 0, 0x1000, 0x11 );
    freeze_add ( 0, 0, 0x2000, 0x22 );
    freeze_add ( 1, 0, 0x3000, 0x33 );

    freeze_clear ( );
    TEST_ASSERT_EQUAL_size_t ( 0, freeze_count ( ) );
    TEST_ASSERT_EQUAL_UINT ( 0u, g_freeze_active );
}


/* T8: storage limit - po FREEZE_MAX_ENTRIES add další selže. */
static void test_storage_limit ( void )
{
    for ( int i = 0; i < FREEZE_MAX_ENTRIES; i++ ) {
        TEST_ASSERT_TRUE_MESSAGE ( freeze_add ( 0, 0, ( uint32_t ) i,
                                                 ( uint8_t ) ( i & 0xFF ) ),
                                   "fill storage should succeed" );
    }
    /* Storage plný - jeden další add selže. */
    TEST_ASSERT_FALSE ( freeze_add ( 0, 0, 0xDEAD, 0xFF ) );
    TEST_ASSERT_EQUAL_size_t ( ( size_t ) FREEZE_MAX_ENTRIES, freeze_count ( ) );

    /* Po remove je zase místo. */
    TEST_ASSERT_TRUE ( freeze_remove ( 0, 0, 0 ) );
    TEST_ASSERT_TRUE ( freeze_add ( 0, 0, 0xDEAD, 0xFF ) );
}


/* T9: get_slot vrací non-NULL pro obsazený slot, NULL pro prázdný/mimo. */
static void test_get_slot ( void )
{
    freeze_add ( 7, 3, 0xCAFE, 0x42 );

    /* Najdi slot s našimi hodnotami - iteruje všechny aby pokryl slot
     * mismatch (add může jít do jiného než [0]). */
    bool found = false;
    for ( size_t i = 0; i < FREEZE_MAX_ENTRIES; i++ ) {
        const st_FREEZE_ENTRY *e = freeze_get_slot ( i );
        if ( !e ) continue;
        if ( e->region_kind == 7 && e->sub_id == 3 && e->offset == 0xCAFE ) {
            TEST_ASSERT_EQUAL_HEX8 ( 0x42, e->value );
            TEST_ASSERT_TRUE ( e->in_use );
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE ( found, "entry must be findable via get_slot" );

    /* Mimo rozsah vrací NULL. */
    TEST_ASSERT_NULL ( freeze_get_slot ( FREEZE_MAX_ENTRIES ) );
    TEST_ASSERT_NULL ( freeze_get_slot ( FREEZE_MAX_ENTRIES + 100 ) );
}


/* T10: is_frozen s NULL out_value funguje (jen test existence). */
static void test_is_frozen_null_out ( void )
{
    freeze_add ( 0, 0, 0x1234, 0xAB );
    TEST_ASSERT_TRUE ( freeze_is_frozen ( 0, 0, 0x1234, NULL ) );
    TEST_ASSERT_FALSE ( freeze_is_frozen ( 0, 0, 0x5678, NULL ) );
}


int main ( int argc, char *argv[] )
{
    ( void ) argc;
    ( void ) argv;

    UNITY_BEGIN ( );
    RUN_TEST ( test_init_empty );
    RUN_TEST ( test_add_one );
    RUN_TEST ( test_add_duplicate_updates );
    RUN_TEST ( test_add_distinct );
    RUN_TEST ( test_remove );
    RUN_TEST ( test_remove_last_deactivates );
    RUN_TEST ( test_clear );
    RUN_TEST ( test_storage_limit );
    RUN_TEST ( test_get_slot );
    RUN_TEST ( test_is_frozen_null_out );
    return UNITY_END ( );
}
