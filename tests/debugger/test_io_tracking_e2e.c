/*
 * test_io_tracking_e2e.c - end-to-end IORQ tracking pipeline (Sprint 3 / V1.5).
 *
 * Integration test podobný `test_bp_integration.c`, ale pro IORQ tracking
 * pipeline:
 *
 *   1. Aktivovat g_io_window_tracking_active = 1.
 *   2. Inject ASM v RAM který generuje IORQ (OUT/IN).
 *   3. Spustit z80_step přes test_run_steps.
 *   4. Verify že hook port_*_with_logging_cb zaznamenal:
 *      - g_io_activity[bus_addr].total_hits_in / .total_hits_out inkrementován
 *        per smer (V1.5 fix #1: 16-bit, V1.5.D fix #2: per-direction counters)
 *      - g_io_history.count >= N
 *      - io_history_get(idx) vrátí očekávaný port / value / type.
 *
 * Pokrývá:
 *  - 8-bit OUT / IN pres LD A,n + OUT (n),A / IN A,(n)
 *  - 16-bit IORQ přes LD BC,n16 + OUT (C),A / IN A,(C)
 *  - Tracking flag = 0 → žádný záznam
 *  - Activity counter inkrement vícenásobně
 *
 * TEST_LEVEL gating: MZTEST_LEVEL_FULL (= integration test default skip).
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include "debugger/breakpoints.h"
#include "debugger/bptmap.h"
#include "debugger/bp_vars.h"
#include "debugger/dbgapi_emu.h"
#include "debugger/dbgapi_msg.h"
#include "debugger/debugger.h"
#include "debugger/io_activity.h"
#include "debugger/io_history.h"
#include "emulator/emulator.h"
#include "mzarch/mzarch.h"
#include "mzarch/mz800/mz800_iorq.h"
#include "hw-generic/memory/memory.h"
#include "libs/cpu-z80/z80.h"

#include <string.h>
#include <stdio.h>
#include <glib.h>


/* ========================================================================= */
/*  Mock MSG dispatcher (= prevent dispatch crash, žádný BP fire očekáván)  */
/* ========================================================================= */


static void mock_msg_dispatcher ( en_DBGAPI_MSG msg,
                                   st_DBGAPI_MSG_DATA *data,
                                   void *user_data ) {
    (void) msg;
    (void) user_data;
    if ( data ) {
        g_free ( data );
    };
}


/* ========================================================================= */
/*  Headless emu fixture - replikace test_bp_integration patternu             */
/* ========================================================================= */


/**
 * Vykoná jednu CPU instrukci.
 */
static int test_step_one ( void ) {
    g_mzarch_main.instruction_addr = g_mzarch_main.cpu->pc;

    if ( g_emulator.paused ) return 0;

    return z80_step ( g_mzarch_main.cpu );
}


/**
 * Spustí emu N kroků.
 */
static int test_run_steps ( int max_steps ) {
    int n = 0;
    while ( n < max_steps && !g_emulator.paused ) {
        test_step_one ( );
        n++;
    };
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
}


/* ========================================================================= */
/*  setUp / tearDown                                                          */
/* ========================================================================= */


void setUp ( void ) {
    breakpoints_clear_all ( );
    g_breakpoints.next_id = 1;
    bp_vars_clear_storage ( );
    g_emulator.paused = false;
    dbgapi_emu_register_msg_dispatcher ( mock_msg_dispatcher, NULL );

    /* Zajistit fresh stav io_history + io_activity. */
    io_history_init ( IO_HISTORY_DEFAULT_CAPACITY );
    io_history_clear ( );
    io_activity_init ( );

    /* Vyčistit ROM mapping aby PC v RAM 0x1000 nešlo přes ROM hook. */
    g_memory.map = 0;

    /* Přepnout CPU port callbacky na "with_logging" varianty - to je kde
     * je hook na io_activity_record_hit + io_history_record. Bez toho by
     * tracking nefungoval (= default port_read_cb / port_write_cb). */
    z80_set_mread  ( g_mzarch_main.cpu, memory_read_with_logging_cb,  NULL );
    z80_set_mwrite ( g_mzarch_main.cpu, memory_write_with_logging_cb, NULL );
    z80_set_pread  ( g_mzarch_main.cpu, port_read_with_logging_cb,    NULL );
    z80_set_pwrite ( g_mzarch_main.cpu, port_write_with_logging_cb,   NULL );

    /* Reset CPU state. */
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
    cpu->halted = false;
    g_debugger.skip_bp_at_pc = -1;

    /* Tracking flag default OFF, jednotlivé testy si zapnou explicit. */
    g_io_window_tracking_active = 0;
}


void tearDown ( void ) {
    g_io_window_tracking_active = 0;
    io_history_clear ( );
    io_activity_init ( );
    dbgapi_emu_register_msg_dispatcher ( NULL, NULL );
}


/* ========================================================================= */
/*  E2E testy IORQ tracking                                                   */
/* ========================================================================= */


/**
 * Test 1: 1 OUT instrukce → 1 history event s port match + value match.
 *
 * ASM:
 *   1000: 3E 42         LD A, 0x42
 *   1002: D3 CE         OUT (0xCE), A    ; 8-bit IORQ write na port 0xCE
 *   1004: 76            HALT
 *
 * Pozn.: Z80 OUT (n),A staví port adresu jako (A << 8) | n - high byte BC
 * je hodnota A (= 0x42). io_history_record bere addr přímo z IORQ
 * adresy (= 16-bit). V1.5 fix #1: io_activity_record_hit nyní indexuje
 * plnou 16-bit adresu (= 0x42CE pro tento test), ne pouze low byte.
 */
void test_iorq_out_records_history ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x3E, 0x42,             /* LD A, 0x42 */
        0xD3, 0xCE,             /* OUT (0xCE), A */
        0x76,                   /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    g_io_window_tracking_active = 1;

    test_run_steps ( 10 );

    /* History musí mít alespoň 1 event (jen 1 OUT). */
    TEST_ASSERT_GREATER_OR_EQUAL_size_t ( 1, g_io_history.count );

    /* Hledej OUT event s portem 0xCE. */
    int found = 0;
    for ( size_t i = 0; i < g_io_history.count; i++ ) {
        const st_IO_HISTORY_EVENT *e = io_history_get ( i );
        TEST_ASSERT_NOT_NULL ( e );
        if ( ! ( e->flags & IO_HISTORY_FLAG_READ ) && ( e->port & 0xFF ) == 0xCE && e->value == 0x42 ) {
            found = 1;
            break;
        };
    };
    TEST_ASSERT_TRUE_MESSAGE ( found, "OUT (0xCE),0x42 nezaznamenán v io_history" );

    /* V1.5 fix #1: 16-bit indexing. OUT (0xCE),A staví bus addr =
     * (A << 8) | 0xCE = 0x42CE pro A=0x42. Direct slot 0x42CE má hit.
     * V1.5.D fix #2: OUT inkrementuje total_hits_out, ne total_hits_in. */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64 ( 1, g_io_activity[ 0x42CE ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0x42CE ].total_hits_in );
    /* Slot 0x00CE (= low byte alone) sám nedostává nic. */
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0x00CE ].total_hits_out );
}


/**
 * Test 2: 1 IN instrukce → 1 history event s is_in=1.
 *
 * ASM:
 *   1000: DB CE         IN A, (0xCE)     ; 8-bit IORQ read z 0xCE
 *   1002: 76            HALT
 */
void test_iorq_in_records_history ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0xDB, 0xCE,             /* IN A, (0xCE) */
        0x76,                   /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    g_io_window_tracking_active = 1;

    test_run_steps ( 10 );

    TEST_ASSERT_GREATER_OR_EQUAL_size_t ( 1, g_io_history.count );

    /* Najdi IN event na portu 0xCE. */
    int found_in = 0;
    for ( size_t i = 0; i < g_io_history.count; i++ ) {
        const st_IO_HISTORY_EVENT *e = io_history_get ( i );
        TEST_ASSERT_NOT_NULL ( e );
        if ( ( e->flags & IO_HISTORY_FLAG_READ ) && ( e->port & 0xFF ) == 0xCE ) {
            found_in = 1;
            break;
        };
    };
    TEST_ASSERT_TRUE_MESSAGE ( found_in, "IN A,(0xCE) nezaznamenán v io_history (is_in=1)" );
}


/**
 * Test 3: 5 OUTs na stejný port → activity counter == 5.
 *
 * ASM:
 *   1000: 3E 11         LD A, 0x11
 *   1002: D3 CE         OUT (0xCE), A
 *   1004: 3E 22         LD A, 0x22
 *   1006: D3 CE         OUT (0xCE), A
 *   1008: 3E 33         LD A, 0x33
 *   100A: D3 CE         OUT (0xCE), A
 *   100C: 3E 44         LD A, 0x44
 *   100E: D3 CE         OUT (0xCE), A
 *   1010: 3E 55         LD A, 0x55
 *   1012: D3 CE         OUT (0xCE), A
 *   1014: 76            HALT
 */
void test_iorq_activity_counter_increments ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x3E, 0x11, 0xD3, 0xCE,
        0x3E, 0x22, 0xD3, 0xCE,
        0x3E, 0x33, 0xD3, 0xCE,
        0x3E, 0x44, 0xD3, 0xCE,
        0x3E, 0x55, 0xD3, 0xCE,
        0x76,
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    g_io_window_tracking_active = 1;

    test_run_steps ( 30 );

    /* V1.5 fix #1: 5 OUTs s ruznymi A hodnotami (0x11..0x55) → 5 hits
     * rozpadnutých přes 5 různých 16-bit slotů (0x11CE, 0x22CE,
     * 0x33CE, 0x44CE, 0x55CE). Per-slot total_hits_out = 1 každý.
     * Aggregator přes 256 high-byte slotů = 5.
     * V1.5.D fix #2: per-direction counters - OUT plni total_hits_out. */
    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0x11CE ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0x22CE ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0x33CE ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0x44CE ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 1, g_io_activity[ 0x55CE ].total_hits_out );
}


/**
 * Test 4: Tracking flag = 0 → žádný záznam.
 *
 * Stejné ASM jako test 1, ale g_io_window_tracking_active = 0. Hook
 * uvnitř port_*_with_logging_cb je gated, takže nic nezaznamenává.
 */
void test_iorq_tracking_off_no_record ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x3E, 0x42,             /* LD A, 0x42 */
        0xD3, 0xCE,             /* OUT (0xCE), A */
        0x76,                   /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    /* Explicit OFF - reaffirm setUp default. */
    g_io_window_tracking_active = 0;

    test_run_steps ( 10 );

    /* Žádný záznam. */
    TEST_ASSERT_EQUAL_size_t ( 0, g_io_history.count );
    /* V1.5 fix #1: 16-bit slot kontrola (= bus addr 0x42CE pro A=0x42).
     * Tracking off → counter zustal 0. V1.5.D fix #2: per-smer. */
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0x42CE ].total_hits_out );
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0x42CE ].total_hits_in );
}


/**
 * Test 5: 16-bit IORQ pres LD BC,n16 + OUT (C),A → port v history je
 * plná 16-bit BC adresa (= 0x06CF pro CRTC family BCOL).
 *
 * Z80 OUT (C),A pattern: B = high byte, C = low byte port. io_history
 * uloží addr = full 16-bit BC.
 *
 * ASM:
 *   1000: 01 CF 06       LD BC, 0x06CF   ; B=0x06, C=0xCF
 *   1003: 3E 05          LD A, 0x05      ; payload (BCOL = magenta)
 *   1005: ED 79          OUT (C), A      ; 16-bit IORQ na 0x06CF
 *   1007: 76             HALT
 */
void test_iorq_16bit_port_recorded ( void ) {
    MZTEST_REQUIRE_LEVEL ( MZTEST_LEVEL_FULL );

    static const uint8_t code[] = {
        0x01, 0xCF, 0x06,       /* LD BC, 0x06CF */
        0x3E, 0x05,             /* LD A, 0x05 */
        0xED, 0x79,             /* OUT (C), A */
        0x76,                   /* HALT */
    };
    test_inject_asm ( 0x1000, code, sizeof ( code ) );

    g_io_window_tracking_active = 1;

    test_run_steps ( 10 );

    TEST_ASSERT_GREATER_OR_EQUAL_size_t ( 1, g_io_history.count );

    /* Hledej OUT event s portem 0x06CF (= full 16-bit BC). */
    int found_16 = 0;
    for ( size_t i = 0; i < g_io_history.count; i++ ) {
        const st_IO_HISTORY_EVENT *e = io_history_get ( i );
        TEST_ASSERT_NOT_NULL ( e );
        if ( ! ( e->flags & IO_HISTORY_FLAG_READ ) && e->port == 0x06CF && e->value == 0x05 ) {
            found_16 = 1;
            break;
        };
    };
    TEST_ASSERT_TRUE_MESSAGE ( found_16,
        "OUT (C),A s BC=0x06CF nezaznamenán s plnou 16-bit adresou" );

    /* V1.5 fix #1: 16-bit indexing - bus addr = 0x06CF (= B=0x06, C=0xCF).
     * Counter je primo na 16-bit slotu (= ne na low-byte 0xCF samostatně).
     * V1.5.D fix #2: OUT inkrementuje total_hits_out. */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64 ( 1, g_io_activity[ 0x06CF ].total_hits_out );
    /* Slot 0x00CF (= low byte alone) sám nemá hit (high byte je 0x06). */
    TEST_ASSERT_EQUAL_UINT64 ( 0, g_io_activity[ 0x00CF ].total_hits_out );
}


/* ========================================================================= */
/*  MAIN                                                                     */
/* ========================================================================= */


int main ( int argc, char *argv[] ) {
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    RUN_TEST ( test_iorq_out_records_history );
    RUN_TEST ( test_iorq_in_records_history );
    RUN_TEST ( test_iorq_activity_counter_increments );
    RUN_TEST ( test_iorq_tracking_off_no_record );
    RUN_TEST ( test_iorq_16bit_port_recorded );

    int result = UNITY_END ( );

    mztest_teardown ( );
    return result;
}
