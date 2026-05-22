/*
 * dbg_extra_disasm.h - Sekundární Disassembly okna #2 - #5
 *
 * Toggluje se z menu Debugger -> Other disassembly. Každé okno hosti
 * vlastní instanci DisassembledView (bez horní historie, bez animace na
 * PC). Layout (pozice, velikost) persistuje přes imgui.ini, focus_addr
 * persistuje přes cfgmain modul DEBUGGER (klíče disasm_extra<N>_focus).
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

#ifndef DBG_EXTRA_DISASM_H
#define DBG_EXTRA_DISASM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Počet sekundárních Disassembly oken (= #2, #3, #4, #5).
 */
#define DBG_EXTRA_DISASM_COUNT 4

/**
 * @brief Vykreslí všechna otevřená sekundární Disassembly okna.
 *
 * Iteruje g_gui->showDisasmExtraWindow[0..DBG_EXTRA_DISASM_COUNT-1].
 * Pro otevřené okno lazy-vytvoří instanci DisassembledView (s history=false,
 * animation=false) a deleguje render. Pro zavřené okno instanci uvolní.
 *
 * Volat z hlavní render funkce mezi PočátečnímBeginu framu a EndFrame
 * (= z myimgui_render_cb).
 *
 * @pre Voláno pouze z UI vlákna mezi začátkem a koncem ImGui framu.
 */
void dbg_extra_disasm_render_all(void);

/**
 * @brief Otevře sekundární disasm okno N a nastaví focus_addr.
 *
 * Pro N v rozsahu 2..5 (= sekundární okna #2 .. #5):
 *   - pokud okno není otevřené, nastaví viditelný flag
 *     g_gui->showDisasmExtraWindow[N-2] = true (= lazy create instance
 *     proběhne v render_one_slot v dalším framu); persistovaný focus_addr
 *     pro slot je rovnou přepsán hodnotou @p addr a slot->focus_applied
 *     se zresetuje (= aplikace na nově vytvořenou instanci v dalším framu).
 *   - pokud okno je už otevřené, nastaví focus_addr přímo na běžící
 *     instanci přes její setter API.
 *
 * Pro N mimo rozsah 2..5 = no-op.
 *
 * Volá se z context menu "Show in #N" v dbg_disassembled.
 *
 * @param n     pořadové číslo extra okna (2 .. 5)
 * @param addr  cílová adresa pro focus_addr
 */
void dbg_extra_disasm_show_window(int n, uint16_t addr);

/**
 * @brief V9.3 pattern: vyžádá si bring-to-front sekundárního disasm okna
 *        slotu @p slot_idx v dalším volání renderu.
 *
 * Nastaví per-slot pending flag. `render_one_slot` v dalším volání:
 *
 * 1. PŘED `Begin`: zavolá `ImGui::SetNextWindowFocus()` (= ImGui-internal
 *    z-order zvedne).
 * 2. PO `Begin`: pokud je okno v separate platform viewport (multi-viewport
 *    mode), zavolá `Platform_SetWindowFocus(viewport)` = `SDL_RaiseWindow`
 *    (= OS-level bring-to-front).
 *
 * Bez tohoto dual-step patternu by samotné `SetNextWindowFocus` v
 * multi-viewport módu jen přepnulo title flash, ale OS-level window
 * z-order by zůstal beze změny (viz V9.3 commit log v stack_window.cpp).
 *
 * @param slot_idx index slotu 0..DBG_EXTRA_DISASM_COUNT-1
 *                 (0 = Disassembly #2, 1 = #3, 2 = #4, 3 = #5).
 *                 Mimo rozsah = no-op.
 *
 * @pre Volat pouze z UI vlákna.
 */
void dbg_extra_disasm_request_focus(int slot_idx);

/**
 * @brief Inicializace persistovaných hodnot focus_addr v cfgmain.
 *
 * Registruje DEBUGGER cfgmain klíče "disasm_extra<N>_focus" pro každé
 * sekundární okno. Volá se z debugger_init() po vytvoření CFGMOD modulu
 * DEBUGGER. Hodnoty se naplní z config souboru a později aplikují při
 * lazy-create instance v dbg_extra_disasm_render_all().
 *
 * @param cmod CFGMODULE pointer (= "DEBUGGER" modul z debugger_init)
 */
void dbg_extra_disasm_register_cfg(void *cmod);

#ifdef __cplusplus
}
#endif

#endif /* DBG_EXTRA_DISASM_H */
