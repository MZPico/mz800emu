#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

#include "libs/imgui/imgui.h"

// Lokalizace
#include "i18n.h"

#include "ui-imgui/topmenu/topmenu.h"
#include "emulator/mzarch/mzarch.h"
#include "emulator/hw-generic/cmt/cmt.h"

extern "C"
{
    void imgui_menu_mz800_dip_switch(void);
}

void imgui_menu_mz800_dip_switch(void)
{
    if (ImGui::BeginMenu(_L("Rear DIP Switch")))
    {
#if MZARCH != 700
        GString *str0 = g_string_new(_("Mode 700 Compat.: "));
        if (MZARCH_TEST_REAR_DIP_SWITCH700)
        {
            g_string_append(str0, _("OFF"));
        }
        else
        {
            g_string_append(str0, _("ON (default)"));
        };

        if (ImGui::MenuItem(str0->str, NULL, !MZARCH_TEST_REAR_DIP_SWITCH700))
        {
            mzarch_rear_dip_switch_mz700_compat(!MZARCH_TEST_REAR_DIP_SWITCH700);
        };
        g_string_free(str0, TRUE);
#endif /* MZARCH != 700 */

        GString *str1 = g_string_new(_("CMT Polarity: "));
        if (CMT_TEST_POLARITY_INVERTED)
        {
            g_string_append(str1, _("Inverted"));
        }
        else
        {
            g_string_append(str1, _("Normal"));
        };

        if (ImGui::MenuItem(str1->str, NULL, CMT_TEST_POLARITY_INVERTED, CMT_TEST_STOP))
        {
            cmt_rear_dip_switch_cmt_inverted_polarity(!CMT_TEST_POLARITY_INVERTED);
        };

        ImGui::EndMenu();
    };
}
