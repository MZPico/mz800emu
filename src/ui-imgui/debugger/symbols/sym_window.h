/*
 * File:   sym_window.h
 *
 * Symbol Browser panel - dokovatelné ImGui okno pro správu symbol DB
 * (D.8.6).
 *
 * Funkce:
 *   - Tabulka přes všechny symboly z sym_db (Name | Addr | Bank |
 *     Source | Comment)
 *   - Search filter (= substring match na Name nebo Comment)
 *   - "Load From..." (= file dialog, auto-detekce formátu dle suffixu)
 *   - "Save .lbl As..." (= serializace jen LBL záznamů)
 *   - "Add User Label" form (addr + name + comment)
 *   - "Delete Selected" (= jen LBL záznamy, ostatní z buildchain
 *     nelze mazat - reload souboru je obnoví)
 *   - Inline edit Comment column - automatická promote na LBL
 *
 * Viditelnost: g_gui->showSymbolsWindow (přidáno do MyImGui).
 *
 * Threading: jen UI vlákno; sym_db lookup volá i disassembler section,
 * ale ten také běží na UI vlákně. Žádný přístup z hot path EMU vlákna.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later, viz licence header v breakpoints.h.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef SYM_WINDOW_H
#define SYM_WINDOW_H

#include "mzarch/mzarch_config.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Render hlavního Symbol Browser okna.
 *
 * Volá se z imgui_main_window každý frame, pokud je
 * g_gui->showSymbolsWindow == true. Po zavření křížkem ImGui
 * vynuluje *p_open na false.
 *
 * @param p_open Pointer na bool řídící viditelnost
 *               (typicky &g_gui->showSymbolsWindow).
 *
 * Side effects: může mutovat sym_db storage (Add / Delete / Set comment
 * / Load file / Save file).
 *
 * Threading: jen UI vlákno.
 */
extern void sym_window_render ( bool *p_open );


/**
 * @brief Toggle viditelnost okna.
 *
 * Přepne g_gui->showSymbolsWindow. Volá se z menu položky.
 */
extern void sym_window_show_hide ( void );


#ifdef __cplusplus
}
#endif

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */

#endif /* SYM_WINDOW_H */
