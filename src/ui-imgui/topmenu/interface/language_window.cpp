/**
 * @file language_window.cpp
 * @brief Dialog pro výběr jazyka UI
 *
 * Menu → Interface → Language...
 */

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#include "libs/imgui/imgui.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "ui-imgui/bootstrap/images.h"
#include "ui-imgui/auto_layout.h"

// Lokalizace
#include "i18n.h"

extern "C"
{
#include "emulator/i18n_lang.h"
#include "emulator/cfgmain.h"
    void imgui_language_window(void);
}

void imgui_language_window(void)
{
    if (!g_gui->showLanguageWindow)
        return;

    const char *title = _("Language");

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    /* Auto-layout při fresh open. */
    auto_layout_first_use_portrait(title, 350.0f, 400.0f);
    if (!ImGui::Begin(title, &g_gui->showLanguageWindow, flags))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("%s:", _("Select language"));
    ImGui::Separator();
    ImGui::Spacing();

    en_I18N_LANG current = g_i18n_lang_settings.language;
    bool changed = false;

    /* Výška vlajky pro zarovnání s textem */
    const float flag_display_h = ImGui::GetTextLineHeight();
    const float flag_display_w = flag_display_h * 1.5f; /* poměr stran vlajky ~3:2 */

    for (int i = 0; i < I18N_LANG_COUNT; i++)
    {
        const st_I18N_LANG_INFO *info = &g_i18n_lang_info[i];

        /* Vlajka před radio buttonem */
        if (info->flag_image)
        {
            ui_glimage_t *flag = imgui_images_get_image_by_name(info->flag_image);
            if (flag)
            {
                ImGui::Image(flag->texture, ImVec2(flag_display_w, flag_display_h));
                ImGui::SameLine();
            }
        }

        /* Popisek: pro AUTO je speciální, pro ostatní nativní název */
        char label[128];
        if (info->id == I18N_LANG_AUTO)
        {
            snprintf(label, sizeof(label), "%s (auto)", _("System default"));
        }
        else
        {
            snprintf(label, sizeof(label), "%s", info->native_name);
        }

        if (ImGui::RadioButton(label, current == info->id))
        {
            if (current != info->id)
            {
                current = info->id;
                changed = true;
            }
        }
    }

    if (changed)
    {
        i18n_lang_set(current);
    }

    ImGui::End();
}
