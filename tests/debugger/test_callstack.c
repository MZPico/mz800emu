/*
 * test_callstack.c - unit testy shadow stack subsystému callstack
 *
 * Pokrývá:
 *  - happy path push CALL + pop RET (3x do hloubky 3)
 *  - overflow při překročení CALLSTACK_MAX_DEPTH
 *  - synthetic divergent emit při RET nad prázdným shadow
 *  - longjmp pop hluboko v stacku (= match middle entry)
 *  - listener register/unregister + on_enter/on_exit fíringy
 *  - snapshot_get + snapshot_free (= atomická kopie pro UI)
 *  - reset (shadow + stats)
 *  - RST push přes callstack_on_cpu_ctrl_event
 *  - IRQ accept push přes callstack_on_irq_accept (IM 2)
 *  - IFF change RETI pop přes callstack_on_iff_change
 *  - gate-off (= g_callstack_active == 0 nezpůsobí push)
 *
 * Test neběží reálný Z80 - simuluje hot-path callbacky přímým
 * voláním cpu->call_cb / cpu->ret_cb (které callstack_set_active
 * zaregistrovala) a internal fan-out hooků.
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "debugger/callstack.h"
#include "libs/cpu-z80/z80.h"
#include "mzarch/mzarch.h"


/* ========================================================================= */
/*  Test-only listener accounting                                            */
/* ========================================================================= */


/**
 * @brief Counters pro listener callbacky (na test vlákně, single-thread).
 *
 * Resetují se v setUp na 0. Listener registrujeme přes
 * callstack_register_listener s ukazatelem na tuto strukturu jako user.
 */
typedef struct {
    int enter_count;                 /**< Počet on_enter volání. */
    int exit_count;                  /**< Počet on_exit volání. */
    st_CALLSTACK_ENTRY last_enter;   /**< Snapshot poslední pushnuté entry. */
    st_CALLSTACK_ENTRY last_exit;    /**< Snapshot poslední popnuté entry. */
} listener_state_t;


static listener_state_t g_lst;


static void test_on_enter ( const st_CALLSTACK_ENTRY *entry, void *user )
{
    listener_state_t *s = (listener_state_t *) user;
    s->enter_count++;
    s->last_enter = *entry;
}


static void test_on_exit ( const st_CALLSTACK_ENTRY *entry, void *user )
{
    listener_state_t *s = (listener_state_t *) user;
    s->exit_count++;
    s->last_exit = *entry;
}


/* ========================================================================= */
/*  setUp / tearDown                                                         */
/* ========================================================================= */


void setUp ( void )
{
    /* Aktivace registruje Z80 CALL/RET hooky + vynuluje shadow + stats. */
    callstack_set_active ( true );

    /* Vynulovat listener state + registrovat. */
    memset ( &g_lst, 0, sizeof ( g_lst ) );
    callstack_register_listener ( test_on_enter, test_on_exit, &g_lst );
}


void tearDown ( void )
{
    callstack_unregister_listener ( &g_lst );
    callstack_set_active ( false );
    /* Po deaktivaci ještě resetneme shadow (= clean state pro další test). */
    callstack_reset ( );
}


/* ========================================================================= */
/*  Pomocné helpery                                                          */
/* ========================================================================= */


/**
 * @brief Ručně dispatch CALL přes registrovaný callstack hook.
 *
 * Volá cpu->call_cb (= z80_hook_call uvnitř callstack.c). Tím simulujeme
 * fire callbacku z z80_execute() bez nutnosti reálné instrukce.
 *
 * Důležité: cpu->sp musí být nastaveno PŘED voláním (= hodnota PO push
 * return_addr; hook si spočte sp_at_entry = cpu->sp - 2 podle aktuálního
 * SP).
 */
static void dispatch_call ( uint16_t call_site, uint16_t target,
                            uint16_t return_addr, uint16_t sp_after_push )
{
    z80_t *cpu = g_mzarch_main.cpu;
    TEST_ASSERT_NOT_NULL ( cpu );
    TEST_ASSERT_NOT_NULL ( cpu->call_cb );
    /* z80_hook_call čte cpu->sp a počítá sp_at_entry = sp - 2. */
    cpu->sp = (uint16_t) ( sp_after_push + 2 );
    cpu->call_cb ( cpu, call_site, target, return_addr, cpu->call_data );
}


/**
 * @brief Ručně dispatch RET přes registrovaný callstack hook.
 */
static void dispatch_ret ( uint16_t pop_target, uint16_t sp_before_pop )
{
    z80_t *cpu = g_mzarch_main.cpu;
    TEST_ASSERT_NOT_NULL ( cpu );
    TEST_ASSERT_NOT_NULL ( cpu->ret_cb );
    cpu->sp = sp_before_pop;
    cpu->ret_cb ( cpu, pop_target, sp_before_pop, cpu->ret_data );
}


/* ========================================================================= */
/*  Testy                                                                    */
/* ========================================================================= */


/**
 * @brief Happy path: 3x CALL do hloubky 3, pak 3x RET zpět na 0.
 */
void test_basic_call_ret ( void )
{
    st_CALLSTACK_STATS s;

    /* 3x CALL - postupně rostoucí hloubka. */
    dispatch_call ( 0x1000, 0x2000, 0x1003, 0xFFFC );  /* sp_at_entry = 0xFFFC */
    dispatch_call ( 0x2010, 0x3000, 0x2013, 0xFFFA );
    dispatch_call ( 0x3020, 0x4000, 0x3023, 0xFFF8 );

    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 3, s.current_depth );
    TEST_ASSERT_EQUAL_INT ( 3, s.max_depth_reached );
    TEST_ASSERT_EQUAL_UINT32 ( 0, s.divergence_count );
    TEST_ASSERT_EQUAL_UINT32 ( 0, s.overflow_count );
    TEST_ASSERT_EQUAL_INT ( 3, g_lst.enter_count );

    /* 3x RET - LIFO pořadí (= sp_at_entry musí matchnout shadow top). */
    dispatch_ret ( 0x3023, 0xFFF8 );
    dispatch_ret ( 0x2013, 0xFFFA );
    dispatch_ret ( 0x1003, 0xFFFC );

    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 0, s.current_depth );
    TEST_ASSERT_EQUAL_UINT32 ( 0, s.divergence_count );
    TEST_ASSERT_EQUAL_INT ( 3, g_lst.exit_count );
    /* max_depth_reached zůstává 3 (= persistent přes reset_stats). */
    TEST_ASSERT_EQUAL_INT ( 3, s.max_depth_reached );
}


/**
 * @brief Overflow: 300x CALL bez RET = depth se zafixuje na MAX_DEPTH (256),
 *        overflow_count = 300 - 256 = 44. Emulátor neimpactnut.
 */
void test_overflow ( void )
{
    st_CALLSTACK_STATS s;

    for ( int i = 0; i < 300; i++ ) {
        uint16_t addr = (uint16_t) ( 0x1000 + i );
        dispatch_call ( addr, (uint16_t) ( 0x4000 + i ),
                        (uint16_t) ( addr + 3 ),
                        (uint16_t) ( 0xFFFE - 2 * i ) );
    }

    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( CALLSTACK_MAX_DEPTH, s.current_depth );
    TEST_ASSERT_EQUAL_INT ( CALLSTACK_MAX_DEPTH, s.max_depth_reached );
    TEST_ASSERT_EQUAL_UINT32 ( 300u - CALLSTACK_MAX_DEPTH, s.overflow_count );
    /* Listener fire pouze pro úspěšné push (= 256x), ne pro 44 overflow. */
    TEST_ASSERT_EQUAL_INT ( CALLSTACK_MAX_DEPTH, g_lst.enter_count );
}


/**
 * @brief RET bez předchozího CALL = synthetic divergent emit přes on_exit.
 */
void test_push_pop_synthetic_divergent ( void )
{
    st_CALLSTACK_STATS s;

    /* Shadow je prázdný (setUp callstack_set_active provedl reset). */
    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 0, s.current_depth );

    dispatch_ret ( 0x1234, 0xFFFC );

    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 0, s.current_depth );
    TEST_ASSERT_EQUAL_UINT32 ( 1, s.divergence_count );
    TEST_ASSERT_EQUAL_INT ( 1, g_lst.exit_count );
    TEST_ASSERT_EQUAL_UINT8 ( (uint8_t) CS_KIND_SYNTHETIC, g_lst.last_exit.kind );
    TEST_ASSERT_TRUE ( g_lst.last_exit.flags & CS_FLAG_DIVERGENT );
    TEST_ASSERT_EQUAL_UINT16 ( 0x1234, g_lst.last_exit.return_addr );
}


/**
 * @brief Longjmp pattern: CALL A, B, C (depth 3). RET na return_addr A
 *        = skip B+C. Shadow musí klesnout na 0, divergence_count == 1
 *        (jen 1 longjmp event, ne 3).
 */
void test_longjmp_pop ( void )
{
    st_CALLSTACK_STATS s;

    /* 3x CALL. Return adresy: A=0x1003, B=0x2003, C=0x3003. */
    dispatch_call ( 0x1000, 0x2000, 0x1003, 0xFFFC );
    dispatch_call ( 0x2010, 0x3000, 0x2003, 0xFFFA );
    dispatch_call ( 0x3020, 0x4000, 0x3003, 0xFFF8 );

    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 3, s.current_depth );

    int enter_before = g_lst.enter_count;
    int exit_before  = g_lst.exit_count;

    /* RET na 0x1003 (= return_addr nejhlubšího CALL A). sp_before_pop
     * neodpovídá top entry (= mismatch trigger longjmp logiky). Použijeme
     * SP shodný s top entry hodnotou aby _alespoň_ první match check
     * neuspěl podle return_addr (top má 0x3003, hledáme 0x1003 - mismatch). */
    dispatch_ret ( 0x1003, 0xFFF8 );

    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 0, s.current_depth );
    TEST_ASSERT_EQUAL_UINT32 ( 1, s.divergence_count );
    /* Listener on_exit fíruje JEN pro top entry s DIVERGENT flag. */
    TEST_ASSERT_EQUAL_INT ( enter_before, g_lst.enter_count );
    TEST_ASSERT_EQUAL_INT ( exit_before + 1, g_lst.exit_count );
    TEST_ASSERT_TRUE ( g_lst.last_exit.flags & CS_FLAG_DIVERGENT );
}


/**
 * @brief Register + unregister listener: po unregister už nefíruje.
 */
void test_listener_register_unregister ( void )
{
    /* Sanity - listener už zaregistrován v setUp. */
    dispatch_call ( 0x1000, 0x2000, 0x1003, 0xFFFC );
    TEST_ASSERT_EQUAL_INT ( 1, g_lst.enter_count );

    callstack_unregister_listener ( &g_lst );

    dispatch_call ( 0x2000, 0x3000, 0x2003, 0xFFFA );
    /* enter_count se NEinkrementuje. */
    TEST_ASSERT_EQUAL_INT ( 1, g_lst.enter_count );

    /* Vrátit pro tearDown idempotenci. */
    callstack_register_listener ( test_on_enter, test_on_exit, &g_lst );
}


/**
 * @brief Snapshot_get + snapshot_free.
 */
void test_snapshot_get_free ( void )
{
    /* Push 5 entries. */
    for ( int i = 0; i < 5; i++ ) {
        dispatch_call ( (uint16_t) ( 0x1000 + i ),
                        (uint16_t) ( 0x4000 + i ),
                        (uint16_t) ( 0x1003 + i ),
                        (uint16_t) ( 0xFFFC - 2 * i ) );
    }

    st_CALLSTACK_ENTRY *entries = NULL;
    int count = -1;
    int rc = callstack_snapshot_get ( &entries, &count );
    TEST_ASSERT_EQUAL_INT ( 0, rc );
    TEST_ASSERT_EQUAL_INT ( 5, count );
    TEST_ASSERT_NOT_NULL ( entries );

    /* Verifikace entries[0] = nejstarší (= CALL i=0). */
    TEST_ASSERT_EQUAL_UINT16 ( 0x1003, entries[0].return_addr );
    TEST_ASSERT_EQUAL_UINT16 ( 0x4000, entries[0].target_addr );
    TEST_ASSERT_EQUAL_UINT8 ( (uint8_t) CS_KIND_CALL, entries[0].kind );

    /* entries[4] = TOP. */
    TEST_ASSERT_EQUAL_UINT16 ( 0x1007, entries[4].return_addr );
    TEST_ASSERT_EQUAL_UINT16 ( 0x4004, entries[4].target_addr );

    callstack_snapshot_free ( entries );

    /* Prázdný snapshot. */
    callstack_reset ( );
    entries = (st_CALLSTACK_ENTRY *) 0xDEADBEEF;
    count = -1;
    rc = callstack_snapshot_get ( &entries, &count );
    TEST_ASSERT_EQUAL_INT ( 0, rc );
    TEST_ASSERT_NULL ( entries );
    TEST_ASSERT_EQUAL_INT ( 0, count );
}


/**
 * @brief Reset - shadow se vyprázdní, statistiky se vynulují.
 */
void test_reset ( void )
{
    for ( int i = 0; i < 5; i++ ) {
        dispatch_call ( (uint16_t) ( 0x1000 + i ),
                        (uint16_t) ( 0x4000 + i ),
                        (uint16_t) ( 0x1003 + i ),
                        (uint16_t) ( 0xFFFC - 2 * i ) );
    }

    /* Wygeneruj jednu divergenci pro neprázdné stat counters. */
    callstack_reset ( );
    dispatch_ret ( 0x1234, 0xFFFC );  /* synthetic, divergence_count = 1 */

    st_CALLSTACK_STATS s;
    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_UINT32 ( 1, s.divergence_count );

    callstack_reset ( );
    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 0, s.current_depth );
    TEST_ASSERT_EQUAL_INT ( 0, s.max_depth_reached );
    TEST_ASSERT_EQUAL_UINT32 ( 0, s.divergence_count );
    TEST_ASSERT_EQUAL_UINT32 ( 0, s.overflow_count );
    TEST_ASSERT_EQUAL_UINT32 ( 0, s.sp_swap_count );
}


/**
 * @brief RST push přes callstack_on_cpu_ctrl_event.
 *
 * Z80_CPU_CTRL_RST_28 = 7 → target_addr = (7 - 2) * 8 = 0x28.
 */
void test_rst_push ( void )
{
    /* Nastavíme cpu->sp na nějakou hodnotu - hook si dopočte sp_at_entry. */
    g_mzarch_main.cpu->sp = 0xFFFE;

    /* pc = 0x1234 (= za RST opcode, = co se pushuje jako return_addr). */
    callstack_on_cpu_ctrl_event ( (uint8_t) Z80_CPU_CTRL_RST_28, 0x1234 );

    st_CALLSTACK_STATS s;
    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 1, s.current_depth );
    TEST_ASSERT_EQUAL_INT ( 1, g_lst.enter_count );
    TEST_ASSERT_EQUAL_UINT8 ( (uint8_t) CS_KIND_RST, g_lst.last_enter.kind );
    TEST_ASSERT_EQUAL_UINT16 ( 0x0028, g_lst.last_enter.target_addr );
    TEST_ASSERT_EQUAL_UINT16 ( 0x1234, g_lst.last_enter.return_addr );
    TEST_ASSERT_EQUAL_UINT16 ( 0x1233, g_lst.last_enter.call_site_addr );
}


/**
 * @brief IRQ accept push v IM 2 - kind = CS_KIND_IRQ_IM2, im2_vector_addr
 *        zachováno.
 */
void test_irq_accept_push_im2 ( void )
{
    callstack_on_irq_accept ( 0x4321, 0x9000, 2, 0x3F00 );

    st_CALLSTACK_STATS s;
    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 1, s.current_depth );
    TEST_ASSERT_EQUAL_INT ( 1, g_lst.enter_count );
    TEST_ASSERT_EQUAL_UINT8 ( (uint8_t) CS_KIND_IRQ_IM2, g_lst.last_enter.kind );
    TEST_ASSERT_EQUAL_UINT16 ( 0x9000, g_lst.last_enter.target_addr );
    TEST_ASSERT_EQUAL_UINT16 ( 0x4321, g_lst.last_enter.return_addr );
    TEST_ASSERT_EQUAL_UINT8 ( 2, g_lst.last_enter.im_mode );
    TEST_ASSERT_EQUAL_UINT16 ( 0x3F00, g_lst.last_enter.im2_vector_addr );
}


/**
 * @brief Pop IRQ frame přes iff_change reason RETI.
 */
void test_iff_reti_pop ( void )
{
    /* Push IRQ frame. */
    callstack_on_irq_accept ( 0x4321, 0x9000, 2, 0x3F00 );
    st_CALLSTACK_STATS s;
    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 1, s.current_depth );

    int exit_before = g_lst.exit_count;

    callstack_on_iff_change ( (uint8_t) Z80_IFF_REASON_RETI );

    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 0, s.current_depth );
    TEST_ASSERT_EQUAL_INT ( exit_before + 1, g_lst.exit_count );
    TEST_ASSERT_EQUAL_UINT8 ( (uint8_t) CS_KIND_IRQ_IM2, g_lst.last_exit.kind );
    /* Divergence_count zůstává 0 - byl reálný IRQ frame. */
    TEST_ASSERT_EQUAL_UINT32 ( 0, s.divergence_count );
}


/**
 * @brief Gate-off: g_callstack_active = 0 → fan-out hooky no-op.
 *
 * Z80 hooky se nedispatch protože callstack_set_active(false) je
 * v cpu->call_cb / cpu->ret_cb nahradí za NULL. Testujeme přes
 * cpu_ctrl_event a irq_accept (= ty si dělají internal gate check).
 */
void test_gate_off_no_fire ( void )
{
    /* Vypnout subsystém. */
    callstack_set_active ( false );

    /* Re-registrace listeneru (set_active(false) ho neshazuje, ale pro
     * jistotu). */
    memset ( &g_lst, 0, sizeof ( g_lst ) );
    callstack_register_listener ( test_on_enter, test_on_exit, &g_lst );

    /* Z80 callbacky musejí být NULL = pokud bychom je dispatch volali,
     * crash. Místo toho testujeme cpu_ctrl + irq_accept (gate check
     * interně). */
    callstack_on_cpu_ctrl_event ( (uint8_t) Z80_CPU_CTRL_RST_28, 0x1234 );
    callstack_on_irq_accept ( 0x4321, 0x9000, 1, 0 );
    callstack_on_iff_change ( (uint8_t) Z80_IFF_REASON_RETI );

    st_CALLSTACK_STATS s;
    callstack_get_stats ( &s );
    TEST_ASSERT_EQUAL_INT ( 0, s.current_depth );
    TEST_ASSERT_EQUAL_INT ( 0, g_lst.enter_count );
    TEST_ASSERT_EQUAL_INT ( 0, g_lst.exit_count );

    /* Z80 CALL/RET sloty jsou NULL (= odregistrované). */
    TEST_ASSERT_NULL ( g_mzarch_main.cpu->call_cb );
    TEST_ASSERT_NULL ( g_mzarch_main.cpu->ret_cb );

    /* Reaktivace pro tearDown. */
    callstack_set_active ( true );
}


/* ========================================================================= */
/*  MAIN                                                                     */
/* ========================================================================= */


int main ( int argc, char *argv[] )
{
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    RUN_TEST ( test_basic_call_ret );
    RUN_TEST ( test_overflow );
    RUN_TEST ( test_push_pop_synthetic_divergent );
    RUN_TEST ( test_longjmp_pop );
    RUN_TEST ( test_listener_register_unregister );
    RUN_TEST ( test_snapshot_get_free );
    RUN_TEST ( test_reset );
    RUN_TEST ( test_rst_push );
    RUN_TEST ( test_irq_accept_push_im2 );
    RUN_TEST ( test_iff_reti_pop );
    RUN_TEST ( test_gate_off_no_fire );

    return UNITY_END ( );
}
