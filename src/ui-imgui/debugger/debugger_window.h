/*
 * debugger_window.h — Hlavní okno debuggeru (ImGui)
 *
 * Veřejné API pro hlavní okno debuggeru MZ-800/MZ-1500.
 * Okno se zobrazuje/skrývá přes g_gui->showDebuggerWindow.
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

#ifndef DEBUGGER_WINDOW_H
#define DEBUGGER_WINDOW_H

#ifdef __cplusplus
extern "C"
{
#endif

    /* Hlavní vykreslovací funkce debugger okna.
     * Volá se každý frame z main_window.cpp.
     * p_open — ukazatel na bool řídící viditelnost okna (g_gui->showDebuggerWindow). */
    void imgui_debugger_window(bool *p_open);

    /*
     * UI funkce volané z jádra debuggeru (debugger.c).
     * Manipulují s g_gui->showDebuggerWindow.
     * Jádro je volá přes debugger_show/hide_main_window().
     */
    void ui_debugger_show_main_window(void);
    void ui_debugger_hide_main_window(void);

    /**
     * @brief V9.3 pattern: vyžádá si bring-to-front hlavního debug okna
     *        v dalším volání renderu.
     *
     * Nastaví interní pending flag, který `imgui_debugger_window` v dalším
     * volání vyhodnotí a pokud je true:
     *
     * 1. PŘED `Begin`: zavolá `ImGui::SetNextWindowFocus()` (= ImGui-internal
     *    z-order zvedne).
     * 2. PO `Begin`: pokud je okno v separate platform viewport
     *    (multi-viewport mode), zavolá `Platform_SetWindowFocus(viewport)`
     *    = `SDL_RaiseWindow` (= OS-level bring-to-front).
     *
     * Bez tohoto dual-step patternu by samotné `SetNextWindowFocus` v
     * multi-viewport módu jen přepnulo title flash, ale OS-level window
     * z-order by zůstal beze změny (viz V9.3 commit log v stack_window.cpp).
     *
     * Idempotentní v rámci framu (násobné volání = jeden focus request).
     *
     * Side effects: nastaví modul-lokální static flag konzumovaný v
     *               `imgui_debugger_window` při dalším volání.
     *
     * @pre Volat pouze z UI vlákna.
     */
    void debugger_window_request_focus(void);

#ifdef __cplusplus
}
#endif

#endif /* DEBUGGER_WINDOW_H */
