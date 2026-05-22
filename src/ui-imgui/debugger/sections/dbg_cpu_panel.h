/*
 * dbg_cpu_panel.h - CPU registr panel v pravém sloupci debuggeru.
 *
 * Variant A (compact) layout. Renderuje hlavní registr file (AF/BC/DE/HL/
 * IX/IY/SP/PC + alternativni sada AF'/BC'/DE'/HL'), rozpis flagu F a
 * specialni sekci (I/R/IM/IFF1/IFF2/HALT).
 *
 * Pred kazdym 16-bit parem registru je tlacitko ">" (focus button,
 * pattern z Bookmarks): levy klik = focus primarni Disassembly na
 * hodnotu registru (NEpauzuje), pravy klik = popup menu.
 *
 * Auto-pauza spousti jen klik na editovatelnou hodnotu (registr, flag
 * bit) - navigacni akce zustavaji live.
 *
 * Refresh dat: pres DbgRefreshController (g_dbg_ui.refresh.should_refresh)
 * tj. interval 100 ms + force pri pauze/step. Panel ctve GET_ALL_REGS +
 * GET_CPU_FLAGS pres dbgapi.
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

#ifndef DBG_CPU_PANEL_H
#define DBG_CPU_PANEL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Vykresli CPU panel do aktualniho ImGui kontejneru.
 *
 * Predpoklada se, ze caller (= debugger_window.cpp) jiz vytvoril
 * pravy sloupec/child window a panel obsadi celou dostupnou plochu
 * (Variant A compact). Panel sam si zapina monospace font (pres
 * DBG_CONTENT_FONT_SIZE_OFFSET) a obsluhuje kompletni render vcetne
 * focus tlacitek a popup menu.
 *
 * Data ctve pres dbgapi (GET_ALL_REGS + GET_CPU_FLAGS) v okamzicich
 * kdy g_dbg_ui.refresh.should_refresh == true. Mezi refreshi pouziva
 * cache (panel_state).
 *
 * Auto-pauza: klik na hodnotu registru / flag bit v running mode
 * vola dbg_autopause_on_interaction() (= request pauzu). V0 jen
 * request - in-place edit prijde v V1.
 *
 * @pre Volat pouze z UI vlakna mezi NewFrame a EndFrame ImGui,
 *      uvnitr ImGui::Begin() okna debuggeru.
 */
void dbg_cpu_panel_render(void);

#ifdef __cplusplus
}
#endif

#endif /* DBG_CPU_PANEL_H */
