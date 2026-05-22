/*
 * dbgapi_msg.h — MSG kanál: asynchronní notifikace z Emulátoru do UI
 *
 * Jednosměrný komunikační kanál pro krátké zprávy (signály).
 * Doručení EMU→UI řeší registrovaný dispatcher (viz dbgapi_emu.h
 * dbgapi_emu_register_msg_dispatcher()). Implementace dispatcheru
 * je v UI vrstvě (src/ui-imgui/debugger/dbgapi_dispatcher.cpp) a
 * obvykle používá SDL custom event mechanismus pro thread switch.
 *
 * MSG jsou stručné — „probuď se, něco se stalo". Pokud příjemce potřebuje
 * detailní data, pošle CMDRQ dotaz.
 *
 * Předávání dat:
 * - Jednoduchá data (int): GINT_TO_POINTER() v st_DBGAPI_MSG_DATA
 * - Složitější data: heap alokace (g_new0), příjemce uvolní (g_free)
 * - Většinou MSG nepotřebuje data — je to jen „zvonění budíku"
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
#ifndef DBGAPI_MSG_H
#define DBGAPI_MSG_H

#include <stdint.h>
#include <stdbool.h>
#include "app/app_thread.h"

/* ============================================================================
 * TYPY ZPRÁV (MSG)
 *
 * Emulátor posílá tyto notifikace do UI vlákna. UI na ně reaguje buď
 * přímou akcí (refresh), nebo přes CMDRQ dotazy (zjištění stavu).
 * ============================================================================ */

typedef enum en_DBGAPI_MSG
{
    DBGAPI_MSG_NONE = 0,       /* Bez efektu — ping */
    DBGAPI_MSG_PAUSED,         /* Emulace pozastavena (důvod v datech) */
    DBGAPI_MSG_RUNNING,        /* Emulace spuštěna */
    DBGAPI_MSG_EXITING,        /* Emulátor se chystá ukončit — konec komunikace */
    DBGAPI_MSG_BREAKPOINT_HIT, /* Breakpoint zasažen — emulace automaticky pozastavena */
    DBGAPI_MSG_STEP_COMPLETE,  /* Krok dokončen — emulace automaticky pozastavena */
    DBGAPI_MSG_RESET_DONE,     /* Reset CPU proveden */
} en_DBGAPI_MSG;


/* ============================================================================
 * DOPLŇKOVÁ DATA PRO MSG
 *
 * Některé MSG mohou nést doplňková data. Struktura se alokuje na heapu
 * odesílatelem a uvolňuje příjemcem (g_free).
 *
 * Použití:
 *   - BREAKPOINT_HIT: addr = adresa breakpointu, id = ID breakpointu
 *   - STEP_COMPLETE: addr = aktuální PC po kroku
 *   - PAUSED: addr = aktuální PC v okamžiku pauzy
 *   - Ostatní: data nejsou potřeba (msg_data = NULL)
 * ============================================================================ */

typedef struct st_DBGAPI_MSG_DATA
{
    en_DBGAPI_MSG msg_type;    /* Typ zprávy (redundantní, pro kontrolu) */
    uint16_t addr;             /* Relevantní adresa (PC, breakpoint, ...) */
    int id;                    /* Relevantní ID (breakpoint ID, ...) */
} st_DBGAPI_MSG_DATA;


#endif /* DBGAPI_MSG_H */
