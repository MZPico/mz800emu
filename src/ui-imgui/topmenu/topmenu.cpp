#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>
#include "libs/imgui/imgui.h"
#include "ui-imgui/bootstrap/myimgui.h"

// Lokalizace
#include "i18n.h"

#include "emulator.h"
#include "mzarch/mzarch_platform_functions.h"
// #include "iface/iface_video.h"
#include "topmenu.h"
#include "ui-imgui/debugger/dbgapi_helpers.h"

// #if HAVE_JOY
// #include "iface/iface_joy.h"
// #endif

// #ifdef MZ800EMU_CFG_UI_ENABLED
// #include "ui-gtk3/ui_main.h"
// #include "ui-gtk3/ui_cmt.h"
// #include "ui-gtk3/vkbd/ui_vkbd.h"
// #else
// #define ui_cmt_window_show_hide()
// #define ui_vkbd_show_hide()
// #endif

// #ifdef MZ800EMU_CFG_UI_ENABLED
// #include "ui-gtk3/ui_main.h"
// #include "ui-gtk3/ui_cmt.h"
// #include "ui-gtk3/vkbd/ui_vkbd.h"
// #else
// #define ui_cmt_window_show_hide()
// #define ui_vkbd_show_hide()
// #endif

// #if CFG_HWEXT_HAVE_FDC
// #include "hw-generic/fdc/fdc.h"
// #endif /* CFG_HWEXT_HAVE_FDC */

// #include "hw-generic/pio8255/pio8255.h"

// #if HAVE_JOY
// #include "hw-generic/joy/joy.h"
// #endif

// #ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
// #include "debugger/debugger.h"

// #ifdef MZ800EMU_CFG_UI_ENABLED
// #include "ui-gtk3/debugger/ui_breakpoints.h"
// #include "ui-gtk3/debugger/ui_membrowser.h"
// #include "ui-gtk3/debugger/ui_dissassembler.h"
// #include "ui-gtk3/ui_main.h"
// #define TEST_HOTKEYS_DISABLED (g_ui.disable_hotkeys)
// #else
// #define debugger_show_hide_main_window()
// #define ui_breakpoints_show_hide_window()
// #define ui_membrowser_show_hide()
// #define ui_dissassembler_show_hide_window()
// #define ui_dissassembler_show_window()
// #define TEST_HOTKEYS_DISABLED 0
// #endif /* MZ800EMU_CFG_UI_ENABLED */
// #else
// #define TEST_HOTKEYS_DISABLED 0
// #endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */

extern "C"
{
    void imgui_topmenu_handler(void);
    void imgui_topmenu_body(void);
} // extern "C"

void imgui_topmenu_body(void)
{
    imgui_menu_devices();
    imgui_menu_interface();
    imgui_menu_speed();
    imgui_menu_emulator();
    imgui_menu_snapshot();
    ImGui::Separator();

    if ((ImGui::MenuItem(_L("Pause Emulation"), "Alt+P", EMULATOR_TEST_PAUSED)) || ImGui::Shortcut(ImGuiMod_Alt | ImGuiKey_P))
    {
        dbg_ui_pause_toggle();
    };

    if ((ImGui::MenuItem(_L("Reset"), "F12")) || ImGui::Shortcut(ImGuiKey_F12))
    {
        mzarch_platform_fn_reset_request();
    }

    ImGui::Separator();

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
    imgui_menu_debugger(DBG_MENU_CALLER_TOPMENU);
    ImGui::Separator();
#endif

    imgui_menu_tools();

    ImGui::Separator();

    if (ImGui::MenuItem(_L("About...")))
    {
        g_gui->showAboutWindow = true;
    };

    ImGui::EndPopup();
}

void imgui_topmenu_handler(void) // pouzivame pokud je emulator zobrazen jako pozadi
{
    if (ImGui::BeginPopupContextVoid("EmulatorPopupMenu", ImGuiPopupFlags_MouseButtonRight))
    {
        imgui_topmenu_body();
    }
}
