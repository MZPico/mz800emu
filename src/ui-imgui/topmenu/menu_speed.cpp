#include "main.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

#include "libs/imgui/imgui.h"

// Lokalizace
#include "i18n.h"

#include "emulator.h"
#include "customspeed.h"
#include "topmenu.h"

#define UI_CUSTOMSPEED_SLIDER_MIN 1
#define UI_CUSTOMSPEED_SLIDER_MAX 400

extern "C"
{
    void imgui_menu_speed(void);
} // extern "C"

void imgui_menu_speed(void)
{
    // Sekce Speed
    if (ImGui::BeginMenu(_L("Speed")))
    {
        if (ImGui::MenuItem(_L("Normal CPU Speed"), "Alt+N", EMULATOR_TEST_NORMAL_SPEED))
        {
            emulator_switch_to_normal_speed();
        };

        if (ImGui::MenuItem(_L("Custom CPU Speed"), "Alt+Shift+M", EMULATOR_TEST_CUSTOM_SPEED))
        {
            emulator_switch_to_custom_speed();
        };

        if (ImGui::MenuItem(_L("Max CPU Speed"), "Alt+M", EMULATOR_TEST_MAX_SPEED))
        {
            emulator_max_speed(!EMULATOR_TEST_MAX_SPEED);
        };

        ImGui::Separator();

        if (ImGui::BeginMenu(_L("Relative Adjust Speed by %")))
        {
            if (ImGui::MenuItem(_L("Increase - step 1 %"), "Alt+UpArrow"))
            {
                customspeed_step_up_request(1);
            };

            if (ImGui::MenuItem(_L("Decrease - step 1 %"), "Alt+DownArrow"))
            {
                customspeed_step_down_request(1);
            };

            ImGui::Separator();

            if (ImGui::MenuItem(_L("Increase - step 10 %"), "Alt+Shift+UpArrow"))
            {
                customspeed_step_up_request(10);
            };

            if (ImGui::MenuItem(_L("Decrease - step 10 %"), "Alt+Shift+DownArrow"))
            {
                customspeed_step_down_request(10);
            };

            ImGui::Separator();

            if (ImGui::MenuItem(_L("Increase - step 100 %"), "Alt+PageUp"))
            {
                customspeed_step_up_request(100);
            };

            if (ImGui::MenuItem(_L("Decrease - step 100 %"), "Alt+PageDown"))
            {
                customspeed_step_down_request(100);
            };

            ImGui::EndMenu();
        };

        if (ImGui::BeginMenu(_L("Custom Speed Options")))
        {
            imgui_customspeed_setup();
            ImGui::EndMenu();
        };

        ImGui::EndMenu();
    };
}
