/*
 * mhmap_window_grid.cpp - Heatmap grid rendering.
 *
 * Phase C - vykreslení gridu přes ImDrawList::AddRectFilled, RGB color mode,
 * hover tooltip, click select. Filter bar (color mode, log/linear, threshold,
 * zoom) přidá Phase D - tady jen čteme z g_mhmap_window s rozumnými defaulty.
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

#include "main.h"

#include "mzarch/mzarch_config.h"

#if defined(MZ800EMU_CFG_DEBUGGER_ENABLED) && ( ( MZARCH == 800 ) || ( MZARCH == 1500 ) || ( MZARCH == 700 ) )

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "libs/imgui/imgui.h"
#include "i18n.h"

#include "debugger/mhmap.h"

#include "mhmap_window_state.h"
#include "mhmap_window_grid.h"


/* ========================================================================= */
/*  Layout                                                                   */
/* ========================================================================= */


st_MHMAP_GRID_LAYOUT mhmap_grid_pick_layout ( unsigned size_cells )
{
    /* Známé velikosti - viz PLAN-MemoryHeatmap.md tabulka. */
    switch ( size_cells )
    {
        case 0x80000: return { 512, 1024 }; /* 512K = memext */
        case 0x10000: return { 256, 256 };  /* 64K = bus, ram */
        case 0x8000:  return { 256, 128 };  /* 32K = vram MZ-800 */
        case 0x4000:  return { 256,  64 };  /* 16K = vram800-640*, MZ-1500 rom */
        case 0x2000:  return { 128,  64 };  /* 8K  = vram800-320*, vram-plane, pcg, rom-upper */
        case 0x1000:  return {  64,  64 };  /* 4K  = vram700*, rom-cg, rom-lower, cgrom, vram MZ-1500 */
        case 0x0100:  return {  16,  16 };  /* 256 = iorq-8bit, iorq-gdg */
    }

    /* Fallback - power-of-2 čtvercový layout (mírně přesahující). */
    int side = 1;
    while ( (unsigned)( side * side ) < size_cells ) side *= 2;
    int height = (int)( ( size_cells + side - 1 ) / side );
    return { side, height };
}


/* ========================================================================= */
/*  Statistiky                                                               */
/* ========================================================================= */


st_MHMAP_REGION_STATS mhmap_grid_compute_stats ( const st_MHMAP_CELL *cells, unsigned size_cells )
{
    st_MHMAP_REGION_STATS s = { };

    if ( !cells || size_cells == 0 ) return s;

    for ( unsigned i = 0; i < size_cells; i++ )
    {
        const st_MHMAP_CELL &c = cells[ i ];

        if ( c.r > s.max_r ) s.max_r = c.r;
        if ( c.w > s.max_w ) s.max_w = c.w;
        if ( c.x > s.max_x ) s.max_x = c.x;
        if ( c.s > s.max_s ) s.max_s = c.s;

        s.total_r += c.r;
        s.total_w += c.w;
        s.total_x += c.x;
        s.total_s += c.s;

        if ( c.r | c.w | c.x | c.s ) s.active_cells++;
    }

    return s;
}


/* ========================================================================= */
/*  Color computation                                                        */
/* ========================================================================= */


/**
 * @brief Normalizace counter hodnoty na <0, 1>.
 */
static float norm_value ( uint32_t v, uint32_t max_v, bool log_scale )
{
    if ( max_v == 0 ) return 0.0f;
    if ( log_scale )
    {
        return logf ( (float)v + 1.0f ) / logf ( (float)max_v + 1.0f );
    }
    return (float)v / (float)max_v;
}


/**
 * @brief Vytáhnout R, G, B byte z IM_COL32 hodnoty.
 */
static inline void unpack_rgb ( ImU32 c, int *r, int *g, int *b )
{
    *r = ( c >> IM_COL32_R_SHIFT ) & 0xFF;
    *g = ( c >> IM_COL32_G_SHIFT ) & 0xFF;
    *b = ( c >> IM_COL32_B_SHIFT ) & 0xFF;
}


/**
 * @brief Spočítat barvu cell podle aktuálního color mode.
 *
 * RGB mód = aditivní směs uživatelských barev R/W/X/S (z @c g_mhmap_window),
 * vážených normalizovanou intenzitou jednotlivých counterů. Default mapování:
 *  - R (read)        → modrá
 *  - W (write)       → červená
 *  - X (execute)     → zelená
 *  - S (stack write) → cyan (V5)
 *
 * Pokud @c show_s_category = false, S counter se ve vykreslení ignoruje
 * (= ne přispívá do směsi v RGB ani není dostupný jako separátní mode -
 * S radio button se v UI vyfiltruje voláním).
 *
 * Single-channel mode (R / W / X / S): grayscale jednoho counteru.
 */
static ImU32 cell_color ( const st_MHMAP_CELL &c,
                          const st_MHMAP_REGION_STATS &stats,
                          en_MHMAP_WINDOW_COLOR_MODE mode,
                          bool log_scale )
{
    if ( mode == MHMAP_WINDOW_COLOR_RGB )
    {
        /* Per-counter normalizovaná intenzita. */
        float i_r = norm_value ( c.r, stats.max_r, log_scale );
        float i_w = norm_value ( c.w, stats.max_w, log_scale );
        float i_x = norm_value ( c.x, stats.max_x, log_scale );
        float i_s = g_mhmap_window.show_s_category
                  ? norm_value ( c.s, stats.max_s, log_scale )
                  : 0.0f;

        int rR, rG, rB;  unpack_rgb ( g_mhmap_window.color_r_rgba, &rR, &rG, &rB );
        int wR, wG, wB;  unpack_rgb ( g_mhmap_window.color_w_rgba, &wR, &wG, &wB );
        int xR, xG, xB;  unpack_rgb ( g_mhmap_window.color_x_rgba, &xR, &xG, &xB );
        int sR, sG, sB;  unpack_rgb ( g_mhmap_window.color_s_rgba, &sR, &sG, &sB );

        /* Aditivní směs: out = sum( color_i * intensity_i ), clamp 0..255. */
        int sumR = (int)( rR * i_r + wR * i_w + xR * i_x + sR * i_s );
        int sumG = (int)( rG * i_r + wG * i_w + xG * i_x + sG * i_s );
        int sumB = (int)( rB * i_r + wB * i_w + xB * i_x + sB * i_s );
        if ( sumR > 255 ) sumR = 255;
        if ( sumG > 255 ) sumG = 255;
        if ( sumB > 255 ) sumB = 255;
        return IM_COL32 ( sumR, sumG, sumB, 255 );
    }

    uint32_t v   = 0;
    uint32_t mx  = 0;
    switch ( mode )
    {
        case MHMAP_WINDOW_COLOR_R: v = c.r; mx = stats.max_r; break;
        case MHMAP_WINDOW_COLOR_W: v = c.w; mx = stats.max_w; break;
        case MHMAP_WINDOW_COLOR_X: v = c.x; mx = stats.max_x; break;
        case MHMAP_WINDOW_COLOR_S: v = c.s; mx = stats.max_s; break;
        default: break;
    }
    int b = (int)( norm_value ( v, mx, log_scale ) * 255 );
    return IM_COL32 ( b, b, b, 255 );
}


/* ========================================================================= */
/*  Render                                                                   */
/* ========================================================================= */


void mhmap_grid_render ( const st_MHMAP_EXPORT_REGION *region,
                      const st_MHMAP_CELL *cells,
                      const st_MHMAP_REGION_STATS *stats )
{
    if ( !region || !cells || !stats ) return;

    st_MHMAP_GRID_LAYOUT layout = mhmap_grid_pick_layout ( region->size_cells );

    int zoom = (int)g_mhmap_window.zoom;
    if ( zoom < 1 ) zoom = 1;

    en_MHMAP_WINDOW_COLOR_MODE mode = g_mhmap_window.color_mode;
    bool log_scale = g_mhmap_window.log_scale;
    uint32_t threshold = g_mhmap_window.threshold;

    int total_w = layout.width  * zoom;
    int total_h = layout.height * zoom;

    ImVec2 origin = ImGui::GetCursorScreenPos ( );
    ImDrawList *dl = ImGui::GetWindowDrawList ( );

    /* Černé pozadí pod celý grid (pokrývá threshold-prázdné cell). */
    dl->AddRectFilled ( origin,
                        ImVec2 ( origin.x + total_w, origin.y + total_h ),
                        IM_COL32 ( 0, 0, 0, 255 ) );

    /* Per-cell rect. Threshold-skryté cell zůstanou černé. */
    for ( int y = 0; y < layout.height; y++ )
    {
        for ( int x = 0; x < layout.width; x++ )
        {
            unsigned idx = (unsigned)( y * layout.width + x );
            if ( idx >= region->size_cells ) break;

            const st_MHMAP_CELL &c = cells[ idx ];

            uint32_t maxv = c.r;
            if ( c.w > maxv ) maxv = c.w;
            if ( c.x > maxv ) maxv = c.x;
            /* V5: zahrň S do max jen pokud je viditelný (= jinak by S-only
             * cell zůstala viditelná navzdory tomu, že je toggle skrytá). */
            if ( g_mhmap_window.show_s_category && c.s > maxv ) maxv = c.s;
            if ( maxv < threshold ) continue;
            if ( maxv == 0 ) continue;  /* nulové cell zůstanou černé */

            ImU32 col = cell_color ( c, *stats, mode, log_scale );

            float px0 = origin.x + (float)( x * zoom );
            float py0 = origin.y + (float)( y * zoom );
            dl->AddRectFilled ( ImVec2 ( px0, py0 ),
                                ImVec2 ( px0 + (float)zoom, py0 + (float)zoom ),
                                col );
        }
    }

    /* Reservace místa pro hit-testing - Dummy item pokrývá celý grid. */
    ImGui::Dummy ( ImVec2 ( (float)total_w, (float)total_h ) );

    bool hovered = ImGui::IsItemHovered ( );

    /* Ctrl + wheel zoom - když je kurzor nad gridem a Ctrl drží.
     * Procházíme zoom_steps nahoru/dolů podle směru kolečka. */
    if ( hovered )
    {
        ImGuiIO &io = ImGui::GetIO ( );
        if ( io.KeyCtrl && io.MouseWheel != 0.0f )
        {
            static const unsigned zoom_steps[ ] = { 1, 2, 4, 8, 16, 32 };
            static const int zsc = (int)( sizeof ( zoom_steps ) / sizeof ( zoom_steps[ 0 ] ) );
            int idx = 0;
            for ( int i = 0; i < zsc; i++ )
            {
                if ( zoom_steps[ i ] == g_mhmap_window.zoom ) { idx = i; break; };
            }
            if ( io.MouseWheel > 0.0f && idx < zsc - 1 ) idx++;
            else if ( io.MouseWheel < 0.0f && idx > 0 ) idx--;
            g_mhmap_window.zoom = zoom_steps[ idx ];
        }
    }

    /* Hover - tooltip a klik selection. */
    if ( hovered )
    {
        ImVec2 mp = ImGui::GetMousePos ( );
        int cx = (int)( ( mp.x - origin.x ) / (float)zoom );
        int cy = (int)( ( mp.y - origin.y ) / (float)zoom );
        if ( cx >= 0 && cx < layout.width && cy >= 0 && cy < layout.height )
        {
            unsigned idx = (unsigned)( cy * layout.width + cx );
            if ( idx < region->size_cells )
            {
                const st_MHMAP_CELL &c = cells[ idx ];

                ImGui::BeginTooltip ( );
                ImGui::Text ( "Region: %s",  region->name );
                ImGui::Text ( "Offset: 0x%04X (%u)", idx, idx );
                ImGui::Separator ( );
                ImGui::Text ( "R = %u",      c.r );
                ImGui::Text ( "W = %u",      c.w );
                ImGui::Text ( "X = %u",      c.x );
                ImGui::Text ( "S = %u",      c.s );
                ImGui::EndTooltip ( );

                if ( ImGui::IsMouseClicked ( ImGuiMouseButton_Left ) )
                {
                    g_mhmap_window.selected_cell_offset = (int)idx;
                };
            };
        };
    };

    /* Highlight selected cell - dvojitý rámeček bílý vnější + žlutý vnitřní,
     * aby byl výrazně viditelný na libovolném podkladě (i na světle žlutém). */
    if ( g_mhmap_window.selected_cell_offset >= 0
         && g_mhmap_window.selected_cell_offset < (int)region->size_cells )
    {
        int sel = g_mhmap_window.selected_cell_offset;
        int cx = sel % layout.width;
        int cy = sel / layout.width;
        float px0 = origin.x + (float)( cx * zoom );
        float py0 = origin.y + (float)( cy * zoom );
        float px1 = px0 + (float)zoom;
        float py1 = py0 + (float)zoom;

        /* Vnější bílý rámeček (halo) - 3px tlustý, +/-3px přesah. */
        dl->AddRect ( ImVec2 ( px0 - 3.0f, py0 - 3.0f ),
                      ImVec2 ( px1 + 3.0f, py1 + 3.0f ),
                      IM_COL32 ( 255, 255, 255, 255 ),
                      0.0f, 0, 3.0f );
        /* Vnitřní žlutý rámeček - 2px tlustý, +/-1px přesah. */
        dl->AddRect ( ImVec2 ( px0 - 1.0f, py0 - 1.0f ),
                      ImVec2 ( px1 + 1.0f, py1 + 1.0f ),
                      IM_COL32 ( 255, 220, 0, 255 ),
                      0.0f, 0, 2.0f );
    };
}


#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED && (MZARCH == 800 || MZARCH == 1500) */
