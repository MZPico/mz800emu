/*
 * File:   watch_window.h
 *
 * Watch panel - dokovatelné ImGui okno pro user-defined paměťové hlídky
 * (Watch V1, Phase A = L0 + Phase B = L1 + Phase C = L2 expression).
 *
 * Layout L2:
 *   +-------------------------------------------------------+
 *   | Watch                                            [X]  |
 *   +-------------------------------------------------------+
 *   | [+ Add]    (n watches)                                |
 *   | [Save] [Save As...] [Load From...] [Clear All...]     |
 *   |                                                       |
 *   | (Add popup pri kliknuti na +Add:                      |
 *   |   Mode:     [Address|Expr scalar|Expr deref]          |
 *   |   Address:  [_______________]      (mode=Address)     |
 *   |   Expression: [______________]     (mode=Expr*)       |
 *   |   Name:     [_______________] (optional)              |
 *   |   Type:     [u8|i8|u16le|...] dropdown                |
 *   |   Length / Bit spinner per typu                       |
 *   |   [Add] [Cancel])                                     |
 *   +-------------------------------------------------------+
 *   |  | Name   | Addr/Expr   | Type | Value      | [del]   |
 *   |--|--------|-------------|------|------------|-----    |
 *   |  | score  | 0xC080      | u8   | 0x0F       | x       |
 *   |  | @HL    | [HL]        | u8   | 0x42       | x       |
 *   |  | (anon) | 0xC080+1    | u16le| 0xCAFE     | x       |
 *   |  | bad    | [[          | u8   | !parse: ...| x       |
 *   +-------------------------------------------------------+
 *
 * Refresh: kazdy frame cte aktualni hodnoty pres watch_read_int /
 * watch_read_bytes (memory_read_byte_no_se polling z UI vlakna).
 * Pro EXPR módy navíc watch_eval_expr / _as_addr (bp_expr_eval s
 * side_effect=false = nepuštíme MEM_R BP).
 *
 * Visibility: g_gui->showWatchWindow (toggle z Debugger menu nebo
 * Alt+Shift+W). Window ID stabilita: ###watch_panel (tri hashe pro
 * dynamicky titulek).
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later, viz licence header v breakpoints.h.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef WATCH_WINDOW_H
#define WATCH_WINDOW_H

#include "mzarch/mzarch_config.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Render hlavniho Watch okna.
 *
 * Vola se z imgui_main_window kazdy frame pokud
 * g_gui->showWatchWindow == true. Po zavreni krizkem ImGui vynuluje
 * *p_open na false.
 *
 * @param p_open Pointer na bool ridici viditelnost
 *               (typicky &g_gui->showWatchWindow).
 *
 * Side effects: muze mutovat watch storage (Add/Del/Rename/Move).
 *
 * Threading: jen UI vlakno. Polling read pres
 * memory_read_byte_no_se z UI vlakna - race s EMU vlaknem ma nejvyse
 * efekt "stale display", ne korupci. Viz watch.h file-level doc.
 */
extern void watch_window_render ( bool *p_open );


/**
 * @brief Toggle viditelnost okna.
 *
 * Prepne g_gui->showWatchWindow. Vola se z menu polozky / shortcutu.
 */
extern void watch_window_show_hide ( void );


#ifdef __cplusplus
}
#endif

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */

#endif /* WATCH_WINDOW_H */
