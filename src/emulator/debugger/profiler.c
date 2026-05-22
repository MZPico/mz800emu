/*
 * profiler.c - per-function CPU profiler (mutant profiler V1, fáze F1).
 *
 * Implementace viz profiler.h. Tento soubor obsahuje:
 *   - hash mapa (target_addr, kind) → st_PROF_ENTRY
 *   - parallel stack pro výpočet exclusive cycles + child bubble-up
 *   - 64-bit cycles extension (prof_extend_cycles) s wraparound detekcí
 *   - callstack listener handlery (prof_on_enter / prof_on_exit)
 *   - callstack gate ownership (set_active management)
 *   - cfgmain integrace [PROFILER] sekce s klíčem `active`
 *
 * Hot-path strategie (= memory feedback_emu_perf_dispatch):
 *   - žádný `if (enabled)` v hot path - když je profiler OFF, listener
 *     není zaregistrovaný a callstack listener slot drží NULL.
 *     Fan-out v push_entry/pop_top_entry se vyhne callbacku NULL-checkem.
 *
 * ----------------------------- License -------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 */

#include "profiler.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <glib.h>

#include "libs/sdlapp/sdlapp_options.h"
#include "libs/cfgfile/cfgmodule.h"
#include "libs/cpu-z80/z80.h"
#include "emulator/cfgmain.h"
#include "emulator/mzarch/mzarch.h"
#include "emulator/debugger/callstack.h"


/**
 * @brief Forward extern do UI vrstvy (profiler_window.cpp).
 *
 * Registruje klíč `show_window` v cfgmodule sekce [PROFILER] nad statickou
 * cache UI vrstvy. Volá se z profiler_init() po vytvoření cmodu. C jádro
 * vlastní cmod handle, UI vlastní cache - žádná závislost na g_gui v C.
 *
 * Definice: src/ui-imgui/debugger/profiler/profiler_window.cpp.
 */
extern void profiler_window_register_cfg ( void *cmod_void );


/* ============================================================================
 * Globální stav (private)
 * ============================================================================ */

/**
 * @brief Gate flag (= viz profiler.h dokumentace).
 *
 * Modifikuje výhradně @ref profiler_set_active. Hot path používá raw čtení
 * (volatile není nutné - GCC v MSYS2 single-thread per modify, stejné jako
 * g_callstack_active).
 */
uint8_t g_profiler_active = 0;


/**
 * @brief Hash mapa (key uint32 = (kind<<16) | target_addr) → st_PROF_ENTRY *.
 *
 * Vlastník hodnot - při clear / shutdown g_free každý value pointer
 * (destroy_value callback v g_hash_table_new_full).
 *
 * Alokace v profiler_init, uvolnění v profiler_shutdown.
 */
static GHashTable *g_prof_table = NULL;


/**
 * @brief Slot parallel stacku - drží otevřené framy mezi on_enter a
 *        on_exit pro výpočet inclusive/exclusive cycles + child bubble-up.
 *
 * Hloubka roste s on_enter, klesá s on_exit. Při on_enter overflow
 * (>= PROFILER_MAX_DEPTH) se push tichá no-op a counter
 * g_prof_overflow_count++ - pop pak nemá z čeho odečíst, takže
 * následující on_exit párový frame ignoruje (g_prof_depth zůstává
 * na maximu nebo padá zpět paralelně s callstack).
 */
struct prof_frame {
    uint16_t target_addr;          /**< Identifikuje bucket pro update. */
    uint8_t  kind;
    uint8_t  _pad;
    uint64_t cycles_at_entry_64;   /**< 64-bit extended cycles při on_enter. */
    uint64_t child_incl_sum;       /**< Suma inclusive cycles dětí (pro excl). */
};


/**
 * @brief Parallel stack a counters - BSS sekce, žádná dynamická alokace.
 */
static struct prof_frame g_prof_stack [ PROFILER_MAX_DEPTH ];
static int               g_prof_depth = 0;

static uint64_t g_prof_total_calls       = 0;
static uint32_t g_prof_irq_entries       = 0;
static uint32_t g_prof_unmatched_returns = 0;
static uint32_t g_prof_max_depth         = 0;
static uint32_t g_prof_overflow_count    = 0;


/**
 * @brief 64-bit cycles extension - baseline + last seen + anchor.
 *
 * Logika prof_extend_cycles:
 *   real_now_64 = baseline_64 + now32   (po wraparound detekci)
 *   returned    = real_now_64 - anchor_32_as_64
 *
 * Při reset se anchor nastaví na aktuální cpu->total_cycles - tj.
 * další volání vrátí 0 a pak se delta inkrementuje.
 *
 * Při wraparound (now32 < last_seen_32) baseline_64 += 0x100000000.
 */
static uint64_t g_prof_cycles_baseline_64 = 0;
static uint32_t g_prof_last_seen_32       = 0;
static uint64_t g_prof_anchor_64          = 0;


/**
 * @brief Ownership flag - profiler zapnul callstack (= musí ho symetricky
 *        vypnout při set_active(false)).
 */
static uint8_t g_profiler_owns_callstack = 0;


/**
 * @brief Idempotence flag pro profiler_init.
 */
static bool s_initialized = false;


/* ============================================================================
 * Pomocné funkce (private)
 * ============================================================================ */

/**
 * @brief Spočítá hash mapy klíč z (target_addr, kind).
 *
 * Layout: high 16 bitů = kind cast, low 16 bitů = target_addr. Klíč
 * unikátní pokud kind je v rozsahu 0..255 (= max 8 bitů, ale použijeme
 * 16-bit slot pro budoucí rozšíření).
 *
 * @param target_addr CPU 16-bit adresa cíle volání.
 * @param kind        en_CALLSTACK_ENTRY_KIND cast na uint8_t.
 * @return Klíč pro g_hash_table.
 */
static inline uint32_t prof_make_key ( uint16_t target_addr, uint8_t kind )
{
    return ( (uint32_t) kind << 16 ) | (uint32_t) target_addr;
}


/**
 * @brief Vrátí aktuální cpu->total_cycles, NULL-safe (vrací 0 pokud cpu == NULL).
 *
 * @return cpu->total_cycles nebo 0.
 */
static inline uint32_t prof_cpu_total_cycles_32 ( void )
{
    if ( !g_mzarch_main.cpu ) return 0;
    return g_mzarch_main.cpu->total_cycles;
}


/**
 * @brief 64-bit cycles extension s wraparound detekcí.
 *
 * Pokud now32 < g_prof_last_seen_32, předpokládá se wraparound uint32
 * counteru - baseline_64 se inkrementuje o 0x100000000. Vrací delta
 * od anchor (= "cycles od posledního resetu").
 *
 * @param now32 Aktuální cpu->total_cycles cast na uint32_t (= raw hodnota).
 * @return Cycles od resetu (64-bit, monotone rostoucí).
 *
 * @note Není thread-safe - voláno výhradně z EMU vlákna (listener handlery).
 */
static uint64_t prof_extend_cycles ( uint32_t now32 )
{
    if ( now32 < g_prof_last_seen_32 ) {
        /* Detekovaný wraparound uint32 counteru. */
        g_prof_cycles_baseline_64 += 0x100000000ULL;
    }
    g_prof_last_seen_32 = now32;
    uint64_t real_now_64 = g_prof_cycles_baseline_64 + (uint64_t) now32;
    if ( real_now_64 < g_prof_anchor_64 ) {
        /* Bezpečnost: anchor byl nastaven v budoucnosti (= reset bezprostředně
         * po wraparound). Vrátíme 0 místo obrovské hodnoty z uint64 underflow.
         * Edge case za normálního provozu nenastává. */
        return 0;
    }
    return real_now_64 - g_prof_anchor_64;
}


/**
 * @brief Lookup nebo vytvoření entry v hash mapě.
 *
 * Při miss alokuje nový st_PROF_ENTRY přes g_new0, inicializuje
 * cycles_incl_min na UINT64_MAX (aby první update zachytil reálnou
 * hodnotu).
 *
 * @param target_addr Cíl volání.
 * @param kind        en_CALLSTACK_ENTRY_KIND.
 * @return Pointer na akumulátor (vlastník = hash mapa).
 */
static st_PROF_ENTRY *prof_table_get_or_create ( uint16_t target_addr,
                                                  uint8_t kind )
{
    uint32_t key = prof_make_key ( target_addr, kind );
    st_PROF_ENTRY *acc = (st_PROF_ENTRY *) g_hash_table_lookup (
        g_prof_table, GUINT_TO_POINTER ( key ) );
    if ( acc ) return acc;

    acc = g_new0 ( st_PROF_ENTRY, 1 );
    acc->target_addr     = target_addr;
    acc->kind            = kind;
    acc->cycles_incl_min = UINT64_MAX;
    g_hash_table_insert ( g_prof_table, GUINT_TO_POINTER ( key ), acc );
    return acc;
}


/* ============================================================================
 * Callstack listener handlery
 * ============================================================================ */

/**
 * @brief Listener on_enter - push do parallel stacku + counters.
 *
 * Volaný z EMU vlákna z callstack push_entry. Pointer @c e platí jen
 * po dobu volání (= si nedržíme, jen čteme target_addr / kind /
 * cycles_at_entry).
 *
 * Pro IRQ accept (kind IRQ_IM0/1/2/NMI) inkrementuje samostatný
 * counter (g_prof_irq_entries) - umožňuje UI rozlišit "kolik z toho
 * byly IRQ".
 *
 * @param e    Právě pushnutá callstack entry.
 * @param user Nepoužito (NULL při registraci).
 */
static void prof_on_enter ( const st_CALLSTACK_ENTRY *e, void *user )
{
    (void) user;

    g_prof_total_calls++;

    /* IRQ entries counter - rozsah CS_KIND_IRQ_IM0..CS_KIND_NMI. */
    if ( e->kind >= CS_KIND_IRQ_IM0 && e->kind <= CS_KIND_NMI ) {
        g_prof_irq_entries++;
    }

    if ( g_prof_depth >= PROFILER_MAX_DEPTH ) {
        /* Tichá ztráta - paralelní stack je full. Hot path nesmí padat. */
        g_prof_overflow_count++;
        return;
    }

    int idx = g_prof_depth++;
    if ( (uint32_t) g_prof_depth > g_prof_max_depth ) {
        g_prof_max_depth = (uint32_t) g_prof_depth;
    }

    struct prof_frame *f = &g_prof_stack[ idx ];
    f->target_addr        = e->target_addr;
    f->kind               = e->kind;
    f->_pad               = 0;
    /* Callstack entry cycles_at_entry je uint64, ale ground truth je
     * jen low 32 bitů (= cpu->total_cycles cast). Maskujeme a předáme
     * extend funkci. */
    f->cycles_at_entry_64 = prof_extend_cycles (
        (uint32_t) ( e->cycles_at_entry & 0xFFFFFFFFu ) );
    f->child_incl_sum     = 0;
}


/**
 * @brief Listener on_exit - pop z parallel stacku + update akumulátoru.
 *
 * Volaný z EMU vlákna z callstack pop_top_entry. Divergent exit
 * (CS_FLAG_DIVERGENT) se nezapočítává do agregátoru - jen inkrementuje
 * unmatched_returns counter (= callstack si poradil s divergencí, ale
 * profiler nemá párový enter).
 *
 * Při pop validní entry:
 *   incl = cycles_now_64 - frame.cycles_at_entry_64
 *   excl = incl - frame.child_incl_sum   (= clamp na 0 při underflow)
 * a aktualizuje hash bucket. Inclusive cycles bubble-upne do parent
 * frame přes child_incl_sum.
 *
 * @param e    Právě popnutá callstack entry.
 * @param user Nepoužito.
 */
static void prof_on_exit ( const st_CALLSTACK_ENTRY *e, void *user )
{
    (void) user;

    /* Divergent exit (= callstack si poradil přes longjmp/synthetic).
     * Profiler agregátor nevidí spárovaný enter - skip a counter++. */
    if ( e->flags & CS_FLAG_DIVERGENT ) {
        g_prof_unmatched_returns++;
        return;
    }

    if ( g_prof_depth <= 0 ) {
        /* Pop nad prázdným parallel stackem - rozdíl mezi callstack a
         * profiler hloubkou (např. po overflow). Counter++ a skip. */
        g_prof_unmatched_returns++;
        return;
    }

    int idx = --g_prof_depth;
    struct prof_frame *f = &g_prof_stack[ idx ];

    uint64_t cycles_now_64 = prof_extend_cycles ( prof_cpu_total_cycles_32 ( ) );
    uint64_t incl = ( cycles_now_64 >= f->cycles_at_entry_64 )
                  ? ( cycles_now_64 - f->cycles_at_entry_64 )
                  : 0;
    uint64_t excl = ( incl > f->child_incl_sum )
                  ? ( incl - f->child_incl_sum )
                  : 0;

    st_PROF_ENTRY *acc = prof_table_get_or_create ( f->target_addr, f->kind );
    acc->calls++;
    acc->cycles_incl_sum += incl;
    acc->cycles_excl_sum += excl;
    if ( incl < acc->cycles_incl_min ) acc->cycles_incl_min = incl;
    if ( incl > acc->cycles_incl_max ) acc->cycles_incl_max = incl;

    /* Bubble inclusive do parent frame (= náš čas patří jeho dětem). */
    if ( g_prof_depth > 0 ) {
        g_prof_stack[ g_prof_depth - 1 ].child_incl_sum += incl;
    }
}


/* ============================================================================
 * Lifecycle (init / shutdown / set_active / reset)
 * ============================================================================ */

void profiler_reset ( void )
{
    /* Hash mapa wipe - destroy_value callback uvolní každý st_PROF_ENTRY. */
    if ( g_prof_table ) {
        g_hash_table_remove_all ( g_prof_table );
    }

    g_prof_depth             = 0;
    g_prof_total_calls       = 0;
    g_prof_irq_entries       = 0;
    g_prof_unmatched_returns = 0;
    g_prof_max_depth         = 0;
    g_prof_overflow_count    = 0;

    /* Baseline cycles - bere aktuální cpu->total_cycles jako "0". */
    uint32_t now32 = prof_cpu_total_cycles_32 ( );
    g_prof_cycles_baseline_64 = 0;
    g_prof_last_seen_32       = now32;
    g_prof_anchor_64          = (uint64_t) now32;
}


void profiler_notify_cpu_reset ( void )
{
    /* CPU hard reset = cpu->total_cycles vrácen na 0 (nebo blízko k 0).
     * Vyresetujeme baseline counter ale NE agregátor (= měření přes reset
     * je legal use case). */
    g_prof_cycles_baseline_64 = 0;
    g_prof_last_seen_32       = 0;
    g_prof_anchor_64          = 0;
}


void profiler_set_active ( bool enable )
{
    /* Idempotence. */
    if ( (bool) g_profiler_active == enable ) return;

    if ( enable ) {
        /* Pokud callstack OFF, zapneme ho a zapamatujeme ownership. */
        if ( !g_callstack_active ) {
            callstack_set_active ( true );
            g_profiler_owns_callstack = 1;
        } else {
            g_profiler_owns_callstack = 0;
        }

        /* Reset agregátoru pro čistý start měření. */
        profiler_reset ( );

        /* Registrujeme listener pár - od teď budou prof_on_enter /
         * prof_on_exit volány z callstack hot pathu. */
        callstack_register_listener ( prof_on_enter, prof_on_exit, NULL );

        g_profiler_active = 1;
    } else {
        g_profiler_active = 0;

        /* Odregistrujeme listener - od teď callstack listener slot opět
         * NULL → fan-out v push/pop hooks zero-overhead skip. */
        callstack_unregister_listener ( NULL );

        /* Pokud profiler zapnul callstack, vrátíme ho do předchozího stavu. */
        if ( g_profiler_owns_callstack ) {
            callstack_set_active ( false );
            g_profiler_owns_callstack = 0;
        }

        /* Data v agregátoru ponecháme - UI je dále zobrazuje (Stop pak Resume
         * měření = pokračovat, Reset = explicitně vyčistit). */
    }
}


/* ============================================================================
 * Snapshot API
 * ============================================================================ */

/**
 * @brief Pomocná struktura pro snapshot iteraci hash mapy.
 */
struct prof_snapshot_ctx {
    st_PROF_ENTRY *out;
    int            idx;
    int            capacity;
};


/**
 * @brief Hash table foreach callback - kopíruje entry do snapshot pole.
 */
static void prof_snapshot_copy_one ( gpointer key, gpointer value,
                                     gpointer user_data )
{
    (void) key;
    struct prof_snapshot_ctx *ctx = (struct prof_snapshot_ctx *) user_data;
    if ( ctx->idx >= ctx->capacity ) return;
    ctx->out[ ctx->idx ] = *(const st_PROF_ENTRY *) value;
    ctx->idx++;
}


void profiler_get_stats ( st_PROF_STATS *out )
{
    if ( !out ) return;
    out->active            = g_profiler_active ? true : false;
    out->total_cycles_64   = prof_extend_cycles ( prof_cpu_total_cycles_32 ( ) );
    out->total_calls       = g_prof_total_calls;
    out->irq_entries       = g_prof_irq_entries;
    out->unmatched_returns = g_prof_unmatched_returns;
    out->entry_count       = g_prof_table
                             ? (uint32_t) g_hash_table_size ( g_prof_table )
                             : 0;
    out->max_depth_reached = g_prof_max_depth;
    out->overflow_count    = g_prof_overflow_count;
}


int profiler_snapshot_get ( st_PROF_SNAPSHOT *out )
{
    if ( !out ) return -1;

    profiler_get_stats ( &out->stats );

    int n = (int) out->stats.entry_count;
    if ( n <= 0 || !g_prof_table ) {
        out->entries     = NULL;
        out->entry_count = 0;
        return 0;
    }

    size_t bytes = (size_t) n * sizeof ( st_PROF_ENTRY );
    st_PROF_ENTRY *buf = (st_PROF_ENTRY *) g_malloc ( bytes );
    if ( !buf ) {
        out->entries     = NULL;
        out->entry_count = 0;
        return -1;
    }

    struct prof_snapshot_ctx ctx = { buf, 0, n };
    g_hash_table_foreach ( g_prof_table, prof_snapshot_copy_one, &ctx );

    out->entries     = buf;
    out->entry_count = ctx.idx;
    return 0;
}


void profiler_snapshot_free ( st_PROF_SNAPSHOT *snap )
{
    if ( !snap ) return;
    if ( snap->entries ) {
        g_free ( snap->entries );
        snap->entries = NULL;
    }
    snap->entry_count = 0;
}


/* ============================================================================
 * cfgmain integrace ([PROFILER] sekce)
 * ============================================================================ */

/**
 * @brief Propagate cb pro [PROFILER] active - aplikuje hodnotu z INI.
 *
 * Pokud INI říká active=1, zapne profiler. Tím se zároveň vynutí
 * callstack_set_active(true) (= ownership tracking interní).
 */
static void cfg_propagate_active ( void *e, void *data )
{
    (void) data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    int v = cfgelement_get_bool_value ( elm );
    profiler_set_active ( v ? true : false );
}


/**
 * @brief Save cb pro [PROFILER] active - zapíše g_profiler_active.
 */
static void cfg_save_active ( void *e, void *data )
{
    (void) data;
    st_CFGELEMENT *elm = (st_CFGELEMENT *) e;
    cfgelement_set_bool_value ( elm, g_profiler_active ? 1 : 0 );
}


void profiler_init ( void )
{
    if ( s_initialized ) return;
    s_initialized = true;

    /* Hash mapa - klíč jako packed uint32, vlastník hodnot
     * (destroy_value uvolní každý st_PROF_ENTRY při remove / table free). */
    g_prof_table = g_hash_table_new_full ( g_direct_hash, g_direct_equal,
                                           NULL, g_free );

    /* Inicializovat baseline ze startu emu. */
    profiler_reset ( );

    /* cfgmain [PROFILER] sekce - klíče:
     *   - active      (bool, default 0) profiler subsystem ON/OFF při startu
     *   - show_window (bool, default 0) viditelnost CPU Profiler okna při startu
     * Registrace show_window je delegovaná do UI vrstvy (profiler_window.cpp)
     * přes profiler_window_register_cfg(), aby C jádro nemělo závislost na
     * g_gui. Identický pattern jako event_viewer_window_register_persistence. */
    CFGMOD *cmod = cfgroot_register_new_module ( g_cfgmain, "PROFILER" );
    if ( cmod ) {
        CFGELM *elm = cfgmodule_register_new_element ( cmod, "active",
                                                       CFGENTYPE_BOOL, 0 );
        cfgelement_set_propagate_cb ( elm, cfg_propagate_active, NULL );
        cfgelement_set_save_cb      ( elm, cfg_save_active,      NULL );

#ifndef MZTEST_HEADLESS
        /* Registrace UI-vlastněného klíče show_window. Volá se před parse,
         * aby propagate aplikoval hodnotu z INI do statické cache UI vrstvy.
         * Headless test build nelinkuje UI vrstvu, klíč se neregistruje
         * (= INI hodnota ignorovaná, žádný save). */
        profiler_window_register_cfg ( cmod );
#endif

        cfgmodule_parse ( cmod );
        cfgmodule_propagate ( cmod );
    }
    /* Pokud cmod == NULL (= duplicate registrace), běžíme bez persistence -
     * profiler zůstává OFF dokud někdo nezavolá profiler_set_active(true). */
}


void profiler_apply_cli_options ( void )
{
    /* CLI --profiler je FLAG (žádná hodnota). Pokud přítomen, vynutí
     * aktivaci nad INI hodnotou. Vzor: callstack_apply_cli_options. */
    if ( sdlapp_option_present ( "--profiler" ) ) {
        profiler_set_active ( true );
    }
}


void profiler_shutdown ( void )
{
    if ( !s_initialized ) return;

    /* Vypnout (= odregistrovat listener, vrátit callstack state pokud
     * jsme ho zapnuli my). */
    profiler_set_active ( false );

    if ( g_prof_table ) {
        g_hash_table_destroy ( g_prof_table );
        g_prof_table = NULL;
    }

    g_prof_depth             = 0;
    g_prof_total_calls       = 0;
    g_prof_irq_entries       = 0;
    g_prof_unmatched_returns = 0;
    g_prof_max_depth         = 0;
    g_prof_overflow_count    = 0;
    g_prof_cycles_baseline_64 = 0;
    g_prof_last_seen_32       = 0;
    g_prof_anchor_64          = 0;
    g_profiler_owns_callstack = 0;

    s_initialized = false;
}

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
