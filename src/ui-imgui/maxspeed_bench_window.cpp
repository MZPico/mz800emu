/*
 * File:   maxspeed_bench_window.cpp
 *
 * ImGui okno MAX SPEED benchmarku - živé zobrazení efektivity emulace
 * v MAX SPEED (vrstva A kumulativ + vrstva B distribuce) s tlačítky Reset
 * a Print to console.
 *
 * Okno je ZÁMĚRNĚ mimo debugger modul (src/ui-imgui/, ne ui-imgui/debugger/),
 * aby bylo dostupné i v buildu MZ800EMU_NO_DEBUGGER - benchmark koncepčně
 * není debugger feature. Čte data přes emulator_measuring_maxspeed_report,
 * který je thread-safe.
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
#include "ui-imgui/bootstrap/myimgui.h"
#include "ui-imgui/auto_layout.h"
#include "libs/imgui/imgui.h"
#include <stdio.h>
#include <inttypes.h>

// Lokalizace
#include "i18n.h"

#include "emulator.h"
#include "emulator_measuring.h"

extern "C"
{
    void imgui_maxspeed_bench(bool *p_open);
};

void imgui_maxspeed_bench(bool *p_open)
{
    if (!*p_open)
        return;

    ImGui::SetNextWindowSize(ImVec2(320, 340), ImGuiCond_FirstUseEver);
    /* Auto-layout při fresh open - cache _L() do lokální proměnné. */
    const char *title = _L("MAX SPEED Benchmark");
    auto_layout_first_use_portrait(title, 360.0f, 420.0f);
    if (ImGui::Begin(title, p_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        st_MAXSPEED_BENCH_RESULT r;
        emulator_measuring_maxspeed_report(&r);

        /* Stavový řádek - zda se právě měří */
        if (!EMULATOR_TEST_MAX_SPEED)
        {
            ImGui::TextDisabled("%s", _("Not in MAX SPEED (Alt+M to enable)"));
        }
        else if (r.segment_active)
        {
            ImGui::TextUnformatted(_("Measuring (MAX SPEED active)"));
        }
        else
        {
            ImGui::TextDisabled("%s", _("MAX SPEED paused / stalled"));
        };

        ImGui::Separator();

        if (!r.valid)
        {
            ImGui::TextUnformatted(_("No MAX SPEED time measured yet."));
        }
        else if (ImGui::BeginTable("MaxSpeedBenchTable", 3, ImGuiTableFlags_SizingFixedFit))
        {
            /* Sloupce label i hodnota se sizují podle obsahu (font, délka
             * přeloženého textu). Prostřední sloupec je hardcoded mezera mezi
             * labelem a hodnotou - šíře ~3 mezery aktuálního fontu, takže škáluje
             * s velikostí fontu i lokalizací. Tím se label a hodnota neslijí
             * ani u nejdelšího přeloženého labelu. */
            const float gap = ImGui::CalcTextSize("   ").x;
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Gap", ImGuiTableColumnFlags_WidthFixed, gap);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed);

            char buf[64];
            /* Hodnota pravostranně zarovnaná v rámci svého (obsahem sizovaného)
             * sloupce - čísla jsou tak zarovnaná vpravo pod sebou. */
            auto row = [](const char *label, const char *value)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(label);
                ImGui::TableNextColumn(); /* mezera */
                ImGui::TableNextColumn();
                float textWidth = ImGui::CalcTextSize(value).x;
                float cellLeft = ImGui::GetCursorPosX();
                float cursorX = cellLeft + ImGui::GetColumnWidth() - textWidth;
                if (cursorX > cellLeft)
                    ImGui::SetCursorPosX(cursorX);
                ImGui::TextUnformatted(value);
            };

            snprintf(buf, sizeof(buf), "%.2f s%s", r.total_wall_sec, r.segment_active ? " *" : "");
            row(_("Measured time:"), buf);

            snprintf(buf, sizeof(buf), "%" PRIu64, r.total_ticks);
            row(_("Emulated pxCLK:"), buf);

            snprintf(buf, sizeof(buf), "%.0f pxCLK/s", r.pxclk_per_sec);
            row(_("Throughput:"), buf);

            snprintf(buf, sizeof(buf), "%.2f %%", r.efficiency_percent);
            row(_("Efficiency:"), buf);

            snprintf(buf, sizeof(buf), "%.2f", r.fps);
            row(_("FB-FPS:"), buf);

            if (r.dist_samples > 0)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Separator();
                ImGui::TableNextColumn();
                ImGui::Separator();
                ImGui::TableNextColumn();
                ImGui::Separator();

                snprintf(buf, sizeof(buf), "%u", r.dist_samples);
                row(_("Samples:"), buf);

                snprintf(buf, sizeof(buf), "%.2f / %.2f %%", r.eff_min, r.eff_max);
                row(_("Eff. min / max:"), buf);

                snprintf(buf, sizeof(buf), "%.2f / %.2f %%", r.eff_mean, r.eff_median);
                row(_("Eff. mean / median:"), buf);

                snprintf(buf, sizeof(buf), "%.2f %%", r.eff_stddev);
                row(_("Eff. stddev:"), buf);
            };

            ImGui::EndTable();
        };

        ImGui::Separator();

        if (ImGui::Button(_L("Reset")))
        {
            emulator_measuring_maxspeed_reset();
        };
        ImGui::SameLine();
        if (ImGui::Button(_L("Print to console")))
        {
            emulator_measuring_maxspeed_print();
        };

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            *p_open = false;
        };

        ImGui::End();
    }
}
