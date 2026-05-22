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
#include "topmenu.h"
#include "ui-imgui/debugger/dbgapi_helpers.h"

extern "C"
{
    void imgui_menu_snapshot(void);
} // extern "C"

/* Stav pro řízení save/load procesu */
static bool s_snapshot_save_requested = false;
static bool s_snapshot_load_requested = false;
static bool s_snapshot_quicksave_requested = false;
static bool s_snapshot_quickload_requested = false;
static bool s_was_running_before_pause = false;

/* Přístup k flagům z jiných modulů */
extern "C" bool snapshot_ui_is_save_requested(void) { return s_snapshot_save_requested; }
extern "C" bool snapshot_ui_is_load_requested(void) { return s_snapshot_load_requested; }
extern "C" bool snapshot_ui_is_quicksave_requested(void) { return s_snapshot_quicksave_requested; }
extern "C" bool snapshot_ui_is_quickload_requested(void) { return s_snapshot_quickload_requested; }

extern "C" void snapshot_ui_clear_save_request(void) { s_snapshot_save_requested = false; }
extern "C" void snapshot_ui_clear_load_request(void) { s_snapshot_load_requested = false; }
extern "C" void snapshot_ui_clear_quicksave_request(void) { s_snapshot_quicksave_requested = false; }
extern "C" void snapshot_ui_clear_quickload_request(void) { s_snapshot_quickload_requested = false; }

extern "C" bool snapshot_ui_was_running_before_pause(void) { return s_was_running_before_pause; }

/* Pomocná funkce pro pause + nastavení requestu */
static void snapshot_pause_and_request(bool *request_flag)
{
    s_was_running_before_pause = !EMULATOR_TEST_PAUSED;
    if (s_was_running_before_pause)
        dbg_ui_pause();
    *request_flag = true;
}

/* Veřejné funkce pro spouštění snapshot akcí (z global_shortcuts) */
extern "C" void snapshot_ui_request_load(void) { snapshot_pause_and_request(&s_snapshot_load_requested); }
extern "C" void snapshot_ui_request_save(void) { snapshot_pause_and_request(&s_snapshot_save_requested); }
extern "C" void snapshot_ui_request_quickload(void) { snapshot_pause_and_request(&s_snapshot_quickload_requested); }
extern "C" void snapshot_ui_request_quicksave(void) { snapshot_pause_and_request(&s_snapshot_quicksave_requested); }


void imgui_menu_snapshot(void)
{
    if (ImGui::BeginMenu(_L("Snapshot")))
    {
        if (ImGui::MenuItem(_L("Load Snapshot..."), "Alt+F7"))
        {
            snapshot_pause_and_request(&s_snapshot_load_requested);
        }

        if (ImGui::MenuItem(_L("Save Snapshot..."), "Alt+F6"))
        {
            snapshot_pause_and_request(&s_snapshot_save_requested);
        }

        ImGui::Separator();

        if (ImGui::MenuItem(_L("Quick Load"), "Alt+F9"))
        {
            snapshot_pause_and_request(&s_snapshot_quickload_requested);
        }

        if (ImGui::MenuItem(_L("Quick Save"), "Alt+F8"))
        {
            snapshot_pause_and_request(&s_snapshot_quicksave_requested);
        }

        ImGui::Separator();

        if (ImGui::MenuItem(_L("Snapshot Setup...")))
        {
            g_gui->showSnapshotSetupWindow = true;
        }

        ImGui::EndMenu();
    }
}
