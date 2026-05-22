#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

#include "libs/imgui/imgui.h"

// Lokalizace
#include "i18n.h"

#include "ui-imgui/topmenu/topmenu.h"

#include "hw-generic/unicard/unicard.h"
#include "baseui/baseui_filechooser.h"
#include "emulator/mzarch/mzarch_platform.h"
#include "ui-imgui/message/message_window.h"

extern "C"
{
    void imgui_menu_unicard(void);
    void imgui_unicard_runtime_mismatch_dialog(void);
    void imgui_unicard_reinit_choice_dialog(void);
}


/* State pro pending Reinit-choice dialog (MZ-800 only). Nastaveno
 * v menu po kliknutí "Reinitialize Runtime"; UI dialog je otevřen
 * v dalším frame. */
static bool s_unicard_reinit_choice_pending = false;


/* Pomocna - human-readable nazev FW pro dialog text. */
static const char *unicard_fw_label(en_UNICARD_FW fw)
{
    return (fw == UNICARD_FW_UC1) ? "uc1 (rev.60)" : "uc3 (v0.26)";
}


/* Pomocna - co je v SD podle pending detekce. Pokrývá všechny en_UNICARD_RUNTIME_CHECK
 * stavy které mohou triggerovat dialog (DETECTED_UC*, UNKNOWN, NO_DIR). */
static const char *unicard_runtime_label(int detected)
{
    switch (detected)
    {
    case UNICARD_RUNTIME_CHECK_DETECTED_UC1: return "uc1 (mgr V2.4)";
    case UNICARD_RUNTIME_CHECK_DETECTED_UC3: return "uc3 (mgr V2.11b)";
    case UNICARD_RUNTIME_CHECK_UNKNOWN:      return "incomplete / inconsistent";
    case UNICARD_RUNTIME_CHECK_NO_DIR:       return "missing '{SD}/unicard/' subdir";
    default:                                  return "unknown";
    }
}


/* Modal dialog s nabidkou {Reinit, Continue, Cancel} pri mismatch
 * mezi SD runtime a aktualni FW volbou. Vola se z imgui_message_window
 * call-site (= per-frame, v main_window render path). */
void imgui_unicard_runtime_mismatch_dialog(void)
{
    if (g_unicard_runtime_mismatch_pending == 0) return;

    /* Otevre popup pri prvnim frame s pending flagem. */
    ImGui::OpenPopup("Unicard runtime mismatch##unicard_rt_mismatch");

    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Unicard runtime mismatch##unicard_rt_mismatch",
                               NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const char *detected = unicard_runtime_label(g_unicard_runtime_mismatch_pending);
        const char *selected = unicard_fw_label(unicard_get_fw());

        ImGui::TextWrapped("%s",
            _("SD card runtime in '{SD}/unicard/' is missing, incomplete, "
              "or does not match the selected firmware."));
        ImGui::Spacing();
        ImGui::Text("%s %s", _("State on SD:"), detected);
        ImGui::Text("%s %s", _("Selected firmware:"), selected);
        ImGui::Spacing();
        ImGui::TextWrapped("%s",
            _("[Reinitialize] overwrites the SD manager files to match the "
              "selected firmware. [Continue] connects with the mismatch "
              "(client may see inconsistent behavior). [Cancel] leaves "
              "the Unicard disconnected."));
        ImGui::Separator();

        if (ImGui::Button(_L("Reinitialize##unicard_rt_reinit"), ImVec2(120, 0)))
        {
            /* Smaze stary MGR + nainstaluje aktualni FW manager + cfg. */
            if (EXIT_SUCCESS == unicard_reinit_sd_for_current_fw())
            {
                g_unicard_runtime_mismatch_pending = 0;
                ImGui::CloseCurrentPopup();
                /* Pripojit s ciste prepisem - ted match. */
                unicard_set_connected(UNICARD_CONNECTION_CONNECTED);
            }
            else
            {
                /* Reinit selhal - zobrazime error a necháme dialog
                 * otevreny ať user zkusí Cancel. */
                imgui_message_create(MESSAGE_TYPE_ERROR,
                    _("Failed to reinitialize SD runtime."));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(_L("Continue##unicard_rt_continue"), ImVec2(120, 0)))
        {
            g_unicard_runtime_mismatch_pending = 0;
            ImGui::CloseCurrentPopup();
            /* Pripojit s zachovanim stareho runtime (mismatch). Klient
             * uvidi staré MGR ale dispatch běží podle nové FW volby.
             * One-shot bypass aby se znovu neotevřel mismatch dialog. */
            unicard_bypass_runtime_check_once();
            unicard_set_connected(UNICARD_CONNECTION_CONNECTED);
        }
        ImGui::SameLine();
        if (ImGui::Button(_L("Cancel##unicard_rt_cancel"), ImVec2(120, 0)))
        {
            g_unicard_runtime_mismatch_pending = 0;
            ImGui::CloseCurrentPopup();
            /* Necháme Unicard disconnected. */
        }
        ImGui::EndPopup();
    }
}


/* Modal dialog na MZ-800 pri rucnim Reinit Runtime z menu - nabidne
 * uzivateli volbu mgr V2.4 (uc1) vs V2.11b (uc3). Volba je nezavisla
 * na aktualnim FW radio (user muze explicitne chtit jinou verzi pro
 * testovani). Vola se per-frame z main_window render path. */
void imgui_unicard_reinit_choice_dialog(void)
{
    if (!s_unicard_reinit_choice_pending) return;

    ImGui::OpenPopup("Reinitialize Unicard runtime##unicard_reinit_choice");

    if (ImGui::BeginPopupModal("Reinitialize Unicard runtime##unicard_reinit_choice",
                               NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("%s",
            _("Choose which manager version to install in '{SD}/unicard/'. "
              "This will overwrite the existing manager files."));
        ImGui::Spacing();
        ImGui::TextWrapped("%s",
            _("Note: This choice is independent of the currently selected "
              "simulated firmware. You can install one version while "
              "simulating the other (useful for testing)."));
        ImGui::Separator();

        /* Auto-sized buttons (ImVec2(0,0)) - ImGui spočítá šířku dle
         * textu + padding. Pevná šířka 160 px useknula label
         * "mgr V2.11b (uc3)". Dialog se přizpůsobí (AlwaysAutoResize). */
        if (ImGui::Button(_L("mgr V2.4 (uc1)##unicard_reinit_v24")))
        {
            s_unicard_reinit_choice_pending = false;
            ImGui::CloseCurrentPopup();
            if (EXIT_SUCCESS == unicard_reinit_sd_for_fw(UNICARD_FW_UC1))
            {
                imgui_message_create(MESSAGE_TYPE_INFO,
                    _("Unicard runtime reinstalled with mgr V2.4 (uc1)."));
            }
            else
            {
                imgui_message_create(MESSAGE_TYPE_WARNING,
                    _("Unicard runtime reinitialization failed."));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(_L("mgr V2.11b (uc3)##unicard_reinit_v211b")))
        {
            s_unicard_reinit_choice_pending = false;
            ImGui::CloseCurrentPopup();
            if (EXIT_SUCCESS == unicard_reinit_sd_for_fw(UNICARD_FW_UC3))
            {
                imgui_message_create(MESSAGE_TYPE_INFO,
                    _("Unicard runtime reinstalled with mgr V2.11b (uc3)."));
            }
            else
            {
                imgui_message_create(MESSAGE_TYPE_WARNING,
                    _("Unicard runtime reinitialization failed."));
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(_L("Cancel##unicard_reinit_cancel")))
        {
            s_unicard_reinit_choice_pending = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void imgui_menu_unicard(void)
{
    if (ImGui::BeginMenu(_L("Unicard")))
    {
        /* Label se mění podle stavu (= užitečnější než pevný "Connected"
         * s checkboxem - user okamžitě vidí v menu zda je připojeno). */
        const char *conn_label = UNICARD_TEST_IS_CONNECTED
                                  ? _L("Unicard Connected")
                                  : _L("Unicard Not Connected");
        if (ImGui::MenuItem(conn_label, NULL, UNICARD_TEST_IS_CONNECTED))
        {
            bool was_connected = UNICARD_TEST_IS_CONNECTED;
            unicard_set_connected(was_connected ? UNICARD_CONNECTION_DISCONNECTED : UNICARD_CONNECTION_CONNECTED);
        };

        ImGui::Separator();

        /* Radio simulované verze Unicard firmware. uc1 (originální rev.60,
         * mgr V2.4) je dostupný JEN pro MZ-800 (= jediná platforma kde
         * byl uc1 ekosystém vůbec použit). uc3 (komunitní fork v0.26,
         * mgr V2.11b) pro všechny platformy. Default = uc3.
         *
         * Na ne-MZ-800 platformách radio nezobrazujeme vůbec - uc1 tu
         * není volba (= zbytečný UI noise), uc3 je jediná možnost
         * (= radio s 1 položkou nedává smysl). FW volba se tam drží
         * tichá v g_unicard.fw_emulated = UC3.
         *
         * Při změně FW se vynutí disconnect+reconnect (viz unicard_set_fw),
         * protože dispatch logika několika UNIMGR příkazů závisí na FW
         * verzi. */
        if (g_mzarch_platform_numeric == 800)
        {
            en_UNICARD_FW current_fw = unicard_get_fw();

            bool is_uc1 = (current_fw == UNICARD_FW_UC1);
            if (ImGui::MenuItem(_L("Firmware: uc1 (rev.60)"), NULL, is_uc1))
            {
                if (!is_uc1) unicard_set_fw(UNICARD_FW_UC1);
            };

            bool is_uc3 = (current_fw == UNICARD_FW_UC3);
            /* Sestavit dynamický label s aktuální uc3 verzí (drží stabilní
             * ID přes ###suffix). */
            char uc3_label[64];
            snprintf(uc3_label, sizeof(uc3_label),
                     _("Firmware: uc3 (v%d.%d)###unicard_fw_uc3"),
                     UNICARD_FW_UC3_MAJOR, UNICARD_FW_UC3_MINOR);
            if (ImGui::MenuItem(uc3_label, NULL, is_uc3))
            {
                if (!is_uc3) unicard_set_fw(UNICARD_FW_UC3);
            };
        }

        bool runtime_check = unicard_get_runtime_check_on_connect() ? true : false;
        if (ImGui::MenuItem(_L("Check runtime version on connect"), NULL, runtime_check))
        {
            unicard_set_runtime_check_on_connect(runtime_check ? 0 : 1);
        };
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s",
                _("On Connect, verify that {SD}/unicard/ contains the manager "
                  "matching the selected firmware. Mismatch shows a dialog "
                  "with option to reinitialize."));
        };

        /* R/O režim SD karty - blokuje zápisové operace z emulovaného
         * programu. Volitelné per uživatel, persistentní v ini souboru.
         * Lze togglovat za běhu i bez disconnect/connect. */
        bool ro = unicard_test_readonly() ? true : false;
        if (ImGui::MenuItem(_L("Read-only SD"), NULL, ro))
        {
            unicard_set_readonly(ro ? 0 : 1);
        };

        ImGui::Separator();

        if (ImGui::MenuItem(_L("Set SD Root Directory..."), NULL, false, !UNICARD_TEST_IS_CONNECTED))
        {
            unicard_ui_select_sd_root_directory();
        };

        /* Manuální reinicializace runtime souborů v {SD}/unicard/.
         * Na MZ-800: zobrazí dialog s volbou mgr 2.4 / 2.11b (= volba
         * je nezávislá na FW radio, user může explicitně chtít druhou
         * verzi pro testovací scénáře).
         * Na ne-MZ-800: bez dialogu, přepíše aktuální V2.11b verzí.
         *
         * Enabled vždy pokud máme platný SD root path - reinit nepotřebuje
         * aktivní Connect (volá unicard_reinit_sd_for_fw která pracuje
         * přímo s INI sd_root). Naopak je užitečná i pokud user zrušil
         * Connect po mismatch dialogu a chce SD přepsat před dalším
         * pokusem. */
        char *sd_root_now = unicard_get_sd_root_dirpath();
        bool reinit_enabled = (sd_root_now != NULL && sd_root_now[0] != '\0');
        if (ImGui::MenuItem(_L("Reinitialize Runtime..."), NULL, false, reinit_enabled))
        {
            if (g_mzarch_platform_numeric == 800)
            {
                /* MZ-800: dialog 2.4 / 2.11 / Cancel */
                s_unicard_reinit_choice_pending = true;
            }
            else
            {
                /* Ostatní mzarch: jen jedna varianta (V2.11b uc3),
                 * provedeme rovnou s overwrite. */
                if (EXIT_SUCCESS == unicard_reinit_sd_for_fw(UNICARD_FW_UC3))
                {
                    imgui_message_create(MESSAGE_TYPE_INFO,
                        _("Unicard runtime files in '{SD}/unicard/' have been reinitialized."));
                }
                else
                {
                    imgui_message_create(MESSAGE_TYPE_WARNING,
                        _("Unicard runtime reinitialization failed."));
                };
            }
        };
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s",
                _("Overwrites manager files in '{SD}/unicard/' (mgr.mzf, "
                  "mgr800.mzf, mgr700.mzf, mgr1500.mzf) with the selected "
                  "version. On MZ-800 prompts for mgr V2.4 vs V2.11b."));
        };

        ImGui::EndMenu();
    };
}
