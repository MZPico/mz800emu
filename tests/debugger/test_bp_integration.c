/*
 * test_bp_integration.c - integration testy s headless emu (Sprint 4 / F3)
 *
 * Replikuje minimum mzarch_main hot loop logiky pro headless step-by-step
 * běh CPU se zachováním BP enforcement hooks. ASM bytes jsou injectované
 * přímo do g_memory.RAM - žádný MZF loader, žádný ROM call.
 *
 * Testy ověřují že real Z80 ctx (cpu->pc, cpu->sp, register values, banking,
 * IORQ) se správně propaguje do bp_expr_ctx_t a že enforce hooks fire na
 * správných místech v reálné instrukční smyčce.
 *
 * TEST_LEVEL gating: testy běží jen v --level full (= integration), default
 * unit je přeskočí (kompromis "rychlé testy default + důkladné na vyžádání").
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include "debugger/breakpoints.h"
#include "debugger/bptmap.h"
#include "debugger/bp_vars.h"
#include "debugger/bp_event.h"
#include "debugger/dbgapi_emu.h"
#include "debugger/dbgapi_msg.h"
#include "debugger/debugger.h"
#include "emulator/emulator.h"
#include "mzarch/mzarch.h"
#include "mzarch/mz800/mz800_iorq.h"
#include "hw-generic/memory/memory.h"
#include "libs/cpu-z80/z80.h"

#include <string.h>
#include <stdio.h>
#include <glib.h>


/* ========================================================================= */
/*  Mock MSG dispatcher - zachytí BREAKPOINT_HIT eventy                       */
/* ========================================================================= */


typedef struct {
    int call_count;
    int last_id;
    uint16_t last_addr;
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
        s_mock_state.last_addr = data->addr;
        g_free ( data );
    };
}


static void mock_msg_reset ( void ) {
    memset ( &s_mock_state, 0, sizeof ( s_mock_state ) );
}


/* ========================================================================= */
/*  Headless emu fixture - replikace mzarch_main hot loop pro N kroků        */
/* ========================================================================= */


/**
 * Vykoná jednu CPU instrukci se všemi BP enforcement hooks.
 * Vrátí počet T-states, nebo 0 pokud emu paused (= BP stop).
 */
static int test_step_one ( void ) {
    g_mzarch_main.instruction_addr = g_mzarch_main.cpu->pc;

    /* PC_EXEC fast path. */
    if ( g_bptmap.bpmap[ g_mzarch_main.instruction_addr ] != BREAKPOINT_TYPE_NONE ) {
        breakpoints_enforce_pc_exec ( g_mzarch_main.instruction_addr );
    };
    /* GLOBAL per-instruction hook. */
    if ( g_bptmap.per_type_active[ BPTMAP_IDX_GLOBAL ] ) {
        breakpoints_enforce_global ( );
    };
    if ( g_emulator.paused ) return 0;

    int ts = z80_step ( g_mzarch_main.cpu );

    /* SP threshold polling. */
    if ( g_bptmap.per_type_active[ BPTMAP_IDX_SP_THRESHOLD ] ) {
        uint16_t sp = g_mzarch_main.cpu->sp;
        if ( sp != g_mzarch_main.bp_sp_prev ) {
            breakpoints_enforce_sp_threshold ( g_mzarch_main.bp_sp_prev, sp );
            g_mzarch_main.bp_sp_prev = sp;
        };
    };

    return ts;
}


/**
 * Spustí emu N kroků nebo dokud nepauseuje. Vrátí počet skutečně
 * provedených kroků.
 */
static int test_run_steps ( int max_steps ) {
    int n = 0;
    while ( n < max_steps && !g_emulator.paused ) {
        test_step_one ( );
        n++;
    }
    return n;
}


/**
 * Inject ASM bytes do RAM od adresy addr a nastav PC.
 */
static void test_inject_asm ( uint16_t addr, const uint8_t *bytes, size_t len ) {
    size_t i;
    for ( i = 0; i < len; i++ ) {
        g_memory.RAM[ addr + i ] = bytes[ i ];
    };
    g_mzarch_main.cpu->pc = addr;
    g_mzarch_main.bp_sp_prev = g_mzarch_main.cpu->sp;
}


/* ========================================================================= */
/*  setUp / tearDown                                                          */
/* ========================================================================= */


void setUp ( void ) {
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    bp_vars_clear_storage ( );
    g_emulator.paused = false;
    mock_msg_reset ( );
    dbgapi_emu_register_msg_dispatcher ( mock_msg_dispatcher, NULL );

    /* Vyčistit ROM mapping aby PC v RAM 0x1000 nešlo přes ROM hook. */
    g_memory.map = 0;

    /* Přepnout CPU memory/port callbacky na "with_logging" varianty,
     * které volají breakpoints_enforce_mem_w / iorq_w / iorq_r enforce
     * hooks. Bez toho by MEM_W / IORQ BP nefirovaly. PC_EXEC enforce
     * je v hot loop sám (test_step_one volá explicit). */
    z80_set_mread  ( g_mzarch_main.cpu, memory_read_with_logging_cb,  NULL );
    z80_set_mwrite ( g_mzarch_main.cpu, memory_write_with_logging_cb, NULL );
    z80_set_pread  ( g_mzarch_main.cpu, port_read_with_logging_cb,    NULL );
    z80_set_pwrite ( g_mzarch_main.cpu, port_write_with_logging_cb,   NULL );

    /* Reset CPU state - clean PC, SP, registers, halted flag. */
    z80_t *cpu = g_mzarch_main.cpu;
    cpu->pc = 0x0000;
    cpu->sp = 0xFFFE;
    cpu->af.w = 0;
    cpu->bc.w = 0;
    cpu->de.w = 0;
    cpu->hl.w = 0;
    cpu->iff1 = 0;
    cpu->iff2 = 0;
    cpu->im = 0;
    cpu->halted = false;  /* HALT z předchozího testu by jinak zablokoval z80_step */
    g_debugger.skip_bp_at_pc = -1;
}


void tearDown ( void ) {
    dbgapi_emu_register_msg_dispatcher ( NULL, NULL );
}


/* ========================================================================= */
/*  Helper: vytvoří BP daného typu                                            */
/* ========================================================================= */


static int make_bp ( en_BPT_TYPE type, uint16_t addr ) {
    int id = breakpoints_add ( addr, "T", -1 );
    breakpoints_set_type ( id, type );
    return id;
}


/* ========================================================================= */
/*  F3.2 Integration scénáře                                                  */
/* ========================================================================= */


/**
 * Test 1: PC_EXEC v jednoduché smyčce.
 *
 * ASM:
 *   1000: 06 05         LD B, 5
 *   1002: 10 FE         DJNZ -2  ; cíl je sám sebe-2 = 0x1002 (loop)
 *   1004: 00            NOP      (po loopu)
 *   1005: 76            HALT     (terminator)
 *
 * BP na 0x1002 (DJNZ) s action "$cnt += 1". Po doběhnutí očekáváme
 * 5 hitů (B=5..1, DJNZ vykonána 5×).
 */
void test_int_pc_exec_loop ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x06, 0x05,       /* LD B, 5 */
        0x10, 0xFE,       /* DJNZ -2 */
        0x00,             /* NOP */
        0x76,             /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_PC_EXEC, 0x1002 );
    breakpoints_set_action ( id, "$cnt += 1" );

    test_run_steps ( 50 );

    /* DJNZ provedena 5× (B = 5..1). */
    TEST_ASSERT_EQUAL_INT32 ( 5, bp_var_get ( "cnt" ) );
    TEST_ASSERT_FALSE ( g_emulator.paused );
}


/**
 * Test 2: MEM_W zachytí jednu hodnotu napsanou CPU instrukcí.
 *
 * V1.5 fix: CPU writes přes z80 mwrite callback (= memory_write_with_logging_cb
 * v debugger active mode) volají breakpoints_enforce_mem_w. Tento test ověřuje
 * že MEM_W BP fire pro skutečný CPU LD (nn),A.
 *
 * ASM:
 *   1000: 3E 42         LD A, 0x42
 *   1002: 32 50 10      LD (0x1050), A
 *   1005: 76            HALT
 *
 * BP MEM_W na 0x1050 s action "$last = Value". Po doběhnutí očekáváme
 * $last = 0x42.
 */
void test_int_mem_w_capture_value ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x3E, 0x42,             /* LD A, 0x42 */
        0x32, 0x50, 0x10,       /* LD (0x1050), A */
        0x76,                   /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_MEM_W, 0x1050 );
    breakpoints_set_action ( id, "$last = Value" );

    test_run_steps ( 20 );

    TEST_ASSERT_EQUAL_INT32 ( 0x42, bp_var_get ( "last" ) );
    (void) id;
}


/**
 * Test 3: MEM_W RANGE - více CPU writes do oblasti.
 *
 * ASM zapisuje 4× do 0x1050..0x1053 přes (HL), přičemž BP RANGE je
 * 0x1050..0x1052 (= 3 hits).
 *
 *   1000: 21 50 10       LD HL, 0x1050
 *   1003: 36 11          LD (HL), 0x11   ; 0x1050 = hit
 *   1005: 23             INC HL
 *   1006: 36 22          LD (HL), 0x22   ; 0x1051 = hit
 *   1008: 23             INC HL
 *   1009: 36 33          LD (HL), 0x33   ; 0x1052 = hit
 *   100B: 23             INC HL
 *   100C: 36 44          LD (HL), 0x44   ; 0x1053 = mimo, no fire
 *   100E: 76             HALT
 */
void test_int_mem_w_range ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x21, 0x50, 0x10,    /* LD HL, 0x1050 */
        0x36, 0x11,          /* LD (HL), 0x11 */
        0x23,                /* INC HL */
        0x36, 0x22,          /* LD (HL), 0x22 */
        0x23,                /* INC HL */
        0x36, 0x33,          /* LD (HL), 0x33 */
        0x23,                /* INC HL */
        0x36, 0x44,          /* LD (HL), 0x44 (mimo range) */
        0x76,                /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_MEM_W, 0x1050 );
    breakpoints_set_addr_match_mode ( id, BP_MATCH_RANGE );
    breakpoints_set_addr_range ( id, 0x1050, 0x1052 );
    breakpoints_set_action ( id, "$cnt += 1" );

    test_run_steps ( 30 );

    TEST_ASSERT_EQUAL_INT32 ( 3, bp_var_get ( "cnt" ) );
    (void) id;
}


/**
 * Test 4: PC_EXEC stop semantika - emu se zastaví na BP bez action.
 *
 * ASM:
 *   1000: 00            NOP
 *   1001: 00            NOP
 *   1002: 00            NOP
 *   1003: 76            HALT
 *
 * BP PC_EXEC na 0x1002 bez action = stop. Run 10 kroků - paused musí být
 * true a CPU má pc=0x1002 (instrukce ještě nevykonána).
 */
void test_int_pc_exec_stop_semantics ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x00, 0x00, 0x00, 0x76,
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_PC_EXEC, 0x1002 );
    (void) id;

    int n = test_run_steps ( 10 );

    TEST_ASSERT_TRUE ( g_emulator.paused );
    TEST_ASSERT_EQUAL_HEX16 ( 0x1002, g_mzarch_main.cpu->pc );
    TEST_ASSERT_EQUAL_INT ( 1, s_mock_state.call_count );
    /* Provedeny 2 instrukce (2 NOP), pak BP fire před 3. */
    TEST_ASSERT_EQUAL_INT ( 3, n );  /* 2 NOP + 1 step kde fire pause */
}


/**
 * Test 5: Action disable_self - jen první hit fire.
 *
 * ASM: 5× NOP + HALT. BP PC_EXEC na 0x1002 s action "disable_self;
 * $hits += 1". První execute na 0x1002 → fire, BP disable. Další iterace
 * (= cyklický návrat NEJEDE - jen 1× projde 0x1002), ale ověřujeme že
 * $hits = 1.
 */
void test_int_action_disable_self ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x76,  /* 5× NOP + HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_PC_EXEC, 0x1002 );
    breakpoints_set_action ( id, "$hits += 1; disable_self" );

    test_run_steps ( 20 );

    TEST_ASSERT_EQUAL_INT32 ( 1, bp_var_get ( "hits" ) );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_FALSE ( bpt->enabled );
}


/**
 * Test 6: Action set <reg> - modifikace CPU registru z action.
 *
 * ASM:
 *   1000: 3E 10         LD A, 0x10
 *   1002: 00            NOP    ; BP zde, action set A 0xFF
 *   1003: 76            HALT
 *
 * Po BP musí A = 0xFF (přepsáno action), ne 0x10.
 */
void test_int_action_set_reg ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x3E, 0x10,    /* LD A, 0x10 */
        0x00,          /* NOP */
        0x76,          /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_PC_EXEC, 0x1002 );
    breakpoints_set_action ( id, "set A 0xFF" );

    test_run_steps ( 10 );

    TEST_ASSERT_EQUAL_HEX8 ( 0xFF, g_mzarch_main.cpu->af.h );
    (void) id;
}


/**
 * Test 7: Action poke - zápis do RAM z action skriptu.
 *
 * ASM: NOP × 3 + HALT.
 * BP PC_EXEC na 0x1001 s action "poke 0x1500 0xAB". Po BP musí
 * RAM[0x1500] = 0xAB.
 */
void test_int_action_poke ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x00, 0x00, 0x00, 0x76,
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );
    g_memory.RAM[ 0x1500 ] = 0x00;  /* baseline */

    int id = make_bp ( BPT_TYPE_PC_EXEC, 0x1001 );
    breakpoints_set_action ( id, "poke 0x1500 0xAB" );

    test_run_steps ( 10 );

    TEST_ASSERT_EQUAL_HEX8 ( 0xAB, g_memory.RAM[ 0x1500 ] );
    (void) id;
}


/**
 * Test 8: SP_THRESHOLD edge crossing po CALL.
 *
 * ASM:
 *   1000: 31 00 80      LD SP, 0x8000
 *   1003: CD 00 20      CALL 0x2000     ; SP klesne pod 0x7FFF
 *   1006: 76            HALT
 *   2000: C9            RET
 *
 * BP SP_THRESHOLD na 0x7FFF SINGLE - fire při SP přechodu z >= 0x7FFF na <.
 * CALL pushne 2 byte (návratová addr), SP -> 0x7FFE = pod threshold.
 */
void test_int_sp_threshold_call ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x31, 0x00, 0x80,    /* LD SP, 0x8000 */
        0xCD, 0x00, 0x20,    /* CALL 0x2000 */
        0x76,                /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    /* RET v 0x2000 (= ASM "C9"). */
    g_memory.RAM[ 0x2000 ] = 0xC9;

    int id = breakpoints_add ( 0, "SP", -1 );
    breakpoints_set_type ( id, BPT_TYPE_SP_THRESHOLD );
    breakpoints_set_sp_mode ( id, BP_SP_SINGLE );
    breakpoints_set_sp_threshold ( id, 0x7FFF );
    breakpoints_set_action ( id, "$crossed += 1" );

    test_run_steps ( 20 );

    TEST_ASSERT_EQUAL_INT32 ( 1, bp_var_get ( "crossed" ) );
}


/**
 * Test 9: Hit count - fire jen na N. hitu.
 *
 * ASM: DJNZ loop B=10×. BP PC_EXEC na DJNZ s hit_count=5 → fire jen
 * na 5. iteraci, pak stop.
 */
void test_int_hit_count_5 ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x06, 0x0A,          /* LD B, 10 */
        0x10, 0xFE,          /* DJNZ -2 */
        0x76,                /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_PC_EXEC, 0x1002 );
    breakpoints_set_hit_count ( id, 5 );

    test_run_steps ( 50 );

    /* Po 5 iteracích DJNZ - fire (= stop). */
    TEST_ASSERT_TRUE ( g_emulator.paused );
    TEST_ASSERT_EQUAL_INT ( 1, s_mock_state.call_count );

    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 5, bpt->hits );
}


/**
 * Test 10: Skip count - prvních N hitů přeskočeno.
 *
 * Stejné jako test 9 ale s skip_count=3 - fire na 4. iteraci.
 */
void test_int_skip_count_3 ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x06, 0x0A,          /* LD B, 10 */
        0x10, 0xFE,          /* DJNZ -2 */
        0x76,                /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_PC_EXEC, 0x1002 );
    breakpoints_set_skip_count ( id, 3 );

    test_run_steps ( 50 );

    /* Po 3 skip + 1 hit = 4. iterace fire. */
    TEST_ASSERT_TRUE ( g_emulator.paused );
    st_BPT *bpt = breakpoints_find_by_id ( id );
    TEST_ASSERT_EQUAL_UINT64 ( 1, bpt->hits );  /* skip není v hits */
    /* CPU PC = 0x1002 (= ještě nevykonáno po pause). */
    TEST_ASSERT_EQUAL_HEX16 ( 0x1002, g_mzarch_main.cpu->pc );
}


/**
 * Test 11: Condition Address == X v MEM_W (CPU-driven).
 *
 * BP MEM_W RANGE 0x3000..0x4000 s condition "Address == 0x4000". Hit jen
 * pro 0x4000, ne pro ostatní v range. Writes provedeny CPU instrukcemi
 * LD (HL),val přes z80 mwrite callback.
 *
 * ASM:
 *   1000: 21 00 30       LD HL, 0x3000
 *   1003: 36 AA          LD (HL), 0xAA   ; in range, condition false
 *   1005: 21 00 40       LD HL, 0x4000
 *   1008: 36 BB          LD (HL), 0xBB   ; in range, condition true
 *   100A: 76             HALT
 */
void test_int_mem_w_condition_filter ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x21, 0x00, 0x30,    /* LD HL, 0x3000 */
        0x36, 0xAA,          /* LD (HL), 0xAA */
        0x21, 0x00, 0x40,    /* LD HL, 0x4000 */
        0x36, 0xBB,          /* LD (HL), 0xBB */
        0x76,                /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_MEM_W, 0x3000 );
    breakpoints_set_addr_match_mode ( id, BP_MATCH_RANGE );
    breakpoints_set_addr_range ( id, 0x3000, 0x4000 );
    breakpoints_set_expr ( id, "Address == 0x4000" );
    breakpoints_set_action ( id, "$hit_v = Value" );

    test_run_steps ( 30 );

    /* Jen 0xBB protože condition matches jen 0x4000. */
    TEST_ASSERT_EQUAL_INT32 ( 0xBB, bp_var_get ( "hit_v" ) );
    (void) id;
}


/**
 * Test 12: MEM_R zachytí jednu hodnotu přečtenou CPU instrukcí.
 *
 * 2.4b fix: CPU reads přes z80 mread callback (= memory_read_with_logging_cb
 * v debugger active mode) volají breakpoints_enforce_mem_r jen pro
 * access_kind == R (data read, NE instruction fetch).
 *
 * ASM:
 *   1000: 3E 42         LD A, 0x42
 *   1002: 32 50 10      LD (0x1050), A   ; write 0x42 do RAM
 *   1005: 3A 50 10      LD A, (0x1050)   ; data read = MEM_R fire
 *   1008: 76            HALT
 *
 * BP MEM_R na 0x1050 s action "$reads += 1; $last = Value". Po doběhnutí
 * očekáváme $reads == 1 (= jen LD A,(nn) fire, NE instrukční fetch
 * žádného z opcode bytů 0x3A/0x50/0x10) a $last == 0x42.
 */
void test_int_mem_r_capture_value ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x3E, 0x42,             /* LD A, 0x42 */
        0x32, 0x50, 0x10,       /* LD (0x1050), A */
        0x3A, 0x50, 0x10,       /* LD A, (0x1050) */
        0x76,                   /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_MEM_R, 0x1050 );
    breakpoints_set_action ( id, "$reads += 1; $last = Value" );

    test_run_steps ( 20 );

    /* Jen 1 fire = LD A,(0x1050) = data read. Instrukční fetch bytů
     * 0x3A/0x50/0x10 NEsmí fire (= access_kind X, filtrováno). */
    TEST_ASSERT_EQUAL_INT32 ( 1, bp_var_get ( "reads" ) );
    TEST_ASSERT_EQUAL_INT32 ( 0x42, bp_var_get ( "last" ) );
    (void) id;
}


/**
 * Test 13: MEM_R NEsmí fire na instruction fetch bytech.
 *
 * Kritický test sémantiky filtru: BP MEM_R na adrese 0x1000 (= start
 * instrukčního streamu), kde běží 16× NOP. Z80 dělá M1 fetch každého NOP
 * = 16× read z 0x1000..0x100F. Filter access_kind == R MUSÍ tyto fetche
 * odfiltrovat (= MHMAP_ACCESS_X), takže fire == 0.
 *
 * ASM:
 *   1000..100F: 16× NOP
 *   1010: HALT
 */
void test_int_mem_r_no_fire_on_instruction_fetch ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 16× NOP */
        0x76,                                            /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_MEM_R, 0x1000 );
    breakpoints_set_action ( id, "$fires += 1" );

    test_run_steps ( 30 );

    /* Žádný fire - všechny CPU reads jsou M1 instruction fetch
     * (access_kind X), filter access_kind == R je odmítne. */
    TEST_ASSERT_EQUAL_INT32 ( 0, bp_var_get ( "fires" ) );
    (void) id;
}


/**
 * Test 14: MEM_R RANGE - 3 data reads přes (HL).
 *
 * ASM:
 *   1000: 21 50 10       LD HL, 0x1050
 *   1003: 7E             LD A, (HL)        ; data read 0x1050 = hit
 *   1004: 23             INC HL
 *   1005: 7E             LD A, (HL)        ; data read 0x1051 = hit
 *   1006: 23             INC HL
 *   1007: 7E             LD A, (HL)        ; data read 0x1052 = hit
 *   1008: 76             HALT
 *
 * BP MEM_R RANGE 0x1050..0x1052 s action "$cnt += 1". Cílová paměť
 * pre-vyplněna (0xAA, 0xBB, 0xCC) aby read měl co vracet.
 */
void test_int_mem_r_range ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x21, 0x50, 0x10,    /* LD HL, 0x1050 */
        0x7E,                /* LD A, (HL) */
        0x23,                /* INC HL */
        0x7E,                /* LD A, (HL) */
        0x23,                /* INC HL */
        0x7E,                /* LD A, (HL) */
        0x76,                /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    /* Pre-fill target memory aby reads vracely deterministické hodnoty. */
    g_memory.RAM[ 0x1050 ] = 0xAA;
    g_memory.RAM[ 0x1051 ] = 0xBB;
    g_memory.RAM[ 0x1052 ] = 0xCC;

    int id = make_bp ( BPT_TYPE_MEM_R, 0x1050 );
    breakpoints_set_addr_match_mode ( id, BP_MATCH_RANGE );
    breakpoints_set_addr_range ( id, 0x1050, 0x1052 );
    breakpoints_set_action ( id, "$cnt += 1" );

    test_run_steps ( 30 );

    TEST_ASSERT_EQUAL_INT32 ( 3, bp_var_get ( "cnt" ) );
    (void) id;
}


/**
 * V1.6+ TODO 4.3: Integration test pro PC_EXEC MASK plny support.
 *
 * Predtim (V1.5.E) MASK BP fire jen na ref addr (= degraded fallback).
 * V1.6+ MASK fire na **vsech** matching addrs pres BPTMAP_IDX_PC_EXEC_NONSINGLE
 * per-instruction iteraci.
 *
 * Scenar: krátká smyčka přes adresy 0x1000-0x100F. BP MASK na high byte
 * 0x10 (= mask 0xFF00, ref 0x1000) = mel by fire prakticky kazdou instrukci
 * v rozsahu.
 *
 * ASM:
 *   1000: 21 00 10        LD HL, 0x1000
 *   1003: 23              INC HL          ; HL=1001
 *   1004: 23              INC HL          ; HL=1002
 *   1005: 23              INC HL          ; HL=1003
 *   1006: 76              HALT
 *
 * BP PC_EXEC MASK addr 0x1000, mask 0xFF00 + action "$cnt += 1".
 * Ocekavame ze BP zafire na PC = 0x1000, 0x1003, 0x1004, 0x1005, 0x1006
 * (vsech 5 instrukcnich adres v 0x10xx range = 5 hits).
 */
void test_int_pc_exec_mask_full_support ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x21, 0x00, 0x10,    /* LD HL, 0x1000 */
        0x23,                /* INC HL */
        0x23,                /* INC HL */
        0x23,                /* INC HL */
        0x76,                /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    int id = make_bp ( BPT_TYPE_PC_EXEC, 0x1000 );
    breakpoints_set_addr_match_mode ( id, BP_MATCH_MASK );
    breakpoints_set_addr_mask ( id, 0xFF00 );  /* match high byte = 0x10 */
    breakpoints_set_action ( id, "$cnt += 1" );

    test_run_steps ( 30 );

    /* 5 instrukcnich adres (0x1000, 0x1003, 0x1004, 0x1005, 0x1006). */
    TEST_ASSERT_EQUAL_INT32 ( 5, bp_var_get ( "cnt" ) );
    (void) id;
}


/* ========================================================================= */
/*  MAIN                                                                     */
/* ========================================================================= */


int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    RUN_TEST ( test_int_pc_exec_loop );
    RUN_TEST ( test_int_mem_w_capture_value );
    RUN_TEST ( test_int_mem_w_range );
    RUN_TEST ( test_int_pc_exec_stop_semantics );
    RUN_TEST ( test_int_action_disable_self );
    RUN_TEST ( test_int_action_set_reg );
    RUN_TEST ( test_int_action_poke );
    RUN_TEST ( test_int_sp_threshold_call );
    RUN_TEST ( test_int_hit_count_5 );
    RUN_TEST ( test_int_skip_count_3 );
    RUN_TEST ( test_int_mem_w_condition_filter );
    RUN_TEST ( test_int_mem_r_capture_value );
    RUN_TEST ( test_int_mem_r_no_fire_on_instruction_fetch );
    RUN_TEST ( test_int_mem_r_range );
    RUN_TEST ( test_int_pc_exec_mask_full_support );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
