/**
 * @file dsk_create_window.cpp
 * @brief Informační dialog - DSK tools přemístěny do samostatné aplikace mzdisk
 *
 * Původní implementace DSK Create dialog (= GUI pro vytváření Extended CPC
 * DSK image souborů s předdefinovanými geometriemi) byla odstraněna. Funkce
 * je nyní v samostatné aplikaci mzdisk: https://github.com/michalhucik/mzdisk
 *
 * Toto okno už jen zobrazí informaci s URL + tlačítka Copy URL / Open URL / OK.
 */

#include "main.h"
#include "libs/imgui/imgui.h"
#include "i18n.h"
#include "ui-imgui/auto_layout.h"

#include "dsk_create_window.h"

/* URL na samostatnou aplikaci mzdisk - definováno jako konstanta pro
 * konzistentní použití v Copy URL + Open URL handlerech. */
static const char *DSK_MZDISK_URL = "https://github.com/michalhucik/mzdisk";


extern "C" void imgui_dsk_create_window(bool *p_open)
{
    if (!p_open || !*p_open) return;

    /* Multi-viewport ImGui aktivní - TopMost flag zajistí, že SDL platform
     * window bude vždy nahoře v OS Z-orderu (= nezakryjí ho main / terminál). */
    {
        ImGuiWindowClass wc;
        wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_TopMost;
        ImGui::SetNextWindowClass(&wc);
    }

    /* Pevná rozumná šířka aby TextWrapped nepoint-wrapoval na slovo. */
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28.0f, 0.0f),
                              ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                             ImGuiCond_Appearing,
                             ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoDocking;

    /* Auto-layout při fresh open - cache _L() do lokální proměnné.
     * Šířka 760: spodní řádek má 3 tlačítka btn_w = font_size * 8,
     * při fontu 28 px = 3 * 224 + spacing ≈ 690 px, plus URL string
     * (~520 px) má vykreslit bez wrap. Výška 240: titlebar +
     * 2 řádky textu + URL + separátor + tlačítka. Okno má NoResize,
     * takže default musí být dimenzovaný podle obsahu. */
    const char *dsk_title = _L("DSK Tools - mzdisk###dsk_info_window");
    auto_layout_first_use_portrait(dsk_title, 760.0f, 240.0f);
    if (!ImGui::Begin(dsk_title, p_open, flags))
    {
        ImGui::End();
        return;
    };

    ImGui::TextWrapped("%s",
        _("DSK image tools have moved to a separate standalone application "
          "called mzdisk."));
    ImGui::Spacing();
    ImGui::TextWrapped("%s",
        _("Project page:"));
    ImGui::Spacing();

    /* URL zobrazená monospace fontem pro snadnější přečtení / výběr. */
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 180, 255, 255));
    ImGui::TextUnformatted(DSK_MZDISK_URL);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Tří-tlačítkový řádek vpravo zarovnaný: [Copy URL] [Open URL] [OK] */
    float btn_w = ImGui::GetFontSize() * 8.0f;
    float total_w = btn_w * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > total_w) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - total_w));
    };

    if (ImGui::Button(_L("Copy URL"), ImVec2(btn_w, 0)))
    {
        ImGui::SetClipboardText(DSK_MZDISK_URL);
        /* Okno NEzavírat - uživatel může pak ještě kliknout Open URL nebo OK. */
    };
    ImGui::SameLine();
    if (ImGui::Button(_L("Open URL"), ImVec2(btn_w, 0)))
    {
        /* ImGui platform helper - na Windows volá ShellExecuteW open,
         * na Linux xdg-open. Default implementace registrovaná v
         * ImGuiContext při init. */
        if (ImGui::GetPlatformIO().Platform_OpenInShellFn) {
            ImGui::GetPlatformIO().Platform_OpenInShellFn(
                ImGui::GetCurrentContext(), DSK_MZDISK_URL);
        };
        *p_open = false;
    };
    ImGui::SameLine();
    if (ImGui::Button(_L("OK"), ImVec2(btn_w, 0)))
    {
        *p_open = false;
    };
    ImGui::SetItemDefaultFocus();

    /* ESC zavře okno (= jako klik na OK). */
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        *p_open = false;
    };

    ImGui::End();
}
