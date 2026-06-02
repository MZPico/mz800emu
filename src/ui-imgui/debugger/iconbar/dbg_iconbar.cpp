/*
 * dbg_iconbar.cpp — Iconbar (toolbar) hlavního okna debuggeru
 *
 * Toolbar ve spodní části okna debuggeru. Tlačítka jsou ikonová (24×24 px),
 * ikony se načítají přes images.c z ui_resources/imgui/images/debugger/.
 * Pokud ikona chybí, použije se textový fallback.
 *
 * Obsahuje 3 skupiny tlačítek. Veškerá akce řízení emulace probíhá přes
 * dbgapi CMDRQ kanál (helpery dbg_ui_* z dbgapi_helpers.h) - UI vlákno
 * jen submituje příkaz, vlastní logika je v EMU dispatcheru
 * (src/emulator/debugger/dbgapi.c).
 *
 * SKUPINA 1 — ŘÍZENÍ EMULACE
 * ===========================
 * - Continue (F5): dbg_ui_run() -> CMD_RUN -> emulator_pause(false)
 * - Pause (Ctrl+F5): dbg_ui_pause() -> CMD_PAUSE -> emulator_pause(true)
 *
 * SKUPINA 2 — KROKOVÁNÍ
 * =====================
 * - Step Over (F8): dbg_ui_step_over() -> CMD_STEP_OVER. Detekce
 *   CALL/RST/blokových instrukcí + nastavení temp BP na addr+length
 *   řeší EMU dispatcher (dbgapi_emu_do_step_over).
 * - Step Into (F7): dbg_ui_step_into() -> CMD_STEP_INTO -> debugger_step_call(1)
 * - Run to Cursor (F4): dbg_ui_run_to(addr) -> CMD_RUN_TO. Cíl: adresa
 *   selektovaného řádku disassembly (g_dbg_ui.selected_addr).
 *
 * SKUPINA 3 — DALŠÍ OKNA (PLACEHOLDER)
 * =====================================
 * - Breakpoints (Alt+B): Otevře okno breakpointů
 * - Memory Browser (Alt+E): Otevře memory browser (zatím neimplementováno)
 * - Disassembler (Alt+Shift+D): Otevře samostatné Disassembler okno V1.
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

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include "libs/imgui/imgui.h"
#include "i18n.h"
#include "emulator/emulator.h"
#include "debugger/debugger.h"
#include "mzarch/mzarch.h"
#include "libs/cpu-z80/z80.h"

#include "dbg_iconbar.h"
#include "../debugger_state.h"
#include "../dbgapi_helpers.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "ui-imgui/bootstrap/images.h"

#include <stdint.h>


/*
 * Velikost ikon v iconbaru.
 * Ikony jsou 24×24 pixelové obrázky načtené přes images.c,
 * zobrazené v 1,5× velikosti (36×36) pro lepší viditelnost.
 */
static const ImVec2 ICON_SIZE = ImVec2(36, 36);


/*
 * dbg_icon_button — tlačítko s ikonou v iconbaru
 *
 * Vykreslí ImGui::ImageButton() s ikonou z images cache.
 * Pokud ikona není nalezena (chybí soubor), použije textový fallback.
 *
 * Styl tlačítka:
 * - V klidu: transparentní pozadí (ikona splývá s pozadím okna)
 * - Při hover: tlačítko se "rozsvítí" (standardní hover barva)
 * - Při stisku: standardní active barva
 *
 * Parametry:
 *   str_id    — ImGui identifikátor tlačítka (např. "##dbg_continue")
 *   img_name  — název obrázku v images cache (např. "dbg-continue")
 *   fallback  — textový fallback pokud ikona chybí
 *
 * Návratová hodnota: true pokud bylo tlačítko stisknuto
 */
static bool dbg_icon_button(const char *str_id, const char *img_name, const char *fallback)
{
    ui_glimage_t *img = imgui_images_get_image_by_name(img_name);
    if (img && img->texture)
    {
        /* Transparentní pozadí v klidu — ikona splývá s oknem */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        bool pressed = ImGui::ImageButton(str_id, (ImTextureID)(intptr_t)img->texture, ICON_SIZE);
        ImGui::PopStyleColor();
        return pressed;
    };
    /* Fallback — textové tlačítko pokud ikona nebyla nalezena */
    return ImGui::Button(fallback);
}


/*
 * dbg_do_step_over — Step Over (F8) přes dbgapi.
 *
 * Logika rozpoznání CALL/RST/blokových instrukcí + nastavení temp BP
 * je v EMU straně dispatcheru (DBGAPI_CMD_STEP_OVER, dbgapi.c +
 * dbgapi_emu_do_step_over). UI vlákno jen odešle CMDRQ a čeká na
 * potvrzení.
 *
 * Pokud emulace běží, dispatcher ji pozastaví (CMD_STEP_OVER má
 * tu logiku zabudovanou).
 */
static void dbg_do_step_over(void)
{
    dbg_ui_step_over();
}


/*
 * dbg_do_run_to_cursor — Run to Cursor (F4) přes dbgapi CMD_RUN_TO.
 *
 * Cíl: adresa SELEKTOVANÉHO řádku v disasm (= selected_addr), ne
 * focus_addr. selected_addr se cachuje v render funkci disasm sekce
 * každý frame.
 *
 * Safety: pokud selected_addr == pc, return. UI tlačítko + F4 keybind
 * jsou v iconbaru disabled v této situaci, ale tato kontrola je
 * defenzivní (= ochrana proti race conditions / double-trigger).
 *
 * Pokud emulace běží, dispatcher CMD_RUN_TO ji pozastaví (UX shodné
 * s předchozí inline implementací).
 */
static void dbg_do_run_to_cursor(void)
{
    if (g_dbg_ui.selected_addr == g_mzarch_main.cpu->pc)
        return;

    dbg_ui_run_to(g_dbg_ui.selected_addr);
}


/*
 * Vertikální separátor mezi skupinami tlačítek v iconbaru.
 * Přidává mezery a vertikální čáru pro vizuální oddělení.
 */
static void dbg_iconbar_separator(void)
{
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();
}


void dbg_iconbar_render(void)
{
    /* Mezera 5px nad separátorem */
    ImGui::Dummy(ImVec2(0, 5.0f));
    ImGui::Separator();
    /* Mezera 5px pod separátorem */
    ImGui::Dummy(ImVec2(0, 5.0f));

    bool is_paused = EMULATOR_TEST_PAUSED;
    bool has_focus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    /*
     * SKUPINA 1 — Řízení emulace
     *
     * Continue (F5): pokračovat v emulaci (nebo pozastavit, pokud běží).
     * Pause (Ctrl+F5): pozastavit emulaci.
     */

    /* Continue — disabled pokud emulace běží (už je v "continue" stavu).
     * step_call flag je v paused stavu už 0 (mzarch.c:582 paused loop ho
     * vynuluje při entry), takže CMD_RUN -> emulator_pause(false) nevyrobí
     * regrese vůči staré dvojici debugger_step_call(0); emulator_pause(false). */
    ImGui::BeginDisabled(!is_paused);
    if (dbg_icon_button("##dbg_continue", "dbg-continue", "Continue") ||
        (has_focus && ImGui::IsKeyPressed(ImGuiKey_F5, false) && !ImGui::GetIO().KeyCtrl))
    {
        dbg_ui_run();
    };
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s (F5)", _("Continue emulation"));
    ImGui::EndDisabled();

    ImGui::SameLine();

    /* Pause — disabled pokud emulace už je pozastavena */
    ImGui::BeginDisabled(is_paused);
    if (dbg_icon_button("##dbg_pause", "dbg-pause", "Pause") ||
        (has_focus && ImGui::IsKeyPressed(ImGuiKey_F5, false) && ImGui::GetIO().KeyCtrl))
    {
        dbg_ui_pause();
    };
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s (Ctrl+F5)", _("Pause emulation"));
    ImGui::EndDisabled();

    /*
     * SKUPINA 2 — Krokování
     *
     * Step Over (F8): přeskočí CALL/RST/blokové instrukce
     * Step Into (F7): provede jednu instrukci (vstoupí do podprogramů)
     * Run to Cursor (F4): běží do adresy na aktivním řádku
     *
     * Všechna tlačítka jsou disabled pokud emulace běží (animační režim).
     */
    dbg_iconbar_separator();

    ImGui::BeginDisabled(!is_paused);

    if (dbg_icon_button("##dbg_step_over", "dbg-step-over", "Step Over") ||
        (has_focus && is_paused && ImGui::IsKeyPressed(ImGuiKey_F8, false)))
    {
        dbg_do_step_over();
    };
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s (F8)", _("Step over CALL/RST/block instructions"));

    ImGui::SameLine();

    if (dbg_icon_button("##dbg_step_into", "dbg-step-into", "Step Into") ||
        (has_focus && is_paused && ImGui::IsKeyPressed(ImGuiKey_F7, false)))
    {
        dbg_ui_step_into();
    };
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s (F7)", _("Execute single instruction"));

    ImGui::SameLine();

    /* Run To Cursor: navíc disabled pokud kurzor leží přímo na PC.
     * Cíl by se rovnal aktuálnímu PC → emu by stejně musela proběhnout
     * celý cyklus zpět nebo nedělat nic. Lepší UX = tlačítko vizuálně
     * disable + F4 keybind ignorovat. Vnořené BeginDisabled (uvnitř
     * vnějšího is_paused) - pokud vnější disable, vnitřní ho nezruší. */
    bool cursor_at_pc = (g_dbg_ui.selected_addr == g_mzarch_main.cpu->pc);
    ImGui::BeginDisabled(cursor_at_pc);

    if (dbg_icon_button("##dbg_run_to_cursor", "dbg-run-to-cursor", "Run to Cursor") ||
        (has_focus && is_paused && !cursor_at_pc && ImGui::IsKeyPressed(ImGuiKey_F4, false)))
    {
        dbg_do_run_to_cursor();
    };
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        if (cursor_at_pc)
            ImGui::SetTooltip("%s (F4) - %s",
                              _("Run to cursor address"),
                              _("disabled: cursor is at current PC"));
        else
            ImGui::SetTooltip("%s (F4)", _("Run to cursor address"));
    };

    ImGui::EndDisabled();

    ImGui::EndDisabled();

    /*
     * SKUPINA 3 — Další okna (placeholder)
     *
     * Breakpoints, Memory Browser, Disassembler — budou implementovány
     * jako samostatná okna v pozdějších fázích.
     */
    dbg_iconbar_separator();

    /* Placeholder tlačítka — zatím disabled (kromě Breakpoints) */

    if (dbg_icon_button("##dbg_breakpoints", "dbg-breakpoints", "BP"))
    {
        g_gui->showBreakpointsWindow = !g_gui->showBreakpointsWindow;
    };
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s (Alt+B)", _("Breakpoints window"));

    ImGui::SameLine();

    /*
     * Memory Map iconbar tlačítko bylo v review V0 zrušeno - okno se
     * otevírá z hlavního topmenu emulátoru (Debugger -> Memory Map).
     */

    /* Memory Browser - V0 hex MVP (membrowser mutant). */
    if (dbg_icon_button("##dbg_memory", "dbg-memory", "MEM"))
    {
        g_gui->showMemoryBrowserWindow = !g_gui->showMemoryBrowserWindow;
    };
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s (Alt+E)", _("Memory Browser window"));

    ImGui::SameLine();

    /* Disassembler V1 - samostatné range-based disasm okno. */
    if (dbg_icon_button("##dbg_disasm", "dbg-disasm", "DASM"))
    {
        g_gui->showDisassemblerWindow = !g_gui->showDisassemblerWindow;
    };
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s (Alt+Shift+D)", _("Disassembler window"));
}

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
