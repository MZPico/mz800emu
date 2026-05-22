#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

#include "libs/imgui/imgui.h"

// Lokalizace
#include "i18n.h"

#include "ui-imgui/topmenu/topmenu.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "ui-imgui/mywidgets/mywidgets.h"

extern "C"
{
    void imgui_menu_keyjoy(void);
}

void imgui_menu_keyjoy(void)
{
    if (ImGui::BeginMenu(_L("Keyboard & Joystick")))
    {
        if (ImGui::MenuItem(_L("Virtual Keyboard..."), NULL))
        {
            g_gui->showVirtualKeyboardWindow = !g_gui->showVirtualKeyboardWindow;
        };

        if (ImGui::MenuItem(_L("Autotype Buffer"), NULL, g_gui->showAutotypeWindow))
        {
            g_gui->showAutotypeWindow = !g_gui->showAutotypeWindow;
        };

        ImGui::Separator();

        if (ImGui::MenuItem(_L("Joystick Setup..."), NULL))
        {
            g_gui->showJoystickSetupWindow = true;
        };

        ImGui::EndMenu();
    };
}
