/*
 * test_profiler.c - unit testy CPU profileru (mutant profiler V1, fáze F6).
 *
 * Pokrývá:
 *  - init / shutdown idempotence
 *  - set_active lifecycle (listener registered/unregistered)
 *  - callstack ownership management
 *  - single CALL/RET pár - inclusive cycles agregace
 *  - vnořené volání - exclusive cycles výpočet (parent/child bubble-up)
 *  - min/max update napříč více voláními
 *  - CS_FLAG_DIVERGENT exit - skip + unmatched_returns counter
 *  - uint32 cycles wraparound resilience
 *  - reset() - vynulování stavu + baseline anchor
 *  - IRQ entries counter
 *  - snapshot allocation/free roundtrip
 *  - max depth 256+ - žádný overflow / crash
 *
 * Profiler je listener nad callstack V1. Testy proto dispatchují CALL/RET
 * hooky přes registrované cpu->call_cb / cpu->ret_cb (stejný pattern jako
 * test_callstack.c). cycles_at_entry se inject přes přímý zápis do
 * cpu->total_cycles (= profiler i callstack si ho čtou odtud).
 *
 * Licence: GPLv3
 */

#include "mztest.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "debugger/profiler.h"
#include "debugger/callstack.h"
#include "libs/cpu-z80/z80.h"
#include "mzarch/mzarch.h"


/* ========================================================================= */
/*  setUp / tearDown                                                         */
/* ========================================================================= */


/**
 * @brief Per-test setUp.
 *
 * Profiler_init je idempotentní (= INI sekce [PROFILER] registrovaná
 * jednou za běh procesu). set_active(true) zapne listener + callstack;
 * reset uvnitř set_active vyčistí předchozí stav.
 *
 * cpu->total_cycles se nuluje aby měření začalo od deterministické 0.
 */
void setUp ( void )
{
    profiler_init ( );

    /* Před aktivací nuluj cpu cycles, aby anchor v reset() byl 0. */
    g_mzarch_main.cpu->total_cycles = 0;

    profiler_set_active ( true );

    /* Sanity - listener registered, callstack zapnut. */
    TEST_ASSERT_TRUE ( g_profiler_active );
    TEST_ASSERT_TRUE ( g_callstack_active );
}


/**
 * @brief Per-test tearDown.
 *
 * Vypneme profiler (= odregistruje listener + vrátí callstack do stavu
 * před setUp). Callstack reset pak vyčistí shadow pro další test.
 */
void tearDown ( void )
{
    profiler_set_active ( false );
    callstack_reset ( );
    g_mzarch_main.cpu->total_cycles = 0;
}


/* ========================================================================= */
/*  Helpers                                                                  */
/* ========================================================================= */


/**
 * @brief Ručně dispatch CALL přes registrovaný callstack hook.
 *
 * Před voláním nastaví cpu->total_cycles na zadanou hodnotu - callstack
 * hook ji přečte a uloží do entry->cycles_at_entry, profiler listener
 * jí přes prof_extend_cycles převede na 64-bit baseline.
 *
 * @param target           Cíl volání.
 * @param return_addr      Návratová adresa (= PC po CALL).
 * @param cycles_at_entry  Hodnota cpu->total_cycles při push.
 */
static void dispatch_call_at ( uint16_t target, uint16_t return_addr,
                               uint32_t cycles_at_entry )
{
    z80_t *cpu = g_mzarch_main.cpu;
    TEST_ASSERT_NOT_NULL ( cpu );
    TEST_ASSERT_NOT_NULL ( cpu->call_cb );

    cpu->total_cycles = cycles_at_entry;
    /* sp dummy hodnota - callstack hook si dopočte sp_at_entry, profileru
     * je SP lhostejné. */
    cpu->sp = 0xFFFE;
    cpu->call_cb ( cpu, (uint16_t) ( return_addr - 3 ), target, return_addr,
                   cpu->call_data );
}


/**
 * @brief Ručně dispatch RET přes registrovaný callstack hook.
 *
 * @param return_addr  Adresa, na kterou se RET vrátí (= top shadow entry).
 * @param cycles_now   Hodnota cpu->total_cycles při pop.
 */
static void dispatch_ret_at ( uint16_t return_addr, uint32_t cycles_now )
{
    z80_t *cpu = g_mzarch_main.cpu;
    TEST_ASSERT_NOT_NULL ( cpu );
    TEST_ASSERT_NOT_NULL ( cpu->ret_cb );

    cpu->total_cycles = cycles_now;
    /* sp musí odpovídat top entry sp_at_entry (= cpu->sp - 2 byl zápis
     * při push s sp_after_push = 0xFFFC → sp_at_entry uložené 0xFFFC).
     * Pro pop musí být cpu->sp = sp_at_entry (= 0xFFFC). */
    cpu->sp = 0xFFFC;
    cpu->ret_cb ( cpu, return_addr, cpu->sp, cpu->ret_data );
}


/**
 * @brief Hledá entry v snapshot poli podle (target, kind).
 *
 * @return Pointer na nalezené entry, nebo NULL.
 */
static const st_PROF_ENTRY *find_entry ( const st_PROF_SNAPSHOT *snap,
                                          uint16_t target, uint8_t kind )
{
    for ( int i = 0; i < snap->entry_count; i++ ) {
        if ( snap->entries[i].target_addr == target
             && snap->entries[i].kind == kind ) {
            return &snap->entries[i];
        }
    }
    return NULL;
}


/* ========================================================================= */
/*  Testy                                                                    */
/* ========================================================================= */


/**
 * @brief init/shutdown - idempotentní.
 *
 * Druhý init nesmí ztratit stav (= žádný re-create hash mapy). Shutdown
 * smí být zavolán víckrát.
 */
void test_profiler_init_shutdown ( void )
{
    /* setUp už volal init - druhé volání no-op. */
    profiler_init ( );
    profiler_init ( );
    /* Žádné assert na vnitřní stav - jen že se neshodí runtime. */
    TEST_ASSERT_TRUE ( true );
}


/**
 * @brief set_active(true/false) řídí listener registraci a callstack.
 *
 * setUp aktivoval profiler → callstack je ON, profiler gate ON.
 * set_active(false) vypne profiler, ale callstack ownership způsobí
 * vrácení callstack do stavu před setUp (= byl OFF v test setUp začátku).
 */
void test_profiler_set_active_lifecycle ( void )
{
    TEST_ASSERT_TRUE ( g_profiler_active );
    TEST_ASSERT_TRUE ( g_callstack_active );

    profiler_set_active ( false );

    TEST_ASSERT_FALSE ( g_profiler_active );
    /* Profiler vlastnil callstack → vypnul ho. */
    TEST_ASSERT_FALSE ( g_callstack_active );

    /* Idempotence. */
    profiler_set_active ( false );
    TEST_ASSERT_FALSE ( g_profiler_active );

    /* Znovu zapnout pro tearDown čistý průchod. */
    profiler_set_active ( true );
    TEST_ASSERT_TRUE ( g_profiler_active );
    TEST_ASSERT_TRUE ( g_callstack_active );
}


/**
 * @brief Ownership: když uživatel zapne callstack před profilerem,
 *        profiler set_active(false) nesmí callstack zhasnout.
 */
void test_profiler_owns_callstack ( void )
{
    /* Reset do známého stavu - profiler OFF, callstack OFF. */
    profiler_set_active ( false );
    /* Po set_active(false) by callstack měl být OFF (vlastnil ho profiler). */
    TEST_ASSERT_FALSE ( g_callstack_active );

    /* User zapne callstack ručně. */
    callstack_set_active ( true );
    TEST_ASSERT_TRUE ( g_callstack_active );

    /* Profiler ON - vidí callstack už zapnutý, ownership = 0. */
    profiler_set_active ( true );
    TEST_ASSERT_TRUE ( g_profiler_active );
    TEST_ASSERT_TRUE ( g_callstack_active );

    /* Profiler OFF - callstack zůstává ON (user ownership). */
    profiler_set_active ( false );
    TEST_ASSERT_FALSE ( g_profiler_active );
    TEST_ASSERT_TRUE ( g_callstack_active );

    /* tearDown vrátí profiler do očekávaného stavu. */
    profiler_set_active ( true );
}


/**
 * @brief Jeden CALL/RET pár - calls=1, inclusive cycles = exit - entry.
 */
void test_profiler_single_call_aggregation ( void )
{
    /* CALL na 0x4000 v t=100, RET v t=200 → incl = 100 cyklů. */
    dispatch_call_at ( 0x4000, 0x1003, 100 );
    dispatch_ret_at  ( 0x1003, 200 );

    st_PROF_SNAPSHOT snap;
    int rc = profiler_snapshot_get ( &snap );
    TEST_ASSERT_EQUAL_INT ( 0, rc );
    TEST_ASSERT_EQUAL_INT ( 1, snap.entry_count );

    const st_PROF_ENTRY *e = find_entry ( &snap, 0x4000, CS_KIND_CALL );
    TEST_ASSERT_NOT_NULL ( e );
    TEST_ASSERT_EQUAL_UINT64 ( 1u,   e->calls );
    TEST_ASSERT_EQUAL_UINT64 ( 100u, e->cycles_incl_sum );
    TEST_ASSERT_EQUAL_UINT64 ( 100u, e->cycles_excl_sum );
    TEST_ASSERT_EQUAL_UINT64 ( 100u, e->cycles_incl_min );
    TEST_ASSERT_EQUAL_UINT64 ( 100u, e->cycles_incl_max );

    profiler_snapshot_free ( &snap );
}


/**
 * @brief Vnořené volání: parent(100-500), child(200-300).
 *
 * Parent inclusive = 400, exclusive = 400 - 100 (child) = 300.
 * Child inclusive = 100, exclusive = 100.
 */
void test_profiler_nested_excl_calc ( void )
{
    /* Parent CALL t=100 */
    dispatch_call_at ( 0x4000, 0x1003, 100 );
    /* Child CALL t=200 (uvnitř parent) */
    dispatch_call_at ( 0x5000, 0x4050, 200 );
    /* Child RET t=300 */
    dispatch_ret_at  ( 0x4050, 300 );
    /* Pozor - po dispatch_ret_at se cpu->sp = 0xFFFC; pro další pop
     * (parent) musí být stejné, helper to nastaví. */
    dispatch_ret_at  ( 0x1003, 500 );

    st_PROF_SNAPSHOT snap;
    TEST_ASSERT_EQUAL_INT ( 0, profiler_snapshot_get ( &snap ) );
    TEST_ASSERT_EQUAL_INT ( 2, snap.entry_count );

    const st_PROF_ENTRY *parent = find_entry ( &snap, 0x4000, CS_KIND_CALL );
    const st_PROF_ENTRY *child  = find_entry ( &snap, 0x5000, CS_KIND_CALL );
    TEST_ASSERT_NOT_NULL ( parent );
    TEST_ASSERT_NOT_NULL ( child );

    TEST_ASSERT_EQUAL_UINT64 ( 1u,   parent->calls );
    TEST_ASSERT_EQUAL_UINT64 ( 400u, parent->cycles_incl_sum );
    TEST_ASSERT_EQUAL_UINT64 ( 300u, parent->cycles_excl_sum );

    TEST_ASSERT_EQUAL_UINT64 ( 1u,   child->calls );
    TEST_ASSERT_EQUAL_UINT64 ( 100u, child->cycles_incl_sum );
    TEST_ASSERT_EQUAL_UINT64 ( 100u, child->cycles_excl_sum );

    profiler_snapshot_free ( &snap );
}


/**
 * @brief 3x volání stejné funkce s incl 50/200/100 → min=50, max=200.
 */
void test_profiler_min_max ( void )
{
    /* 1. call: 1000 → 1050 (incl=50) */
    dispatch_call_at ( 0x4000, 0x1003, 1000 );
    dispatch_ret_at  ( 0x1003, 1050 );
    /* 2. call: 2000 → 2200 (incl=200) */
    dispatch_call_at ( 0x4000, 0x1003, 2000 );
    dispatch_ret_at  ( 0x1003, 2200 );
    /* 3. call: 3000 → 3100 (incl=100) */
    dispatch_call_at ( 0x4000, 0x1003, 3000 );
    dispatch_ret_at  ( 0x1003, 3100 );

    st_PROF_SNAPSHOT snap;
    TEST_ASSERT_EQUAL_INT ( 0, profiler_snapshot_get ( &snap ) );
    TEST_ASSERT_EQUAL_INT ( 1, snap.entry_count );

    const st_PROF_ENTRY *e = find_entry ( &snap, 0x4000, CS_KIND_CALL );
    TEST_ASSERT_NOT_NULL ( e );
    TEST_ASSERT_EQUAL_UINT64 ( 3u,   e->calls );
    TEST_ASSERT_EQUAL_UINT64 ( 350u, e->cycles_incl_sum );  /* 50+200+100 */
    TEST_ASSERT_EQUAL_UINT64 ( 50u,  e->cycles_incl_min );
    TEST_ASSERT_EQUAL_UINT64 ( 200u, e->cycles_incl_max );

    profiler_snapshot_free ( &snap );
}


/**
 * @brief Divergent exit (synthetic) - unmatched_returns++, agregátor zůstane.
 *
 * Empty shadow + dispatch_ret = callstack emituje synthetic DIVERGENT exit.
 * Profiler skipne agregaci, jen counter inkrementuje.
 */
void test_profiler_divergent_exit_skip ( void )
{
    /* Shadow je prázdný (setUp callstack_set_active provedl reset). */
    dispatch_ret_at ( 0x1234, 100 );

    st_PROF_STATS st;
    profiler_get_stats ( &st );
    /* Žádné CALL nepáruje → unmatched. */
    TEST_ASSERT_EQUAL_UINT32 ( 1u, st.unmatched_returns );
    /* Žádný bucket v hash mapě. */
    TEST_ASSERT_EQUAL_UINT32 ( 0u, st.entry_count );
}


/**
 * @brief Wraparound uint32 cycles counteru.
 *
 * Anchor po reset = 0. Push v t=0xFFFFFFF0 (= 64-bit cycles_from_anchor =
 * 0xFFFFFFF0). Pop v t=0x00000010 → wraparound detekován,
 * baseline_64 += 0x100000000. Inclusive = (0x100000010 - 0xFFFFFFF0) = 0x20.
 */
void test_profiler_wraparound ( void )
{
    /* Posuneme anchor na velkou hodnotu blízkou wraparound. Nejjednodušší
     * cesta: vynutit anchor přes reset s cpu->total_cycles = 0xFFFFFFF0
     * pak push (na stejné hodnotě = relativní 0), pak posun cpu->total_cycles
     * přes wraparound. */
    g_mzarch_main.cpu->total_cycles = 0xFFFFFFF0u;
    profiler_reset ( );

    /* CALL v t=0xFFFFFFF0 (relativní 0). */
    dispatch_call_at ( 0x4000, 0x1003, 0xFFFFFFF0u );
    /* RET v t=0x00000010 (= reálná pozice +0x20 cyklů přes wraparound). */
    dispatch_ret_at  ( 0x1003, 0x00000010u );

    st_PROF_SNAPSHOT snap;
    TEST_ASSERT_EQUAL_INT ( 0, profiler_snapshot_get ( &snap ) );
    TEST_ASSERT_EQUAL_INT ( 1, snap.entry_count );

    const st_PROF_ENTRY *e = find_entry ( &snap, 0x4000, CS_KIND_CALL );
    TEST_ASSERT_NOT_NULL ( e );
    /* Anchor = 0xFFFFFFF0, push v relativním 0, pop v relativním 0x20. */
    TEST_ASSERT_EQUAL_UINT64 ( 0x20u, e->cycles_incl_sum );

    profiler_snapshot_free ( &snap );
}


/**
 * @brief Reset vynuluje agregátor a statistiky.
 */
void test_profiler_reset ( void )
{
    /* Wygeneruj nějaká data. */
    dispatch_call_at ( 0x4000, 0x1003, 100 );
    dispatch_ret_at  ( 0x1003, 200 );

    st_PROF_STATS st;
    profiler_get_stats ( &st );
    TEST_ASSERT_EQUAL_UINT64 ( 1u, st.total_calls );
    TEST_ASSERT_EQUAL_UINT32 ( 1u, st.entry_count );

    profiler_reset ( );

    profiler_get_stats ( &st );
    TEST_ASSERT_EQUAL_UINT64 ( 0u, st.total_calls );
    TEST_ASSERT_EQUAL_UINT32 ( 0u, st.entry_count );
    TEST_ASSERT_EQUAL_UINT32 ( 0u, st.unmatched_returns );
    TEST_ASSERT_EQUAL_UINT32 ( 0u, st.irq_entries );
    TEST_ASSERT_EQUAL_UINT32 ( 0u, st.max_depth_reached );
    TEST_ASSERT_EQUAL_UINT32 ( 0u, st.overflow_count );
}


/**
 * @brief IRQ entries counter - kind IRQ_IM0/IM1/IM2/NMI inkrementuje
 *        samostatný counter.
 */
void test_profiler_irq_counter ( void )
{
    /* 2x CALL (= total_calls += 2, irq_entries 0). */
    dispatch_call_at ( 0x4000, 0x1003, 100 );
    dispatch_ret_at  ( 0x1003, 150 );
    dispatch_call_at ( 0x4000, 0x1003, 200 );
    dispatch_ret_at  ( 0x1003, 250 );

    /* 3x IRQ accept přes callstack API - každý zvedne irq_entries++. */
    g_mzarch_main.cpu->total_cycles = 300;
    callstack_on_irq_accept ( 0x2000, 0x9000, 1, 0 );
    g_mzarch_main.cpu->total_cycles = 310;
    callstack_on_irq_accept ( 0x2000, 0x9000, 1, 0 );
    g_mzarch_main.cpu->total_cycles = 320;
    callstack_on_irq_accept ( 0x2000, 0x9000, 1, 0 );

    st_PROF_STATS st;
    profiler_get_stats ( &st );
    TEST_ASSERT_EQUAL_UINT64 ( 5u, st.total_calls );   /* 2 CALL + 3 IRQ */
    TEST_ASSERT_EQUAL_UINT32 ( 3u, st.irq_entries );
}


/**
 * @brief snapshot alloc + free roundtrip.
 *
 * Prázdný stav: entries = NULL, count = 0, rc = 0.
 * Po pushi: entries != NULL, count > 0. Po free: žádný leak (= valgrind clean,
 * mimo scope unit testu, jen že free neshodí runtime).
 */
void test_profiler_snapshot_alloc ( void )
{
    /* Empty - free musí být safe. */
    st_PROF_SNAPSHOT snap_empty;
    memset ( &snap_empty, 0xAA, sizeof ( snap_empty ) );  /* poison */
    TEST_ASSERT_EQUAL_INT ( 0, profiler_snapshot_get ( &snap_empty ) );
    TEST_ASSERT_NULL ( snap_empty.entries );
    TEST_ASSERT_EQUAL_INT ( 0, snap_empty.entry_count );
    profiler_snapshot_free ( &snap_empty );

    /* Populated. */
    dispatch_call_at ( 0x4000, 0x1003, 100 );
    dispatch_ret_at  ( 0x1003, 200 );
    dispatch_call_at ( 0x5000, 0x2003, 300 );
    dispatch_ret_at  ( 0x2003, 400 );

    st_PROF_SNAPSHOT snap;
    TEST_ASSERT_EQUAL_INT ( 0, profiler_snapshot_get ( &snap ) );
    TEST_ASSERT_NOT_NULL ( snap.entries );
    TEST_ASSERT_EQUAL_INT ( 2, snap.entry_count );

    profiler_snapshot_free ( &snap );
    /* Po free entries musí být NULL (= idempotence dalších free). */
    TEST_ASSERT_NULL ( snap.entries );
    TEST_ASSERT_EQUAL_INT ( 0, snap.entry_count );

    /* Idempotence druhého volání. */
    profiler_snapshot_free ( &snap );
    profiler_snapshot_free ( NULL );
}


/**
 * @brief Max depth: 300x enter bez exitu → žádný crash, depth se zafixuje.
 *
 * PROFILER_MAX_DEPTH = 256. Callstack samotný v overflow stavu listenery
 * NEvolá (= push_entry odmítne nad CALLSTACK_MAX_DEPTH a inkrementuje
 * callstack overflow counter, ale listener fire neproběhne). Profiler
 * tedy uvidí jen 256x on_enter → total_calls = 256, overflow_count = 0
 * (= profiler parallel stack se na 256. zaplní, ale callstack už další
 * eventy nepošle).
 *
 * Test smyslem: žádný crash, žádný buffer overrun v parallel stacku,
 * max_depth_reached = 256.
 */
void test_profiler_max_depth ( void )
{
    z80_t *cpu = g_mzarch_main.cpu;
    /* 300x CALL bez RET, postupně klesající SP aby každý frame byl unikátní. */
    for ( int i = 0; i < 300; i++ ) {
        cpu->total_cycles = (uint32_t) ( 1000 + i * 10 );
        cpu->sp = (uint16_t) ( 0xFFFE - 2 * i );
        cpu->call_cb ( cpu, (uint16_t) ( 0x1000 + i ),
                       (uint16_t) ( 0x4000 + i ),
                       (uint16_t) ( 0x1003 + i ),
                       cpu->call_data );
    }

    st_PROF_STATS st;
    profiler_get_stats ( &st );
    /* Callstack overflow gate listener fire na 256 push, dalších 44 ignoruje. */
    TEST_ASSERT_EQUAL_UINT64 ( (uint64_t) PROFILER_MAX_DEPTH, st.total_calls );
    /* Profiler overflow counter = 0 (= callstack neposlal eventy, parallel
     * stack se na 256. zaplnil "v bezpečné mezi" - listener prošel přesně
     * PROFILER_MAX_DEPTH krát). */
    TEST_ASSERT_EQUAL_UINT32 ( 0u, st.overflow_count );
    TEST_ASSERT_EQUAL_UINT32 ( (uint32_t) PROFILER_MAX_DEPTH,
                                st.max_depth_reached );
}


/* ========================================================================= */
/*  MAIN                                                                     */
/* ========================================================================= */


int main ( int argc, char *argv[] )
{
    mztest_parse_args ( argc, argv );
    mztest_init ( );

    UNITY_BEGIN ( );

    RUN_TEST ( test_profiler_init_shutdown );
    RUN_TEST ( test_profiler_set_active_lifecycle );
    RUN_TEST ( test_profiler_owns_callstack );
    RUN_TEST ( test_profiler_single_call_aggregation );
    RUN_TEST ( test_profiler_nested_excl_calc );
    RUN_TEST ( test_profiler_min_max );
    RUN_TEST ( test_profiler_divergent_exit_skip );
    RUN_TEST ( test_profiler_wraparound );
    RUN_TEST ( test_profiler_reset );
    RUN_TEST ( test_profiler_irq_counter );
    RUN_TEST ( test_profiler_snapshot_alloc );
    RUN_TEST ( test_profiler_max_depth );

    return UNITY_END ( );
}
