/*
 * bpt_window.h — Veřejné API okna Breakpoints
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

#ifndef BPT_WINDOW_H
#define BPT_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

    void imgui_breakpoints_window ( bool *p_open );

    /* Inicializace UI stavu breakpointů (volat z bootstrapu) */
    void bpt_state_init ( void );

    /* Toggle viditelnosti okna breakpointů */
    void breakpoints_show_hide_window ( void );

    /**
     * @brief V1.E.6.A - otevre okno, oznaci BP s danym ID a otevre
     *        editacni panel.
     *
     * Pouziti: routing z Activity okna (= dvojklik na radek s
     * entity_kind = DBGAPI_ENTITY_KIND_BP, entity_id = bp_id).
     * Volani provede:
     *   1) Otevre Breakpoints okno
     *   2) Pokud BP s danym ID v storage existuje, nastavi
     *      selected_id + selected_type = EVENT
     *   3) Otevre editacni panel pres bpt_edit_panel_open()
     *
     * Pokud BP s danym ID neexistuje (mezicasem smazan), okno se
     * jen otevre bez selection (= silent fallback).
     *
     * Thread-safety: UI vlakno only.
     *
     * @param bp_id  Breakpoint ID (= field st_BPT.id, >= 1).
     */
    void bpt_window_focus_id ( int bp_id );

    /**
     * @brief V1.7 C.3.5 - registrace persistence pro BP panel UI state.
     *
     * Volá se z debugger.c při registraci cfgmain modulů. Vytvoří
     * cfgelementy v modulu [BREAKPOINTS_PANEL]:
     *   active_tab       (int)   - posledně aktivní tab (-1 = All)
     *   tab<N>_filter    (text)  - per-typ tab text filter (N = 0..5)
     *   tab<N>_enabled_only (bool) - per-typ enabled-only toggle
     *
     * Po propagaci z .ini se restore_active_tab_once = true a první
     * render BP okna force-selectne odpovídající tab přes
     * ImGuiTabItemFlags_SetSelected.
     *
     * @param cmod_void  Pointer na CFGMOD (jako void*, aby header zůstal
     *                   bez cfg includes pro C/C++ kompat).
     */
    void bpt_window_register_persistence ( void *cmod_void );

#ifdef __cplusplus
}
#endif

#endif /* BPT_WINDOW_H */
