/*
 * test_breakpoints.c — komplexní testy modulu breakpoints
 *
 * Testuje:
 * - životní cyklus (init, exit)
 * - CRUD skupiny (add, remove, find, set_*)
 * - CRUD breakpointy (add, add_auto, remove, find, set_*)
 * - hierarchie (parent, reparenting, cyklus)
 * - efektivní povolení (effectively enabled)
 * - synchronizace s bptmap
 * - persistence (save/load + validace)
 * - hromadné operace (clear_all, sync_bptmap)
 * - edge cases (overflow, duplicitní adresy, prázdný soubor)
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include "debugger/breakpoints.h"
#include "debugger/bptmap.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>


/* ========================================================================= */
/*  setUp / tearDown                                                         */
/* ========================================================================= */


void setUp ( void ) {
    /* Reinicializovat breakpoints před každým testem */
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
}


void tearDown ( void ) {
}


/* ========================================================================= */
/*  Inicializace a základní stav                                             */
/* ========================================================================= */


void test_initial_state ( void ) {
    TEST_ASSERT_EQUAL_UINT ( 0, g_breakpoints.groups->len );
    TEST_ASSERT_EQUAL_UINT ( 0, g_breakpoints.breakpoints->len );
    TEST_ASSERT_EQUAL_INT ( 1, g_breakpoints.next_id );
}


void test_clear_all ( void ) {
    breakpoints_group_add ( "G1", -1 );
    breakpoints_add ( 0x1234, "B1", -1 );

    TEST_ASSERT_EQUAL_UINT ( 1, g_breakpoints.groups->len );
    TEST_ASSERT_EQUAL_UINT ( 1, g_breakpoints.breakpoints->len );

    breakpoints_clear_all ( );

    TEST_ASSERT_EQUAL_UINT ( 0, g_breakpoints.groups->len );
    TEST_ASSERT_EQUAL_UINT ( 0, g_breakpoints.breakpoints->len );
}


/* ========================================================================= */
/*  CRUD skupiny                                                             */
/* ========================================================================= */


void test_group_add ( void ) {
    int id = breakpoints_group_add ( "Test Group", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id );

    st_BPTGROUP *grp = breakpoints_group_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( grp );
    TEST_ASSERT_EQUAL_STRING ( "Test Group", grp->name );
    TEST_ASSERT_EQUAL_INT ( -1, grp->parent );
    TEST_ASSERT_TRUE ( grp->enabled );
    TEST_ASSERT_EQUAL_UINT ( 1, breakpoints_group_count ( ) );
}


void test_group_add_multiple ( void ) {
    int id1 = breakpoints_group_add ( "Group A", -1 );
    int id2 = breakpoints_group_add ( "Group B", -1 );
    int id3 = breakpoints_group_add ( "Group C", id1 );

    TEST_ASSERT_NOT_EQUAL ( id1, id2 );
    TEST_ASSERT_NOT_EQUAL ( id2, id3 );
    TEST_ASSERT_EQUAL_UINT ( 3, breakpoints_group_count ( ) );

    /* Ověřit parent */
    st_BPTGROUP *grp3 = breakpoints_group_find_by_id ( id3 );
    TEST_ASSERT_NOT_NULL ( grp3 );
    TEST_ASSERT_EQUAL_INT ( id1, grp3->parent );
}


void test_group_remove ( void ) {
    int id = breakpoints_group_add ( "To Remove", -1 );
    TEST_ASSERT_EQUAL_UINT ( 1, breakpoints_group_count ( ) );

    bool ok = breakpoints_group_remove ( id );
    TEST_ASSERT_TRUE ( ok );
    TEST_ASSERT_EQUAL_UINT ( 0, breakpoints_group_count ( ) );
    TEST_ASSERT_NULL ( breakpoints_group_find_by_id ( id ) );
}


void test_group_remove_reparents_children ( void ) {
    int parent_id = breakpoints_group_add ( "Parent", -1 );
    int child_grp = breakpoints_group_add ( "Child Group", parent_id );
    int child_bpt = breakpoints_add ( 0x1000, "Child BPT", parent_id );

    /* Odstraníme rodiče — potomci by měli jít do root */
    breakpoints_group_remove ( parent_id );

    st_BPTGROUP *grp = breakpoints_group_find_by_id ( child_grp );
    TEST_ASSERT_NOT_NULL ( grp );
    TEST_ASSERT_EQUAL_INT ( -1, grp->parent );

    st_BPT *bpt = breakpoints_find_by_id ( child_bpt );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_INT ( -1, bpt->parent );
}


void test_group_remove_nonexistent ( void ) {
    bool ok = breakpoints_group_remove ( 9999 );
    TEST_ASSERT_FALSE ( ok );
}


void test_group_set_enabled ( void ) {
    int id = breakpoints_group_add ( "G", -1 );

    bool ok = breakpoints_group_set_enabled ( id, false );
    TEST_ASSERT_TRUE ( ok );

    st_BPTGROUP *grp = breakpoints_group_find_by_id ( id );
    TEST_ASSERT_FALSE ( grp->enabled );

    breakpoints_group_set_enabled ( id, true );
    grp = breakpoints_group_find_by_id ( id );
    TEST_ASSERT_TRUE ( grp->enabled );
}


void test_group_set_name ( void ) {
    int id = breakpoints_group_add ( "Old", -1 );
    breakpoints_group_set_name ( id, "New Name" );

    st_BPTGROUP *grp = breakpoints_group_find_by_id ( id );
    TEST_ASSERT_EQUAL_STRING ( "New Name", grp->name );
}


void test_group_set_colors ( void ) {
    int id = breakpoints_group_add ( "G", -1 );
    breakpoints_group_set_colors ( id, 0x112233, 0xAABBCC );

    st_BPTGROUP *grp = breakpoints_group_find_by_id ( id );
    TEST_ASSERT_EQUAL_HEX32 ( 0x112233, grp->bg_rgb );
    TEST_ASSERT_EQUAL_HEX32 ( 0xAABBCC, grp->fg_rgb );
}


void test_group_set_order ( void ) {
    int id = breakpoints_group_add ( "G", -1 );
    breakpoints_group_set_order ( id, 3.14f );

    st_BPTGROUP *grp = breakpoints_group_find_by_id ( id );
    TEST_ASSERT_FLOAT_WITHIN ( 0.01f, 3.14f, grp->order );
}


void test_group_set_parent ( void ) {
    int id1 = breakpoints_group_add ( "A", -1 );
    int id2 = breakpoints_group_add ( "B", -1 );

    bool ok = breakpoints_group_set_parent ( id2, id1 );
    TEST_ASSERT_TRUE ( ok );

    st_BPTGROUP *grp = breakpoints_group_find_by_id ( id2 );
    TEST_ASSERT_EQUAL_INT ( id1, grp->parent );
}


void test_group_set_parent_cycle_detection ( void ) {
    int id1 = breakpoints_group_add ( "A", -1 );
    int id2 = breakpoints_group_add ( "B", id1 );
    int id3 = breakpoints_group_add ( "C", id2 );

    /* Pokus o vytvoření cyklu: A → B → C → A */
    bool ok = breakpoints_group_set_parent ( id1, id3 );
    TEST_ASSERT_FALSE ( ok );

    /* A by mělo stále být root */
    st_BPTGROUP *grp = breakpoints_group_find_by_id ( id1 );
    TEST_ASSERT_EQUAL_INT ( -1, grp->parent );
}


void test_group_set_parent_self ( void ) {
    int id = breakpoints_group_add ( "Self", -1 );

    /* Nelze nastavit sám sebe jako rodiče */
    bool ok = breakpoints_group_set_parent ( id, id );
    TEST_ASSERT_FALSE ( ok );
}


void test_group_set_parent_nonexistent ( void ) {
    int id = breakpoints_group_add ( "G", -1 );

    /* Neexistující rodič */
    bool ok = breakpoints_group_set_parent ( id, 9999 );
    TEST_ASSERT_FALSE ( ok );
}


void test_group_set_parent_to_root ( void ) {
    int id1 = breakpoints_group_add ( "Parent", -1 );
    int id2 = breakpoints_group_add ( "Child", id1 );

    bool ok = breakpoints_group_set_parent ( id2, -1 );
    TEST_ASSERT_TRUE ( ok );

    st_BPTGROUP *grp = breakpoints_group_find_by_id ( id2 );
    TEST_ASSERT_EQUAL_INT ( -1, grp->parent );
}


/* ========================================================================= */
/*  CRUD breakpointy                                                         */
/* ========================================================================= */


void test_bpt_add ( void ) {
    int id = breakpoints_add ( 0x0000, "Reset", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_STRING ( "Reset", bpt->name );
    TEST_ASSERT_EQUAL_HEX16 ( 0x0000, bpt->addr );
    TEST_ASSERT_TRUE ( bpt->enabled );
    TEST_ASSERT_EQUAL_INT ( -1, bpt->parent );
    TEST_ASSERT_EQUAL_UINT64 ( 0, bpt->hits );
    TEST_ASSERT_EQUAL_UINT ( 1, breakpoints_count ( ) );
}


void test_bpt_add_conflict ( void ) {
    int id1 = breakpoints_add ( 0x1234, "First", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id1 );

    /* Pokus o přidání na stejnou adresu — breakpoints_add odmítne */
    int id2 = breakpoints_add ( 0x1234, "Second", -1 );
    TEST_ASSERT_EQUAL_INT ( -1, id2 );

    /* Pouze jeden BPT by měl existovat */
    TEST_ASSERT_EQUAL_UINT ( 1, breakpoints_count ( ) );
}


void test_bpt_add_auto ( void ) {
    int id1 = breakpoints_add ( 0x1234, "First", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id1 );

    /* add_auto na obsazenou adresu — vytvoří, ale disabled */
    int id2 = breakpoints_add_auto ( 0x1234, "Second", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id2 );
    TEST_ASSERT_NOT_EQUAL ( id1, id2 );

    st_BPT *bpt2 = breakpoints_find_by_id ( id2 );
    TEST_ASSERT_NOT_NULL ( bpt2 );
    TEST_ASSERT_FALSE ( bpt2->enabled ); /* automaticky disabled */
    TEST_ASSERT_EQUAL_HEX16 ( 0x1234, bpt2->addr );

    TEST_ASSERT_EQUAL_UINT ( 2, breakpoints_count ( ) );
}


void test_bpt_add_auto_free_address ( void ) {
    /* add_auto na volnou adresu — enabled */
    int id = breakpoints_add_auto ( 0xABCD, "Free", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_TRUE ( bpt->enabled );
}


void test_bpt_remove ( void ) {
    int id = breakpoints_add ( 0x5678, "To Remove", -1 );
    TEST_ASSERT_EQUAL_UINT ( 1, breakpoints_count ( ) );

    bool ok = breakpoints_remove ( id );
    TEST_ASSERT_TRUE ( ok );
    TEST_ASSERT_EQUAL_UINT ( 0, breakpoints_count ( ) );
    TEST_ASSERT_NULL ( breakpoints_find_by_id ( id ) );
}


void test_bpt_remove_nonexistent ( void ) {
    bool ok = breakpoints_remove ( 9999 );
    TEST_ASSERT_FALSE ( ok );
}


void test_bpt_find_by_addr ( void ) {
    breakpoints_add ( 0x1000, "A", -1 );
    breakpoints_add ( 0x2000, "B", -1 );

    st_BPT *bpt = breakpoints_find_by_addr ( 0x2000 );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_STRING ( "B", bpt->name );

    TEST_ASSERT_NULL ( breakpoints_find_by_addr ( 0x3000 ) );
}


void test_bpt_set_enabled ( void ) {
    int id = breakpoints_add ( 0x1000, "Test", -1 );

    breakpoints_set_enabled ( id, false );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_FALSE ( bpt->enabled );

    breakpoints_set_enabled ( id, true );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_TRUE ( bpt->enabled );
}


void test_bpt_set_enabled_conflict ( void ) {
    int id1 = breakpoints_add ( 0x1000, "A", -1 );
    int id2 = breakpoints_add_auto ( 0x1000, "B", -1 );

    /* id2 je disabled (konflikl). Pokus o povolení selže. */
    bool ok = breakpoints_set_enabled ( id2, true );
    TEST_ASSERT_FALSE ( ok );

    /* Pokud zakážeme id1, pak id2 by měl jít povolit */
    breakpoints_set_enabled ( id1, false );
    ok = breakpoints_set_enabled ( id2, true );
    TEST_ASSERT_TRUE ( ok );
}


void test_bpt_set_name ( void ) {
    int id = breakpoints_add ( 0x1000, "Old", -1 );
    breakpoints_set_name ( id, "New Name" );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_STRING ( "New Name", bpt->name );
}


void test_bpt_set_colors ( void ) {
    int id = breakpoints_add ( 0x1000, "C", -1 );
    breakpoints_set_colors ( id, 0xFF0000, 0x00FF00 );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_HEX32 ( 0xFF0000, bpt->bg_rgb );
    TEST_ASSERT_EQUAL_HEX32 ( 0x00FF00, bpt->fg_rgb );
}


void test_bpt_set_parent ( void ) {
    int grp = breakpoints_group_add ( "G", -1 );
    int bpt_id = breakpoints_add ( 0x1000, "B", -1 );

    bool ok = breakpoints_set_parent ( bpt_id, grp );
    TEST_ASSERT_TRUE ( ok );

    st_BPT *bpt = breakpoints_find_by_id ( bpt_id );
    TEST_ASSERT_EQUAL_INT ( grp, bpt->parent );
}


void test_bpt_set_parent_nonexistent_group ( void ) {
    int bpt_id = breakpoints_add ( 0x1000, "B", -1 );
    bool ok = breakpoints_set_parent ( bpt_id, 9999 );
    TEST_ASSERT_FALSE ( ok );
}


void test_bpt_set_parent_to_root ( void ) {
    int grp = breakpoints_group_add ( "G", -1 );
    int bpt_id = breakpoints_add ( 0x1000, "B", grp );

    bool ok = breakpoints_set_parent ( bpt_id, -1 );
    TEST_ASSERT_TRUE ( ok );

    st_BPT *bpt = breakpoints_find_by_id ( bpt_id );
    TEST_ASSERT_EQUAL_INT ( -1, bpt->parent );
}


void test_bpt_set_addr ( void ) {
    int id = breakpoints_add ( 0x1000, "Orig", -1 );
    bool ok = breakpoints_set_addr ( id, 0x2000 );
    TEST_ASSERT_TRUE ( ok );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_HEX16 ( 0x2000, bpt->addr );
}


void test_bpt_set_addr_conflict ( void ) {
    breakpoints_add ( 0x1000, "A", -1 );
    int id2 = breakpoints_add ( 0x2000, "B", -1 );

    /* Přesun B na adresu A → auto-disable */
    breakpoints_set_addr ( id2, 0x1000 );

    st_BPT *bpt = breakpoints_find_by_id ( id2 );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1000, bpt->addr );
    TEST_ASSERT_FALSE ( bpt->enabled ); /* auto-disabled kvůli konfliktu */
}


void test_bpt_hits ( void ) {
    int id = breakpoints_add ( 0x1000, "H", -1 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 0, bpt->hits );

    breakpoints_increment_hits ( id );
    breakpoints_increment_hits ( id );
    breakpoints_increment_hits ( id );

    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 3, bpt->hits );

    breakpoints_reset_hits ( id );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 0, bpt->hits );
}


/* ========================================================================= */
/*  Efektivní povolení (effectively enabled)                                 */
/* ========================================================================= */


void test_effectively_enabled_simple ( void ) {
    int id = breakpoints_add ( 0x1000, "B", -1 );
    TEST_ASSERT_TRUE ( breakpoints_is_effectively_enabled ( id ) );

    breakpoints_set_enabled ( id, false );
    TEST_ASSERT_FALSE ( breakpoints_is_effectively_enabled ( id ) );
}


void test_effectively_enabled_parent_disabled ( void ) {
    int grp = breakpoints_group_add ( "G", -1 );
    int bpt_id = breakpoints_add ( 0x1000, "B", grp );

    /* BPT enabled, skupina enabled → efektivně enabled */
    TEST_ASSERT_TRUE ( breakpoints_is_effectively_enabled ( bpt_id ) );

    /* Zakážeme skupinu → BPT efektivně disabled */
    breakpoints_group_set_enabled ( grp, false );
    TEST_ASSERT_FALSE ( breakpoints_is_effectively_enabled ( bpt_id ) );
}


void test_effectively_enabled_nested_groups ( void ) {
    int g1 = breakpoints_group_add ( "L1", -1 );
    int g2 = breakpoints_group_add ( "L2", g1 );
    int bpt_id = breakpoints_add ( 0x1000, "B", g2 );

    TEST_ASSERT_TRUE ( breakpoints_is_effectively_enabled ( bpt_id ) );

    /* Zakážeme vnější skupinu */
    breakpoints_group_set_enabled ( g1, false );
    TEST_ASSERT_FALSE ( breakpoints_is_effectively_enabled ( bpt_id ) );

    /* Povolíme vnější, zakážeme vnitřní */
    breakpoints_group_set_enabled ( g1, true );
    breakpoints_group_set_enabled ( g2, false );
    TEST_ASSERT_FALSE ( breakpoints_is_effectively_enabled ( bpt_id ) );
}


/* ========================================================================= */
/*  Synchronizace s bptmap                                                   */
/* ========================================================================= */


void test_bptmap_sync_after_add ( void ) {
    int id = breakpoints_add ( 0x1234, "Sync", -1 );

    /* bptmap by měl mít tento BPT zaregistrovaný */
    int map_id = bptmap_event_get_id_by_addr ( 0x1234 );
    TEST_ASSERT_EQUAL_INT ( id, map_id );
}


void test_bptmap_sync_after_remove ( void ) {
    int id = breakpoints_add ( 0x1234, "Remove", -1 );
    breakpoints_remove ( id );

    int map_id = bptmap_event_get_id_by_addr ( 0x1234 );
    TEST_ASSERT_EQUAL_INT ( BREAKPOINT_TYPE_NONE, map_id );
}


void test_bptmap_sync_after_disable ( void ) {
    int id = breakpoints_add ( 0x1234, "Disable", -1 );
    breakpoints_set_enabled ( id, false );

    int map_id = bptmap_event_get_id_by_addr ( 0x1234 );
    TEST_ASSERT_EQUAL_INT ( BREAKPOINT_TYPE_NONE, map_id );
}


void test_bptmap_sync_group_disable ( void ) {
    int grp = breakpoints_group_add ( "G", -1 );
    int bpt_id = breakpoints_add ( 0x5000, "InGroup", grp );

    /* BPT je v bptmap */
    TEST_ASSERT_EQUAL_INT ( bpt_id, bptmap_event_get_id_by_addr ( 0x5000 ) );

    /* Zakážeme skupinu → BPT zmizí z bptmap */
    breakpoints_group_set_enabled ( grp, false );
    TEST_ASSERT_EQUAL_INT ( BREAKPOINT_TYPE_NONE, bptmap_event_get_id_by_addr ( 0x5000 ) );

    /* Povolíme skupinu → BPT se vrátí do bptmap */
    breakpoints_group_set_enabled ( grp, true );
    TEST_ASSERT_EQUAL_INT ( bpt_id, bptmap_event_get_id_by_addr ( 0x5000 ) );
}


void test_bptmap_preserves_temporary ( void ) {
    bptmap_set_temporary_event ( 0xAAAA );

    int id = breakpoints_add ( 0x1000, "T", -1 );
    breakpoints_sync_bptmap ( );

    /* Temporary BPT by měl přežít sync */
    TEST_ASSERT_EQUAL_INT ( (int)0xAAAA, g_bptmap.temporary_bpt_addr );

    /* Normální BPT taky */
    TEST_ASSERT_EQUAL_INT ( id, bptmap_event_get_id_by_addr ( 0x1000 ) );

    bptmap_reset_temporary_event ( );
}


/* ========================================================================= */
/*  Version tracking                                                         */
/* ========================================================================= */


void test_version_increments ( void ) {
    unsigned v0 = g_breakpoints.version;

    breakpoints_group_add ( "G", -1 );
    TEST_ASSERT_GREATER_THAN ( v0, g_breakpoints.version );

    unsigned v1 = g_breakpoints.version;
    breakpoints_add ( 0x1000, "B", -1 );
    TEST_ASSERT_GREATER_THAN ( v1, g_breakpoints.version );
}


/* ========================================================================= */
/*  Persistence                                                              */
/* ========================================================================= */


void test_save_load_roundtrip ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_roundtrip.ini";

    /* Vytvořit data */
    int g1 = breakpoints_group_add ( "System", -1 );
    breakpoints_group_set_order ( g1, 1.0f );
    breakpoints_group_set_colors ( g1, 0x112233, 0xAABBCC );

    int g2 = breakpoints_group_add ( "ROM", g1 );
    breakpoints_group_set_order ( g2, 2.0f );

    int b1 = breakpoints_add ( 0x0000, "Reset", g1 );
    breakpoints_increment_hits ( b1 );
    breakpoints_increment_hits ( b1 );

    int b2 = breakpoints_add ( 0x1234, "Entry", g2 );
    breakpoints_set_colors ( b2, 0xFF0000, 0x00FF00 );

    int b3 = breakpoints_add ( 0xFFFF, "Top", -1 );
    (void)b3;

    /* Uložit */
    breakpoints_save_to_filepath ( tmpfile );

    /* Vyčistit a ověřit, že je prázdné */
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    TEST_ASSERT_EQUAL_UINT ( 0, breakpoints_group_count ( ) );
    TEST_ASSERT_EQUAL_UINT ( 0, breakpoints_count ( ) );

    /* Načíst */
    breakpoints_load_from_filepath ( tmpfile );

    /* Ověřit počty */
    TEST_ASSERT_EQUAL_UINT ( 2, breakpoints_group_count ( ) );
    TEST_ASSERT_EQUAL_UINT ( 3, breakpoints_count ( ) );

    /* Ověřit, že next_id je platný (>= 1) */
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, g_breakpoints.next_id );

    /* Ověřit, že po načtení lze přidávat nové BPT */
    int new_id = breakpoints_add ( 0x9999, "After Load", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, new_id );
    TEST_ASSERT_EQUAL_UINT ( 4, breakpoints_count ( ) );

    /* Vyčistit dočasný soubor */
    remove ( tmpfile );
}


void test_load_empty_file ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_empty.ini";

    /* Uložit prázdný stav */
    breakpoints_save_to_filepath ( tmpfile );

    /* Vyčistit */
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;

    /* Načíst prázdný soubor */
    breakpoints_load_from_filepath ( tmpfile );

    /* next_id musí být >= 1 (BUG FIX: dříve bylo 0 po načtení prázdného souboru) */
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, g_breakpoints.next_id );

    /* Musí jít přidávat nové BPT */
    int id = breakpoints_add ( 0x1000, "After Empty Load", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id );
    TEST_ASSERT_EQUAL_UINT ( 1, breakpoints_count ( ) );

    remove ( tmpfile );
}


void test_load_nonexistent_file ( void ) {
    /* Načtení neexistujícího souboru by nemělo spadnout */
    breakpoints_load_from_filepath ( "nonexistent_breakpoints_XYZZY.ini" );

    /* next_id by měl zůstat platný */
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, g_breakpoints.next_id );

    /* Musí jít přidávat */
    int id = breakpoints_add ( 0x2000, "OK", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id );
}


void test_save_load_preserves_hits ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_hits.ini";

    int id = breakpoints_add ( 0x0042, "Hit Test", -1 );
    breakpoints_increment_hits ( id );
    breakpoints_increment_hits ( id );
    breakpoints_increment_hits ( id );
    breakpoints_increment_hits ( id );
    breakpoints_increment_hits ( id );

    breakpoints_save_to_filepath ( tmpfile );
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    /* Najít BPT podle adresy (ID se mohlo změnit reoptimalizací) */
    st_BPT *bpt = breakpoints_find_by_addr ( 0x0042 );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_UINT64 ( 5, bpt->hits );

    remove ( tmpfile );
}


void test_save_load_preserves_hierarchy ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_hierarchy.ini";

    int g1 = breakpoints_group_add ( "Parent", -1 );
    int g2 = breakpoints_group_add ( "Child", g1 );
    breakpoints_add ( 0x1000, "InChild", g2 );

    breakpoints_save_to_filepath ( tmpfile );
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    /* Ověřit hierarchii — BPT by měl mít parent = child group */
    st_BPT *bpt = breakpoints_find_by_addr ( 0x1000 );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_GREATER_OR_EQUAL ( 0, bpt->parent ); /* má rodiče */

    /* Rodičovská skupina by měla mít svého rodiče */
    st_BPTGROUP *child_grp = breakpoints_group_find_by_id ( bpt->parent );
    TEST_ASSERT_NOT_NULL ( child_grp );
    TEST_ASSERT_GREATER_OR_EQUAL ( 0, child_grp->parent ); /* má rodiče */

    remove ( tmpfile );
}


/* ========================================================================= */
/*  auto_name vlastnost                                                      */
/* ========================================================================= */


void test_auto_name_default_true ( void ) {
    int id = breakpoints_add ( 0x1234, "Test", -1 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_TRUE ( bpt->auto_name );
}


void test_auto_name_add_auto_default_true ( void ) {
    int id = breakpoints_add_auto ( 0x5678, "Test", -1 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_TRUE ( bpt->auto_name );
}


void test_set_auto_name_generates_name ( void ) {
    int id = breakpoints_add ( 0x0042, "Manual name", -1 );
    breakpoints_set_auto_name ( id, true );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_TRUE ( bpt->auto_name );
    TEST_ASSERT_EQUAL_STRING ( "Addr: 0x0042", bpt->name );
}


void test_set_auto_name_false ( void ) {
    int id = breakpoints_add ( 0x0042, "My name", -1 );
    breakpoints_set_auto_name ( id, false );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_FALSE ( bpt->auto_name );
    TEST_ASSERT_EQUAL_STRING ( "My name", bpt->name );
}


void test_set_addr_auto_updates_name ( void ) {
    int id = breakpoints_add ( 0x0042, "Addr: 0x0042", -1 );
    /* auto_name je implicitně true */
    breakpoints_set_addr ( id, 0x1234 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_STRING ( "Addr: 0x1234", bpt->name );
}


void test_set_addr_no_update_when_auto_name_false ( void ) {
    int id = breakpoints_add ( 0x0042, "Custom", -1 );
    breakpoints_set_auto_name ( id, false );
    breakpoints_set_addr ( id, 0x1234 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_EQUAL_STRING ( "Custom", bpt->name );
}


void test_save_load_preserves_auto_name ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_UNIT );

    const char *tmpfile = "test_breakpoints_autoname.ini";

    int id1 = breakpoints_add ( 0x0042, "Auto", -1 );
    /* id1: auto_name = true (default) */

    int id2 = breakpoints_add_auto ( 0x0043, "Manual", -1 );
    breakpoints_set_auto_name ( id2, false );

    breakpoints_save_to_filepath ( tmpfile );
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    breakpoints_load_from_filepath ( tmpfile );

    /* Po roundtripu: auto_name flag zachován */
    st_BPT *bpt1 = breakpoints_find_by_addr ( 0x0042 );
    TEST_ASSERT_NOT_NULL ( bpt1 );
    TEST_ASSERT_TRUE ( bpt1->auto_name );

    st_BPT *bpt2 = breakpoints_find_by_addr ( 0x0043 );
    TEST_ASSERT_NOT_NULL ( bpt2 );
    TEST_ASSERT_FALSE ( bpt2->auto_name );

    remove ( tmpfile );
}


/* ========================================================================= */
/*  Edge cases a regresní testy                                              */
/* ========================================================================= */


void test_add_after_clear_all ( void ) {
    breakpoints_add ( 0x1000, "A", -1 );
    breakpoints_add ( 0x2000, "B", -1 );
    breakpoints_clear_all ( );

    /* Po clear_all musí jít přidávat (next_id nesmí být 0) */
    int id = breakpoints_add ( 0x3000, "C", -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id );
}


void test_multiple_add_auto_same_address ( void ) {
    int id1 = breakpoints_add_auto ( 0x0000, "First", -1 );
    int id2 = breakpoints_add_auto ( 0x0000, "Second", -1 );
    int id3 = breakpoints_add_auto ( 0x0000, "Third", -1 );

    /* První enabled, zbytek disabled */
    st_BPT *bpt1 = breakpoints_find_by_id ( id1 );
    st_BPT *bpt2 = breakpoints_find_by_id ( id2 );
    st_BPT *bpt3 = breakpoints_find_by_id ( id3 );

    TEST_ASSERT_TRUE ( bpt1->enabled );
    TEST_ASSERT_FALSE ( bpt2->enabled );
    TEST_ASSERT_FALSE ( bpt3->enabled );

    TEST_ASSERT_EQUAL_UINT ( 3, breakpoints_count ( ) );
}


void test_unique_ids ( void ) {
    int ids[10];
    unsigned i, j;

    for ( i = 0; i < 10; i++ ) {
        if ( i < 5 ) {
            ids[i] = breakpoints_group_add ( "G", -1 );
        } else {
            ids[i] = breakpoints_add ( (uint16_t)(0x1000 + i), "B", -1 );
        };
        TEST_ASSERT_GREATER_OR_EQUAL ( 1, ids[i] );
    };

    /* Všechna ID musí být unikátní */
    for ( i = 0; i < 10; i++ ) {
        for ( j = i + 1; j < 10; j++ ) {
            TEST_ASSERT_NOT_EQUAL ( ids[i], ids[j] );
        };
    };
}


void test_null_name ( void ) {
    int id = breakpoints_add ( 0x1000, NULL, -1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_NOT_NULL ( bpt );
    TEST_ASSERT_NOT_NULL ( bpt->name ); /* g_strdup(NULL) → g_strdup("") */
}


void test_boundary_addresses ( void ) {
    int id1 = breakpoints_add ( 0x0000, "Min", -1 );
    int id2 = breakpoints_add ( 0xFFFF, "Max", -1 );

    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id1 );
    TEST_ASSERT_GREATER_OR_EQUAL ( 1, id2 );

    TEST_ASSERT_EQUAL_HEX16 ( 0x0000, breakpoints_find_by_id ( id1 )->addr );
    TEST_ASSERT_EQUAL_HEX16 ( 0xFFFF, breakpoints_find_by_id ( id2 )->addr );

    /* bptmap sync */
    TEST_ASSERT_EQUAL_INT ( id1, bptmap_event_get_id_by_addr ( 0x0000 ) );
    TEST_ASSERT_EQUAL_INT ( id2, bptmap_event_get_id_by_addr ( 0xFFFF ) );
}


/* ========================================================================= */
/*  MAIN                                                                     */
/* ========================================================================= */


int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    /* Inicializace a základní stav */
    RUN_TEST ( test_initial_state );
    RUN_TEST ( test_clear_all );

    /* CRUD skupiny */
    RUN_TEST ( test_group_add );
    RUN_TEST ( test_group_add_multiple );
    RUN_TEST ( test_group_remove );
    RUN_TEST ( test_group_remove_reparents_children );
    RUN_TEST ( test_group_remove_nonexistent );
    RUN_TEST ( test_group_set_enabled );
    RUN_TEST ( test_group_set_name );
    RUN_TEST ( test_group_set_colors );
    RUN_TEST ( test_group_set_order );
    RUN_TEST ( test_group_set_parent );
    RUN_TEST ( test_group_set_parent_cycle_detection );
    RUN_TEST ( test_group_set_parent_self );
    RUN_TEST ( test_group_set_parent_nonexistent );
    RUN_TEST ( test_group_set_parent_to_root );

    /* CRUD breakpointy */
    RUN_TEST ( test_bpt_add );
    RUN_TEST ( test_bpt_add_conflict );
    RUN_TEST ( test_bpt_add_auto );
    RUN_TEST ( test_bpt_add_auto_free_address );
    RUN_TEST ( test_bpt_remove );
    RUN_TEST ( test_bpt_remove_nonexistent );
    RUN_TEST ( test_bpt_find_by_addr );
    RUN_TEST ( test_bpt_set_enabled );
    RUN_TEST ( test_bpt_set_enabled_conflict );
    RUN_TEST ( test_bpt_set_name );
    RUN_TEST ( test_bpt_set_colors );
    RUN_TEST ( test_bpt_set_parent );
    RUN_TEST ( test_bpt_set_parent_nonexistent_group );
    RUN_TEST ( test_bpt_set_parent_to_root );
    RUN_TEST ( test_bpt_set_addr );
    RUN_TEST ( test_bpt_set_addr_conflict );
    RUN_TEST ( test_bpt_hits );

    /* Efektivní povolení */
    RUN_TEST ( test_effectively_enabled_simple );
    RUN_TEST ( test_effectively_enabled_parent_disabled );
    RUN_TEST ( test_effectively_enabled_nested_groups );

    /* Synchronizace s bptmap */
    RUN_TEST ( test_bptmap_sync_after_add );
    RUN_TEST ( test_bptmap_sync_after_remove );
    RUN_TEST ( test_bptmap_sync_after_disable );
    RUN_TEST ( test_bptmap_sync_group_disable );
    RUN_TEST ( test_bptmap_preserves_temporary );

    /* Version tracking */
    RUN_TEST ( test_version_increments );

    /* Persistence */
    RUN_TEST ( test_save_load_roundtrip );
    RUN_TEST ( test_load_empty_file );
    RUN_TEST ( test_load_nonexistent_file );
    RUN_TEST ( test_save_load_preserves_hits );
    RUN_TEST ( test_save_load_preserves_hierarchy );

    /* auto_name vlastnost */
    RUN_TEST ( test_auto_name_default_true );
    RUN_TEST ( test_auto_name_add_auto_default_true );
    RUN_TEST ( test_set_auto_name_generates_name );
    RUN_TEST ( test_set_auto_name_false );
    RUN_TEST ( test_set_addr_auto_updates_name );
    RUN_TEST ( test_set_addr_no_update_when_auto_name_false );
    RUN_TEST ( test_save_load_preserves_auto_name );

    /* Edge cases */
    RUN_TEST ( test_add_after_clear_all );
    RUN_TEST ( test_multiple_add_auto_same_address );
    RUN_TEST ( test_unique_ids );
    RUN_TEST ( test_null_name );
    RUN_TEST ( test_boundary_addresses );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
