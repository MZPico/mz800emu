/**
 * @file chip_window_helpers.cpp
 * @brief Sdílené ImGui helpery pro per-chip debug okna - implementace.
 *
 * Viz `chip_window_helpers.h` pro kontrakt a popis sloupců.
 *
 * License: GPLv3.
 */

#include "libs/imgui/imgui.h"
#include "i18n.h"
#include "chip_window_helpers.h"

void render_hex8_row(const char *name, uint8_t value, const char *tooltip)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(name);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("0x%02X", value);
    ImGui::TableSetColumnIndex(2);
    /* Binární reprezentace pro bit-fieldy, MSB vlevo. */
    char binbuf[10];
    for (int i = 7; i >= 0; --i)
        binbuf[7 - i] = (value & (1u << i)) ? '1' : '0';
    binbuf[8] = '\0';
    ImGui::Text("%s", binbuf);
}

void render_hex16_row(const char *name, uint16_t value, const char *tooltip)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(name);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("0x%04X", value);
    ImGui::TableSetColumnIndex(2);
    /* Decimální zápis 16-bit hodnoty - binární by mělo 16 znaků (nepřehledné). */
    ImGui::Text("%u", (unsigned)value);
}

void render_dec_row(const char *name, unsigned value, const char *tooltip)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(name);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%u", value);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextDisabled("-");
}

void render_flag_row(const char *name, int value, const char *tooltip)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(name);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    ImGui::TableSetColumnIndex(1);
    if (value)
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "1");
    else
        ImGui::TextDisabled("0");
    ImGui::TableSetColumnIndex(2);
    ImGui::TextDisabled("-");
}

void render_enum_row(const char *name, unsigned value, const char *const *names,
                     unsigned count, const char *tooltip)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(name);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    ImGui::TableSetColumnIndex(1);
    /* Předpokládáme, že prvky pole jsou označené N_() v místě definice,
     * takže byly extrahovány do .pot. Runtime překlad přes _() zde. */
    if (value < count && names != nullptr && names[value] != nullptr)
        ImGui::TextUnformatted(_(names[value]));
    else
        ImGui::TextDisabled("???");
    ImGui::TableSetColumnIndex(2);
    ImGui::Text("%u", value);
}
