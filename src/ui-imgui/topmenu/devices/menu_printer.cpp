#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

#include "libs/imgui/imgui.h"

// Lokalizace
#include "i18n.h"

#include "ui-imgui/topmenu/topmenu.h"
#include "emulator/hw-generic/centronics/centronics.h"

extern "C"
{
    void imgui_menu_centronics(void);
}

/**
 * Položka menu pro virtuální Centronics tiskárnu.
 *
 * Obsahuje:
 *  - přepínač "Capture Enabled" (zapne/vypne handshake + capture),
 *  - stavový řádek s názvem aktuálního capture souboru a počtem bajtů,
 *  - akci "Close Capture File" (uzavře soubor; další bajt založí nový).
 */
void imgui_menu_centronics(void)
{
    if (ImGui::BeginMenu(_L("Centronics Printer")))
    {
        bool active = centronics_get_active();
        if (ImGui::MenuItem(_L("Capture Enabled"), NULL, active))
        {
            centronics_set_active(!active);
        };

        ImGui::Separator();

        // Stav capture souboru
        if (g_centronics.fp != NULL)
        {
            ImGui::Text(_("Capture file: %s"), g_centronics.filename);
            ImGui::Text(_("Bytes captured: %u"), g_centronics.byte_count);
        }
        else
        {
            ImGui::TextDisabled("%s", _("No capture file open"));
        };

        if (g_centronics.total_byte_count != g_centronics.byte_count)
        {
            ImGui::Text(_("Session total: %u bytes"), g_centronics.total_byte_count);
        };

        ImGui::Separator();

        // Uzavření aktuálního souboru (další bajt založí nový jedinečný soubor)
        if (ImGui::MenuItem(_L("Close Capture File"), NULL, false, g_centronics.fp != NULL))
        {
            centronics_close_file();
        };

        ImGui::EndMenu();
    };
}
