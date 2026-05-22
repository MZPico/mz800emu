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

#if CFG_HWEXT_HAVE_FDC
#include "hw-generic/fdc/fdc.h"
#endif

extern "C"
{
    void imgui_menu_fdcontroller(void);
    void imgui_fdc_storage_switch_popup(bool *p_open);
}

/* --- State pro modal "Switch storage mode with pending RAM changes" --- */
typedef struct fdc_storage_switch_pending_t
{
    int drive_id;
    char target_mode[16]; /* "cached" | "direct" */
} fdc_storage_switch_pending_t;

static fdc_storage_switch_pending_t g_fdc_storage_switch_pending = { -1, "" };

/**
 * @brief Aktivuj modal popup pro confirm switch storage mode s dirty RAM.
 *
 * Voláno z menu radio handleru když user přepíná z Discard mode s pending
 * RAM changes na Cached/Direct. Popup nabídne: Save & Switch / Discard &
 * Switch / Cancel.
 */
static void imgui_fdc_storage_switch_activate(int drive_id, const char *target_mode)
{
    g_fdc_storage_switch_pending.drive_id = drive_id;
    g_strlcpy(g_fdc_storage_switch_pending.target_mode, target_mode,
              sizeof(g_fdc_storage_switch_pending.target_mode));
    g_gui->showFdcStorageSwitchWindow = true;
}

/**
 * @brief Provedení skutečného přepnutí storage mode (= zapiš cfgkey + remount).
 *
 * Tato pomocná funkce sdílí kód mezi okamžitým switchem (= bez popupu)
 * a popup "Switch" tlačítky.
 */
static void imgui_fdc_storage_apply_switch(int drive_id, const char *target_mode)
{
    fdc_cfg_set_storage_mode((unsigned)drive_id, target_mode);
    if (FDC_TEST_DRIVE_ID_MOUNTED(drive_id))
    {
        char *pathdup = g_strdup(fdc_get_dsk_filepath((unsigned)drive_id));
        if (pathdup)
        {
            fdc_mount_dskfile((unsigned)drive_id, pathdup);
            g_free(pathdup);
        }
    }
}

void imgui_fdc_storage_switch_popup(bool *p_open)
{
    int drv = g_fdc_storage_switch_pending.drive_id;
    if (drv < 0 || drv >= 4)
    {
        *p_open = false;
        return;
    }

    ImGui::OpenPopup(_L("FDC: Unsaved RAM changes"));

    if (ImGui::BeginPopupModal(_L("FDC: Unsaved RAM changes"), NULL,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped(_("Drive %d is in Discard mode and has unsaved RAM changes."), drv);
        ImGui::TextWrapped(_("Switching to '%s' mode will close the current RAM buffer."),
                           g_fdc_storage_switch_pending.target_mode);
        ImGui::Separator();
        ImGui::TextWrapped(_("What to do with the changes?"));
        ImGui::Spacing();

        /* Spočítej širku tlačítek podle text obsahu před ## (= _L macro
         * suffix který ImGui interně ořezává pro render, ale CalcTextSize
         * ho měří celý). Všechna tři tlačítka dostanou max šířku pro symetrii. */
        const char *btn_save    = _L("Save and switch");
        const char *btn_discard = _L("Switch and discard");
        const char *btn_cancel  = _L("Cancel");

        const ImGuiStyle &style = ImGui::GetStyle();
        float btn_pad = style.FramePadding.x * 2.0f + 8.0f; /* extra margin */
        auto label_width = [&](const char *s) -> float {
            const char *id = strstr(s, "##");
            const char *end = id ? id : s + strlen(s);
            return ImGui::CalcTextSize(s, end).x + btn_pad;
        };
        float w_save = label_width(btn_save);
        float w_disc = label_width(btn_discard);
        float w_canc = label_width(btn_cancel);
        float button_width = w_save;
        if (w_disc > button_width) button_width = w_disc;
        if (w_canc > button_width) button_width = w_canc;

        float spacing = style.ItemSpacing.x;
        float total_width = button_width * 3 + spacing * 2;
        float cursor_x = (ImGui::GetWindowSize().x - total_width) * 0.5f;
        if (cursor_x < style.WindowPadding.x) cursor_x = style.WindowPadding.x;
        ImGui::SetCursorPosX(cursor_x);

        if (ImGui::Button(btn_save, ImVec2(button_width, 0)))
        {
            fdc_drive_force_save_to_file((unsigned)drv);
            imgui_fdc_storage_apply_switch(drv, g_fdc_storage_switch_pending.target_mode);
            *p_open = false;
            g_fdc_storage_switch_pending.drive_id = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(btn_discard, ImVec2(button_width, 0)))
        {
            imgui_fdc_storage_apply_switch(drv, g_fdc_storage_switch_pending.target_mode);
            *p_open = false;
            g_fdc_storage_switch_pending.drive_id = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(btn_cancel, ImVec2(button_width, 0)))
        {
            *p_open = false;
            g_fdc_storage_switch_pending.drive_id = -1;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void SubmenuFDDriveImage(int drive_id)
{
#if CFG_HWEXT_HAVE_FDC

    GString *image_basename = g_string_new(NULL);

    if (FDC_TEST_DRIVE_ID_MOUNTED(drive_id))
    {
        const char *basename = g_path_get_basename(fdc_get_dsk_filepath(drive_id));
        // pokud ma vice jak 20 znaku, tak zkratime a na konec pridame "..." - aby se veslo do menu
        if (strlen(basename) > 20)
        {
            g_string_printf(image_basename, "%s...", g_utf8_substring(basename, 0, 20));
        }
        else
        {
            g_string_assign(image_basename, basename);
        };
        g_free((gpointer)basename);
    } 
    else
    {
        g_string_assign(image_basename, _("No image"));
    };

    ImGui::MenuItem(image_basename->str, NULL, false, false);
    
    ImGui::Separator();

    GString *mount_accel = g_string_new(NULL);
    g_string_append_printf(mount_accel, "Alt+%d", drive_id);
    
    GString *umount_accel = g_string_new(NULL);
    g_string_append_printf(umount_accel, "Alt+Shift+%d", drive_id);

    const char *mount_text = (!FDC_TEST_DRIVE_ID_MOUNTED(drive_id)) ? _("Mount...") : _("Re-Mount...");

    if (ImGui::MenuItem(mount_text, mount_accel->str, false))
    {
        fdc_ui_mount(drive_id);
    };

    if (ImGui::MenuItem(_L("Umount"), umount_accel->str, false, FDC_TEST_DRIVE_ID_MOUNTED(drive_id)))
    {
        fdc_umount(drive_id);
    };

    /* --- Per-drive R/O + Storage mode --- */
    bool is_mounted = FDC_TEST_DRIVE_ID_MOUNTED(drive_id);
    bool fs_ro = fdc_drive_fs_readonly(drive_id) != 0;
    int user_ro_cfg = fdc_cfg_get_readonly(drive_id);

    ImGui::Separator();

    /* R/O checkbox: pokud FS read-only, zobraz disabled s pevně ON + popisek.
     * Persistent user pref se touto cestou nemění. */
    if (fs_ro)
    {
        bool forced_ro = true;
        ImGui::BeginDisabled();
        ImGui::MenuItem(_L("Read-only (FS write-protected)"), NULL, &forced_ro, false);
        ImGui::EndDisabled();
    }
    else
    {
        bool ro_pref = (user_ro_cfg != 0);
        if (ImGui::MenuItem(_L("Read-only"), NULL, &ro_pref, true))
        {
            fdc_cfg_set_readonly(drive_id, ro_pref ? 1 : 0);
            /* Pro reflexi změny: pokud je drive aktuálně mounted, remount
             * aplikuje novou volbu. */
            if (is_mounted)
            {
                char *path = (char *)fdc_get_dsk_filepath(drive_id);
                if (path)
                {
                    /* g_strdup aby nám remount nezneplatnil path. */
                    char *pathdup = g_strdup(path);
                    fdc_mount_dskfile(drive_id, pathdup);
                    g_free(pathdup);
                }
            }
        }
    }

    /* Storage mode: combo přes 3 radio položky. */
    const char *mode_str = fdc_cfg_get_storage_mode(drive_id);
    bool mode_cached  = (strcmp(mode_str, "cached") == 0);
    bool mode_direct  = (strcmp(mode_str, "direct") == 0);
    bool mode_discard = (strcmp(mode_str, "discard") == 0);

    ImGui::SeparatorText(_("Storage mode"));

    /* Pomocná lambda: switch storage mode s detekcí "dirty Discard → save mode".
     * Pokud je to případ, otevře modal popup; jinak okamžitý switch. */
    auto try_switch = [&](const char *target) {
        bool needs_prompt = is_mounted && mode_discard
                            && (strcmp(target, "discard") != 0)
                            && (fdc_drive_has_ram_changes(drive_id) != 0);
        if (needs_prompt)
        {
            imgui_fdc_storage_switch_activate(drive_id, target);
        }
        else
        {
            imgui_fdc_storage_apply_switch(drive_id, target);
        }
    };

    if (MenuRadioItem(_("Cached (sync at reset/umount/exit)"), mode_cached))
    {
        if (!mode_cached) try_switch("cached");
    }

    if (MenuRadioItem(_("Direct (write-through to file)"), mode_direct))
    {
        if (!mode_direct) try_switch("direct");
    }

    if (MenuRadioItem(_("Discard changes (in-RAM, no sync)"), mode_discard))
    {
        if (!mode_discard) try_switch("discard");
    }

    /* Sync now - dostupné jen v cached s pending changes. */
    bool sync_enabled = is_mounted && mode_cached
                        && (fdc_drive_has_unsaved_changes(drive_id) != 0);
    if (ImGui::MenuItem(_L("Sync now"), NULL, false, sync_enabled))
    {
        fdc_sync_drive(drive_id);
    }

    g_string_free(image_basename, TRUE);
    g_string_free(mount_accel, TRUE);
    g_string_free(umount_accel, TRUE);
#endif
}

void SubmenuFDDrive(int drive_id)
{
#if CFG_HWEXT_HAVE_FDC
    GString *drive_name = g_string_new(_("FDD "));
    g_string_append_printf(drive_name, "%d", drive_id);

    if (FDC_TEST_WD279X_CONNECTED && (!FDC_TEST_DRIVE_ID_MOUNTED(drive_id)))
    {
        g_string_append(drive_name," ");
        g_string_append(drive_name, _("(Empty)"));
    };

    if (ImGui::BeginMenu(drive_name->str, FDC_TEST_WD279X_CONNECTED))
    {
        SubmenuFDDriveImage(drive_id);
        ImGui::EndMenu();
    };
    g_string_free(drive_name, TRUE);
#endif
}

#if CFG_HWEXT_HAVE_FDC
/**
 * @brief Submenu pro HW volby FDC chipu.
 *
 * Sekce "Hardware options" v hlavním FDC menu. Obsahuje:
 *  - Checkbox HD Patch (port 0xDF EINT obvod).
 *  - Radio bus_xlate (invert = Sharp default / passthrough = experimental).
 *
 * Změna se NEAPLIKUJE za běhu - obě volby jsou čtené při inicializaci
 * chipu. UI pouze přepíše hodnoty v `g_fdc.*` (které se uloží do INI
 * při exitu) a zobrazí "restart required" tooltip.
 */
static void SubmenuFDCHardwareOptions(void)
{
    ImGui::SeparatorText(_("Hardware options"));

    bool hd_patch = (g_fdc.hd_patch != 0);
    if (ImGui::MenuItem(_L("HD Patch enabled"), NULL, &hd_patch, true))
    {
        g_fdc.hd_patch = hd_patch ? 1 : 0;
    };
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _("Restart emulator to apply change."));

    bool xlate_invert = (g_fdc.bus_xlate == FDC_BUS_XLATE_INVERT);
    if (MenuRadioItem(_("Bus translation: invert (Sharp)"), xlate_invert))
    {
        g_fdc.bus_xlate = FDC_BUS_XLATE_INVERT;
    };
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", _("Sharp MZ default - XOR 0xFF between CPU bus and WD279x chip. "
                                   "Restart emulator to apply change."));

    if (MenuRadioItem(_("Bus translation: passthrough"), !xlate_invert))
    {
        g_fdc.bus_xlate = FDC_BUS_XLATE_PASSTHROUGH;
    };
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(_("NOT compatible with Sharp MZ - no inversion between CPU bus "
                                  "and WD279x chip. Reserved for future 'true bus' ROMs / experiments. "
                                  "Restart emulator to apply change."));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
#endif

void imgui_menu_fdcontroller(void)
{
    if (ImGui::BeginMenu(_L("FD Controller"), CFG_HWEXT_HAVE_FDC))
    {
#if CFG_HWEXT_HAVE_FDC
        /*
         *
         * Submenu FDC
         *
         */

        if (MenuRadioItem(_("Not Connected"), FDC_TEST_NOT_CONNECTED))
        {
            FDC_SET_NOT_CONNECTED();
        };

        if (MenuRadioItem(_("WD279x"), FDC_TEST_WD279X_CONNECTED))
        {
            FDC_SET_WD279X_CONNECTED();
        };

        ImGui::SeparatorText(_("Floppy Drives"));

        for (int i = 0; i < 4; i++)
        {
            SubmenuFDDrive(i);
        };

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
        ImGui::SeparatorText(_("Tools"));

        if (ImGui::MenuItem(_L("FDC State (debugger)..."), NULL, g_gui->showFdcStateWindow, true))
        {
            g_gui->showFdcStateWindow = !g_gui->showFdcStateWindow;
        };
#endif

        /* Sekce Hardware options - HD Patch + bus translation polarity. */
        SubmenuFDCHardwareOptions();
#endif
        ImGui::EndMenu();
    };
}
