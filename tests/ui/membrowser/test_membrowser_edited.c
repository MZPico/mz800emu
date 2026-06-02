/*
 * test_membrowser_edited.c - unit testy pro recently-edited storage +
 * rozšířený per-byte undo cycle (edit semantics).
 *
 * Pure logika bez ImGui / emu / SDL závislostí - STANDALONE typ.
 *
 * Pokrývá:
 *   - mark / is_marked basic
 *   - duplicitní mark = no-op
 *   - clear_for_instance / clear_all
 *   - per-instance izolace
 *   - per-region izolace (kind/sub_id rozlišuje)
 *   - mark_range základní + range >= capacity (truncation)
 *   - overflow přes MB_EDITED_MAX_PER_INSTANCE (oldest dropped)
 *   - per-byte undo push len=1 (= edit semantika) - basic cycle
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include "../../../src/ui-imgui/debugger/membrowser/membrowser_edited.h"
#include "../../../src/ui-imgui/debugger/membrowser/membrowser_undo.h"
#include "../../../src/ui-imgui/debugger/membrowser/membrowser_state.h"

#include <string.h>


void setUp(void)
{
    membrowser_edited_clear_all ( );
    membrowser_undo_clear_all ( );
}

void tearDown(void) {}


/* ========================================================== RECENTLY EDITED */

/* T1: mark + is_marked basic. */
static void test_mark_basic(void)
{
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 0x1000 ) );
    membrowser_edited_mark ( 0, 1, 0, 0x1000, 0x00 );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, 0x1000 ) );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 0x1001 ) );
    TEST_ASSERT_EQUAL_INT ( 1, membrowser_edited_count_for_instance ( 0 ) );
}


/* T2: duplicitní mark = no-op (count se nezvyšuje). */
static void test_mark_duplicate(void)
{
    membrowser_edited_mark ( 0, 1, 0, 0x1000, 0x00 );
    membrowser_edited_mark ( 0, 1, 0, 0x1000, 0x00 );
    membrowser_edited_mark ( 0, 1, 0, 0x1000, 0x00 );
    TEST_ASSERT_EQUAL_INT ( 1, membrowser_edited_count_for_instance ( 0 ) );
    TEST_ASSERT_TRUE ( membrowser_edited_is_marked ( 0, 1, 0, 0x1000 ) );
}


/* T3: clear_for_instance vymaže jen daný slot. */
static void test_clear_per_instance(void)
{
    membrowser_edited_mark ( 0, 1, 0, 0x10, 0x00 );
    membrowser_edited_mark ( 1, 1, 0, 0x10, 0x00 );
    membrowser_edited_mark ( 2, 1, 0, 0x10, 0x00 );

    membrowser_edited_clear_for_instance ( 1 );

    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, 0x10 ) );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 1, 1, 0, 0x10 ) );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 2, 1, 0, 0x10 ) );
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_edited_count_for_instance ( 1 ) );
}


/* T4: clear_all vymaže napříč všemi instancemi. */
static void test_clear_all(void)
{
    membrowser_edited_mark ( 0, 1, 0, 0x10, 0x00 );
    membrowser_edited_mark ( 2, 5, 1, 0x20, 0x00 );

    membrowser_edited_clear_all ( );

    for ( int i = 0; i < MB_INSTANCE_COUNT; i++ ) {
        TEST_ASSERT_EQUAL_INT ( 0, membrowser_edited_count_for_instance ( i ) );
    }
}


/* T5: per-instance izolace (stejná region+addr v jiné instanci). */
static void test_instance_isolation(void)
{
    membrowser_edited_mark ( 0, 1, 0, 0x1000, 0x00 );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, 0x1000 ) );
    /* Stejný region+addr v jiné instanci = oddělené. */
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 1, 1, 0, 0x1000 ) );
}


/* T6: per-region izolace (různé kind nebo sub_id = různý záznam). */
static void test_region_isolation(void)
{
    membrowser_edited_mark ( 0, 1, 0, 0x100, 0x00 );
    /* Jiný kind. */
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 2, 0, 0x100 ) );
    /* Stejný kind, jiný sub_id. */
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 1, 0x100 ) );
}


/* T7: mark_range základní. */
static void test_mark_range_basic(void)
{
    membrowser_edited_mark_range ( 0, 1, 0, 0x100, 4, NULL );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, 0x100 ) );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, 0x101 ) );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, 0x102 ) );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, 0x103 ) );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 0x104 ) );
    TEST_ASSERT_EQUAL_INT ( 4, membrowser_edited_count_for_instance ( 0 ) );
}


/* T8: mark_range >= capacity drží jen poslední kapacita bytů. */
static void test_mark_range_overflow(void)
{
    /* Range větší než kapacita - prvních (length - capacity) bytů vypadne. */
    uint32_t big = ( uint32_t ) ( MB_EDITED_MAX_PER_INSTANCE + 50 );
    membrowser_edited_mark_range ( 0, 1, 0, 0, big, NULL );
    TEST_ASSERT_EQUAL_INT ( MB_EDITED_MAX_PER_INSTANCE,
                             membrowser_edited_count_for_instance ( 0 ) );
    /* Adresy 0..49 by měly vypadnout, posledních MB_EDITED_MAX zůstat. */
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 0 ) );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 49 ) );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, 50 ) );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, big - 1 ) );
}


/* T9: overflow per-byte mark (nejstarší vypadne). */
static void test_per_byte_overflow(void)
{
    /* Naplnit kapacitu + 5 navíc. */
    for ( uint32_t a = 0; a < MB_EDITED_MAX_PER_INSTANCE; a++ ) {
        membrowser_edited_mark ( 0, 1, 0, a, 0x00 );
    }
    TEST_ASSERT_EQUAL_INT ( MB_EDITED_MAX_PER_INSTANCE,
                             membrowser_edited_count_for_instance ( 0 ) );
    /* Adresa 0 je v ringu. */
    TEST_ASSERT_TRUE ( membrowser_edited_is_marked ( 0, 1, 0, 0 ) );

    /* Mark 5 dalších - adresy 0..4 vypadnou. */
    for ( uint32_t a = MB_EDITED_MAX_PER_INSTANCE;
          a < MB_EDITED_MAX_PER_INSTANCE + 5; a++ ) {
        membrowser_edited_mark ( 0, 1, 0, a, 0x00 );
    }
    TEST_ASSERT_EQUAL_INT ( MB_EDITED_MAX_PER_INSTANCE,
                             membrowser_edited_count_for_instance ( 0 ) );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 0 ) );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 4 ) );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, 5 ) );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0,
                          MB_EDITED_MAX_PER_INSTANCE + 4 ) );
}


/* T10: invalid instance_idx = no-op (žádný crash). */
static void test_invalid_instance(void)
{
    membrowser_edited_mark ( -1, 1, 0, 0x100, 0x00 );
    membrowser_edited_mark ( MB_INSTANCE_COUNT, 1, 0, 0x100, 0x00 );
    membrowser_edited_mark ( 9999, 1, 0, 0x100, 0x00 );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( -1, 1, 0, 0x100 ) );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( MB_INSTANCE_COUNT, 1, 0, 0x100 ) );
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_edited_count_for_instance ( -1 ) );
}


/* T_AU1: check_unmark vrací byte na original → odebrat. */
static void test_check_unmark_back_to_original(void)
{
    /* Mark byte at 0x500 with original=0x42 (= pre-edit value). */
    membrowser_edited_mark ( 0, 1, 0, 0x500, 0x42 );
    TEST_ASSERT_TRUE ( membrowser_edited_is_marked ( 0, 1, 0, 0x500 ) );
    TEST_ASSERT_EQUAL_INT ( 1, membrowser_edited_count_for_instance ( 0 ) );

    /* Check_unmark s current=0x99 (jiná hodnota) → keep. */
    bool removed = membrowser_edited_check_unmark ( 0, 1, 0, 0x500, 0x99 );
    TEST_ASSERT_FALSE ( removed );
    TEST_ASSERT_TRUE ( membrowser_edited_is_marked ( 0, 1, 0, 0x500 ) );

    /* Check_unmark s current=0x42 (== original) → odebrat. */
    removed = membrowser_edited_check_unmark ( 0, 1, 0, 0x500, 0x42 );
    TEST_ASSERT_TRUE ( removed );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 0x500 ) );
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_edited_count_for_instance ( 0 ) );
}


/* T_AU2: opakovaný mark preservuje původní original (subsequent mark no-op). */
static void test_mark_preserves_first_original(void)
{
    /* První mark: original=0xAA (pre-edit). */
    membrowser_edited_mark ( 0, 1, 0, 0x600, 0xAA );
    /* Druhý mark (subsequent edit): původní hodnota nyní 0xBB (= post-prvního-edit).
     * Mark by měl být no-op a original_byte zůstat 0xAA. */
    membrowser_edited_mark ( 0, 1, 0, 0x600, 0xBB );

    /* Verify: check_unmark s current=0xBB → keep (0xBB != stored 0xAA). */
    TEST_ASSERT_FALSE ( membrowser_edited_check_unmark ( 0, 1, 0, 0x600, 0xBB ) );
    TEST_ASSERT_TRUE ( membrowser_edited_is_marked ( 0, 1, 0, 0x600 ) );

    /* check_unmark s current=0xAA → odebrat (== session original). */
    TEST_ASSERT_TRUE ( membrowser_edited_check_unmark ( 0, 1, 0, 0x600, 0xAA ) );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 0x600 ) );
}


/* T_AU3: check_unmark_range per-byte. */
static void test_check_unmark_range(void)
{
    uint8_t originals[4] = { 0x10, 0x20, 0x30, 0x40 };
    membrowser_edited_mark_range ( 0, 1, 0, 0x700, 4, originals );
    TEST_ASSERT_EQUAL_INT ( 4, membrowser_edited_count_for_instance ( 0 ) );

    /* Current = mix: 0x10 (= original), 0xFF, 0x30 (= original), 0x40 (= original).
     * Po check_unmark_range: index 0, 2, 3 odebrány; jen 0x701 zůstává. */
    uint8_t current[4] = { 0x10, 0xFF, 0x30, 0x40 };
    membrowser_edited_check_unmark_range ( 0, 1, 0, 0x700, 4, current );

    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 0x700 ) );
    TEST_ASSERT_TRUE  ( membrowser_edited_is_marked ( 0, 1, 0, 0x701 ) );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 0x702 ) );
    TEST_ASSERT_FALSE ( membrowser_edited_is_marked ( 0, 1, 0, 0x703 ) );
    TEST_ASSERT_EQUAL_INT ( 1, membrowser_edited_count_for_instance ( 0 ) );
}


/* T_AU4: check_unmark na neexistujícím entry = no-op false. */
static void test_check_unmark_no_entry(void)
{
    bool removed = membrowser_edited_check_unmark ( 0, 1, 0, 0x999, 0x00 );
    TEST_ASSERT_FALSE ( removed );
    TEST_ASSERT_EQUAL_INT ( 0, membrowser_edited_count_for_instance ( 0 ) );
}


/* ================================================== PER-BYTE UNDO INTEGRACE */

/* T11: per-byte undo push (len=1) + pop cycle. */
static void test_per_byte_undo_cycle(void)
{
    uint8_t old_byte = 0xAA;
    int rv = membrowser_undo_push ( 1, 0, 0x100, &old_byte, 1 );
    TEST_ASSERT_EQUAL_INT ( 0, rv );

    int u_n = 0, r_n = 0;
    membrowser_undo_stats ( 1, 0, &u_n, &r_n );
    TEST_ASSERT_EQUAL_INT ( 1, u_n );
    TEST_ASSERT_EQUAL_INT ( 0, r_n );

    st_MB_UNDO_ENTRY e;
    memset ( &e, 0, sizeof ( e ) );
    rv = membrowser_undo_pop ( 1, 0, &e );
    TEST_ASSERT_EQUAL_INT ( 0, rv );
    TEST_ASSERT_EQUAL_UINT32 ( 1, e.len );
    TEST_ASSERT_EQUAL_UINT64 ( 0x100, e.offset );
    TEST_ASSERT_NOT_NULL ( e.bytes );
    TEST_ASSERT_EQUAL_HEX8 ( 0xAA, e.bytes[0] );
    membrowser_undo_free_entry ( &e );
}


/* T12: per-byte undo overflow (>MB_UNDO_MAX_LEVELS = 10) shiftuje. */
static void test_per_byte_undo_overflow(void)
{
    /* Push 12 single-byte entries - prvních 2 vypadne. */
    for ( int i = 0; i < 12; i++ ) {
        uint8_t b = ( uint8_t ) i;
        membrowser_undo_push ( 1, 0, ( uint64_t ) ( 0x100 + i ), &b, 1 );
    }
    int u_n = 0;
    membrowser_undo_stats ( 1, 0, &u_n, NULL );
    TEST_ASSERT_EQUAL_INT ( 10, u_n );

    /* Top = posledně pushnutý (index 11 = byte 0x0B at 0x10B). */
    st_MB_UNDO_ENTRY e;
    memset ( &e, 0, sizeof ( e ) );
    membrowser_undo_pop ( 1, 0, &e );
    TEST_ASSERT_EQUAL_UINT64 ( 0x10B, e.offset );
    TEST_ASSERT_EQUAL_HEX8 ( 0x0B, e.bytes[0] );
    membrowser_undo_free_entry ( &e );
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST ( test_mark_basic );
    RUN_TEST ( test_mark_duplicate );
    RUN_TEST ( test_clear_per_instance );
    RUN_TEST ( test_clear_all );
    RUN_TEST ( test_instance_isolation );
    RUN_TEST ( test_region_isolation );
    RUN_TEST ( test_mark_range_basic );
    RUN_TEST ( test_mark_range_overflow );
    RUN_TEST ( test_per_byte_overflow );
    RUN_TEST ( test_invalid_instance );
    RUN_TEST ( test_check_unmark_back_to_original );
    RUN_TEST ( test_mark_preserves_first_original );
    RUN_TEST ( test_check_unmark_range );
    RUN_TEST ( test_check_unmark_no_entry );
    RUN_TEST ( test_per_byte_undo_cycle );
    RUN_TEST ( test_per_byte_undo_overflow );
    return UNITY_END();
}
