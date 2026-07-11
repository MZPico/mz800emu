/*
 * test_bp_zone.c - testy banking zone awareness pro smart BP (D.5)
 *
 * Pokrytí:
 *   - bp_zone_is_active_at() per zóna (MZ-800 build):
 *       * CPU_VIEW vždy true
 *       * ROM_LOWER fire jen když ROM_0000/ROM_1000 mapped
 *       * ROM_UPPER fire jen když ROM_E000 mapped
 *       * RAM = vše ostatní (= ne ROM, ne VRAM, ne CGRAM)
 *       * VRAM_FB v 8000/A000/D000 oknech podle DMD
 *       * PCG / CGRAM v MZ-700 modu
 *       * MMEXT_BANK podle g_memext.map[] (= bank_id reverz)
 *   - bp_zone_active_at_pc() priorita pro různé adresy
 *   - JSON persist zone + bank_id round-trip
 *   - String konverze bp_zone_to_string / _from_string (= z D.1.1)
 *   - Enforce filter: BP s zone != CPU_VIEW respektuje banking
 *
 * Test mockuje globály g_memory.map / g_memext.map / g_gdg.regDMD
 * přímo (= bez spouštění CPU). bp_zone_*() funkce čtou jen tyto
 * globály, takže jsou plně testovatelné headless.
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include "debugger/breakpoints.h"
#include "debugger/bp_zone.h"
#include "debugger/bptmap.h"
#include "debugger/bp_expr.h"
#include "debugger/bp_action.h"
#include "debugger/bp_vars.h"
#include "debugger/debugger.h"
#include "debugger/dbgapi_emu.h"
#include "debugger/dbgapi_msg.h"
#include "emulator/emulator.h"
#include "hw-generic/memory/memory.h"
#include "hw-generic/memory/memext.h"
#include "mzarch/mz800/gdg/mz800_gdg.h"
#include "mzarch/mz800/memory/mz800_memory.h"

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>


/* ========================================================================= */
/*  Mock MSG dispatcher                                                       */
/* ========================================================================= */


typedef struct {
    int call_count;
    int last_id;
} mock_msg_state_t;

static mock_msg_state_t s_mock_state;


static void mock_msg_dispatcher ( en_DBGAPI_MSG msg,
                                   st_DBGAPI_MSG_DATA *data,
                                   void *user_data ) {
    (void) user_data;
    (void) msg;
    s_mock_state.call_count++;
    if ( data ) {
        s_mock_state.last_id = data->id;
        g_free ( data );
    };
}


static void mock_msg_reset ( void ) {
    memset ( &s_mock_state, 0, sizeof ( s_mock_state ) );
}


/* ========================================================================= */
/*  setUp / tearDown                                                          */
/* ========================================================================= */


/* Saved baseline pro restore v tearDown. */
static uint32_t s_saved_map;
static uint8_t s_saved_dmd;
static en_MEMEXT_CONNECTION s_saved_memext_conn;
static en_MEMEXT_TYPE s_saved_memext_type;
static uint32_t s_saved_memext_map[ MEMEXT_RAW_MAP_SIZE ];


void setUp ( void ) {
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    bp_vars_clear_storage ( );
    g_emulator.paused = false;
    mock_msg_reset ( );
    dbgapi_emu_register_msg_dispatcher ( mock_msg_dispatcher, NULL );

    /* Backup banking state - testy můžou g_memory.map / g_gdg.regDMD
     * mutovat, tearDown obnoví. */
    s_saved_map = g_memory.map;
    s_saved_dmd = g_gdg.regDMD;
    s_saved_memext_conn = g_memext.connection;
    s_saved_memext_type = g_memext.type;
    memcpy ( s_saved_memext_map, g_memext.map, sizeof ( s_saved_memext_map ) );

    /* Default banking pro testy: vše ROM unmap, MZ-800 mode, Memext disconnected. */
    g_memory.map = 0;
    g_gdg.regDMD = 0;
    g_memext.connection = MEMEXT_CONNECTION_NO;
}


void tearDown ( void ) {
    dbgapi_emu_register_msg_dispatcher ( NULL, NULL );
    g_memory.map = s_saved_map;
    g_gdg.regDMD = s_saved_dmd;
    g_memext.connection = s_saved_memext_conn;
    g_memext.type = s_saved_memext_type;
    memcpy ( g_memext.map, s_saved_memext_map, sizeof ( s_saved_memext_map ) );
}


/* ========================================================================= */
/*  bp_zone_is_active_at - CPU_VIEW                                           */
/* ========================================================================= */


/**
 * CPU_VIEW vždy aktivní (= legacy, default chování).
 */
void test_zone_cpu_view_always_active ( void ) {
    /* Bez ohledu na map/DMD. */
    g_memory.map = 0;
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_CPU_VIEW, 0x0000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_CPU_VIEW, 0xE000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_CPU_VIEW, 0x8000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


/* ========================================================================= */
/*  bp_zone_is_active_at - ROM_LOWER                                          */
/* ========================================================================= */


void test_zone_rom_lower_when_mapped ( void ) {
    g_memory.map = MEMORY_MZ800_MAP_FLAG_ROM_0000 | MEMORY_MZ800_MAP_FLAG_ROM_1000;
    /* 0x0042 v ROM_0000 = active. */
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_ROM_LOWER, 0x0042, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    /* 0x1500 v ROM_1000 = active. */
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_ROM_LOWER, 0x1500, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    /* 0x2000 mimo ROM_LOWER range. */
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_ROM_LOWER, 0x2000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


void test_zone_rom_lower_when_unmapped ( void ) {
    g_memory.map = 0;  /* ROM_0000 unmapped. */
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_ROM_LOWER, 0x0042, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_ROM_LOWER, 0x1500, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


void test_zone_rom_lower_partial_mapping ( void ) {
    /* Jen ROM_0000 mapped, ROM_1000 unmapped. */
    g_memory.map = MEMORY_MZ800_MAP_FLAG_ROM_0000;
    TEST_ASSERT_TRUE  ( bp_zone_is_active_at ( BP_ZONE_ROM_LOWER, 0x0042, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_ROM_LOWER, 0x1500, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


/* ========================================================================= */
/*  bp_zone_is_active_at - ROM_UPPER                                          */
/* ========================================================================= */


void test_zone_rom_upper_when_mapped ( void ) {
    g_memory.map = MEMORY_MZ800_MAP_FLAG_ROM_E000;
    TEST_ASSERT_TRUE  ( bp_zone_is_active_at ( BP_ZONE_ROM_UPPER, 0xE000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_TRUE  ( bp_zone_is_active_at ( BP_ZONE_ROM_UPPER, 0xFFFE, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_ROM_UPPER, 0xDFFF, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_ROM_UPPER, 0x4000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


void test_zone_rom_upper_when_unmapped ( void ) {
    g_memory.map = 0;
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_ROM_UPPER, 0xE000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


/* ========================================================================= */
/*  bp_zone_is_active_at - VRAM_FB                                            */
/* ========================================================================= */


/**
 * VRAM_FB v non-MZ-700 modu (= MZ-800 graphics mode):
 *   8000-9FFF aktivní pokud CGRAM_VRAM flag.
 */
void test_zone_vram_rf_mz800_8000 ( void ) {
    /* MZ-800 mode: regDMD bez MZ700 bitu. */
    g_gdg.regDMD = 0;
    g_memory.map = MEMORY_MZ800_MAP_FLAG_CGRAM_VRAM;
    TEST_ASSERT_TRUE  ( bp_zone_is_active_at ( BP_ZONE_VRAM_FB, 0x8000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_TRUE  ( bp_zone_is_active_at ( BP_ZONE_VRAM_FB, 0x9FFF, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    /* A000-BFFF jen v SCRW640. */
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_VRAM_FB, 0xA000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


void test_zone_vram_rf_mz800_a000_scrw640 ( void ) {
    g_gdg.regDMD = REGISTER_DMD_FLAG_SCRW640;
    g_memory.map = MEMORY_MZ800_MAP_FLAG_CGRAM_VRAM;
    /* SCRW640: VRAM 16K, takže 8000-BFFF. */
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_VRAM_FB, 0x8000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_VRAM_FB, 0xA500, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


/**
 * VRAM_FB v MZ-700 modu: 0xD000-0xD7FF (atributová VRAM).
 *
 * POZN MZ-800 macro logic (mz800_memory.h ř. 83):
 *   MEMORY_MZ800_MAP_TEST_VRAM_D000 = MZ-700 mode && ROM_E000 flag set
 *
 * Tj. v MZ-700 modu znamená ROM_E000 flag = "VRAM/atributová oblast na
 * D000" (= MZ-700 hardware overload semantika - bit který v MZ-800
 * unmap horní ROM, v MZ-700 modu mapne VRAM). Takže pro tento test
 * musíme nastavit ROM_E000 i v MZ-700 modu.
 */
void test_zone_vram_rf_mz700_d000 ( void ) {
    g_gdg.regDMD = REGISTER_DMD_FLAG_MZ700;
    g_memory.map = MEMORY_MZ800_MAP_FLAG_ROM_E000;
    TEST_ASSERT_TRUE  ( bp_zone_is_active_at ( BP_ZONE_VRAM_FB, 0xD000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    /* 8000 NENÍ VRAM v MZ-700 modu (= MAP_TEST_VRAM_8000 vyžaduje
     * !MZ700). */
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_VRAM_FB, 0x8000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


/* ========================================================================= */
/*  bp_zone_is_active_at - PCG (CGRAM)                                        */
/* ========================================================================= */


void test_zone_pcg_mz700_cgram ( void ) {
    /* MZ-800 v MZ-700 modu + CGRAM mapped (= bit 2 CGRAM_VRAM). */
    g_gdg.regDMD = REGISTER_DMD_FLAG_MZ700;
    g_memory.map = MEMORY_MZ800_MAP_FLAG_CGRAM_VRAM;
    TEST_ASSERT_TRUE  ( bp_zone_is_active_at ( BP_ZONE_PCG, 0xC000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_TRUE  ( bp_zone_is_active_at ( BP_ZONE_PCG, 0xCFFF, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_PCG, 0xD000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


void test_zone_pcg_unmapped ( void ) {
    g_gdg.regDMD = 0;  /* MZ-800 mode = žádný PCG. */
    g_memory.map = MEMORY_MZ800_MAP_FLAG_CGRAM_VRAM;
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_PCG, 0xC000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


/* ========================================================================= */
/*  bp_zone_is_active_at - RAM (= "vše ostatní")                              */
/* ========================================================================= */


void test_zone_ram_when_no_overlay ( void ) {
    /* Vše ROM unmapped, MZ-800 mode, žádné VRAM mapping. */
    g_memory.map = 0;
    g_gdg.regDMD = 0;
    /* Adresa 0x4000 = RAM (= ne ROM, ne VRAM). */
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_RAM, 0x4000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    /* I 0x0042 je RAM když ROM_0000 unmap. */
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_RAM, 0x0042, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    /* I 0xE000 je RAM když ROM_E000 unmap. */
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_RAM, 0xE000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


void test_zone_ram_excludes_rom ( void ) {
    g_memory.map = MEMORY_MZ800_MAP_FLAG_ROM_0000 |
                   MEMORY_MZ800_MAP_FLAG_ROM_E000;
    /* 0x0042 = ROM, ne RAM. */
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_RAM, 0x0042, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    /* 0xE000 = ROM, ne RAM. */
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_RAM, 0xE000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
    /* 0x4000 stále RAM. */
    TEST_ASSERT_TRUE ( bp_zone_is_active_at ( BP_ZONE_RAM, 0x4000, BP_MATCH_SINGLE, 0, 0, 0xFF ) );
}


/* ========================================================================= */
/*  bp_zone_is_active_at - MMEXT_BANK                                          */
/* ========================================================================= */


void test_zone_pehu_disconnected_no_fire ( void ) {
    g_memext.connection = MEMEXT_CONNECTION_NO;
    /* Bez ohledu na bank_id. */
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_MMEXT_BANK, 0x2000, BP_MATCH_SINGLE, 9, 9, 0xFF ) );
}


void test_zone_pehu_bank_active ( void ) {
    g_memext.connection = MEMEXT_CONNECTION_YES;
    g_memext.type = MEMEXT_TYPE_PEHU;
    /* Simuluj memext_map_pwrite( addr_point=2, value=9 ) pro PEHU:
     *   ap = 2 & 0xfe = 2
     *   rawbank = 9 * 2 = 18
     *   map[2] = 18, map[3] = 19. */
    g_memext.map[ 2 ] = 18;
    g_memext.map[ 3 ] = 19;
    /* Adresa 0x2000 -> addr_point 2 -> rawbank 18 -> pehu_bank 9. */
    TEST_ASSERT_TRUE  ( bp_zone_is_active_at ( BP_ZONE_MMEXT_BANK, 0x2000, BP_MATCH_SINGLE, 9, 9, 0xFF ) );
    TEST_ASSERT_TRUE  ( bp_zone_is_active_at ( BP_ZONE_MMEXT_BANK, 0x3500, BP_MATCH_SINGLE, 9, 9, 0xFF ) );
    /* Jiný bank ID = no fire. */
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_MMEXT_BANK, 0x2000, BP_MATCH_SINGLE, 10, 10, 0xFF ) );
    /* Addr v jiném okně (= addr_point != 2/3) = no fire. */
    TEST_ASSERT_FALSE ( bp_zone_is_active_at ( BP_ZONE_MMEXT_BANK, 0x4000, BP_MATCH_SINGLE, 9, 9, 0xFF ) );
}


/* Feature D: přepočet CPU adresy na offset v PEHU bance. */
void test_mmext_pehu_offset_resolver ( void ) {
    g_memext.connection = MEMEXT_CONNECTION_YES;
    g_memext.type = MEMEXT_TYPE_PEHU;
    g_memext.map[ 2 ] = 18;   /* bank 9 dolní půl (offset 0x000..0xFFF) */
    g_memext.map[ 3 ] = 19;   /* bank 9 horní půl (offset 0x1000..0x1FFF) */

    /* addr 0x2463: rawbank 18 (sudý) -> offset (0<<12)|0x463 = 0x0463 */
    TEST_ASSERT_EQUAL_HEX32 ( 0x0463, mmext_pehu_offset_from_addr ( 0x2463 ) );
    /* addr 0x3463: rawbank 19 (lichý) -> offset (1<<12)|0x463 = 0x1463 */
    TEST_ASSERT_EQUAL_HEX32 ( 0x1463, mmext_pehu_offset_from_addr ( 0x3463 ) );

    /* Stránka bez PEHU banky (rawbank>=128) -> -1. */
    g_memext.map[ 4 ] = 200;
    TEST_ASSERT_EQUAL_INT32 ( -1, mmext_pehu_offset_from_addr ( 0x4000 ) );

    /* PEHU odpojen -> -1. */
    g_memext.connection = MEMEXT_CONNECTION_NO;
    TEST_ASSERT_EQUAL_INT32 ( -1, mmext_pehu_offset_from_addr ( 0x2463 ) );
}


/* ========================================================================= */
/*  bp_zone_active_at_pc - priority                                            */
/* ========================================================================= */


void test_active_at_pc_rom_lower ( void ) {
    g_memory.map = MEMORY_MZ800_MAP_FLAG_ROM_0000;
    TEST_ASSERT_EQUAL ( BP_ZONE_ROM_LOWER, bp_zone_active_at_pc ( 0x0042 ) );
}


void test_active_at_pc_rom_upper ( void ) {
    g_memory.map = MEMORY_MZ800_MAP_FLAG_ROM_E000;
    TEST_ASSERT_EQUAL ( BP_ZONE_ROM_UPPER, bp_zone_active_at_pc ( 0xF000 ) );
}


void test_active_at_pc_ram_default ( void ) {
    g_memory.map = 0;
    g_gdg.regDMD = 0;
    /* 0x4000 = klasická RAM. */
    TEST_ASSERT_EQUAL ( BP_ZONE_RAM, bp_zone_active_at_pc ( 0x4000 ) );
}


void test_active_at_pc_vram ( void ) {
    g_memory.map = MEMORY_MZ800_MAP_FLAG_CGRAM_VRAM;
    g_gdg.regDMD = 0;  /* MZ-800 mode */
    TEST_ASSERT_EQUAL ( BP_ZONE_VRAM_FB, bp_zone_active_at_pc ( 0x8000 ) );
}


/* ========================================================================= */
/*  Enforce filter integrace                                                  */
/* ========================================================================= */


/**
 * BP s zone=ROM_LOWER fire jen když ROM mapped.
 *
 * Test: 1) registruj BP na 0x0042 zone=ROM_LOWER, 2) ROM unmapped =>
 * enforce nesmí fire, 3) ROM mapped => fire.
 */
void test_enforce_pc_exec_zone_rom_lower ( void ) {
    int id = breakpoints_add ( 0x0042, "T", -1 );
    breakpoints_set_zone ( id, BP_ZONE_ROM_LOWER );

    /* ROM unmapped - BP nesmí fire. */
    g_memory.map = 0;
    breakpoints_enforce_pc_exec ( 0x0042 );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 0, bpt->hits );
    TEST_ASSERT_FALSE ( g_emulator.paused );

    /* ROM mapped - BP musí fire. */
    g_memory.map = MEMORY_MZ800_MAP_FLAG_ROM_0000;
    breakpoints_enforce_pc_exec ( 0x0042 );
    bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 1, bpt->hits );
    TEST_ASSERT_TRUE ( g_emulator.paused );
}


/**
 * BP s zone=CPU_VIEW (default) fire vždy = backward compat s legacy BP.
 */
void test_enforce_zone_cpu_view_default ( void ) {
    int id = breakpoints_add ( 0x4000, "T", -1 );
    /* Necháme default zone = BP_ZONE_CPU_VIEW. */

    g_memory.map = 0;  /* Žádné ROM mapping. */
    breakpoints_enforce_pc_exec ( 0x4000 );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 1, bpt->hits );
    TEST_ASSERT_TRUE ( g_emulator.paused );
}


/* ========================================================================= */
/*  String konverze                                                            */
/* ========================================================================= */


void test_zone_to_string_roundtrip ( void ) {
    en_BP_ZONE z;
    TEST_ASSERT_EQUAL_STRING ( "CPU_VIEW",  bp_zone_to_string ( BP_ZONE_CPU_VIEW ) );
    TEST_ASSERT_EQUAL_STRING ( "ROM_LOWER", bp_zone_to_string ( BP_ZONE_ROM_LOWER ) );
    TEST_ASSERT_EQUAL_STRING ( "MMEXT_BANK", bp_zone_to_string ( BP_ZONE_MMEXT_BANK ) );

    TEST_ASSERT_TRUE ( bp_zone_from_string ( "ROM_UPPER", &z ) );
    TEST_ASSERT_EQUAL ( BP_ZONE_ROM_UPPER, z );

    TEST_ASSERT_TRUE ( bp_zone_from_string ( "VRAM_FB", &z ) );
    TEST_ASSERT_EQUAL ( BP_ZONE_VRAM_FB, z );

    /* Legacy alias VRAM_RF -> VRAM_FB (rename V1.7+: RF/WF jsou MZ-800-only). */
    TEST_ASSERT_TRUE ( bp_zone_from_string ( "VRAM_RF", &z ) );
    TEST_ASSERT_EQUAL ( BP_ZONE_VRAM_FB, z );

    TEST_ASSERT_FALSE ( bp_zone_from_string ( "BLABLA", &z ) );
}


/* ========================================================================= */
/*  Persist - JSON round-trip zone + bank_id                                  */
/* ========================================================================= */


/**
 * Save BP s zone=MMEXT_BANK + bank_id=9 do tmp souboru, clear, load,
 * ověř že zone a bank_id jsou correct.
 */
void test_persist_zone_bank_id_roundtrip ( void ) {
    int id = breakpoints_add ( 0x2080, "T", -1 );
    breakpoints_set_zone ( id, BP_ZONE_MMEXT_BANK );
    breakpoints_set_bank_id ( id, 9 );
    /* Feature D: bp_addr_space musí přežít save/load. */
    {
        st_BPT *pre = &g_array_index ( g_breakpoints.breakpoints, st_BPT, 0 );
        pre->bp_addr_space = BP_ADDR_SPACE_BANK_OFFSET;
    }

    gchar *tmpfile = g_build_filename ( g_get_tmp_dir ( ),
                                         "test_bp_zone.bpt", NULL );
    breakpoints_save_to_filepath ( tmpfile );

    breakpoints_clear_all ( );
    breakpoints_load_from_filepath ( tmpfile );
    TEST_ASSERT_EQUAL_INT ( 1, g_breakpoints.breakpoints->len );

    st_BPT *bpt = &g_array_index ( g_breakpoints.breakpoints, st_BPT, 0 );
    TEST_ASSERT_EQUAL ( BP_ZONE_MMEXT_BANK, bpt->zone );
    TEST_ASSERT_EQUAL_UINT8 ( 9, bpt->bank_id );
    TEST_ASSERT_EQUAL ( BP_ADDR_SPACE_BANK_OFFSET, bpt->bp_addr_space );

    g_unlink ( tmpfile );
    g_free ( tmpfile );
}


/* ------------------------------------------------------------------------- */
/*  Feature D - en_BP_ADDR_SPACE string round-trip                             */
/* ------------------------------------------------------------------------- */

void test_addr_space_string_roundtrip ( void ) {
    TEST_ASSERT_EQUAL_STRING ( "cpu_view",    bp_addr_space_to_string ( BP_ADDR_SPACE_CPU_VIEW ) );
    TEST_ASSERT_EQUAL_STRING ( "bank_offset", bp_addr_space_to_string ( BP_ADDR_SPACE_BANK_OFFSET ) );
    /* Mimo rozsah -> bezpečný default. */
    TEST_ASSERT_EQUAL_STRING ( "cpu_view",    bp_addr_space_to_string ( (en_BP_ADDR_SPACE) 99 ) );

    en_BP_ADDR_SPACE v;
    TEST_ASSERT_TRUE  ( bp_addr_space_from_string ( "bank_offset", &v ) );
    TEST_ASSERT_EQUAL ( BP_ADDR_SPACE_BANK_OFFSET, v );
    TEST_ASSERT_TRUE  ( bp_addr_space_from_string ( "cpu_view", &v ) );
    TEST_ASSERT_EQUAL ( BP_ADDR_SPACE_CPU_VIEW, v );
    TEST_ASSERT_FALSE ( bp_addr_space_from_string ( "nonsense", &v ) );
}


/* ========================================================================= */
/*  Test runner                                                                */
/* ========================================================================= */


int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    /* Feature D - addr space string round-trip */
    RUN_TEST ( test_addr_space_string_roundtrip );

    /* CPU_VIEW */
    RUN_TEST ( test_zone_cpu_view_always_active );

    /* ROM_LOWER */
    RUN_TEST ( test_zone_rom_lower_when_mapped );
    RUN_TEST ( test_zone_rom_lower_when_unmapped );
    RUN_TEST ( test_zone_rom_lower_partial_mapping );

    /* ROM_UPPER */
    RUN_TEST ( test_zone_rom_upper_when_mapped );
    RUN_TEST ( test_zone_rom_upper_when_unmapped );

    /* VRAM_FB */
    RUN_TEST ( test_zone_vram_rf_mz800_8000 );
    RUN_TEST ( test_zone_vram_rf_mz800_a000_scrw640 );
    RUN_TEST ( test_zone_vram_rf_mz700_d000 );

    /* PCG */
    RUN_TEST ( test_zone_pcg_mz700_cgram );
    RUN_TEST ( test_zone_pcg_unmapped );

    /* RAM */
    RUN_TEST ( test_zone_ram_when_no_overlay );
    RUN_TEST ( test_zone_ram_excludes_rom );

    /* MMEXT_BANK */
    RUN_TEST ( test_zone_pehu_disconnected_no_fire );
    RUN_TEST ( test_zone_pehu_bank_active );
    RUN_TEST ( test_mmext_pehu_offset_resolver );

    /* active_at_pc */
    RUN_TEST ( test_active_at_pc_rom_lower );
    RUN_TEST ( test_active_at_pc_rom_upper );
    RUN_TEST ( test_active_at_pc_ram_default );
    RUN_TEST ( test_active_at_pc_vram );

    /* Enforce */
    RUN_TEST ( test_enforce_pc_exec_zone_rom_lower );
    RUN_TEST ( test_enforce_zone_cpu_view_default );

    /* Konverze + persist */
    RUN_TEST ( test_zone_to_string_roundtrip );
    RUN_TEST ( test_persist_zone_bank_id_roundtrip );

    return UNITY_END ( );
}
