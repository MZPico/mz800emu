/*
 * File:   mhmap_window.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 30. dubna 2026
 *
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

/**
 * @file mhmap_window.h
 * @brief Memory Heatmap GUI okno - vizualizace CDL dat.
 *
 * Samostatné dokovatelné ImGui okno, které slouží jako primární UI nad CDL
 * (Code/Data Logger / Memory Heatmap) modulem (@ref mhmap.h). Duplikuje
 * ovládací volby z Debugger Settings -> CDL submenu a navíc poskytuje
 * vizualizaci access counterů jako heatmap grid.
 *
 * Viditelnost okna drží @c g_gui->showMemoryHeatmapWindow (toggle z View
 * menu emulátoru i z Debugger Settings -> CDL -> Show heatmap window).
 *
 * Implementační plán: viz @c devdoc/PLAN-MemoryHeatmap.md.
 */

#ifndef MHMAP_WINDOW_H
#define MHMAP_WINDOW_H

#include "mzarch/mzarch_config.h"

#if defined(MZ800EMU_CFG_DEBUGGER_ENABLED) && ( ( MZARCH == 800 ) || ( MZARCH == 1500 ) || ( MZARCH == 700 ) )

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Render hlavního Memory Heatmap okna.
     *
     * Volá se z @c imgui_main_window každý frame, pokud je
     * @c g_gui->showMemoryHeatmapWindow nastaveno na @c true. Pokud uživatel
     * okno zavře přes (X) tlačítko, ImGui vynuluje @p p_open na @c false.
     *
     * @param p_open Pointer na bool řídící viditelnost okna
     *               (typicky @c &g_gui->showMemoryHeatmapWindow).
     */
    void mhmap_window_render ( bool *p_open );


    /**
     * @brief Toggle viditelnost Memory Heatmap okna.
     *
     * Přepne @c g_gui->showMemoryHeatmapWindow. Volá se z menu položek.
     */
    void mhmap_window_show_hide ( void );


    /**
     * @brief Zaregistrovat persistenci stavu okna do .ini souboru.
     *
     * Volá se z @c debugger_init na konci registrace DEBUGGER cfg modulu,
     * před @c cfgmodule_parse. Připojí klíče @c mhwindow_color_mode,
     * @c mhwindow_log_scale, @c mhwindow_threshold, @c mhwindow_zoom,
     * @c mhwindow_side_panel_visible, @c mhwindow_selected_region_idx
     * jako handlery na fields v @c g_mhmap_window.
     *
     * Pozn.: viditelnost okna (visible) se nepersistuje (default closed -
     * uživatel otevírá z menu). Pozice/velikost/dock state spravuje ImGui
     * automaticky přes @c imgui.ini.
     *
     * @param cmod_void Pointer na st_CFGMODULE z cfgmodule.h (typed jako void*
     *                  aby header nemusel includovat cfgmodule.h).
     */
    void mhmap_window_register_persistence ( void *cmod_void );

#ifdef __cplusplus
}
#endif

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED && (MZARCH == 800 || MZARCH == 1500) */

#endif /* MHMAP_WINDOW_H */
