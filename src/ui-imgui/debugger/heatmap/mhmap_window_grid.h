/*
 * File:   mhmap_window_grid.h
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
 * @file mhmap_window_grid.h
 * @brief Vykreslení heatmap gridu pro daný region.
 *
 * Voláno z @c mhmap_window_render uvnitř Begin/End. Renderuje grid přes
 * @c ImDrawList::AddRectFilled (rychlejší než per-cell ImGui prvky),
 * obsluhuje hover tooltip a click selection.
 */

#ifndef MHMAP_WINDOW_GRID_H
#define MHMAP_WINDOW_GRID_H

#include "mzarch/mzarch_config.h"

#if defined(MZ800EMU_CFG_DEBUGGER_ENABLED) && ( ( MZARCH == 800 ) || ( MZARCH == 1500 ) || ( MZARCH == 700 ) )

#ifdef __cplusplus

#include "debugger/mhmap.h"


/**
 * @brief Statistika min/max R/W/X přes všechny cell daného regionu.
 *
 * Předpočítává se 1× per frame v @ref mhmap_grid_compute_stats; používá
 * grid render, side panel a filter normalizace.
 */
struct st_MHMAP_REGION_STATS
{
    uint32_t max_r;        /**< největší R counter v regionu */
    uint32_t max_w;        /**< největší W counter v regionu */
    uint32_t max_x;        /**< největší X counter v regionu */
    uint32_t max_s;        /**< největší S counter v regionu (V5) */
    unsigned active_cells; /**< počet cell s @c max(R,W,X,S) > 0 */
    uint64_t total_r;      /**< suma všech R counterů */
    uint64_t total_w;      /**< suma všech W counterů */
    uint64_t total_x;      /**< suma všech X counterů */
    uint64_t total_s;      /**< suma všech S counterů (V5) */
};


/**
 * @brief Layout heatmap gridu (šířka × výška v cell).
 */
struct st_MHMAP_GRID_LAYOUT
{
    int width;             /**< počet sloupců */
    int height;            /**< počet řádků */
};


/**
 * @brief Spočítat layout (W × H) pro daný počet cell.
 *
 * Volí pevné rozměry pro známé velikosti (256/4K/8K/16K/32K/64K),
 * fallback na nejbližší power-of-2 čtvercový layout pro unikátní velikosti.
 *
 * @param size_cells Počet cell v regionu.
 * @return Šířka a výška gridu.
 */
extern st_MHMAP_GRID_LAYOUT mhmap_grid_pick_layout ( unsigned size_cells );


/**
 * @brief Spočítat statistiky regionu (max + total + active count).
 *
 * O(N) průchod přes všechny cell. Volá se 1× per frame.
 *
 * @param cells       Pointer na cell array (typicky @c region->buffer).
 * @param size_cells  Počet cell.
 * @return Vyplněná struktura statistik.
 */
extern st_MHMAP_REGION_STATS mhmap_grid_compute_stats ( const st_MHMAP_CELL *cells, unsigned size_cells );


/**
 * @brief Vykreslit heatmap grid pro region.
 *
 * Drží layout dle @ref mhmap_grid_pick_layout, aplikuje zoom z
 * @c g_mhmap_window.zoom, color mode z @c g_mhmap_window.color_mode, threshold
 * z @c g_mhmap_window.threshold a normalizaci dle @c g_mhmap_window.log_scale.
 *
 * Hover tooltip ukazuje offset + R/W/X. Levý klik nastaví
 * @c g_mhmap_window.selected_cell_offset (Phase E side panel ukazuje detail).
 *
 * @param region Pointer na region popis (z @c mhmap_get_export_regions).
 * @param cells  Pointer na cell array (Live = @c region->buffer, Imported =
 *               příslušný offset v @c g_mhmap_window.import_data).
 * @param stats  Předpočítané statistiky regionu (z @c cells, ne z region->buffer).
 */
extern void mhmap_grid_render ( const st_MHMAP_EXPORT_REGION *region,
                             const st_MHMAP_CELL *cells,
                             const st_MHMAP_REGION_STATS *stats );

#endif /* __cplusplus */

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED && (MZARCH == 800 || MZARCH == 1500) */

#endif /* MHMAP_WINDOW_GRID_H */
