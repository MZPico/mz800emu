/*
 * File:   bm_window.h
 *
 * Bookmarks panel - samostatné dokovatelné ImGui okno pro správu
 * pojmenovaných adresových záložek (= dvojice user_input + comment).
 *
 * Funkce:
 *   - Tabulka přes všechny záložky z bookmarks storage:
 *     Sel | Label | $address | Comment | Actions
 *   - Filter (sticky header) - case-insensitive substring nad
 *     user_input + resolved sym_db jméno + comment
 *   - Add form (= "Address or symbol" + "Comment" + Add button +
 *     Insert PC tlačítko)
 *   - Inline edit per řádek (Edit toggle -> Apply / Cancel)
 *   - Multi-select přes per-row checkbox + Bulk delete
 *   - File ops (Save, Save As..., Load, Merge) přes ImGuiFileDialog
 *   - Klikatelné řádky (Label / $address): focus do Disassembly
 *     (LMB = main, RMB context menu pro slot 1..5)
 *
 * Visibilita drží g_gui->showBookmarksWindow.
 *
 * Refresh: každý frame čte aktuální storage. Threading: jen UI vlákno.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later, viz licence header v bm_window.cpp.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef BM_WINDOW_H
#define BM_WINDOW_H

#include "mzarch/mzarch_config.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Render hlavního Bookmarks okna.
 *
 * Volá se z main_window každý frame, pokud `*p_open` je true.
 * Pokud uživatel okno zavře přes (X), ImGui vynuluje *p_open na
 * false - caller musí mít p_open ve své persistent struktuře.
 *
 * @param p_open Pointer na bool řídící viditelnost (typicky
 *               &g_gui->showBookmarksWindow).
 *
 * Side effects: může mutovat bookmarks storage (Add / Edit / Delete /
 *               Clear / Load / Merge).
 */
extern void bm_window_render ( bool *p_open );


/**
 * @brief Otevře Bookmarks okno a zafokusuje add-form input.
 *
 * Použití: shortcut z context menu "Add to bookmarks" (commit 4),
 * pokud uživatel chce přidat ručně přes okno. Aktuálně jen
 * nastaví g_gui->showBookmarksWindow=true a flagne focus pro
 * příští render.
 */
extern void bm_window_open_with_focus_to_input ( void );


/* Pro focus do disassembly instance používáme přímo veřejné API
 * dbg_disasm_show_in_slot (= sjednocené ensure-open + focus +
 * auto-disable Follow PC). Viz dbg_disassembled.h. */


#ifdef __cplusplus
}
#endif

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */

#endif /* BM_WINDOW_H */
