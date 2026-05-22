#include "main.h"
#include "libs/sdlapp/sdlapp.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "libs/imgui/imgui.h"
#include <stdio.h>
#include <glib.h>
#include <stdarg.h>
#include <string>

// Lokalizace
#include "i18n.h"

#include "message_window.h"

extern "C"
{
    void imgui_message_window(void);
    void imgui_message_create(ui_message_type_t type, const char *message);
    void imgui_message_vprintf(ui_message_type_t type, const char *format, va_list args);
};

static const char *message_type_txt[] = {
    "INFO",
    "ERROR",
    "WARNING",
};

std::string g_message = "";
ui_message_type_t g_message_type = MESSAGE_TYPE_INFO;
std::string g_message_title = "";

void imgui_message_create(ui_message_type_t type, const char *message)
{
    if (message == NULL)
    {
        return;
    };
    if (g_message != "")
    {
        fprintf(stderr, "imgui_message_basic: message already set\n");
        return;
    };
    g_message_type = type;
    g_message = message;
};

void imgui_message_vprintf(ui_message_type_t type, const char *format, va_list args)
{
    char *message = g_strdup_vprintf(format, args);

    imgui_message_create(type, message);
    g_free(message);
};

void ShowModalMessage(ui_message_type_t type, std::string message)
{
    if (g_message_title == "")
    {
        switch (type)
        {
        case MESSAGE_TYPE_INFO:
            g_message_title = _("Information");
            break;

        case MESSAGE_TYPE_ERROR:
            g_message_title = _("Error");
            break;

        case MESSAGE_TYPE_WARNING:
            g_message_title = _("Warning");
            break;

        default:
            g_message_title = _("Information");
            break;
        };

        g_message_title += "##popup_{message_type_txt[mtype]}##";
    };

    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);

    ImGui::OpenPopup(message_type_txt[type]);

    if (ImGui::BeginPopupModal(message_type_txt[type], NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::Text("%s", message.c_str());
        ImGui::Separator();

        if (ImGui::Button(_L("OK"), ImVec2(120, 0)))
        {
            g_message_type = MESSAGE_TYPE_INFO;
            g_message_title = "";
            g_message = "";
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    else
    {
        g_message_type = MESSAGE_TYPE_INFO;
        g_message_title = "";
        g_message = "";
    };
};

void imgui_message_window(void)
{
    if (g_message == "")
    {
        return;
    };

    ShowModalMessage(g_message_type, g_message);
}
