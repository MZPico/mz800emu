#include "main.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

#include "libs/imgui/imgui.h"

// Lokalizace
#include "i18n.h"

#include "emulator.h"
#include "emulator_measuring.h"
#include "ui-imgui/topmenu/topmenu.h"

extern "C"
{
    void imgui_menu_emulator(void);
}

void imgui_menu_emulator(void)
{
    if (ImGui::BeginMenu(_L("Emulator")))
    {
        if (ImGui::MenuItem(_L("Measuring of GDG Speed..."), NULL, EMULATOR_MEASURING_TEST_GDG_ENABLED))
        {
            EMULATOR_MEASURING_SET_GDG_ENABLED_AND_UPDATE_TIME_SEC(!EMULATOR_MEASURING_TEST_GDG_ENABLED, EMULATOR_MEASURING_GET_GDG_UPDATE_TIME_SEC());
        };

        if (ImGui::MenuItem(_L("Frame Timing Accuracy..."), NULL, EMULATOR_MEASURING_TEST_FRAME_TIMING_ENABLED))
        {
            if (!EMULATOR_MEASURING_TEST_FRAME_TIMING_ENABLED)
            {
                emulator_measuring_frame_timing_reset();
            }
            EMULATOR_MEASURING_SET_FRAME_TIMING_ENABLED(!EMULATOR_MEASURING_TEST_FRAME_TIMING_ENABLED);
        };

        ImGui::Separator();

        if (ImGui::MenuItem(_L("Development Mode"), NULL, EMULATOR_TEST_DEVELOPMENT_MODE))
        {
            emulator_set_development_mode(!EMULATOR_TEST_DEVELOPMENT_MODE);
        };

        if (EMULATOR_TEST_DEVELOPMENT_MODE)
        {
            ImGui::Separator();

            if (ImGui::MenuItem(_L("ImGui Demo"), NULL, g_emulator.show_demo_window))
            {
                g_emulator.show_demo_window = !(g_emulator.show_demo_window);
            };
        };

        ImGui::EndMenu();
    };
}
