#include "main.h"
#include "libs/sdlapp/sdlapp.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "ui-imgui/auto_layout.h"
#include "libs/imgui/imgui.h"
#include <stdio.h>
#include <glib.h>
#include <algorithm> // pro std::max
#include <string>
#include <cstdint>
#include <cstdio>  // std::snprintf
#include <sstream> // std::ostringstream fallback formatter

// Lokalizace
#include "i18n.h"

#include "ui/version_check/ui_version_check.h"
#include "version_check/version_check.h"
#include "cfgmain.h"
#include "build_revision/build_revision.h"

extern "C"
{
    void imgui_version_check_setup_window(bool *p_open);
    void imgui_version_check_result_window(bool *p_open);
    void imgui_version_check_report_done(st_VERSION_BRANCH *branch);
};

st_VERSION_BRANCH *g_ui_version_check_branch = NULL;
GMutex g_ui_version_check_branch_mutex;

void imgui_version_check_report_done(st_VERSION_BRANCH *branch)
{
    if (branch == NULL)
        return;

    g_mutex_lock(&g_ui_version_check_branch_mutex);

    g_gui->showVersionCheckResultWindow = true;
    g_ui_version_check_branch = version_check_copy_branch(branch);
    if (g_ui_version_check_branch == NULL)
    {
        fprintf(stderr, "%s():%d - ERROR: g_ui_version_check_branch == NULL\n", __func__, __LINE__);
    };

    g_mutex_unlock(&g_ui_version_check_branch_mutex);
}

/**
 * @brief Draws the ImGui window for version check settings.
 *
 * @param p_open Pointer to a boolean that determines if the window should be open.
 */
void imgui_version_check_setup_window(bool *p_open)
{
    static en_VERSION_CHECK_PERIOD selected_period = VERSION_CHECK_DEFAULT_PERIOD;
    static en_VERSION_CHECK_PROXY selected_proxy = VERSION_CHECK_PROXY_NONE;
    static char proxy_buffer[256] = {0};

    /* Počítadlo framů pro jednorázový OS-level raise okna po jeho zobrazení
     * (viz blok za Begin). 0 = neaktivní. */
    static int s_raise_pending_frames = 0;

    static bool initialized = false;
    if (!initialized)
    {
        selected_period = version_check_get_period();
        selected_proxy = version_check_get_proxy_settings();

        if (selected_period == VERSION_CHECK_PERIOD_UNKNOWN)
        {
            selected_period = VERSION_CHECK_DEFAULT_PERIOD;
        };

        // g_print("selected_period: %d\n", selected_period);
        // g_print("selected_proxy: %d\n", selected_proxy);

        const char *current_proxy = version_check_get_proxy_server();
        if (current_proxy)
        {
            strncpy(proxy_buffer, current_proxy, sizeof(proxy_buffer) - 1);
            proxy_buffer[sizeof(proxy_buffer) - 1] = '\0';
        };
        initialized = true;
    };

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;

    /* Auto-layout při fresh open - cache _L() do lokální proměnné. */
    const char *vc_setup_title = _L("mz800emu - Version Check Setup");
    auto_layout_first_use_portrait(vc_setup_title, 500.0f, 400.0f);
    if (!ImGui::Begin(vc_setup_title, p_open, flags))
    {
        ImGui::End();
        return;
    };

    /* Při prvním zobrazení okna (Appearing) ho dostaneme do popředí
     * (= first-run scénář), aby se neotevřelo za main oknem/terminálem.
     *
     * Dříve se to řešilo permanentním ImGuiViewportFlags_TopMost na window
     * class (= SDL_WINDOW_ALWAYS_ON_TOP). To ale rozbíjelo combo roletky:
     * dropdown, který přesáhne obrys okna, vznikne v multi-viewport ImGui
     * jako separátní NE-topmost OS viewport a always-on-top rodič ho překryl
     * (roletka se "otevřela pod oknem"). ImGui popupům TopMost záměrně nedává
     * (imgui.cpp - auto-TopMost jen pro Tooltip), takže to nešlo obejít přes
     * window class popupu.
     *
     * Místo toho okno jednorázově raisneme na OS úrovni přes
     * Platform_SetWindowFocus (= SDL_RaiseWindow) - stejný vzor jako Stack
     * Monitor okno. SetWindowFocus mění jen ImGui-interní z-order, NE OS
     * window order, proto je nutný ještě platform raise. Platform window
     * separátního viewportu nemusí existovat hned na prvním Appearing framu,
     * proto raise zkoušíme po několik framů, dokud se nepovede. */
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetWindowFocus();
        s_raise_pending_frames = 5;
    };
    if (s_raise_pending_frames > 0)
    {
        s_raise_pending_frames--;
        ImGuiViewport *vp = ImGui::GetWindowViewport();
        ImGuiViewport *main_vp = ImGui::GetMainViewport();
        ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
        if (vp && (vp != main_vp) && vp->PlatformWindowCreated && pio.Platform_SetWindowFocus)
        {
            pio.Platform_SetWindowFocus(vp);
            s_raise_pending_frames = 0;
        };
    };

    ImGui::Text("%s", _("Enable online checking the latest version of the mz800emu."));
    ImGui::Text("%s", _("If a new version is found, an information window will be displayed."));
    ImGui::Text("%s", _("This option can be changed at any time in the version check setup from the Tools submenu."));

    ImGui::Text(" ");
    ImGui::Separator();

    const char *period_items[] = {
        _("never"),
        _("daily"),
        _("weekly"),
        _("monthly"),
        _("sometimes")};

    int period_index = 0;
    switch (selected_period)
    {
    case VERSION_CHECK_PERIOD_DAILY:
        period_index = 1;
        break;
    case VERSION_CHECK_PERIOD_WEEKLY:
        period_index = 2;
        break;
    case VERSION_CHECK_PERIOD_MONTHLY:
        period_index = 3;
        break;
    case VERSION_CHECK_PERIOD_SOMETIMES:
        period_index = 4;
        break;
    case VERSION_CHECK_PERIOD_NEVER:
    default:
        period_index = 0;
        break;
    };

    const char *proxy_items[] = {
        _("none"),
        _("from system environment"),
        _("manual")};

    int proxy_index = (int)selected_proxy;

    if (ImGui::BeginTable("VersionCheckTable", 3, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_NoPadOuterX))
    {
        // Před ImGui::BeginTable
        const char *label1 = _("Check period");
        const char *label2 = _("Proxy server (optional)");
        float label_width = std::max(ImGui::CalcTextSize(label1).x, ImGui::CalcTextSize(label2).x) + 20.0f;

        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, label_width);
        ImGui::TableSetupColumn("Colon", ImGuiTableColumnFlags_WidthFixed, 10.0f);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label1);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(":");
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Combo("##period_option", &period_index, period_items, IM_ARRAYSIZE(period_items)))
        {
            switch (period_index)
            {
            case 1:
                selected_period = VERSION_CHECK_PERIOD_DAILY;
                break;
            case 2:
                selected_period = VERSION_CHECK_PERIOD_WEEKLY;
                break;
            case 3:
                selected_period = VERSION_CHECK_PERIOD_MONTHLY;
                break;
            case 4:
                selected_period = VERSION_CHECK_PERIOD_SOMETIMES;
                break;
            case 0:
            default:
                selected_period = VERSION_CHECK_PERIOD_NEVER;
                break;
            };
        };

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label2);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(":");
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Combo("##proxy_option", &proxy_index, proxy_items, IM_ARRAYSIZE(proxy_items)))
        {
            selected_proxy = (en_VERSION_CHECK_PROXY)proxy_index;
        };

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetColumnIndex(1);
        ImGui::TableSetColumnIndex(2);
        if (selected_proxy == VERSION_CHECK_PROXY_MANUAL)
        {
            ImGui::InputText("##proxy_input", proxy_buffer, sizeof(proxy_buffer));
        }
        else
        {
            ImGui::Text(" ");
        };

        ImGui::EndTable();
    };

    ImGui::Separator();
    ImGui::Text(" ");

    float button_width = ImGui::CalcTextSize(_("Apply")).x + ImGui::GetStyle().FramePadding.x * 8;
    float window_width = ImGui::GetContentRegionAvail().x;
    float offset_x = (window_width - button_width) * 0.5f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

    if (ImGui::Button(_L("Apply"), ImVec2(button_width, 0)))
    {
        *p_open = false;
        version_check_set_proxy_settings(selected_proxy);
        if (selected_proxy == VERSION_CHECK_PROXY_MANUAL)
        {
            version_check_set_proxy_server(proxy_buffer);
        };

        en_VERSION_CHECK_PERIOD old_period = version_check_get_period();

        if (old_period != selected_period)
        {
            version_check_set_period(selected_period);

            if (selected_period > VERSION_CHECK_PERIOD_NEVER)
            {
                uint32_t last_check = version_check_get_last_check();
                uint32_t unix_days = version_check_get_unix_days();
                if ((last_check == 0) || ((unix_days - last_check) >= selected_period))
                {
                    g_print("Version check: %d days since last check\n", unix_days - last_check);
                    version_check_run_test(false);
                };
            };
        };
    };

    ImGui::End();
};

static void CenteredTextLinkOpenURL(const char *label, const char *url)
{
    float window_width = ImGui::GetWindowSize().x;
    float text_width = ImGui::CalcTextSize(label).x;

    ImGui::SetCursorPosX((window_width - text_width) * 0.5f);
    ImGui::TextLinkOpenURL(label, url);
}

void imgui_version_check_result_window(bool *p_open)
{
    if (p_open && !*p_open)
        return;

    const uint32_t running_revision = build_revision_get_int();
    const uint32_t running_version = cfgmain_get_version_uint32();

    const bool new_version_available = (g_ui_version_check_branch &&
                                        (g_ui_version_check_branch->version > running_version ||
                                         g_ui_version_check_branch->revision > running_revision));

    ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_AlwaysAutoResize;

    /* Auto-layout při fresh open - cache _L() do lokální proměnné. */
    const char *vc_report_title = _L("MZ800 Emulator Version Check Report");
    auto_layout_first_use_portrait(vc_report_title, 500.0f, 400.0f);
    if (!ImGui::Begin(vc_report_title, p_open, win_flags))
    {
        ImGui::End();
        return;
    }

    g_mutex_lock(&g_ui_version_check_branch_mutex);

    // ── Headline ────────────────────────────────────────────────────────────
    ImGui::Text("%s", new_version_available ? _("A new version of the mz800emu software is available") : _("Your mz800emu software version is up to date"));

    ImGui::Spacing();
    ImGui::Text(" ");

    // ── Dynamic‑width table ────────────────────────────────────────────────
    const ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerV |
                                        ImGuiTableFlags_BordersOuter |
                                        ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_SizingFixedFit; // NEW: auto‑fit columns

    if (ImGui::BeginTable("##verchecktbl", 3, table_flags))
    {
        // No hard‑coded widths – let ImGui size things based on content.
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide);
        ImGui::TableSetupColumn(_("Running"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(_("Available"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        auto row = [](const char *label, const std::string &running, const std::string &available)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(running.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(available.c_str());
        };

        char *branch_version_txt = g_ui_version_check_branch ? ui_version_check_create_version_string(g_ui_version_check_branch->version) : NULL;

        row(_("Tag"), CFGMAIN_EMULATOR_VERSION_TAG, g_ui_version_check_branch ? g_ui_version_check_branch->tag->str : "―");
        row(_("Version"), CFGMAIN_EMULATOR_VERSION_NUM_STRING, branch_version_txt ? branch_version_txt : "―");
        row(_("Revision"), std::to_string(running_revision), g_ui_version_check_branch ? std::to_string(g_ui_version_check_branch->revision) : "―");
        row(_("Build date"), cfgmain_get_build_date(), g_ui_version_check_branch ? g_ui_version_check_branch->date->str : "―");

        if (branch_version_txt != NULL)
        {
            g_free(branch_version_txt);
            branch_version_txt = NULL;
        };

        ImGui::EndTable();
    }

    if (g_ui_version_check_branch->msg != NULL)
    {
        ImGui::Text(" ");
        ImGui::TextUnformatted(g_ui_version_check_branch->msg->str);
    };

    const char *url = ui_version_check_get_url_by_tag(g_ui_version_check_branch->tag->str);
    if (url != NULL)
    {
        ImGui::Text(" ");
        CenteredTextLinkOpenURL(url, url);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text(" ");

    // ── Buttons ────────────────────────────────────────────────────────────
    const float button_w = 100.0f;

    if (ImGui::Button(_L("Setup"), ImVec2(button_w, 0)))
    {
        g_gui->showVersionCheckSetupWindow = true;
    };

    ImGui::SameLine();
    if (ImGui::Button(_L("Close"), ImVec2(button_w, 0)) && p_open)
    {
        *p_open = false;
        version_check_branch_free(g_ui_version_check_branch);
        g_ui_version_check_branch = NULL;
    };

    g_mutex_unlock(&g_ui_version_check_branch_mutex);

    ImGui::End();
}
