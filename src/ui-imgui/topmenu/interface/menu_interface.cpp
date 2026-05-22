#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "libs/imgui/imgui.h"
#include "ui-imgui/bootstrap/myimgui.h"

// Lokalizace
#include "i18n.h"

extern "C"
{
#include "emulator/i18n_lang.h"
    void imgui_menu_interface(void);
}

#include "ui-imgui/topmenu/topmenu.h"

void imgui_menu_interface(void)
{
    if (ImGui::BeginMenu(_L("Interface")))
    {

        imgui_menu_display();

        if (ImGui::MenuItem(_L("Audio..."), NULL))
        {
            g_gui->showAudioWindow = true;
        };

        imgui_menu_keyjoy();

        ImGui::Separator();

        /* Položka jazyk: v neangličtině přidáme "(Language)" za lokalizovaný název */
        {
            const char *lang_label = _("Language...");
            char lang_buf[128];
            if (strcmp(lang_label, "Language...") != 0)
            {
                /* Lokalizovaný název + anglický v závorce: "Jazyk... (Language)" */
                /* Odstraníme "..." z konce pro závorku */
                char base[64];
                snprintf(base, sizeof(base), "%s", lang_label);
                char *dots = strstr(base, "...");
                if (dots) *dots = '\0';
                snprintf(lang_buf, sizeof(lang_buf), "%s (Language)...", base);
                lang_label = lang_buf;
            }

            if (ImGui::MenuItem(lang_label, NULL))
            {
                g_gui->showLanguageWindow = true;
            };
        }

        ImGui::EndMenu();
    };
}
