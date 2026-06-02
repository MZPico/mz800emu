#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

#include "libs/imgui/imgui.h"

// Lokalizace
#include "i18n.h"

#include "ui-imgui/topmenu/topmenu.h"

extern "C"
{
    void imgui_menu_devices(void);
}

void imgui_menu_devices(void)
{
    // Sekce Devices
    if (ImGui::BeginMenu(_L("HW & Devices")))
    {
        imgui_menu_cmt();
        imgui_menu_fdcontroller();

        imgui_menu_ramdisk();
        imgui_menu_qdisk();
        imgui_menu_unicard();
        imgui_menu_ide8();
        imgui_menu_memext();
        imgui_menu_rom();

#if (MZARCH == 800) || (MZARCH == 1500)
        // Centronics tiskárna existuje jen na architekturách se Z80 PIO
        imgui_menu_centronics();
#endif

        imgui_menu_mz800_dip_switch();

#if MZARCH == 800
        ImGui::Separator();
        imgui_menu_mz800_hw_compat();
#endif

        ImGui::EndMenu();
    };
}
