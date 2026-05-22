/*
 * dbgapi.c — implementace Debugger API (oba kanály: CMDRQ + MSG)
 *
 * Obsahuje:
 * - Inicializace a destrukce CMDRQ fronty
 * - EMU strana: kontrola fronty, dequeue, dispatch, complete, send_msg
 * - UI strana: submit_cmd_sync, kontrola stavu, MSG callback registrace
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
#include "main.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <glib.h>
#include "app/app_thread.h"
#include "dbgapi_cmdrq.h"
#include "dbgapi_msg.h"
#include "dbgapi_emu.h"
#include "dbgapi_ui.h"
#include "debugger.h"
#include "emulator.h"
#include "bptmap.h"
#include "breakpoints.h"
#include "stack_regions.h"
#include "stack_history.h"
#include "callstack.h"
#include "profiler.h"
#include "trace/eventlog.h"
#include "mzarch/mzarch.h"
#include "mzarch/mzarch_platform_functions.h"
#include "libs/dasm-z80/z80_dasm.h"
#include "gdg/video.h"
#include "memory/memory.h"
#if MZARCH == 800
#include "mzarch/mz800/gdg/mz800_gdg.h"
#elif MZARCH == 1500
#include "mzarch/mz1500/gdg/mz1500_gdg.h"
#elif MZARCH == 700
#include "mzarch/mz700/gdg/mz700_gdg.h"
#endif
#if HAVE_PIOZ80
#include "hw-generic/pioz80/pioz80.h"
#endif
#if MZARCH == 800
#include "mzarch/mz800/mz800_main.h"
#elif MZARCH == 1500
#include "mzarch/mz1500/mz1500_main.h"
#elif MZARCH == 700
#include "mzarch/mz700/mz700_main.h"
#endif


/* ============================================================================
 * GLOBÁLNÍ INSTANCE CMDRQ FRONTY
 * ============================================================================ */

st_DBGAPI_CMDRQ_QUEUE g_dbgapi_cmdrq_queue;


/* ============================================================================
 * REGISTROVANÝ MSG CALLBACK (UI strana, listener)
 *
 * Může být zaregistrován pouze jeden callback. Přístup chráněn tím,
 * že registrace/odregistrace probíhá pouze z UI vlákna a callback
 * se volá také pouze z UI vlákna (po thread switchi z dispatcher).
 * ============================================================================ */

static dbgapi_msg_callback_t s_msg_callback = NULL;
static void *s_msg_callback_user_data = NULL;


/* ============================================================================
 * REGISTROVANÝ MSG DISPATCHER (UI strana, thread switch)
 *
 * Dispatcher je registrován UI vrstvou při startup. EMU strana volá
 * dispatcher z dbgapi_emu_send_msg() pro doručení MSG do UI vlákna.
 * Implementace dispatcheru řeší thread switch (typicky SDL custom event)
 * a poté volá dbgapi_ui_invoke_msg_callback() v UI vlákně.
 *
 * Tímto způsobem dbgapi.c nezná SDL/sdlapp - thread switch je čistě
 * v UI vrstvě (src/ui-imgui/debugger/dbgapi_dispatcher.{cpp,h}).
 * ============================================================================ */

static dbgapi_msg_dispatcher_t s_msg_dispatcher = NULL;
static void *s_msg_dispatcher_user_data = NULL;


/* ============================================================================
 * INICIALIZACE A DESTRUKCE
 * ============================================================================ */

void dbgapi_init(st_DBGAPI_CMDRQ_QUEUE *queue)
{
    /* Vynulovat celou frontu */
    memset(queue, 0, sizeof(st_DBGAPI_CMDRQ_QUEUE));

    /* Alokace queue mutex a condition */
    APP_MUTEX_CREATE(queue->queue_mutex);
    APP_COND_CREATE(queue->queue_cond);

    /* Alokace per-slot mutex a condition pro každý slot */
    for (int i = 0; i < DBGAPI_CMDRQ_QUEUE_SIZE; i++)
    {
        st_DBGAPI_CMDRQ *slot = &queue->cmdrq[i];
        slot->cmd_state = DBGAPI_CMDSTATE_NONE;
        slot->cmd = DBGAPI_CMD_NONE;
        slot->data_ptr = NULL;
        slot->result_ptr = NULL;
        slot->success = false;
        APP_MUTEX_CREATE(slot->mutex);
        APP_COND_CREATE(slot->cond);
    };

    queue->head = 0;
    queue->tail = 0;
    queue->reply_state = DBGAPI_CMDREPLY_STATE_NONE;

    /* Reset MSG callbacku a dispatcheru */
    s_msg_callback = NULL;
    s_msg_callback_user_data = NULL;
    s_msg_dispatcher = NULL;
    s_msg_dispatcher_user_data = NULL;
}

void dbgapi_destroy(st_DBGAPI_CMDRQ_QUEUE *queue)
{
    /* Uvolnění per-slot mutex a condition */
    for (int i = 0; i < DBGAPI_CMDRQ_QUEUE_SIZE; i++)
    {
        st_DBGAPI_CMDRQ *slot = &queue->cmdrq[i];
        APP_COND_DESTROY(slot->cond);
        APP_MUTEX_DESTROY(slot->mutex);
    };

    /* Uvolnění queue mutex a condition */
    APP_COND_DESTROY(queue->queue_cond);
    APP_MUTEX_DESTROY(queue->queue_mutex);

    /* Reset MSG callbacku a dispatcheru */
    s_msg_callback = NULL;
    s_msg_callback_user_data = NULL;
    s_msg_dispatcher = NULL;
    s_msg_dispatcher_user_data = NULL;
}


/* ============================================================================
 * INTERNÍ POMOCNÉ FUNKCE
 * ============================================================================ */

/**
 * @brief Step Over - jeden krok přes CALL/RST/DJNZ/blokovou instrukci.
 *
 * Analyzuje opcode na aktuálním PC. Pro CALL nn / CALL cc,nn / RST /
 * DJNZ / ED-prefixed blokové instrukce (LDIR/LDDR/CPIR/CPDR/INIR/
 * INDR/OTIR/OTDR) nastaví dočasný breakpoint na (PC + délka instrukce)
 * a spustí emulaci přes mzarch_run_to_temporary_breakpoint(). Pro
 * ostatní instrukce degraduje na step into (debugger_step_call(1)).
 *
 * Replikuje logiku z dbg_iconbar.cpp::dbg_do_step_over() (= UI vrstva
 * po dobu hybridního modelu UI direct calls + dbgapi).
 *
 * @pre EMULATOR_TEST_PAUSED (= step over má smysl jen ze paused stavu;
 *      caller volá emulator_pause(true) pokud emu běží).
 */
static void dbgapi_emu_do_step_over ( void )
{
    uint16_t pc = g_mzarch_main.cpu->pc;
    uint8_t opcode = debugger_dasm_read_cb ( pc, NULL );

    bool is_step_over_target = false;
    int instr_len = 1;

    if ( opcode == 0xCD )
    {
        /* CALL nn — nepodmíněný CALL */
        is_step_over_target = true;
        instr_len = 3;
    }
    else if ( ( opcode & 0xC7 ) == 0xC4 )
    {
        /* CALL cc,nn — podmíněný CALL */
        is_step_over_target = true;
        instr_len = 3;
    }
    else if ( ( opcode & 0xC7 ) == 0xC7 )
    {
        /* RST xx */
        is_step_over_target = true;
        instr_len = 1;
    }
    else if ( opcode == 0x10 )
    {
        /* DJNZ e */
        is_step_over_target = true;
        instr_len = 2;
    }
    else if ( opcode == 0xED )
    {
        uint8_t opcode2 = debugger_dasm_read_cb ( (uint16_t) ( pc + 1 ), NULL );
        switch ( opcode2 )
        {
            case 0xB0: /* LDIR */
            case 0xB8: /* LDDR */
            case 0xB1: /* CPIR */
            case 0xB9: /* CPDR */
            case 0xB2: /* INIR */
            case 0xBA: /* INDR */
            case 0xB3: /* OTIR */
            case 0xBB: /* OTDR */
                is_step_over_target = true;
                instr_len = 2;
                break;
            default:
                break;
        };
    };

    if ( is_step_over_target )
    {
        bptmap_set_temporary_event ( (uint16_t) ( pc + instr_len ) );
        debugger_step_call ( 0 );
#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
        mzarch_run_to_temporary_breakpoint ( );
#endif
    }
    else
    {
        debugger_step_call ( 1 );
    };
}


/* ============================================================================
 * EMU STRANA — KONTROLA A ZPRACOVÁNÍ FRONTY
 * ============================================================================ */

/*
 * Interní helper: zjistí, zda je fronta prázdná (bez zamykání).
 * Volat pouze s drženým queue_mutex.
 */
static inline bool dbgapi_emu_has_pending_unlocked(st_DBGAPI_CMDRQ_QUEUE *queue)
{
    return (queue->head != queue->tail);
}

bool dbgapi_emu_has_pending(st_DBGAPI_CMDRQ_QUEUE *queue)
{
    APP_MUTEX_LOCK(queue->queue_mutex);
    bool result = dbgapi_emu_has_pending_unlocked(queue);
    APP_MUTEX_UNLOCK(queue->queue_mutex);
    return result;
}

st_DBGAPI_CMDRQ *dbgapi_emu_dequeue(st_DBGAPI_CMDRQ_QUEUE *queue)
{
    APP_MUTEX_LOCK(queue->queue_mutex);

    /* Fronta prázdná? */
    if (!dbgapi_emu_has_pending_unlocked(queue))
    {
        APP_MUTEX_UNLOCK(queue->queue_mutex);
        return NULL;
    };

    /* Vyjmout slot z hlavy fronty */
    st_DBGAPI_CMDRQ *slot = &queue->cmdrq[queue->head];
    queue->head = (queue->head + 1) % DBGAPI_CMDRQ_QUEUE_SIZE;

    APP_MUTEX_UNLOCK(queue->queue_mutex);
    return slot;
}

/**
 * @brief Read callback pro z80_dasm() při dekódování Last instrukce.
 *
 * Čte bajty z lokálně uloženého `st_DEBUGGER_HISTORY_ROW.byte[]`, ne
 * z živé paměti (=  byte[] obsahují bajty platné v okamžiku M1 startu,
 * což je presné a nezávislé na pozdějších banking změnách).
 *
 * Mapování: offset = addr - row->addr. Pokud offset >= 4 (= za hranou
 * uloženého bufferu), vrací 0x00 jako sentinel - z80_dasm by neměl číst
 * víc než length-1 bajtů (= max 3 pro 4-byte instrukci).
 *
 * @param addr      Adresa, kterou disassembler žádá.
 * @param user_data Pointer na st_DEBUGGER_HISTORY_ROW.
 * @return Bajt z row->byte[offset] nebo 0 pri OOB.
 */
static uint8_t dbgapi_last_instr_read_cb(uint16_t addr, void *user_data)
{
    st_DEBUGGER_HISTORY_ROW *row = (st_DEBUGGER_HISTORY_ROW *)user_data;
    int off = (int)(addr - row->addr);
    if (off < 0 || off >= 4) return 0x00;
    return row->byte[off];
}


/**
 * @brief Pomocný handler pro CMD_BP_UPDATE a CMD_BP_CREATE_WITH_INIT.
 *
 * Aplikuje selektivní update polí BP podle p->update_mask voláním
 * existujících breakpoints_set_*() setterů.
 *
 * Pokud allow_create == true (= CMD_BP_CREATE_WITH_INIT):
 *  - p->id musí být -1 na vstupu (jiná hodnota = success false)
 *  - Volá breakpoints_add_auto(p->addr, p->name pokud UM_NAME, p->parent
 *    pokud UM_PARENT, jinak -1). Po úspěchu naplní p->id přiděleným ID.
 *  - Pokud add selže, vrátí false a p->id zůstane -1 (rollback není
 *    potřeba - nic se nevytvořilo).
 *  - Po úspěšném add aplikuje zbytek update_mask přes setter cestu níže.
 *
 * Pokud allow_create == false (= CMD_BP_UPDATE):
 *  - p->id musí ukazovat na existující BP. Pokud
 *    breakpoints_find_by_id(p->id) == NULL, vrátí false a žádnou změnu
 *    neaplikuje (= pre-check před iterací maskou).
 *
 * Apply best-effort: pokud některý setter vrátí false (= např. nečekané),
 * helper pokračuje v zápisu ostatních polí a vrátí AND všech výsledků
 * (= konzistentní s dnešním working_copy_apply, který výsledky setterů
 * neřeší). Pre-check existence BP zachycuje hlavní failure case.
 *
 * @param p          Vstupní payload (id, update_mask, fieldy).
 * @param allow_create true = CMD_BP_CREATE_WITH_INIT (= naplní id),
 *                     false = CMD_BP_UPDATE (= read-only id).
 * @return true = pre-check OK + všechny aktivní settery vrátily true.
 */
static bool dbgapi_emu_bp_apply_update ( st_DBGAPI_BP_UPDATE_PARAM *p, bool allow_create )
{
    if ( !p ) return false;

    int target_id;

    if ( allow_create )
    {
        if ( p->id != -1 ) return false;
        uint16_t init_addr = ( p->update_mask & DBGAPI_BP_UM_ADDR ) ? p->addr : 0;
        const char *init_name =
            ( p->update_mask & DBGAPI_BP_UM_NAME ) ? p->name : NULL;
        int init_parent =
            ( p->update_mask & DBGAPI_BP_UM_PARENT ) ? p->parent : -1;
        int new_id = breakpoints_add_auto ( init_addr, init_name, init_parent );
        if ( new_id < 0 ) return false;
        p->id = new_id;
        target_id = new_id;
        /* Smazat UM_ADDR/_NAME/_PARENT z masky - add_auto je už nastavil.
         * Setter aplikace níže by je přepsala identickou hodnotou (idempotent),
         * takže maska se neořezává - ponecháme. */
    }
    else
    {
        if ( !breakpoints_find_by_id ( p->id ) ) return false;
        target_id = p->id;
    };

    uint64_t mask = p->update_mask;
    bool ok = true;

    /* === Identifikace === */
    if ( mask & DBGAPI_BP_UM_ENABLED )
        ok &= breakpoints_set_enabled ( target_id, p->enabled );
    if ( mask & DBGAPI_BP_UM_AUTO_NAME )
        ok &= breakpoints_set_auto_name ( target_id, p->auto_name );
    if ( mask & DBGAPI_BP_UM_NAME )
        ok &= breakpoints_set_name ( target_id, p->name );
    if ( mask & DBGAPI_BP_UM_COLORS )
        ok &= breakpoints_set_colors ( target_id, p->bg_rgb, p->fg_rgb );
    if ( mask & DBGAPI_BP_UM_PARENT )
        ok &= breakpoints_set_parent ( target_id, p->parent );

    /* === Smart core === */
    if ( mask & DBGAPI_BP_UM_TYPE )
        ok &= breakpoints_set_type ( target_id, (en_BPT_TYPE)p->type );
    if ( mask & DBGAPI_BP_UM_ADDR )
        ok &= breakpoints_set_addr ( target_id, p->addr );
    if ( mask & DBGAPI_BP_UM_ADDR_END )
        ok &= breakpoints_set_addr_end ( target_id, p->addr_end );
    if ( mask & DBGAPI_BP_UM_ZONE )
        ok &= breakpoints_set_zone ( target_id, (en_BP_ZONE)p->zone );
    if ( mask & DBGAPI_BP_UM_BANK_ID )
        ok &= breakpoints_set_bank_id ( target_id, p->bank_id );
    if ( mask & DBGAPI_BP_UM_PORT )
        ok &= breakpoints_set_port ( target_id, p->port );
    if ( mask & DBGAPI_BP_UM_EVENT_NAME )
        ok &= breakpoints_set_event_name ( target_id, p->event_name );
    if ( mask & DBGAPI_BP_UM_EVENT_TRIGGER )
        ok &= breakpoints_set_event_trigger ( target_id, (en_BP_EVENT_TRIGGER)p->event_trigger );
    if ( mask & DBGAPI_BP_UM_SP_THRESHOLD )
        ok &= breakpoints_set_sp_threshold ( target_id, p->sp_threshold );
    if ( mask & DBGAPI_BP_UM_EXPR )
        ok &= breakpoints_set_expr ( target_id, p->expr );
    if ( mask & DBGAPI_BP_UM_ACTION )
        ok &= breakpoints_set_action ( target_id, p->action );
    if ( mask & DBGAPI_BP_UM_HIT_COUNT )
        ok &= breakpoints_set_hit_count ( target_id, p->hit_count );
    if ( mask & DBGAPI_BP_UM_SKIP_COUNT )
        ok &= breakpoints_set_skip_count ( target_id, p->skip_count );
    if ( mask & DBGAPI_BP_UM_EDGE_TRIGGERED )
        ok &= breakpoints_set_edge_triggered ( target_id, p->edge_triggered );

    /* === Match modes === */
    if ( mask & DBGAPI_BP_UM_ADDR_MATCH_MODE )
        ok &= breakpoints_set_addr_match_mode ( target_id, (en_BP_MATCH_MODE)p->addr_match_mode );
    if ( mask & DBGAPI_BP_UM_ADDR_MASK )
        ok &= breakpoints_set_addr_mask ( target_id, p->addr_mask );
    if ( mask & DBGAPI_BP_UM_PORT_MATCH_MODE )
        ok &= breakpoints_set_port_match_mode ( target_id, (en_BP_MATCH_MODE)p->port_match_mode );
    if ( mask & DBGAPI_BP_UM_PORT_END )
        ok &= breakpoints_set_port_end ( target_id, p->port_end );
    if ( mask & DBGAPI_BP_UM_PORT_MASK )
        ok &= breakpoints_set_port_mask ( target_id, p->port_mask );
    if ( mask & DBGAPI_BP_UM_PORT_MODE )
        ok &= breakpoints_set_port_mode ( target_id, (en_BP_PORT_MODE)p->port_mode );
    if ( mask & DBGAPI_BP_UM_BANK_MATCH_MODE )
        ok &= breakpoints_set_bank_match_mode ( target_id, (en_BP_MATCH_MODE)p->bank_match_mode );
    if ( mask & DBGAPI_BP_UM_BANK_ID_END )
        ok &= breakpoints_set_bank_id_end ( target_id, p->bank_id_end );
    if ( mask & DBGAPI_BP_UM_BANK_ID_MASK )
        ok &= breakpoints_set_bank_id_mask ( target_id, p->bank_id_mask );
    if ( mask & DBGAPI_BP_UM_SP_MODE )
        ok &= breakpoints_set_sp_mode ( target_id, (en_BP_SP_MODE)p->sp_mode );
    if ( mask & DBGAPI_BP_UM_SP_UPPER )
        ok &= breakpoints_set_sp_upper ( target_id, p->sp_upper );

    /* === IRQ A8 === */
    if ( mask & DBGAPI_BP_UM_IM2_VECTOR_FILTER )
        ok &= breakpoints_set_im2_vector_filter ( target_id, p->im2_vector_enabled, p->im2_vector_addr );
    if ( mask & DBGAPI_BP_UM_IM2_VECTOR_MATCH_MODE )
        ok &= breakpoints_set_im2_vector_match_mode ( target_id, (en_BP_MATCH_MODE)p->im2_vector_match_mode );
    if ( mask & DBGAPI_BP_UM_IM2_VECTOR_ADDR_END )
        ok &= breakpoints_set_im2_vector_addr_end ( target_id, p->im2_vector_addr_end );
    if ( mask & DBGAPI_BP_UM_IM2_VECTOR_MASK )
        ok &= breakpoints_set_im2_vector_mask ( target_id, p->im2_vector_mask );
    if ( mask & DBGAPI_BP_UM_IM2_ISR_FILTER )
        ok &= breakpoints_set_im2_isr_filter ( target_id, p->im2_isr_enabled, p->im2_isr_addr );
    if ( mask & DBGAPI_BP_UM_IM2_ISR_MATCH_MODE )
        ok &= breakpoints_set_im2_isr_match_mode ( target_id, (en_BP_MATCH_MODE)p->im2_isr_match_mode );
    if ( mask & DBGAPI_BP_UM_IM2_ISR_ADDR_END )
        ok &= breakpoints_set_im2_isr_addr_end ( target_id, p->im2_isr_addr_end );
    if ( mask & DBGAPI_BP_UM_IM2_ISR_MASK )
        ok &= breakpoints_set_im2_isr_mask ( target_id, p->im2_isr_mask );

    /* === IRQ A8.5 === */
    if ( mask & DBGAPI_BP_UM_IM0_ENABLED )
        ok &= breakpoints_set_im_enabled ( target_id, 0, p->im0_enabled );
    if ( mask & DBGAPI_BP_UM_IM1_ENABLED )
        ok &= breakpoints_set_im_enabled ( target_id, 1, p->im1_enabled );
    if ( mask & DBGAPI_BP_UM_IM2_ENABLED )
        ok &= breakpoints_set_im_enabled ( target_id, 2, p->im2_enabled );
    if ( mask & DBGAPI_BP_UM_IM0_RST_MASK )
        ok &= breakpoints_set_im0_rst_mask ( target_id, p->im0_rst_mask );

    /* === IRQ_SIG === */
    if ( mask & DBGAPI_BP_UM_IRQ_SIG_SOURCE_MASK )
        ok &= breakpoints_set_irq_sig_source_mask ( target_id, p->irq_sig_source_mask );

    return ok;
}


void dbgapi_emu_dispatch(st_DBGAPI_CMDRQ *rq)
{
    if (!rq)
        return;

    /* Extrahovat příkaz bez BLOCKING flagu */
    en_DBGAPI_CMD cmd = (en_DBGAPI_CMD)(rq->cmd & DBGAPI_CMD_MASK);

    /*
     * Dispatch příkazů — zatím základní implementace.
     * Konkrétní handlery budou doplněny až budou k dispozici
     * příslušné moduly emulátoru (debugger.h, bptmap.h, cpu-z80, ...).
     *
     * TODO: doplnit handlery pro jednotlivé příkazy
     */
    switch (cmd)
    {
        case DBGAPI_CMD_NONE:
            /* Ping — bez efektu */
            rq->success = true;
            break;

        case DBGAPI_CMD_IS_DEBUGGER_ACTIVE:
            /* Vrátí stav g_debugger.active (= debug okno otevřené)
             * jako bool přes result_ptr. */
            if (rq->result_ptr)
            {
                *((bool *)rq->result_ptr) = TEST_DEBUGGER_ACTIVE;
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_DEBUGGER_ACTIVATE:
            /* Aktivuje debugger (= nastaví g_debugger.active = 1).
             * Side effect: TEST_DEBUGGER_CPUHIST_ACTIVE / MHMAP_ACTIVE
             * v default WITH_WINDOW režimu se zapne, takže CPU instrukční
             * historie a memory heatmap začnou zaznamenávat.
             * Forward na přímou manipulaci globálu (ekvivalent dnešního UI
             * volání debugger_show_main_window()). */
            g_debugger.active = 1;
            rq->success = true;
            break;

        case DBGAPI_CMD_DEBUGGER_DEACTIVATE:
            /* Deaktivuje debugger (= g_debugger.active = 0).
             * Side effect: cpuhist a mhmap recording v WITH_WINDOW režimu
             * se vypne. */
            g_debugger.active = 0;
            rq->success = true;
            break;

        case DBGAPI_CMD_PAUSE:
            /* Pozastaví emulaci. Forward na emulator_pause(true).
             * Side effecty (z emulator.c:273): MZ800_MAIN_SET_EVENT
             * BREAK_EMULATION_PAUSED, audio pause, UI state update,
             * pokud TEST_DEBUGGER_ACTIVE pak hide spinner + reset
             * temporary BP. */
            emulator_pause ( true );
            rq->success = true;
            break;

        case DBGAPI_CMD_FORCE_PAUSE:
            /* Vynutí pauzu emulace. V současné implementaci shodné
             * s CMD_PAUSE - emulator_pause(true) nemá konkurenční
             * cestu která by ho mohla "přeskočit" (= žádný BP context
             * který by zámik bránil v aplikaci pauzy). Pokud v budoucnu
             * vznikne nepřeskočitelný kontext, FORCE_PAUSE bude bypass. */
            emulator_pause ( true );
            rq->success = true;
            break;

        case DBGAPI_CMD_RUN:
            /* Spustí emulaci. Forward na emulator_pause(false).
             * Side effecty (z emulator.c:273): audio resume, UI state
             * update, pokud TEST_DEBUGGER_ACTIVE pak show spinner +
             * window focus. Také vynuluje run_to_temporary_breakpoint
             * flag. */
            emulator_pause ( false );
            rq->success = true;
            break;

        case DBGAPI_CMD_IS_RUNNING:
            /* Vrátí stav emulace (= negace EMULATOR_TEST_PAUSED) jako
             * bool přes result_ptr. */
            if (rq->result_ptr)
            {
                *((bool *)rq->result_ptr) = !EMULATOR_TEST_PAUSED;
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_STEP_INTO:
            /* Step Into - jeden krok přes aktuální instrukci.
             * Forward na debugger_step_call(1). Pokud emulace běží,
             * debugger_step_call() ji nejprve pozastaví a step se
             * neprovede (= musí volat client znovu po pause).
             * Side effecty: g_debugger.step_call = 1 → hot loop
             * mzarch.c:758 detekuje TEST_DEBUGGER_STEP_CALL po jedné
             * instrukci a opět zastaví. */
            debugger_step_call ( 1 );
            rq->success = true;
            break;

        case DBGAPI_CMD_STEP_OVER:
            /* Step Over - CALL/RST/DJNZ/blokové instrukce: nastaví temp BP
             * na addr+length a spustí run-to-temp-BP. Ostatní: step into.
             * Interní helper dbgapi_emu_do_step_over() (= replika logiky
             * z dbg_iconbar.cpp). Pokud emu běží, caller musí pause před
             * voláním (default UX). */
            if ( EMULATOR_TEST_PAUSED )
            {
                dbgapi_emu_do_step_over ( );
            }
            else
            {
                emulator_pause ( true );
            };
            rq->success = true;
            break;

        case DBGAPI_CMD_RUN_TO:
            /* Run To Cursor / Run To Address: nastaví dočasný BP na
             * cílovou adresu a spustí emulaci přes
             * mzarch_run_to_temporary_breakpoint(). Cílová adresa v
             * data_ptr (= uint16_t*). Pokud emu běží, pause + return
             * (= UX z dbg_iconbar.cpp::dbg_do_run_to_cursor). */
            if ( !rq->data_ptr )
            {
                rq->success = false;
                break;
            };
            if ( !EMULATOR_TEST_PAUSED )
            {
                emulator_pause ( true );
                rq->success = true;
                break;
            };
            {
                uint16_t target = *((uint16_t *)rq->data_ptr);
                bptmap_set_temporary_event ( target );
                debugger_step_call ( 0 );
#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
                mzarch_run_to_temporary_breakpoint ( );
#endif
            }
            rq->success = true;
            break;

        case DBGAPI_CMD_RESET:
            /* Reset emulátoru - asynchronní přes
             * mzarch_platform_fn_reset_request(). Reset je proveden
             * v mzarch_main loopu při příští iteraci (= zámek
             * reset_request_mutex + flag). Side effecty: gdg_reset,
             * memory_reset, z80_reset, periferie reset, debugger
             * update. Z paused stavu je reset detekován v pause
             * loop (mzarch.c:594) okamžitě a zachová pause. */
            mzarch_platform_fn_reset_request ( );
            rq->success = true;
            break;

        case DBGAPI_CMD_GET_REG:
            /* Čtení 16bitové hodnoty Z80 registru přes z80_get_reg().
             * data_ptr: uint8_t* - reg_id (= z80_reg_t casted na uint8_t,
             * 0..13 dle z80.h:248-263, viz Z80_REG_AF...Z80_REG_IR).
             * result_ptr: uint16_t* - výstup hodnoty. Pro IR registr
             * vrací 16bitové (I << 8) | R kompozit. */
            if (rq->data_ptr && rq->result_ptr)
            {
                uint8_t reg_id = *((uint8_t *)rq->data_ptr);
                if (reg_id <= (uint8_t)Z80_REG_IR)
                {
                    *((uint16_t *)rq->result_ptr) =
                        z80_get_reg ( g_mzarch_main.cpu, (z80_reg_t)reg_id );
                    rq->success = true;
                }
                else
                {
                    rq->success = false;
                };
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_SET_REG:
            /* Zápis 16bitové hodnoty do Z80 registru. data_ptr:
             * st_DBGAPI_REG_PARAM* (= reg_id + value). Pro IR registr
             * je speciální handling (jen dolní bajt = R), shodný s
             * debugger_change_z80_register(). Forward přes přímý
             * z80_set_reg() bez pause check (= debugger_change funkce
             * dělá pause check pro UI vlákno; dbgapi je už v emu vlákně).
             *
             * (hypotéza) Volání z emu vlákna mimo z80_step() je bezpečné
             * (= debugger.c:582 to také dělá z UI vlákna pres pause). */
            if (rq->data_ptr)
            {
                st_DBGAPI_REG_PARAM *p = (st_DBGAPI_REG_PARAM *)rq->data_ptr;
                if (p->reg_id <= (uint8_t)Z80_REG_IR)
                {
                    if ((z80_reg_t)p->reg_id == Z80_REG_IR)
                    {
                        /* Specialni handling: nastavit jen dolni bajt R,
                         * I (vysoky bajt) zachovat - shodne s
                         * debugger_change_z80_register(). */
                        g_mzarch_main.cpu->r =
                            (uint8_t)(p->value & 0x7F) |
                            (g_mzarch_main.cpu->r & 0x80);
                    }
                    else
                    {
                        z80_set_reg ( g_mzarch_main.cpu,
                                      (z80_reg_t)p->reg_id, p->value );
                    };
                    rq->success = true;
                }
                else
                {
                    rq->success = false;
                };
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_GET_ALL_REGS:
            /* Dump všech Z80 registrů do uint16_t[DBGAPI_REG_COUNT] pole.
             * Pořadí dle z80_reg_t enum (Z80_REG_AF..Z80_REG_IR). Caller
             * alokuje pole; musí mít kapacitu DBGAPI_REG_COUNT * 2 byte. */
            if (rq->result_ptr)
            {
                uint16_t *out = (uint16_t *)rq->result_ptr;
                for (int i = 0; i < DBGAPI_REG_COUNT; i++)
                {
                    out[i] = z80_get_reg ( g_mzarch_main.cpu, (z80_reg_t)i );
                };
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_MEM_READ:
            /* Čtení bloku paměti respektujícího banking. data_ptr:
             * st_DBGAPI_MEM_PARAM* (= addr + len + buf). Buffer
             * vlastní caller, dispatch ho jen vyplní. Forward přes
             * debugger_memory_read_byte() (= memory_read_byte přes
             * banking, BEZ side effects pro VRAM/IORQ ports). */
            if (rq->data_ptr)
            {
                st_DBGAPI_MEM_PARAM *p = (st_DBGAPI_MEM_PARAM *)rq->data_ptr;
                if (p->buf)
                {
                    for (uint32_t i = 0; i < p->len; i++)
                    {
                        p->buf[i] = debugger_memory_read_byte (
                            (uint16_t)(p->addr + i) );
                    };
                    rq->success = true;
                }
                else
                {
                    rq->success = false;
                };
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_MEM_WRITE:
            /* Zápis bloku paměti respektujícího banking. data_ptr:
             * st_DBGAPI_MEM_PARAM* (= addr + len + buf). buf vlastní
             * caller, dispatch ho jen čte. Forward přes
             * debugger_memory_write_byte() (= memory_write_byte přes
             * banking + g_debugger.memop_call flag pro CDL recording). */
            if (rq->data_ptr)
            {
                st_DBGAPI_MEM_PARAM *p = (st_DBGAPI_MEM_PARAM *)rq->data_ptr;
                if (p->buf)
                {
                    for (uint32_t i = 0; i < p->len; i++)
                    {
                        debugger_memory_write_byte (
                            (uint16_t)(p->addr + i), p->buf[i] );
                    };
                    rq->success = true;
                }
                else
                {
                    rq->success = false;
                };
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_BP_ADD:
            /* Přidá execution BP na adrese. data_ptr: st_DBGAPI_BP_PARAM*
             * (= addr + id; id se ignoruje - generuje breakpoints_add_auto).
             * Po návratu je p->id naplněno přiděleným ID (= caller ho
             * potřebuje pro REMOVE). breakpoints_add_auto vrátí -1 při
             * fatální chybě (přetečení ID), jinak vždy přidělí ID a v
             * případě konfliktu na adrese nastaví enabled=false. */
            if (rq->data_ptr)
            {
                st_DBGAPI_BP_PARAM *p = (st_DBGAPI_BP_PARAM *)rq->data_ptr;
                int new_id = breakpoints_add_auto ( p->addr, NULL, -1 );
                if (new_id > 0)
                {
                    p->id = new_id;
                    rq->success = true;
                }
                else
                {
                    rq->success = false;
                };
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_BP_REMOVE:
            /* Odstraní BP podle ID. data_ptr: st_DBGAPI_BP_PARAM*
             * (= id; addr se ignoruje). breakpoints_remove() vrací
             * true při úspěchu, false pokud ID neexistuje. */
            if (rq->data_ptr)
            {
                st_DBGAPI_BP_PARAM *p = (st_DBGAPI_BP_PARAM *)rq->data_ptr;
                rq->success = breakpoints_remove ( p->id );
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_BP_LIST:
            /* Vrátí seznam BP do result_ptr (st_DBGAPI_BP_LIST_RESULT*).
             * Caller alokuje strukturu s flexibilním polem bp[max_count]
             * a nastaví max_count. Dispatch naplní count a bp[] až do
             * max_count (= overflow ořízne, ale úspěch). */
            if (rq->result_ptr)
            {
                st_DBGAPI_BP_LIST_RESULT *r =
                    (st_DBGAPI_BP_LIST_RESULT *)rq->result_ptr;
                int total = (int)g_breakpoints.breakpoints->len;
                int n = ( total < r->max_count ) ? total : r->max_count;
                for (int i = 0; i < n; i++)
                {
                    st_BPT *b = &g_array_index ( g_breakpoints.breakpoints,
                                                 st_BPT, i );
                    r->bp[i].addr = b->addr;
                    r->bp[i].id = b->id;
                    r->bp[i].enabled = b->enabled;
                };
                r->count = n;
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_BP_UPDATE:
            /* Selektivní update existujícího BP. data_ptr:
             * st_DBGAPI_BP_UPDATE_PARAM*. Handler iteruje bity update_mask
             * a volá odpovídající breakpoints_set_*() setter. Setteři
             * vracejí false jen pokud BP neexistuje - po prvním ověření
             * existence je každý setter úspěšný (= validace rozsahů je
             * v setteru, špatná hodnota se uloží ale parsed cache zůstane
             * NULL pro expr/action - identické s dnešním přímým voláním
             * z UI). success = false jen pokud BP s id neexistuje (=
             * žádná změna neaplikována) nebo pokud update_mask požaduje
             * neznámou enum hodnotu. update_mask == 0 = no-op success. */
            if (rq->data_ptr)
            {
                rq->success =
                    dbgapi_emu_bp_apply_update(
                        (st_DBGAPI_BP_UPDATE_PARAM *)rq->data_ptr,
                        /* allow_create */ false );
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_BP_SET_ENABLED:
            /* Quick toggle. Forwarder na breakpoints_set_enabled(). */
            if (rq->data_ptr)
            {
                st_DBGAPI_BP_SET_ENABLED_PARAM *p =
                    (st_DBGAPI_BP_SET_ENABLED_PARAM *)rq->data_ptr;
                rq->success = breakpoints_set_enabled ( p->id, p->enabled );
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_BP_SET_PARENT:
            /* Quick reparent. Forwarder na breakpoints_set_parent(). */
            if (rq->data_ptr)
            {
                st_DBGAPI_BP_SET_PARENT_PARAM *p =
                    (st_DBGAPI_BP_SET_PARENT_PARAM *)rq->data_ptr;
                rq->success = breakpoints_set_parent ( p->id, p->parent_id );
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_BP_CREATE_WITH_INIT:
            /* Atomický create + init. Caller předá st_DBGAPI_BP_UPDATE_PARAM
             * s id=-1. Handler vola breakpoints_add_auto(addr, name, parent)
             * - addr se bere z payload (UM_ADDR by neměl být v mask = handler
             * stejně bere addr přímo, ale pokud user CHCE addr pak UM_ADDR
             * se aplikuje znovu = idempotent). parent se vezme z UM_PARENT
             * pokud nastaveno, jinak default -1. Po úspěchu naplní p->id +
             * aplikuje zbytek update_mask. Pokud add selže, p->id zůstane -1
             * a success = false. */
            if (rq->data_ptr)
            {
                rq->success =
                    dbgapi_emu_bp_apply_update(
                        (st_DBGAPI_BP_UPDATE_PARAM *)rq->data_ptr,
                        /* allow_create */ true );
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_BPGRP_ADD:
            /* Přidá novou skupinu. Forwarder na breakpoints_group_add(name,
             * parent). Po úspěchu naplní p->id, jinak p->id zůstane -1 a
             * success = false. */
            if (rq->data_ptr)
            {
                st_DBGAPI_BPGRP_ADD_PARAM *p =
                    (st_DBGAPI_BPGRP_ADD_PARAM *)rq->data_ptr;
                int new_id = breakpoints_group_add ( p->name, p->parent );
                if ( new_id >= 0 )
                {
                    p->id = new_id;
                    rq->success = true;
                }
                else
                {
                    p->id = -1;
                    rq->success = false;
                };
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_BPGRP_REMOVE:
            /* Odstraní skupinu podle ID. Forwarder na breakpoints_group_remove.
             * Backendová logika hendluje sirotky (děti přesměrovává nebo
             * mazat, viz breakpoints.c). */
            if (rq->data_ptr)
            {
                st_DBGAPI_BPGRP_REMOVE_PARAM *p =
                    (st_DBGAPI_BPGRP_REMOVE_PARAM *)rq->data_ptr;
                rq->success = breakpoints_group_remove ( p->id );
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_BPGRP_UPDATE:
            /* Selektivní update existující skupiny. Handler iteruje
             * update_mask bity + volá existující breakpoints_group_set_*().
             * Pre-check existence skupiny - pokud find_by_id == NULL,
             * vrátí false bez aplikace. update_mask == 0 = no-op success. */
            if (rq->data_ptr)
            {
                st_DBGAPI_BPGRP_UPDATE_PARAM *p =
                    (st_DBGAPI_BPGRP_UPDATE_PARAM *)rq->data_ptr;
                if ( !breakpoints_group_find_by_id ( p->id ) )
                {
                    rq->success = false;
                }
                else
                {
                    uint64_t mask = p->update_mask;
                    bool ok = true;
                    if ( mask & DBGAPI_BPGRP_UM_ENABLED )
                        ok &= breakpoints_group_set_enabled ( p->id, p->enabled );
                    if ( mask & DBGAPI_BPGRP_UM_NAME )
                        ok &= breakpoints_group_set_name ( p->id, p->name );
                    if ( mask & DBGAPI_BPGRP_UM_COLORS )
                        ok &= breakpoints_group_set_colors ( p->id, p->bg_rgb, p->fg_rgb );
                    if ( mask & DBGAPI_BPGRP_UM_PARENT )
                        ok &= breakpoints_group_set_parent ( p->id, p->parent );
                    rq->success = ok;
                };
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_DASM:
            /* Disassembluje N po sobě jdoucích instrukcí od adresy.
             * data_ptr: st_DBGAPI_DASM_PARAM* (= addr + count).
             * result_ptr: st_DBGAPI_DASM_RESULT[count] - caller alokuje
             * pole o velikosti count. Pro každou instrukci vyplní:
             *   .addr (start), .bytes[4], .num_bytes (1-4), .mnemonic.
             * Forward přes z80_dasm() + debugger_dasm_read_cb (= banking
             * aware, no side effects). */
            if (rq->data_ptr && rq->result_ptr)
            {
                st_DBGAPI_DASM_PARAM *p = (st_DBGAPI_DASM_PARAM *)rq->data_ptr;
                st_DBGAPI_DASM_RESULT *out =
                    (st_DBGAPI_DASM_RESULT *)rq->result_ptr;
                uint16_t cur_addr = p->addr;
                for (int i = 0; i < p->count; i++)
                {
                    z80_dasm_inst_t inst;
                    int len = z80_dasm ( &inst, debugger_dasm_read_cb,
                                         NULL, cur_addr );
                    out[i].addr = cur_addr;
                    out[i].num_bytes = len;
                    for (int b = 0; b < 4; b++)
                    {
                        out[i].bytes[b] = inst.bytes[b];
                    };
                    char buf[ sizeof(out[i].mnemonic) ];
                    z80_dasm_to_str ( buf, (int)sizeof(buf), &inst, NULL );
                    /* z80_dasm_to_str zapíše i adresu/bajty - chceme jen
                     * mnemonic. Použijeme inst.mnemonic + operandy přes
                     * to_str se default formátem.
                     * (hypotéza) Pro V1 nám stačí default to_str výstup;
                     * konkrétní layout (s/bez adresy) může caller post-
                     * processovat. Dnešní UI debugger používá vlastní
                     * formátování v dbg_disassembled.cpp. */
                    g_strlcpy ( out[i].mnemonic, buf,
                                sizeof(out[i].mnemonic) );
                    cur_addr = (uint16_t)( cur_addr + len );
                };
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_HISTORY_GET:
            /* Snímá obsah debug history ring bufferu (= 32 posledních
             * dokončených instrukcí, pole g_debugger_history.row[]).
             * result_ptr je buffer pro st_DEBUGGER_HISTORY_ROW pole
             * o velikosti DEBUGGER_HISTORY_LENGTH (= 32 položek). Caller
             * si pole alokuje sám.
             *
             * (hypotéza) Pro V1 vystačíme s g_debugger_history rámcem;
             * pokud bude třeba bohatší metadata (T-states, registry
             * snapshot atd.), přejde V1.5+ na trace-suite cputrack
             * čtení skrz vlastní CMD. */
            if (rq->result_ptr)
            {
                memcpy ( rq->result_ptr,
                         g_debugger_history.row,
                         sizeof(g_debugger_history.row) );
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_GET_CPU_FLAGS:
            /* Doplňkový stav CPU pro CPU window (IFF, IM, HALT, INT/NMI
             * pending, EI delay, Q reg, total/frame cycles, op_tstate).
             * result_ptr: st_DBGAPI_CPU_FLAGS* - caller alokuje. Caller
             * read-only používá fields - update_mask v V0 ignorován
             * (rezerva pro budoucí SET_CPU_FLAGS).
             *
             * Čtení probíhá z g_mzarch_main.cpu v emu vlákně - bez race
             * (jsme v dispatch loopu, žádná instrukce neběží).
             */
            if (rq->result_ptr)
            {
                st_DBGAPI_CPU_FLAGS *out =
                    (st_DBGAPI_CPU_FLAGS *)rq->result_ptr;
                z80_t *cpu = g_mzarch_main.cpu;
                out->iff1         = cpu->iff1 ? 1 : 0;
                out->iff2         = cpu->iff2 ? 1 : 0;
                out->im           = cpu->im;
                out->halted       = cpu->halted ? 1 : 0;
                out->int_pending  = cpu->int_pending ? 1 : 0;
                out->nmi_pending  = cpu->nmi_pending ? 1 : 0;
                out->ei_delay     = cpu->ei_delay ? 1 : 0;
                out->q            = cpu->q;
                out->total_cycles = cpu->total_cycles;
                out->cycles       = cpu->cycles;
                out->op_tstate    = cpu->op_tstate;
                out->update_mask  = 0;
                out->i_reg        = cpu->i;
                out->r_reg        = cpu->r;
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_SET_CPU_FLAGS:
            /* Selektivni zapis CPU stavu (IFF1/IFF2/IM/I/R) dle update_mask.
             *
             * Pro kazdy bit ve flags->update_mask se aplikuje odpovidajici
             * pole z struct. Ostatni fieldy struct se ignoruji (= caller
             * nemusi vyplnovat, jen ten field ktery zapisuje + bit v mask).
             *
             * Bezpecnost: SET behem running stavu je v principu race
             * (modifikuje CPU stav mezi instrukcemi). Handler bezi v emu
             * vlakne v safepointu mezi instrukcemi - atomicita zajistena.
             * UI ridi pause pres dbg_autopause_silent pred submitem.
             *
             * IM validation: 0/1/2 jen, jine hodnoty success=false.
             */
            if (rq->data_ptr)
            {
                st_DBGAPI_CPU_FLAGS *p = (st_DBGAPI_CPU_FLAGS *)rq->data_ptr;
                z80_t *cpu = g_mzarch_main.cpu;
                bool ok = true;

                if (p->update_mask & DBGAPI_CPU_FLAGS_UM_IFF1)
                {
                    cpu->iff1 = p->iff1 ? 1 : 0;
                };
                if (p->update_mask & DBGAPI_CPU_FLAGS_UM_IFF2)
                {
                    cpu->iff2 = p->iff2 ? 1 : 0;
                };
                if (p->update_mask & DBGAPI_CPU_FLAGS_UM_IM)
                {
                    if (p->im <= 2)
                    {
                        cpu->im = p->im;
                    }
                    else
                    {
                        ok = false;
                    };
                };
                if (p->update_mask & DBGAPI_CPU_FLAGS_UM_I)
                {
                    cpu->i = p->i_reg;
                };
                if (p->update_mask & DBGAPI_CPU_FLAGS_UM_R)
                {
                    /* R registr ma vrchni bit (bit 7) nemenne reservovany
                     * po RETI/N (zachovava ho i LD A,R). Zapisujeme jen
                     * dolnich 7 bitu + zachovavame bit 7 z cpu->r,
                     * shodne s logikou v DBGAPI_CMD_SET_REG pro Z80_REG_IR. */
                    cpu->r = (uint8_t)(p->r_reg & 0x7F)
                           | (cpu->r & 0x80);
                };

                rq->success = ok;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_GET_IM2_VECTOR:
            /* IM2 ISR vektor pro CPU window. Vrací stav PIO-Z80 IRQ
             * chainu + dekódovanou ISR table adresu a její dereferenci
             * (= cílovou adresu, kam by Z80 skočil pri IM 2 interruptu).
             *
             * Platformy bez PIO-Z80 (MZ-700) vrací available=0; UI
             * sekci pak skryje. Compile-time gating přes HAVE_PIOZ80
             * (= per-arch makro v mz{700,800,1500}_config.h).
             *
             * PIO-Z80 IRQ chain priorita: port A nad port B - shodné
             * s pioz80_interrupt_ack_im2_cb v hw-generic/pioz80/pioz80.c
             * (iteruje port_id = A; port_id < COUNT, break na první
             * PENDING && ICENA_ENABLED port).
             *
             * Memory dereference (isr_table_addr -> isr_target_addr)
             * čte přes debugger_memory_read_byte (= banking-aware bez
             * side effects), abychom respektovali ROM/RAM mapping.
             */
            if (rq->result_ptr)
            {
                st_DBGAPI_IM2_VECTOR *out =
                    (st_DBGAPI_IM2_VECTOR *)rq->result_ptr;
                memset(out, 0, sizeof(*out));
#if HAVE_PIOZ80
                z80_t *cpu = g_mzarch_main.cpu;
                out->available  = 1;
                out->im         = cpu->im;
                out->i_register = cpu->i;

                /* Detekce pending IRQ + výběr zdroje (port A > port B).
                 * Pokud žádný port není v PENDING && ICENA_ENABLED, vrátíme
                 * vector_byte = 0 (= pioz80_interrupt_ack_im2_cb chování
                 * mimo INTERRUPT_PENDING stav). */
                out->pio_irq_pending = 0;
                out->pio_source      = 0;
                out->vector_byte     = 0;
                if (g_pioz80.interrupt == PIOZ80_INTERRUPT_PENDING)
                {
                    for (int pid = PIOZ80_PORT_A; pid < PIOZ80_PORT_COUNT; pid++)
                    {
                        st_PIOZ80_PORT *port = &g_pioz80.port[pid];
                        if (port->port_int == PIOZ80_PORT_INT_PENDING
                            && port->icena == PIOZ80_ICENA_ENABLED)
                        {
                            out->pio_irq_pending = 1;
                            out->pio_source      = (uint8_t)pid;
                            out->vector_byte     = port->interrupt_vector;
                            break;
                        };
                    };
                };

                out->isr_table_addr  =
                    (uint16_t)(((uint16_t)out->i_register << 8) | out->vector_byte);
                /* Dereferenci provádíme bez ohledu na pending stav - když
                 * není pending, vector_byte=0 a ukazujeme table[0] (=
                 * adresa, kterou by Z80 nedostal, ale UI to vyznačí). */
                uint8_t lo = debugger_memory_read_byte(out->isr_table_addr);
                uint8_t hi = debugger_memory_read_byte((uint16_t)(out->isr_table_addr + 1));
                out->isr_target_addr = (uint16_t)(lo | (hi << 8));
#else
                /* MZ-700: PIO-Z80 nedostupné, IM 2 vector by se musel řešit
                 * jiným zdrojem dat (RST 38h v IM 1 / nepoužívá se). */
                out->available = 0;
#endif
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_GET_LAST_INSTR:
            /* Vrací nejnovější záznam z g_debugger_history ringu (=
             * poslední dokončená instrukce). position field v ringu
             * ukazuje na slot posledního M1 startu - hodnota není
             * volně dostupná přes HISTORY_GET (= jen pole row[]).
             *
             * Délka instrukce se dopočítává přes z80_dasm_op nad
             * uloženými bajty (= row.byte[0..3]). Pokud history neaktivní
             * nebo prázdná (= addr+byte[0] obojí 0 ve slotu), vrátí valid=0. */
            if (rq->result_ptr)
            {
                st_DBGAPI_LAST_INSTR *out =
                    (st_DBGAPI_LAST_INSTR *)rq->result_ptr;
                memset(out, 0, sizeof(*out));

                unsigned pos = debugger_history_position(g_debugger_history.position);
                st_DEBUGGER_HISTORY_ROW *row = &g_debugger_history.row[pos];

                /* Defenzivni "neni co ukazat": vsechny bajty nuly + addr 0.
                 * Po resetu je ring vynulovany a position=0 - byte[0]=0 ale
                 * to muze byt validni NOP (00h) v cervence; po prvni
                 * instrukci uz position!=0 nebo byte[0]!=0. */
                bool empty = (g_debugger_history.position == 0
                              && row->addr == 0
                              && row->byte[0] == 0
                              && row->byte[1] == 0
                              && row->byte[2] == 0
                              && row->byte[3] == 0);
                if (empty)
                {
                    out->valid = 0;
                    rq->success = true;
                    break;
                };

                out->valid = 1;
                out->addr  = row->addr;
                for (int b = 0; b < 4; b++) {
                    out->bytes[b] = row->byte[b];
                };

                /* Dopocet delky z80_dasm() nad row.byte[] - read_fn cte
                 * z bufferu (offset = addr - row->addr), respektuje 4-byte
                 * limit historie. */
                z80_dasm_inst_t inst;
                int len = z80_dasm ( &inst,
                                     dbgapi_last_instr_read_cb,
                                     row, /* user_data = row pointer */
                                     row->addr );
                if (len < 1) len = 1;
                if (len > 4) len = 4;
                out->length = (uint8_t)len;
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_GET_RASTER_POS:
            /* Pozice rastru + Z80 cycle countery pro CPU window
             * "Cycles & raster" sekci. Čteme z g_gdg (= GDG state) +
             * g_mzarch_main.cpu->total_cycles/cycles.
             *
             * scanline = g_gdg.beam_row (= aktuální raster row, sjednoceno
             * s io_history záznamem).
             * column_pixel = VIDEO_GET_SCREEN_COL(g_gdg.total_elapsed.ticks)
             * - poznámka: ticks je počet GDG ticks od začátku snímku,
             * VIDEO_GET_SCREEN_COL = ticks % VIDEO_SCREEN_WIDTH.
             */
            if (rq->result_ptr)
            {
                st_DBGAPI_RASTER_POS *out =
                    (st_DBGAPI_RASTER_POS *)rq->result_ptr;
                z80_t *cpu = g_mzarch_main.cpu;
                out->frame_number = (uint32_t)g_gdg.total_elapsed.screens;
                out->scanline     = (uint16_t)g_gdg.beam_row;
                out->column_pixel = (uint16_t)VIDEO_GET_SCREEN_COL(g_gdg.total_elapsed.ticks);
                out->total_cycles = cpu->total_cycles;
                out->frame_cycles = cpu->cycles;
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_GET_CPU_PANEL_BATCH:
            /* Agregovaný snapshot pro CPU panel v jediném round-tripu.
             * Místo 5 separátních sync calls (= 5 čekání na emu safepoint)
             * UI submituje 1 batch a dostane všechna potřebná data v jedné
             * dispatch iteraci. Per-section gating přes which-mask
             * (data_ptr): UI ptá jen na sekce, které jsou expanded.
             *
             * Regs + flags se naplňují vždy (= core panel, levné čtení).
             * Volitelné sekce (IM2/raster/last_instr) jen pokud caller
             * nastavil odpovídající WANT_* bit ve which-mask.
             *
             * Implementace: jednoduchý copy-paste z původních samostatných
             * handlerů aby zůstaly identické sémantiky (per-arch HAVE_PIOZ80
             * gating, banking-aware mem read, history empty detekce).
             */
            if (rq->result_ptr)
            {
                st_DBGAPI_CPU_PANEL_BATCH *out =
                    (st_DBGAPI_CPU_PANEL_BATCH *)rq->result_ptr;
                uint32_t which = 0;
                if (rq->data_ptr)
                    which = *((uint32_t *)rq->data_ptr);

                /* Hlavní část - vyplnit core fieldy a vynulovat optional
                 * valid flagy pro případ, že caller nepředal which-mask. */
                memset(out, 0, sizeof(*out));
                out->which = which;

                z80_t *cpu = g_mzarch_main.cpu;

                /* === Regs (vždy) === */
                for (int i = 0; i < DBGAPI_REG_COUNT; i++)
                {
                    out->regs[i] = z80_get_reg ( cpu, (z80_reg_t)i );
                };
                out->regs_valid = 1;

                /* === Flags (vždy) === */
                out->flags.iff1         = cpu->iff1 ? 1 : 0;
                out->flags.iff2         = cpu->iff2 ? 1 : 0;
                out->flags.im           = cpu->im;
                out->flags.halted       = cpu->halted ? 1 : 0;
                out->flags.int_pending  = cpu->int_pending ? 1 : 0;
                out->flags.nmi_pending  = cpu->nmi_pending ? 1 : 0;
                out->flags.ei_delay     = cpu->ei_delay ? 1 : 0;
                out->flags.q            = cpu->q;
                out->flags.total_cycles = cpu->total_cycles;
                out->flags.cycles       = cpu->cycles;
                out->flags.op_tstate    = cpu->op_tstate;
                out->flags.update_mask  = 0;
                out->flags.i_reg        = cpu->i;
                out->flags.r_reg        = cpu->r;
                out->flags_valid = 1;

                /* === V3.1 core fieldy (vzdy plnene): frame_number a
                 * user_cycle_origin. UI z toho odvozuje Frame cyc
                 * (= total_cycles - snapshot pri zmene frame_number) a
                 * User cyc (= total_cycles - user_cycle_origin) i pokud
                 * "Cycles & raster" sekce je collapsed. */
                out->frame_number      = (uint32_t)g_gdg.total_elapsed.screens;
                out->user_cycle_origin = g_debugger.user_cycle_origin;

                /* === V3.3 core: PIO-Z80 interrupt vectors + ISR targets.
                 * Vždy plněné při HAVE_PIOZ80 (MZ-800, MZ-1500), aby UI
                 * sekce VECA/ISRA + VECB/ISRB dostala data v každém ticku.
                 * Pro MZ-700 (HAVE_PIOZ80 == 0) má_pioz80 = 0; UI sekce
                 * se na MZ-700 nezobrazuje. */
#if HAVE_PIOZ80
                {
                    out->has_pioz80    = 1;
                    uint8_t va = g_pioz80.port[PIOZ80_PORT_A].interrupt_vector;
                    uint8_t vb = g_pioz80.port[PIOZ80_PORT_B].interrupt_vector;
                    out->pio_int_vec_a = va;
                    out->pio_int_vec_b = vb;
                    uint16_t vec_a = (uint16_t)(((uint16_t)cpu->i << 8) | (uint8_t)(va & 0xFE));
                    uint16_t vec_b = (uint16_t)(((uint16_t)cpu->i << 8) | (uint8_t)(vb & 0xFE));
                    out->veca = vec_a;
                    out->vecb = vec_b;
                    {
                        uint8_t lo_a = debugger_memory_read_byte(vec_a);
                        uint8_t hi_a = debugger_memory_read_byte((uint16_t)(vec_a + 1));
                        out->isra = (uint16_t)(lo_a | (hi_a << 8));
                    };
                    {
                        uint8_t lo_b = debugger_memory_read_byte(vec_b);
                        uint8_t hi_b = debugger_memory_read_byte((uint16_t)(vec_b + 1));
                        out->isrb = (uint16_t)(lo_b | (hi_b << 8));
                    };
                };
#else
                out->has_pioz80 = 0;
#endif

                /* === IM2 ISR vector (volitelne) === */
                if (which & DBGAPI_CPU_PANEL_WANT_IM2)
                {
                    st_DBGAPI_IM2_VECTOR *im2 = &out->im2;
#if HAVE_PIOZ80
                    im2->available  = 1;
                    im2->im         = cpu->im;
                    im2->i_register = cpu->i;
                    im2->pio_irq_pending = 0;
                    im2->pio_source      = 0;
                    im2->vector_byte     = 0;
                    if (g_pioz80.interrupt == PIOZ80_INTERRUPT_PENDING)
                    {
                        for (int pid = PIOZ80_PORT_A; pid < PIOZ80_PORT_COUNT; pid++)
                        {
                            st_PIOZ80_PORT *port = &g_pioz80.port[pid];
                            if (port->port_int == PIOZ80_PORT_INT_PENDING
                                && port->icena == PIOZ80_ICENA_ENABLED)
                            {
                                im2->pio_irq_pending = 1;
                                im2->pio_source      = (uint8_t)pid;
                                im2->vector_byte     = port->interrupt_vector;
                                break;
                            };
                        };
                    };
                    im2->isr_table_addr =
                        (uint16_t)(((uint16_t)im2->i_register << 8) | im2->vector_byte);
                    {
                        uint8_t lo = debugger_memory_read_byte(im2->isr_table_addr);
                        uint8_t hi = debugger_memory_read_byte((uint16_t)(im2->isr_table_addr + 1));
                        im2->isr_target_addr = (uint16_t)(lo | (hi << 8));
                    };
#else
                    im2->available = 0;
#endif
                    out->im2_valid = 1;
                };

                /* === Raster pos + cycles (volitelne) === */
                if (which & DBGAPI_CPU_PANEL_WANT_RASTER)
                {
                    st_DBGAPI_RASTER_POS *r = &out->raster;
                    r->frame_number = (uint32_t)g_gdg.total_elapsed.screens;
                    r->scanline     = (uint16_t)g_gdg.beam_row;
                    r->column_pixel = (uint16_t)VIDEO_GET_SCREEN_COL(g_gdg.total_elapsed.ticks);
                    r->total_cycles = cpu->total_cycles;
                    r->frame_cycles = cpu->cycles;
                    out->raster_valid = 1;
                };

                /* === Last instruction (volitelne) === */
                if (which & DBGAPI_CPU_PANEL_WANT_LAST_INSTR)
                {
                    st_DBGAPI_LAST_INSTR *li = &out->last_instr;
                    unsigned pos = debugger_history_position(g_debugger_history.position);
                    st_DEBUGGER_HISTORY_ROW *row = &g_debugger_history.row[pos];
                    bool empty = (g_debugger_history.position == 0
                                  && row->addr == 0
                                  && row->byte[0] == 0
                                  && row->byte[1] == 0
                                  && row->byte[2] == 0
                                  && row->byte[3] == 0);
                    if (empty)
                    {
                        li->valid = 0;
                    }
                    else
                    {
                        li->valid = 1;
                        li->addr  = row->addr;
                        for (int b = 0; b < 4; b++)
                            li->bytes[b] = row->byte[b];
                        z80_dasm_inst_t inst;
                        int len = z80_dasm ( &inst,
                                             dbgapi_last_instr_read_cb,
                                             row,
                                             row->addr );
                        if (len < 1) len = 1;
                        if (len > 4) len = 4;
                        li->length = (uint8_t)len;
                    };
                    out->last_instr_valid = 1;
                };

                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_SET_PIOZ80_INTERRUPT_VECTOR:
            /* V3.3: zápis g_pioz80.port[id].interrupt_vector. Handler
             * maskuje bit 0 (= IVW spec - vždy 0 v Z80 PIO).
             *
             * Validace: port_id in {0, 1}, jinak success = false.
             * Na MZ-700 (HAVE_PIOZ80 == 0) success = false - PIO-Z80
             * v této architektuře neexistuje (g_pioz80 by ani nebylo
             * linkovatelné).
             *
             * Bezpečnost: handler běží v safepointu emu vlákna mezi
             * instrukcemi, atomicita zápisu uint8_t je triviální. UI
             * řídí pause přes dbg_autopause_silent před submitem (= edit
             * není v running, ale pro jistotu žádný extra lock potřeba). */
#if HAVE_PIOZ80
            if (rq->data_ptr)
            {
                st_DBGAPI_PIOZ80_VEC_PARAM *p =
                    (st_DBGAPI_PIOZ80_VEC_PARAM *)rq->data_ptr;
                if (p->port_id == PIOZ80_PORT_A
                    || p->port_id == PIOZ80_PORT_B)
                {
                    g_pioz80.port[p->port_id].interrupt_vector =
                        (uint8_t)(p->vector_byte & 0xFE);
                    rq->success = true;
                }
                else
                {
                    rq->success = false;
                };
            }
            else
            {
                rq->success = false;
            };
#else
            rq->success = false;
#endif
            break;

        case DBGAPI_CMD_MEM_WRITE_CHECKED:
            /* V3.3: zápis bloku do paměti s region check. Pro každou
             * adresu z [addr..addr+length-1] dotazujeme memmap_query
             * (= druh regionu 4 kB stránky podle aktuálního banking).
             * Pokud region patří mezi ne-zapisovatelné (ROM, CG-ROM,
             * VRAM v MZ-800 native módu, prohibited, unmapped, mapped
             * ports), žádný bajt se nezapíše a handler vrátí
             * success = 0 + first_failed_addr + first_failed_kind.
             *
             * Filozofie "all or nothing": region check je sekvenční,
             * první nezapisovatelná adresa = abort. UI typicky posílá
             * 2 bajty (ISR target little-endian) - pokud addr je v ROM,
             * tak je tam i addr+1 a check rozhoduje na první iteraci.
             *
             * Note: VRAM v MZ-800 native módu (VRAM_I, VRAM_II) je
             * technicky zapisovatelná z CPU strany (= memory_write_byte
             * by neselhal), ale Michalovo zadání ji explicit zakazuje
             * pro ISR target (= ISR vektor v plánové VRAM je nesmyslný,
             * VRAM se přepisuje hrami a ISR by se rozsypala). VRAM_TEXT
             * (MZ-700 / MZ-800 v 700 módu) je obyčejná RAM textového
             * režimu, ta povolena.
             *
             * Banking-aware write provádíme přes debugger_memory_write_byte
             * (= shoduje se s CMD_MEM_WRITE). */
            if (rq->data_ptr)
            {
                st_DBGAPI_MEM_WRITE_CHECKED_PARAM *p =
                    (st_DBGAPI_MEM_WRITE_CHECKED_PARAM *)rq->data_ptr;
                if (!p->data || p->length == 0)
                {
                    p->success = 0;
                    p->first_failed_addr = p->addr;
                    p->first_failed_kind = (uint8_t)MEMMAP_KIND_UNMAPPED;
                    rq->success = false;
                    break;
                };

                /* Fáze 1: region check pro každou adresu. */
                bool all_ok = true;
                for (uint32_t i = 0; i < p->length; i++)
                {
                    uint16_t a = (uint16_t)(p->addr + i);
                    en_MEMMAP_REGION_KIND kind = memmap_query((uint8_t)(a >> 12));
                    bool writable;
                    switch (kind)
                    {
                        case MEMMAP_KIND_RAM:
                        case MEMMAP_KIND_VRAM_TEXT:
                        case MEMMAP_KIND_CGRAM:
                        case MEMMAP_KIND_PCG_1:
                        case MEMMAP_KIND_PCG_2:
                        case MEMMAP_KIND_PCG_3:
                            writable = true;
                            break;
                        case MEMMAP_KIND_ROM_LOW:
                        case MEMMAP_KIND_ROM_HIGH:
                        case MEMMAP_KIND_CGROM:
                        case MEMMAP_KIND_VRAM_I:
                        case MEMMAP_KIND_VRAM_II:
                        case MEMMAP_KIND_MAPPED_PORTS:
                        case MEMMAP_KIND_PROHIBITED:
                        case MEMMAP_KIND_UNMAPPED:
                        default:
                            writable = false;
                            break;
                    };
                    if (!writable)
                    {
                        p->success = 0;
                        p->first_failed_addr = a;
                        p->first_failed_kind = (uint8_t)kind;
                        all_ok = false;
                        break;
                    };
                };

                if (!all_ok)
                {
                    /* Žádný bajt nezapsán - all-or-nothing semantika. */
                    rq->success = true; /* command sám prošel, jen write zamítnut */
                    break;
                };

                /* Fáze 2: vlastní zápis (region check prošel). */
                for (uint32_t i = 0; i < p->length; i++)
                {
                    debugger_memory_write_byte(
                        (uint16_t)(p->addr + i), p->data[i]);
                };
                p->success = 1;
                p->first_failed_addr = 0;
                p->first_failed_kind = 0;
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_SET_USER_CYCLE_ORIGIN:
            /* V3.1: nastavi g_debugger.user_cycle_origin na zadanou hodnotu.
             * UI typicky posila bud aktualni total_cycles (= "Reset" knoflik
             * -> User cyc display nasledne = 0) nebo total_cycles - new_value
             * pri user editu zobrazene hodnoty.
             *
             * data_ptr ukazuje na uint32_t (absolutni snapshot). Atomicita
             * 32-bit store na bezne 32+ bit platforme + emu vlakno v
             * safepointu mezi instrukcemi = bez additional locku.
             */
            if (rq->data_ptr)
            {
                uint32_t new_origin = *((uint32_t *)rq->data_ptr);
                g_debugger.user_cycle_origin = new_origin;
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_STACK_DUMP:
            /* Stack monitor: hex dump paměti kolem SP.
             *
             * Dva režimy okénka (viz st_DBGAPI_STACK_DUMP_PARAM):
             *  - `lines_above > 0` (SP-anchored): handler spočítá
             *    `addr = sp + lines_above * 2` a zapíše ji zpět do
             *    paramu. Tím je okno vždy konzistentní s `sp_now`
             *    z téhož ticku (= odstraňuje 1-tick lag který by vznikl,
             *    kdyby UI počítala base z předchozího `sp_now`).
             *  - `lines_above == 0` (absolute): handler použije `addr`
             *    tak jak ji UI předala (legacy chování pro fixní inspekci).
             *
             * Handler naplní buffer banking-aware čtením (= shoda s
             * CMD_MEM_READ patternem) a navíc do `sp_now` zapíše aktuální
             * SP a do `sp_odd` flag liché hodnoty SP.
             *
             * Pozn.: čtení via debugger_memory_read_byte respektuje aktuální
             * banking (ROM / RAM / VRAM / CGROM podle portů $E0-$E4 na
             * MZ-800). Bez side effects pro VRAM / IORQ - debugger probe
             * se nesmí promítat do emu state. */
            if (rq->data_ptr)
            {
                st_DBGAPI_STACK_DUMP_PARAM *p =
                    (st_DBGAPI_STACK_DUMP_PARAM *)rq->data_ptr;
                if (p->buf && p->len > 0)
                {
                    uint16_t sp = g_mzarch_main.cpu->sp;
                    if (p->lines_above > 0)
                    {
                        /* SP-anchored mode: handler spočítá base ze
                         * SP a buf naplní DESC (= buf[0] je bajt na
                         * adrese base, buf[i] na adrese base-i, ...).
                         * Render v UI bere buf[i*step] jako řádek i,
                         * který odpovídá adrese base - i*step, takže
                         * naplnění musí jít stejným směrem.
                         *
                         * Step (1 nebo 2) se odvozuje z parity SP:
                         * lichý SP = byte-oriented fallback (step=1),
                         * sudý SP = word-oriented default (step=2).
                         * Render v UI počítá step stejnou logikou, takže
                         * obě strany jsou synchronní bez state mismatch.
                         * Bez tohohle: pro lichý SP se base = SP +
                         * lines_above*2 posune dvakrát dál než render
                         * očekává (= SP marker mimo zobrazené okno). */
                        uint32_t step = (sp & 0x01u) ? 1u : 2u;
                        uint16_t base = (uint16_t)(
                            sp + (uint32_t)p->lines_above * step );
                        p->addr = base;
                        for (uint32_t i = 0; i < p->len; i++)
                        {
                            p->buf[i] = debugger_memory_read_byte (
                                (uint16_t)(base - i) );
                        };
                    }
                    else
                    {
                        /* Absolute mode (legacy): buf naplněn ASC od
                         * p->addr nahoru. Vhodné pro inspekci konkrétní
                         * adresy bez vazby na SP. */
                        for (uint32_t i = 0; i < p->len; i++)
                        {
                            p->buf[i] = debugger_memory_read_byte (
                                (uint16_t)(p->addr + i) );
                        };
                    };
                    p->sp_now = sp;
                    p->sp_odd = (uint8_t)(sp & 0x01u);

                    /* V3: disasm-back heuristika pro Decode sloupec.
                     * Vyplňuje se jen pokud caller dodal pole decode_buf
                     * a SP je sudý (word-mode). Pro lichý SP (byte-mode)
                     * není word kandidát definován - decode přeskočíme
                     * a UI ho ignoruje.
                     *
                     * Pro každý řádek tabulky (index `i` v rozsahu
                     * 0..decode_count) odpovídá adresa řádku
                     * `addr_i = p->addr - i*2` (DESC, word step). Word
                     * kandidát na této pozici je LE-word:
                     *   W = mem[addr_i] | (mem[addr_i+1] << 8)
                     * Sharp Z80 stack ukládá návratovou adresu LE
                     * (LSB na nižší adrese, MSB na vyšší).
                     *
                     * Detekce:
                     *  - mem[W-3] == 0xCD          -> CALL nn (3 bajty,
                     *      target = mem[W-2]|mem[W-1]<<8)
                     *  - mem[W-3] in {C4,CC,D4,DC,E4,EC,F4,FC}
                     *                              -> CALL cc,nn (target = idem)
                     *  - mem[W-1] in {C7,CF,D7,DF,E7,EF,F7,FF}
                     *                              -> RST n (target = opcode & 0x38)
                     * Jinak NONE.
                     *
                     * Banking-aware: použito debugger_memory_read_byte,
                     * respektuje aktuální mapping. Stejně jako CMD_MEM_READ
                     * pattern, bez side effects.
                     *
                     * Heuristika je nezávislá na lines_above mode - pracuje
                     * s naplněnou hodnotou p->addr (base). V absolute mode
                     * (lines_above == 0) je word kandidát počítaný stejně,
                     * jen base = původní p->addr před handlerem. */
                    if (p->decode_buf && p->decode_count > 0
                        && (sp & 0x01u) == 0)
                    {
                        uint16_t base = p->addr;
                        uint16_t max_lines = p->decode_count;
                        /* Omezit dle len - jen tolik řádků kolik se vejde
                         * do word-mode tabulky. */
                        uint16_t fit = (uint16_t)(p->len / 2u);
                        if (max_lines > fit) max_lines = fit;

                        for (uint16_t i = 0; i < max_lines; i++)
                        {
                            uint16_t addr_i = (uint16_t)(base - (uint32_t)i * 2u);
                            uint8_t lo = debugger_memory_read_byte(addr_i);
                            uint8_t hi = debugger_memory_read_byte(
                                (uint16_t)(addr_i + 1u));
                            uint16_t w = (uint16_t)(((uint16_t)hi << 8) | lo);

                            st_DBGAPI_STACK_DECODE_INFO *d = &p->decode_buf[i];
                            d->type   = DBGAPI_STACK_DECODE_NONE;
                            d->opcode = 0;
                            d->target = 0;

                            /* CALL family: opcode na W-3. */
                            uint8_t op3 = debugger_memory_read_byte(
                                (uint16_t)(w - 3u));
                            if (op3 == 0xCDu)
                            {
                                /* CALL nn - target ze dvou bajtů za opcode. */
                                uint8_t tlo = debugger_memory_read_byte(
                                    (uint16_t)(w - 2u));
                                uint8_t thi = debugger_memory_read_byte(
                                    (uint16_t)(w - 1u));
                                d->type   = DBGAPI_STACK_DECODE_CALL;
                                d->opcode = op3;
                                d->target = (uint16_t)(
                                    ((uint16_t)thi << 8) | tlo);
                            }
                            else if ((op3 & 0xC7u) == 0xC4u)
                            {
                                /* CALL cc,nn - opcody 0xC4/CC/D4/DC/E4/EC/F4/FC.
                                 * Bity 7..6 = 11, bity 2..0 = 100 -> mask 0xC7
                                 * porovnán s 0xC4. */
                                uint8_t tlo = debugger_memory_read_byte(
                                    (uint16_t)(w - 2u));
                                uint8_t thi = debugger_memory_read_byte(
                                    (uint16_t)(w - 1u));
                                d->type   = DBGAPI_STACK_DECODE_CALL_CC;
                                d->opcode = op3;
                                d->target = (uint16_t)(
                                    ((uint16_t)thi << 8) | tlo);
                            }
                            else
                            {
                                /* RST family: opcode na W-1. RST n má
                                 * masku 0xC7 == 0xC7 (= 11 nnn 111),
                                 * target = nnn * 8 (= opcode & 0x38). */
                                uint8_t op1 = debugger_memory_read_byte(
                                    (uint16_t)(w - 1u));
                                if ((op1 & 0xC7u) == 0xC7u)
                                {
                                    d->type   = DBGAPI_STACK_DECODE_RST;
                                    d->opcode = op1;
                                    d->target = (uint16_t)(op1 & 0x38u);
                                };
                            };
                        };

                        /* Zbytek decode_buf (i >= max_lines) zůstane
                         * nedotčen - caller je odpovědný za inicializaci
                         * nebo si pamatuje hranici z decode_count. Tady
                         * pro defenzivu nulujeme. */
                        for (uint16_t i = max_lines; i < p->decode_count; i++)
                        {
                            p->decode_buf[i].type   = DBGAPI_STACK_DECODE_NONE;
                            p->decode_buf[i].opcode = 0;
                            p->decode_buf[i].target = 0;
                        };
                    }
                    else if (p->decode_buf && p->decode_count > 0)
                    {
                        /* Lichý SP (byte-mode) - decode nedává smysl,
                         * nulujeme celé pole. */
                        for (uint16_t i = 0; i < p->decode_count; i++)
                        {
                            p->decode_buf[i].type   = DBGAPI_STACK_DECODE_NONE;
                            p->decode_buf[i].opcode = 0;
                            p->decode_buf[i].target = 0;
                        };
                    };

                    rq->success = true;
                }
                else
                {
                    rq->success = false;
                };
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_STACK_REGIONS_LIST:
            /* Stack monitor V1: snapshot definovaných stack regionů.
             * UI alokuje strukturu s polem regions[DBGAPI_STACK_REGIONS_MAX]
             * a handler ji naplní z g_stack_regions[]. Per-region flag
             * current_sp_in_region se počítá proti aktuálnímu SP. */
            if (rq->data_ptr)
            {
                st_DBGAPI_STACK_REGIONS_LIST_PARAM *p =
                    (st_DBGAPI_STACK_REGIONS_LIST_PARAM *)rq->data_ptr;
                uint16_t sp = g_mzarch_main.cpu->sp;
                p->sp_now = sp;
                int n = g_stack_regions_count;
                if (n > DBGAPI_STACK_REGIONS_MAX) n = DBGAPI_STACK_REGIONS_MAX;
                p->count = n;
                for (int i = 0; i < n; i++)
                {
                    st_STACK_REGION *src = &g_stack_regions[i];
                    st_DBGAPI_STACK_REGION_INFO *dst = &p->regions[i];
                    /* Kopie jména s ochranou proti přetečení. */
                    memset(dst->name, 0, sizeof(dst->name));
                    strncpy(dst->name, src->name, sizeof(dst->name) - 1);
                    dst->base       = src->base;
                    dst->limit      = src->limit;
                    dst->watermark  = src->watermark;
                    dst->push_count = src->push_count;
                    dst->pop_count  = src->pop_count;
                    dst->current_sp_in_region  =
                        (sp >= src->limit && sp <= src->base) ? 1u : 0u;
                };
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_STACK_REGIONS_ADD:
            /* Stack monitor V1: přidat nový region. Validace v
             * stack_regions_add (= base > limit, name regex, full check).
             * Handler vrací nový index nebo -1 přes p->result_index +
             * rq->success. */
            if (rq->data_ptr)
            {
                st_DBGAPI_STACK_REGIONS_ADD_PARAM *p =
                    (st_DBGAPI_STACK_REGIONS_ADD_PARAM *)rq->data_ptr;
                /* Zajisti '\0' termination - caller mohl předat
                 * bez explicitního konce. */
                p->name[sizeof(p->name) - 1] = '\0';
                int idx = stack_regions_add(p->name, p->base, p->limit);
                p->result_index = idx;
                rq->success = (idx >= 0);
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_STACK_REGIONS_REMOVE:
            /* Stack monitor V1: odebrat region na zadaném indexu. */
            if (rq->data_ptr)
            {
                st_DBGAPI_STACK_REGIONS_REMOVE_PARAM *p =
                    (st_DBGAPI_STACK_REGIONS_REMOVE_PARAM *)rq->data_ptr;
                rq->success = stack_regions_remove(p->index);
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_STACK_REGIONS_RESET_WATERMARK:
            /* Stack monitor V1: reset watermark + counters jednoho regionu. */
            if (rq->data_ptr)
            {
                st_DBGAPI_STACK_REGIONS_REMOVE_PARAM *p =
                    (st_DBGAPI_STACK_REGIONS_REMOVE_PARAM *)rq->data_ptr;
                rq->success = stack_regions_reset_watermark(p->index);
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_STACK_REGIONS_EDIT:
            /* Stack monitor V7: edit existujícího regionu. Validace +
             * overlap detekce v stack_regions_edit. Při úspěchu resetuje
             * watermark + counters (= staré stats nesedí na nový rozsah). */
            if (rq->data_ptr)
            {
                st_DBGAPI_STACK_REGIONS_EDIT_PARAM *p =
                    (st_DBGAPI_STACK_REGIONS_EDIT_PARAM *)rq->data_ptr;
                /* Zajisti '\0' termination - caller mohl předat neukončený
                 * string. */
                p->name[sizeof(p->name) - 1] = '\0';
                bool ok = stack_regions_edit((int)p->idx, p->name,
                                              p->base, p->limit);
                p->success  = ok;
                rq->success = ok;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_STACK_HISTORY_ENABLE:
            /* Stack monitor V2: zapnout/vypnout SP history recording.
             * Vypnutí navíc vyprázdní ring buffer (= další zapnutí začne
             * s čistým stavem). Aktivační flag se promítne do hot-path
             * call site v mzarch.c (= zero overhead kdy default OFF). */
            if (rq->data_ptr)
            {
                st_DBGAPI_STACK_HISTORY_ENABLE_PARAM *p =
                    (st_DBGAPI_STACK_HISTORY_ENABLE_PARAM *)rq->data_ptr;
                stack_history_set_active(p->enable != 0);
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_STACK_HISTORY_GET:
            /* Stack monitor V2: bulk snapshot SP history ring bufferu.
             *
             * Caller alokuje pole samples a předá pres `samples` pointer.
             * Handler ho v safepointu naplní vzorky oldest-first
             * (= samples[0] = nejstarší v okénku, samples[count-1] = nejnovější).
             * Velikost okénka = min(max_count, current_count).
             *
             * Slope se počítá v stejném safepointu (= žádný race vs.
             * záznam z hot-path).
             *
             * `samples` pointer drží caller (= UI alokuje); handler ho
             * jen čte/zapisuje, neuvolňuje. */
            if (rq->data_ptr)
            {
                st_DBGAPI_STACK_HISTORY_GET_PARAM *p =
                    (st_DBGAPI_STACK_HISTORY_GET_PARAM *)rq->data_ptr;
                if (p->samples && p->max_count > 0)
                {
                    /* Kopie přímo do caller bufferu - emu vzorek a
                     * dbgapi vzorek mají stejný layout (oba 8 B,
                     * cycles + sp + pad). Bezpečné typu-pun.
                     *
                     * Pozn.: stack_history_copy_recent očekává
                     * st_STACK_HISTORY_SAMPLE*, který je v hot-path
                     * (emu) headeru. Caller používá
                     * st_DBGAPI_STACK_HISTORY_SAMPLE = identický layout.
                     * Cast je triviální, není UB protože struct má
                     * standard-layout POD typ. */
                    p->count = stack_history_copy_recent(
                        (st_STACK_HISTORY_SAMPLE *)p->samples,
                        p->max_count);
                }
                else
                {
                    p->count = 0;
                };
                p->active = g_stack_history_active ? 1u : 0u;
                /* Slope: pokud caller nezadal window (= 0), použij 256
                 * jako rozumný default. */
                uint32_t win = p->slope_window;
                if (win == 0) win = 256;
                p->slope = stack_history_compute_slope(win);
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_STACK_HISTORY_RESET:
            /* Stack monitor V2: vyprázdnit ring buffer (recording flag
             * zachován). Vhodné pro UI "Reset history" tlačítko. */
            stack_history_reset();
            rq->success = true;
            break;

        /* ============================================================
         * Event Viewer (mutant event-viewer, Vlna 1 Commit 1)
         * ============================================================ */

        case DBGAPI_CMD_EVENTLOG_START:
            /* Spustit zápis do in-memory ringu. Vrací success podle
             * stavu ringu - pokud není alokovaný, eventlog_start vrátí
             * -1 (= chyba propagovaná do rq->success). */
            rq->success = ( eventlog_start ( ) == 0 );
            break;

        case DBGAPI_CMD_EVENTLOG_STOP:
            eventlog_stop ( );
            rq->success = true;
            break;

        case DBGAPI_CMD_EVENTLOG_CLEAR:
            eventlog_clear ( );
            rq->success = true;
            break;

        case DBGAPI_CMD_EVENTLOG_SET_CAPACITY:
            /* Resize ringu (zahodí předchozí data). Handler clampuje
             * hodnotu do [MIN..MAX] a vrátí výslednou velikost. */
            if ( rq->data_ptr )
            {
                st_DBGAPI_EVENTLOG_CAPACITY_PARAM *p =
                    (st_DBGAPI_EVENTLOG_CAPACITY_PARAM *) rq->data_ptr;
                eventlog_set_capacity ( (size_t) p->capacity );
                p->capacity_after = (uint32_t) g_eventlog.capacity;
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_EVENTLOG_SET_MASK:
            /* Atomický přepis 64-bit kategorie masky. UI to volá při
             * každé změně checkboxu (= cheap operace, žádný overhead). */
            if ( rq->data_ptr )
            {
                st_DBGAPI_EVENTLOG_MASK_PARAM *p =
                    (st_DBGAPI_EVENTLOG_MASK_PARAM *) rq->data_ptr;
                g_eventlog_active_mask = p->mask;
                g_eventlog_config.categories_mask = p->mask;
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_EVENTLOG_GET_EVENT:
            /* Snapshot jednoho eventu z ringu na logickém indexu.
             * Pokud idx mimo count, found = 0 a obsah event polí
             * není definovaný (caller nesmí číst). */
            if ( rq->data_ptr )
            {
                st_DBGAPI_EVENTLOG_GET_EVENT_PARAM *p =
                    (st_DBGAPI_EVENTLOG_GET_EVENT_PARAM *) rq->data_ptr;
                const st_EVENTLOG_EVENT *e = eventlog_get_event ( (size_t) p->idx );
                if ( e != NULL )
                {
                    p->found           = 1u;
                    p->pxclk_total     = e->pxclk_total;
                    p->screens_total   = e->screens_total;
                    p->pxclk_in_screen = e->pxclk_in_screen;
                    p->category        = e->category;
                    p->subtype         = e->subtype;
                    p->pc              = e->pc;
                    p->payload         = e->payload;
                }
                else
                {
                    p->found = 0u;
                };
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_GET_CALLSTACK:
            /* Callstack snapshot: alokuje pole entries přes callstack_snapshot_get
             * (callee-allocated, g_malloc). Caller (UI) MUSÍ pole uvolnit přes
             * callstack_snapshot_free po dokončení renderování.
             *
             * Statistiky kopírujeme inline do param (= žádná dodatečná alokace).
             * Při g_callstack_active == 0 vrací handler success = true s
             * count = 0 a entries = NULL (= UI ukáže prázdný stack + hint).
             */
            if ( rq->data_ptr )
            {
                st_DBGAPI_CALLSTACK_GET_PARAM *p =
                    (st_DBGAPI_CALLSTACK_GET_PARAM *) rq->data_ptr;
                st_CALLSTACK_ENTRY *entries = NULL;
                int count = 0;
                int rc = callstack_snapshot_get ( &entries, &count );
                if ( rc != 0 )
                {
                    /* Alokační chyba uvnitř callstack_snapshot_get. */
                    p->entries = NULL;
                    p->count = 0;
                    rq->success = false;
                }
                else
                {
                    p->entries = (void *) entries;
                    p->count   = count;
                    st_CALLSTACK_STATS s;
                    callstack_get_stats ( &s );
                    p->current_depth     = s.current_depth;
                    p->max_depth_reached = s.max_depth_reached;
                    p->divergence_count  = s.divergence_count;
                    p->diverg_trampoline = s.diverg_trampoline;
                    p->diverg_longjmp    = s.diverg_longjmp;
                    p->diverg_mismatch   = s.diverg_mismatch;
                    p->sp_swap_count     = s.sp_swap_count;
                    p->overflow_count    = s.overflow_count;
                    p->stack_discard_count = s.stack_discard_count;
                    p->active            = g_callstack_active;
                    /* cycles_now: cpu->total_cycles snapshot pro UI Cyc-in
                     * absolute výpočet (= cycles uvnitř každého frame od
                     * jeho push do teď). Bez CPU = 0. */
                    p->cycles_now = g_mzarch_main.cpu
                        ? (uint64_t) g_mzarch_main.cpu->total_cycles
                        : 0u;
                    rq->success = true;
                };
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_GET_PROFILER:
            /* Profiler snapshot: alokuje pole entries přes profiler_snapshot_get
             * (callee-allocated, g_malloc). Caller (UI) MUSÍ pole uvolnit
             * po dokončení renderování (= rekonstrukce st_PROF_SNAPSHOT
             * z entries + entry_count a profiler_snapshot_free).
             *
             * Statistiky kopírujeme inline (= bez další alokace). Při
             * g_profiler_active == 0 vrací handler success = true s
             * naposledy nasbíranými daty (= "data po Stopu jsou stále
             * viewable").
             */
            if ( rq->data_ptr )
            {
                st_DBGAPI_PROFILER_GET_PARAM *p =
                    (st_DBGAPI_PROFILER_GET_PARAM *) rq->data_ptr;
                st_PROF_SNAPSHOT snap;
                int rc = profiler_snapshot_get ( &snap );
                if ( rc != 0 )
                {
                    /* Alokační chyba uvnitř profiler_snapshot_get. */
                    p->entries = NULL;
                    p->entry_count = 0;
                    rq->success = false;
                }
                else
                {
                    p->entries           = (void *) snap.entries;
                    p->entry_count       = snap.entry_count;
                    p->active            = snap.stats.active ? 1u : 0u;
                    p->total_cycles_64   = snap.stats.total_cycles_64;
                    p->total_calls       = snap.stats.total_calls;
                    p->irq_entries       = snap.stats.irq_entries;
                    p->unmatched_returns = snap.stats.unmatched_returns;
                    p->max_depth_reached = snap.stats.max_depth_reached;
                    p->overflow_count    = snap.stats.overflow_count;
                    rq->success = true;
                };
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_PROFILER_SET_ACTIVE:
            /* Přepnutí profileru ON/OFF z UI. profiler_set_active je
             * idempotentní a v EMU vlákně safe (= listener slot
             * manipulace je tady mimo hot path). */
            if ( rq->data_ptr )
            {
                st_DBGAPI_PROFILER_SET_ACTIVE_PARAM *p =
                    (st_DBGAPI_PROFILER_SET_ACTIVE_PARAM *) rq->data_ptr;
                profiler_set_active ( p->active ? true : false );
                rq->success = true;
            }
            else
            {
                rq->success = false;
            };
            break;

        case DBGAPI_CMD_PROFILER_RESET:
            /* Vynulování agregátoru za běhu. Nezasahuje g_profiler_active
             * (= pokud běží, dál sbírá nová data). */
            profiler_reset ( );
            rq->success = true;
            break;

        default:
            /* Neznámý příkaz */
            g_warning("dbgapi: unknown command %d", cmd);
            rq->success = false;
            break;
    };
}

void dbgapi_emu_complete(st_DBGAPI_CMDRQ *rq)
{
    if (!rq)
        return;

    /* Zamknout slot, nastavit stav na PROCESSED, signalizovat čekající UI */
    APP_MUTEX_LOCK(rq->mutex);
    rq->cmd_state = DBGAPI_CMDSTATE_PROCESSED;
    APP_COND_SIGNAL(rq->cond);
    APP_MUTEX_UNLOCK(rq->mutex);
}

bool dbgapi_emu_wait_for_cmd(st_DBGAPI_CMDRQ_QUEUE *queue, int timeout_ms)
{
    APP_MUTEX_LOCK(queue->queue_mutex);

    /* Pokud ve frontě už něco je, hned vrátit */
    if (dbgapi_emu_has_pending_unlocked(queue))
    {
        APP_MUTEX_UNLOCK(queue->queue_mutex);
        return true;
    };

    /* Čekat na signál (nový příkaz) nebo timeout */
    APP_COND_WAIT_TIMEOUT_MS(queue->queue_cond, queue->queue_mutex, timeout_ms);

    bool has_cmd = dbgapi_emu_has_pending_unlocked(queue);
    APP_MUTEX_UNLOCK(queue->queue_mutex);
    return has_cmd;
}

void dbgapi_emu_set_ending(st_DBGAPI_CMDRQ_QUEUE *queue)
{
    APP_MUTEX_LOCK(queue->queue_mutex);
    queue->reply_state = DBGAPI_CMDREPLY_STATE_ENDING;
    APP_MUTEX_UNLOCK(queue->queue_mutex);
}


/* ============================================================================
 * EMU STRANA — ODESÍLÁNÍ MSG (EMU → UI)
 *
 * Volá registrovaný dispatcher (přes dbgapi_emu_register_msg_dispatcher)
 * pro doručení MSG do UI vlákna. dbgapi.c nezná SDL ani sdlapp - thread
 * switch řeší dispatcher v UI vrstvě (src/ui-imgui/debugger/dbgapi_dispatcher).
 * ============================================================================ */

void dbgapi_emu_register_msg_dispatcher(dbgapi_msg_dispatcher_t dispatcher,
                                          void *user_data)
{
    s_msg_dispatcher = dispatcher;
    s_msg_dispatcher_user_data = user_data;
}

void dbgapi_emu_send_msg(en_DBGAPI_MSG msg, st_DBGAPI_MSG_DATA *data)
{
    if (!s_msg_dispatcher)
    {
        /* Žádný dispatcher zaregistrován - zahodit MSG, uvolnit data.
         * Tento stav nastává např. v testovém prostředí bez UI, nebo
         * při shutdown po dbgapi_destroy(). */
        if (data)
            g_free(data);
        return;
    };

    /* Předat MSG dispatcheru. Dispatcher přebírá vlastnictví dat
     * (zodpovědnost za uvolnění po doručení nebo zahození). */
    s_msg_dispatcher(msg, data, s_msg_dispatcher_user_data);
}


/* ============================================================================
 * UI STRANA — ODESÍLÁNÍ CMDRQ (UI → EMU)
 * ============================================================================ */

bool dbgapi_ui_submit_cmd_sync(st_DBGAPI_CMDRQ_QUEUE *queue,
                                en_DBGAPI_CMD cmd,
                                void *data_ptr,
                                void *result_ptr,
                                int timeout_ms)
{
    /* Kontrola: emulátor se neukončuje? */
    APP_MUTEX_LOCK(queue->queue_mutex);
    if (queue->reply_state == DBGAPI_CMDREPLY_STATE_ENDING)
    {
        APP_MUTEX_UNLOCK(queue->queue_mutex);
        return false;
    };

    /* Kontrola: fronta není plná? Když emu vlákno blokuje (např. CMT
     * hack čeká na FileBrowser odpověď), tail se vzdaluje od head a
     * fronta se rychle plní. UI klienti (CPU panel refresh tick 100ms)
     * by jinak spam-ovali warning každý sync call.
     *
     * Změna z g_warning na g_debug: pro koncového uživatele tiché (= ne
     * zaplavená console), pro dev viditelné jen s G_MESSAGES_DEBUG.
     * UI klienti by měli sami dělat self-rate-limit přes
     * dbgapi_ui_queue_is_full() PŘED submit (= preferovaná cesta). */
    int next_tail = (queue->tail + 1) % DBGAPI_CMDRQ_QUEUE_SIZE;
    if (next_tail == queue->head)
    {
        APP_MUTEX_UNLOCK(queue->queue_mutex);
        g_debug("dbgapi: CMDRQ queue is full, command dropped");
        return false;
    };

    /* Vložit příkaz do slotu na pozici tail */
    st_DBGAPI_CMDRQ *slot = &queue->cmdrq[queue->tail];
    queue->tail = next_tail;

    /* Inicializace slotu */
    APP_MUTEX_LOCK(slot->mutex);
    slot->cmd = cmd;
    slot->cmd_state = DBGAPI_CMDSTATE_PENDING;
    slot->data_ptr = data_ptr;
    slot->result_ptr = result_ptr;
    slot->success = false;

    /* Signalizovat emulátoru, že ve frontě je nový příkaz */
    APP_COND_SIGNAL(queue->queue_cond);
    APP_MUTEX_UNLOCK(queue->queue_mutex);

    /* Čekat na zpracování příkazu emulátorem */
    if (timeout_ms > 0)
    {
        /* Čekání s timeoutem */
        APP_COND_WAIT_TIMEOUT_MS(slot->cond, slot->mutex, timeout_ms);
    }
    else
    {
        /* Neomezené čekání */
        APP_COND_WAIT(slot->cond, slot->mutex);
    };

    /* Přečíst výsledek */
    bool success = (slot->cmd_state == DBGAPI_CMDSTATE_PROCESSED) && slot->success;

    /* Uvolnit slot */
    slot->cmd_state = DBGAPI_CMDSTATE_NONE;
    slot->cmd = DBGAPI_CMD_NONE;
    slot->data_ptr = NULL;
    slot->result_ptr = NULL;

    APP_MUTEX_UNLOCK(slot->mutex);

    return success;
}

bool dbgapi_ui_queue_is_full(st_DBGAPI_CMDRQ_QUEUE *queue)
{
    APP_MUTEX_LOCK(queue->queue_mutex);
    int next_tail = (queue->tail + 1) % DBGAPI_CMDRQ_QUEUE_SIZE;
    bool full = (next_tail == queue->head);
    APP_MUTEX_UNLOCK(queue->queue_mutex);
    return full;
}

bool dbgapi_ui_is_ending(st_DBGAPI_CMDRQ_QUEUE *queue)
{
    APP_MUTEX_LOCK(queue->queue_mutex);
    bool ending = (queue->reply_state == DBGAPI_CMDREPLY_STATE_ENDING);
    APP_MUTEX_UNLOCK(queue->queue_mutex);
    return ending;
}


/* ============================================================================
 * UI STRANA — REGISTRACE MSG CALLBACKU
 * ============================================================================ */

void dbgapi_ui_register_msg_callback(dbgapi_msg_callback_t callback, void *user_data)
{
    s_msg_callback = callback;
    s_msg_callback_user_data = user_data;
}

void dbgapi_ui_unregister_msg_callback(void)
{
    s_msg_callback = NULL;
    s_msg_callback_user_data = NULL;
}

void dbgapi_ui_invoke_msg_callback(en_DBGAPI_MSG msg, st_DBGAPI_MSG_DATA *data)
{
    if (!s_msg_callback)
    {
        /* Žádný listener není zaregistrován - data uvolnit a vrátit. */
        if (data)
            g_free(data);
        return;
    };

    /* Volat registrovaný listener. Listener je zodpovědný za uvolnění
     * data (per kontrakt dbgapi_msg_callback_t). */
    s_msg_callback(msg, data, s_msg_callback_user_data);
}

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
