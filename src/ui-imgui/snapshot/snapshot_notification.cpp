/**
 * @file snapshot_notification.cpp
 * @brief Notifikace pro snapshot operace — toast zprávy a chybové dialogy
 */

#include "main.h"
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <SDL3/SDL.h>

#include "libs/imgui/imgui.h"
#include "ui-imgui/bootstrap/myimgui.h"

// Lokalizace
#include "i18n.h"

/* Stav notifikace */
static bool s_notification_active = false;
static bool s_notification_is_error = false;
static char s_notification_message[512] = "";
static Uint64 s_notification_start_time = 0;

/* Doba zobrazení notifikace v ms */
#define NOTIFICATION_DURATION_MS 3000
#define NOTIFICATION_FADE_MS 500


extern "C" void snapshot_notification_show(const char *message, bool is_error)
{
    snprintf(s_notification_message, sizeof(s_notification_message), "%s", message);
    s_notification_is_error = is_error;
    s_notification_active = true;
    s_notification_start_time = SDL_GetTicks();
}


extern "C" void imgui_snapshot_notification(void)
{
    if (!s_notification_active)
        return;

    Uint64 elapsed = SDL_GetTicks() - s_notification_start_time;

    /* Chybové hlášení — modální dialog (bez fadeout) */
    if (s_notification_is_error) {
        ImGui::OpenPopup(_L("Snapshot Error"));

        if (ImGui::BeginPopupModal(_L("Snapshot Error"), NULL,
                                    ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", s_notification_message);
            ImGui::Spacing();

            if (ImGui::Button("OK", ImVec2(120, 0))) {
                s_notification_active = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return;
    }

    /* Toast notifikace pro úspěch — overlay s fadeout */
    if (elapsed > NOTIFICATION_DURATION_MS) {
        s_notification_active = false;
        return;
    }

    /* Vypočítat průhlednost (fadeout v posledních 500ms) */
    float alpha = 1.0f;
    if (elapsed > NOTIFICATION_DURATION_MS - NOTIFICATION_FADE_MS) {
        alpha = (float)(NOTIFICATION_DURATION_MS - elapsed) / (float)NOTIFICATION_FADE_MS;
    }

    /* Pozice: dole uprostřed */
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 window_pos = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 60.0f);
    ImVec2 window_pivot = ImVec2(0.5f, 1.0f);

    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pivot);
    ImGui::SetNextWindowBgAlpha(alpha * 0.85f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                              ImGuiWindowFlags_NoInputs |
                              ImGuiWindowFlags_NoNav |
                              ImGuiWindowFlags_AlwaysAutoResize |
                              ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    if (ImGui::Begin("##SnapshotNotification", NULL, flags)) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, alpha), "%s", s_notification_message);
    }
    ImGui::End();

    ImGui::PopStyleVar();
}
