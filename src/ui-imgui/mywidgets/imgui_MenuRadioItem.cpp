#include "main.h"
#include <stdbool.h>
#include "libs/imgui/imgui.h"
#include "libs/imgui/imgui_internal.h"

// Lokalizace
#include "i18n.h"

// Lokalne lze prvek zakazat pouzitim ImGui::BeginDisabled() a ImGui::EndDisabled()

bool MenuRadioItem(const char *label, bool selected)
{
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext &g = *GImGui;
    ImGuiStyle &style = g.Style;
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    // Šířky sloupců — radio kolečko jde do icon sloupce
    float radio_w = g.FontSize;
    float checkmark_w = IM_TRUNC(g.FontSize * 1.20f);
    float min_w = window->DC.MenuColumns.DeclColumns(radio_w, label_size.x, 0.0f, checkmark_w);
    float stretch_w = ImMax(0.0f, ImGui::GetContentRegionAvail().x - min_w);

    // Jeden Selectable přes celou šířku — klik + highlight pokrývá vše
    const ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_SelectOnRelease
                                                | ImGuiSelectableFlags_NoSetKeyOwner
                                                | ImGuiSelectableFlags_SetNavIdOnHover;

    ImGui::PushID(label);
    bool pressed = ImGui::Selectable("", false, selectable_flags | ImGuiSelectableFlags_SpanAvailWidth,
                                     ImVec2(min_w, label_size.y));

    if (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible)
    {
        const ImGuiMenuColumns *offsets = &window->DC.MenuColumns;
        ImVec2 text_pos(pos.x, pos.y + window->DC.CurrLineTextBaseOffset);

        // Vykreslení labelu
        ImGui::RenderText(ImVec2(text_pos.x + offsets->OffsetLabel, text_pos.y), label);

        // Vykreslení radio kolečka v icon sloupci
        float radius = g.FontSize * 0.35f;
        ImVec2 center(pos.x + offsets->OffsetIcon + radio_w * 0.5f,
                      pos.y + label_size.y * 0.5f);
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);

        // Vnější kolečko (vždy)
        window->DrawList->AddCircle(center, radius, col, 12, 1.5f);

        // Vnitřní plné kolečko (jen pokud selected)
        if (selected)
            window->DrawList->AddCircleFilled(center, radius * 0.55f, col, 12);
    }

    ImGui::PopID();
    return pressed;
}
