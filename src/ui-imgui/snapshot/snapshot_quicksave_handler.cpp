/**
 * @file snapshot_quicksave_handler.cpp
 * @brief Zpracování Quick Save/Load požadavků z menu
 */

#include "main.h"
#include <stdio.h>
#include <glib.h>

#include "libs/imgui/imgui.h"
#include "ui-imgui/bootstrap/myimgui.h"

// Lokalizace
#include "i18n.h"

#include "emulator.h"
#include "ui-imgui/debugger/dbgapi_helpers.h"

extern "C"
{
#include "snapshot/snapshot.h"
#include "snapshot/snapshot_quicksave.h"
}

/* Deklarace z menu_snapshot.cpp */
extern "C" bool snapshot_ui_is_quicksave_requested(void);
extern "C" void snapshot_ui_clear_quicksave_request(void);
extern "C" bool snapshot_ui_is_quickload_requested(void);
extern "C" void snapshot_ui_clear_quickload_request(void);
extern "C" bool snapshot_ui_was_running_before_pause(void);

/* Deklarace z snapshot_notification.cpp */
extern "C" void snapshot_notification_show(const char *message, bool is_error);


static void restore_emulation_after_save(void)
{
    if (snapshot_ui_was_running_before_pause())
        dbg_ui_run();
}


static void restore_emulation_after_quickload(bool load_succeeded)
{
    int mode = g_snapshot_settings.quickload_resume_mode;
    if (!load_succeeded) {
        /* Po selhání loadu nechat emulátor v pauze */
        return;
    }
    switch (mode) {
        case SNAPSHOT_RESUME_ALWAYS_RUN:
            dbg_ui_run();
            break;
        case SNAPSHOT_RESUME_ALWAYS_PAUSE:
            /* Nechat v pauze */
            break;
        case SNAPSHOT_RESUME_PREVIOUS:
        default:
            if (snapshot_ui_was_running_before_pause())
                dbg_ui_run();
            break;
    }
}


extern "C" void imgui_snapshot_quicksave_handler(void)
{
    if (snapshot_ui_is_quicksave_requested()) {
        snapshot_ui_clear_quicksave_request();

        en_SNAPSHOT_RESULT result = snapshot_quicksave();
        if (result == SNAPSHOT_OK) {
            snapshot_notification_show(_("Quick Save OK"), false);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "%s: %s", _("Quick Save failed"),
                     snapshot_result_to_string(result));
            snapshot_notification_show(msg, true);
        }
        restore_emulation_after_save();
    }

    if (snapshot_ui_is_quickload_requested()) {
        snapshot_ui_clear_quickload_request();

        en_SNAPSHOT_RESULT result = snapshot_quickload();
        if (result == SNAPSHOT_OK) {
            snapshot_notification_show(_("Quick Load OK"), false);
            restore_emulation_after_quickload(true);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "%s: %s", _("Quick Load failed"),
                     snapshot_result_to_string(result));
            snapshot_notification_show(msg, true);
            restore_emulation_after_quickload(false);
        }
    }
}
