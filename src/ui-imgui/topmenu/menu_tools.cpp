#include "main.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

#include "libs/imgui/imgui.h"
#include "ui-imgui/bootstrap/myimgui.h"

// Lokalizace
#include "i18n.h"

#include "emulator.h"
#include "version_check/version_check.h"

extern "C"
{
    void imgui_menu_tools(void);
} // extern "C"

void imgui_menu_tools(void)
{
    // Sekce Speed
    if (ImGui::BeginMenu(_L("Tools")))
    {
        if (ImGui::MenuItem(_L("DSK Images..."), NULL, false, true))
        {
            g_gui->showDskCreateWindow = true;
        };

        if (ImGui::BeginMenu(_L("Version Check")))
        {
            if (ImGui::MenuItem(_L("Make Online Version Check")))
            {
                version_check_run_test(true);
            };

            if (ImGui::MenuItem(_L("Version Check Setup")))
            {
                g_gui->showVersionCheckSetupWindow = true;
            };

            ImGui::EndMenu();
        };

        ImGui::EndMenu();
    };
}
