/*
 * menu_debugger.cpp — Podmenu "Debugger" / "DbgTool"
 *
 * Sdílená implementace pro dva callsity:
 *  - Hlavní menu emulátoru (caller = DBG_MENU_CALLER_TOPMENU, label
 *    "Debugger"). První položka "MZ-800 Debugger" (Alt+D) toggle-uje
 *    okno debuggeru, oddělena separátorem.
 *  - Menubar hlavního okna debuggeru (caller = DBG_MENU_CALLER_DEBUGGER_WINDOW,
 *    label "DbgTool"). První položka + separator se vynechá - okno
 *    debuggeru je už zobrazené, toggle nedává smysl.
 *
 * Obsahuje položky pro otevření dalších oken debuggeru:
 * - Breakpoints (Alt+B) — okno breakpointů (placeholder)
 * - Memory Map — banking + memext debug okno (per 4 kB stránku)
 * - Memory Browser (Alt+E) — memory browser (placeholder)
 * - Disassembler — disassembler (placeholder)
 *
 * Položky Breakpoints, Memory Browser, Disassembler jsou prozatím
 * disabled — budou implementovány v pozdějších fázích.
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
#include "libs/imgui/imgui.h"
#include "i18n.h"
#include "mzarch/mzarch_config.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "ui-imgui/topmenu/topmenu.h"
#include "debugger/debugger.h"
#if (MZARCH == 800) || (MZARCH == 1500) || (MZARCH == 700)
#include "ui-imgui/debugger/heatmap/mhmap_window.h"
#endif

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

extern "C"
{
    void imgui_menu_debugger(en_DBG_MENU_CALLER caller);
}

void imgui_menu_debugger(en_DBG_MENU_CALLER caller)
{
    /* Label menu se liší podle volajícího (= odlišuje topmenu od debugger
     * window menubaru aby user věděl kde je). */
    const char *menu_label = (caller == DBG_MENU_CALLER_DEBUGGER_WINDOW)
                              ? _L("DbgTool")
                              : _L("Debugger");
    if (ImGui::BeginMenu(menu_label))
    {
        /*
         * Hlavní okno debuggeru — toggle přes debugger_show_hide_main_window().
         * Zkratka Alt+D je obsloužena v global_shortcuts.cpp.
         *
         * Volání z menubaru hlavního okna debuggeru tuto položku vynechá -
         * okno je už zobrazené, toggle nedává smysl.
         */
        if (caller != DBG_MENU_CALLER_DEBUGGER_WINDOW)
        {
            if (ImGui::MenuItem(_L("MZ-800 Debugger"), "Alt+D", g_gui->showDebuggerWindow))
            {
                debugger_show_hide_main_window();
            };
        };

        /*
         * DBG Workplace - perzistované preference, která auxiliary debug
         * okna se otevírají/zavírají automaticky společně s hlavním oknem
         * debuggeru. Pro caller=TOPMENU je toto druhá položka (= za
         * MZ-800 Debugger toggle), pro caller=DEBUGGER_WINDOW je první
         * (= MZ-800 Debugger toggle se vynechává). Separator za submenu
         * je společný divider pro obě variants.
         */
        if (ImGui::BeginMenu(_L("DBG Workplace")))
        {
            bool wp_cpu = (g_debugger.wp_cpu_registers != 0);
            if (ImGui::MenuItem(_L("CPU Registers"), NULL, wp_cpu))
            {
                g_debugger.wp_cpu_registers = wp_cpu ? 0 : 1;
            };
            bool wp_mm = (g_debugger.wp_memory_map != 0);
            if (ImGui::MenuItem(_L("Memory Map"), NULL, wp_mm))
            {
                g_debugger.wp_memory_map = wp_mm ? 0 : 1;
            };
            bool wp_sm = (g_debugger.wp_stack_monitor != 0);
            if (ImGui::MenuItem(_L("Stack Monitor"), NULL, wp_sm))
            {
                g_debugger.wp_stack_monitor = wp_sm ? 0 : 1;
            };
            bool wp_cs = (g_debugger.wp_callstack != 0);
            if (ImGui::MenuItem(_L("Callstack (experimental)"), NULL, wp_cs))
            {
                g_debugger.wp_callstack = wp_cs ? 0 : 1;
            };
            bool wp_bp = (g_debugger.wp_breakpoints != 0);
            if (ImGui::MenuItem(_L("Breakpoints"), NULL, wp_bp))
            {
                g_debugger.wp_breakpoints = wp_bp ? 0 : 1;
            };
            bool wp_w = (g_debugger.wp_watch != 0);
            if (ImGui::MenuItem(_L("Watch"), NULL, wp_w))
            {
                g_debugger.wp_watch = wp_w ? 0 : 1;
            };
            bool wp_p = (g_debugger.wp_profiler != 0);
            if (ImGui::MenuItem(_L("CPU Profiler"), NULL, wp_p))
            {
                g_debugger.wp_profiler = wp_p ? 0 : 1;
            };
            bool wp_bm = (g_debugger.wp_bookmarks != 0);
            if (ImGui::MenuItem(_L("Bookmarks"), NULL, wp_bm))
            {
                g_debugger.wp_bookmarks = wp_bm ? 0 : 1;
            };
            bool wp_sy = (g_debugger.wp_symbols != 0);
            if (ImGui::MenuItem(_L("Symbols"), NULL, wp_sy))
            {
                g_debugger.wp_symbols = wp_sy ? 0 : 1;
            };
            bool wp_va = (g_debugger.wp_variables != 0);
            if (ImGui::MenuItem(_L("Variables"), NULL, wp_va))
            {
                g_debugger.wp_variables = wp_va ? 0 : 1;
            };

            ImGui::Separator();

            /* Disassembly #2..#5 - každé na vlastním řádku jako samostatný
             * checkbox. Label musí být per-instance unikátní pro ImGui ID,
             * proto použijeme různé string konstanty. */
            bool wp_d2 = (g_debugger.wp_disasm_extra[0] != 0);
            if (ImGui::MenuItem(_L("Disassembly #2"), NULL, wp_d2))
            {
                g_debugger.wp_disasm_extra[0] = wp_d2 ? 0 : 1;
            };
            bool wp_d3 = (g_debugger.wp_disasm_extra[1] != 0);
            if (ImGui::MenuItem(_L("Disassembly #3"), NULL, wp_d3))
            {
                g_debugger.wp_disasm_extra[1] = wp_d3 ? 0 : 1;
            };
            bool wp_d4 = (g_debugger.wp_disasm_extra[2] != 0);
            if (ImGui::MenuItem(_L("Disassembly #4"), NULL, wp_d4))
            {
                g_debugger.wp_disasm_extra[2] = wp_d4 ? 0 : 1;
            };
            bool wp_d5 = (g_debugger.wp_disasm_extra[3] != 0);
            if (ImGui::MenuItem(_L("Disassembly #5"), NULL, wp_d5))
            {
                g_debugger.wp_disasm_extra[3] = wp_d5 ? 0 : 1;
            };
            ImGui::EndMenu();
        };

        ImGui::Separator();

        /*
         * CPU Registers (Variant B) - samostatné plovoucí okno s registr fileom,
         * flagy a special sekcí. Toggle přes g_gui->showCpuWindow.
         * Zkratka Alt+Shift+R obsloužena v global_shortcuts.cpp (Alt+R bez
         * Shift kolidoval s NVIDIA Overlay).
         */
        if (ImGui::MenuItem(_L("CPU Registers"), "Alt+Shift+R", g_gui->showCpuWindow))
        {
            g_gui->showCpuWindow = !g_gui->showCpuWindow;
        };
        /*
         * Stack Monitor - samostatné plovoucí okno s hex dumpem paměti
         * kolem aktuálního SP + tlačítkem pro rychlý SP_THRESHOLD BPT
         * (Set BP from SP-256). Toggle přes g_gui->showStackWindow.
         * Zkratka Alt+S obsloužena v global_shortcuts.cpp.
         */
        if (ImGui::MenuItem(_L("Stack Monitor"), "Alt+S", g_gui->showStackWindow))
        {
            g_gui->showStackWindow = !g_gui->showStackWindow;
        };
        /*
         * V8: Stack Regions - samostatné okno s tabulkou stack regionů
         * (1 řádek per region: Name / Base / Limit / SP% / Min / Push /
         * Pop / Trend / [E][R][X] akce). Otevírá se odděleně od Stack
         * Monitor okna (Alt+S), zkratka Alt+Shift+S.
         */
        if (ImGui::MenuItem(_L("Stack Regions"), "Alt+Shift+S",
                             g_gui->showStackRegionsWindow))
        {
            g_gui->showStackRegionsWindow = !g_gui->showStackRegionsWindow;
        };
        /*
         * V9: Stack History - samostatné okno se SP history sparkline.
         * Plot resize s velikostí okna (X i Y). Otevírá se odděleně od
         * Stack Monitor okna (Alt+S), zkratka Alt+Shift+H.
         */
        if (ImGui::MenuItem(_L("Stack History"), "Alt+Shift+H",
                             g_gui->showStackHistoryWindow))
        {
            g_gui->showStackHistoryWindow = !g_gui->showStackHistoryWindow;
        };
        /*
         * Callstack (mutant callstack, Fáze 2C) - samostatné plovoucí okno
         * se shadow stackem volání (CALL/RST/IRQ/NMI frames + divergence
         * counters). Toggle přes g_gui->showCallstackWindow. Aktivace
         * subsystému (g_callstack_active) přes Active checkbox v okně
         * nebo CLI --callstack / INI [CALLSTACK] active = 1.
         */
        if (ImGui::MenuItem(_L("Callstack (experimental)"), NULL, g_gui->showCallstackWindow))
        {
            g_gui->showCallstackWindow = !g_gui->showCallstackWindow;
        };
        /*
         * CPU Profiler (mutant profiler V1) - samostatné plovoucí okno
         * s per-function profilací (Excl/Incl cycles, Calls, Min/Max).
         * Toggle přes g_gui->showProfilerWindow. Aktivace subsystému
         * (g_profiler_active) přes Active checkbox v okně nebo CLI
         * --profiler / INI [PROFILER] active = 1. Zkratka Alt+Shift+P
         * obsloužena v global_shortcuts.cpp (Alt+P = pause toggle, takže
         * profiler je v combo variantě).
         */
        if (ImGui::MenuItem(_L("CPU Profiler"), "Alt+Shift+P", g_gui->showProfilerWindow))
        {
            g_gui->showProfilerWindow = !g_gui->showProfilerWindow;
        };
        /*
         * Bookmarks panel - pojmenované adresové záložky (uživatelské).
         * Klik / RMB v Bookmarks okně otevírá Disassembly na danou adresu.
         */
        if (ImGui::MenuItem(_L("Bookmarks"), NULL, g_gui->showBookmarksWindow))
        {
            g_gui->showBookmarksWindow = !g_gui->showBookmarksWindow;
        };
        /*
         * Okno breakpointů — toggle přes g_gui->showBreakpointsWindow.
         * Zkratka Alt+B je obsloužena v global_shortcuts.cpp.
         */
        if (ImGui::MenuItem(_L("Breakpoints"), "Alt+B", g_gui->showBreakpointsWindow))
        {
            g_gui->showBreakpointsWindow = !g_gui->showBreakpointsWindow;
        };
        /*
         * Variables panel - live $vars storage smart BP (D.6.2).
         * Zkratka Alt+V obsloužena v global_shortcuts.cpp.
         */
        if (ImGui::MenuItem(_L("Variables"), "Alt+V", g_gui->showVarsWindow))
        {
            g_gui->showVarsWindow = !g_gui->showVarsWindow;
        };
        /*
         * Watch panel - user-defined paměťové hlídky (V1 Phase C).
         * Zkratka Alt+Shift+W obsloužena v global_shortcuts.cpp.
         * (Alt+W bez Shift = fix aspect ratio by Width; combo varianta
         * je analogická Alt+Shift+R/S/H pro debugger okna.)
         */
        if (ImGui::MenuItem(_L("Watch"), "Alt+Shift+W", g_gui->showWatchWindow))
        {
            g_gui->showWatchWindow = !g_gui->showWatchWindow;
        };
        /*
         * I/O Ports viewer (V1.5.D rework) - bit-by-bit struct view + History
         * + activity tracking. Zkratka Alt+I obsloužena v global_shortcuts.cpp.
         */
        if (ImGui::MenuItem(_L("I/O Ports"), "Alt+I", g_gui->showIoWindow))
        {
            g_gui->showIoWindow = !g_gui->showIoWindow;
        };
        /*
         * Memory Map - banking + memext debug okno (per 4 kB stránku).
         * Toggle přes g_gui->showMemoryMapWindow. Bez globální zkratky V0.
         */
        if (ImGui::MenuItem(_L("Memory Map"), NULL, g_gui->showMemoryMapWindow))
        {
            g_gui->showMemoryMapWindow = !g_gui->showMemoryMapWindow;
        };
        /*
         * Events - real-time pohled na eventlog ring (Vlna 1 = Log tab,
         * Vlna 2 = Strip tab). V módu WHEN_WINDOW_OPEN otevírá ring
         * recording, zavírá ho zase při zavření okna.
         * Toggle přes g_gui->showEventViewerWindow. Bez globální zkratky.
         */
        if (ImGui::MenuItem(_L("Events"), NULL, g_gui->showEventViewerWindow))
        {
            g_gui->showEventViewerWindow = !g_gui->showEventViewerWindow;
        };
        /*
         * Symbol Browser (D.8) - load NoICE / sdldz80 .map / sjasmplus .sym
         * + user write-back .lbl. Disassembler ukazuje jmena misto hex.
         */
        if (ImGui::MenuItem(_L("Symbols"), NULL, g_gui->showSymbolsWindow))
        {
            g_gui->showSymbolsWindow = !g_gui->showSymbolsWindow;
        };
        ImGui::MenuItem(_L("Memory Browser"), "Alt+E", false, false);
        ImGui::MenuItem(_L("Disassembler"), NULL, false, false);

        /*
         * Submenu "Other disassembly" - 4 sekundární Disassembly okna
         * (#2 - #5). Každé je nezávislá instance DisassembledView bez
         * horní historie a bez animace na PC. Toggle přes
         * g_gui->showDisasmExtraWindow[N]. Render v main_window.cpp.
         */
        if (ImGui::BeginMenu(_L("Other disassembly")))
        {
            if (ImGui::MenuItem(_L("Disassembly #2"), NULL,
                                g_gui->showDisasmExtraWindow[0]))
            {
                g_gui->showDisasmExtraWindow[0] = !g_gui->showDisasmExtraWindow[0];
            };
            if (ImGui::MenuItem(_L("Disassembly #3"), NULL,
                                g_gui->showDisasmExtraWindow[1]))
            {
                g_gui->showDisasmExtraWindow[1] = !g_gui->showDisasmExtraWindow[1];
            };
            if (ImGui::MenuItem(_L("Disassembly #4"), NULL,
                                g_gui->showDisasmExtraWindow[2]))
            {
                g_gui->showDisasmExtraWindow[2] = !g_gui->showDisasmExtraWindow[2];
            };
            if (ImGui::MenuItem(_L("Disassembly #5"), NULL,
                                g_gui->showDisasmExtraWindow[3]))
            {
                g_gui->showDisasmExtraWindow[3] = !g_gui->showDisasmExtraWindow[3];
            };
            ImGui::EndMenu();
        };

#if (MZARCH == 800) || (MZARCH == 1500) || (MZARCH == 700)
        ImGui::Separator();

        /*
         * Memory Heatmap (CDL) - samostatné okno pro vizualizaci access counterů.
         * Toggle přes g_gui->showMemoryHeatmapWindow.
         */
        if (ImGui::MenuItem(_L("Memory Heatmap"), NULL, g_gui->showMemoryHeatmapWindow))
        {
            mhmap_window_show_hide();
        };
#endif

        ImGui::EndMenu();
    };
}

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
