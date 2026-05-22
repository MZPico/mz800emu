#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <glib.h>

#include "libs/imgui/imgui.h"
#include "libs/igfd/ImGuiFileDialog.h"
#include "baseui/baseui_filechooser.h"

// Lokalizace
#include "i18n.h"

bool igfd_ToggleButton(const char *vLabel, bool *vToggled)
{
    bool pressed = false;

    if (vToggled && *vToggled)
    {
        // ImVec4 bua = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        //  ImVec4 buh = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
        //  ImVec4 bu = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        //  ImVec4 te = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        // ImVec4 te = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
        // ImGui::PushStyleColor(ImGuiCol_Button, te);
        // ImGui::PushStyleColor(ImGuiCol_ButtonActive, te);
        // ImGui::PushStyleColor(ImGuiCol_ButtonHovered, te);
        // ImGui::PushStyleColor(ImGuiCol_Text, bua);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
    }

    pressed = ImGui::Button(vLabel);

    if (vToggled && *vToggled)
    {
        // ImGui::PopStyleColor(4); //-V112
        ImGui::PopStyleColor();
    }

    if (vToggled && pressed)
        *vToggled = !*vToggled;

    return pressed;
}