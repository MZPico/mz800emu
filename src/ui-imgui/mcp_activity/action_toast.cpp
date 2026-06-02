/*
 * File:   action_toast.cpp
 *
 * Implementace selektivního action toastu - viz action_toast.h pro
 * architekturu, filter a threading model.
 *
 * Klíčová designová rozhodnutí:
 *
 *   1. Queue = std::deque<ToastEntry>. Drop-tail-old při dosažení
 *      ACTION_TOAST_MAX (= 3 souběžné toasty).
 *
 *   2. Destruktivní filter je hardcoded whitelist v action_toast_is_destructive.
 *      Konzervativní set: RESET, PAUSE/RUN, SNAPSHOT_LOAD*, PLATFORM_SET,
 *      PERIPH_*, NMI/IRQ_INJECT, MEM_WRITE_FORCE.
 *
 *      Read-only operace (mem_read, bp_list, get_*) trigger nemají -
 *      ne každá MCP akce má překvapit uživatele.
 *
 *   3. Render = top-right viewport, vertical stack. Position vypočítaná
 *      přes ImGui::GetMainViewport()->WorkPos/Size. NoSavedSettings flag
 *      = ne uložit do ini.
 *
 *   4. Fade-out = g_get_monotonic_time() delta proti show_time. Alpha
 *      lineárně klesá v posledních 0.7s před expirací. Po elapsed >
 *      ACTION_TOAST_DURATION_SEC se toast odstraní z queue.
 *
 *   5. Threading: queue chráněna GMutex. show() z dispatcher hook
 *      (UI vlákno po thread switchi), render() z main render loop
 *      (UI vlákno). Mutex je defenzivní pro budoucí externí trigger.
 *
 * ----------------------------- License -------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 */

#include "main.h"
#include "libs/imgui/imgui.h"
#include "action_toast.h"

#include <deque>
#include <string>
#include <cstdio>

#include <glib.h>

/* ============================================================================
 * Interní data
 * ============================================================================ */

/**
 * @brief Jeden záznam v toast queue.
 *
 * show_time_us je monotonic clock při show(). Render porovnává proti
 * aktuálnímu g_get_monotonic_time() pro elapsed a fade-out alpha.
 */
struct ToastEntry
{
    uint64_t show_time_us;
    en_DBGAPI_CMD_ORIGIN origin;
    en_DBGAPI_CMD cmd;
    std::string description;
};

static std::deque<ToastEntry> g_toast_queue;
static GMutex g_toast_mutex;
static bool g_toast_initialized = false;


/* ============================================================================
 * Veřejné API - lifecycle
 * ============================================================================ */

extern "C" void action_toast_init(void)
{
    if (g_toast_initialized)
    {
        return;
    }
    g_mutex_init(&g_toast_mutex);
    g_toast_queue.clear();
    g_toast_initialized = true;
}


extern "C" void action_toast_shutdown(void)
{
    if (!g_toast_initialized)
    {
        return;
    }
    g_mutex_lock(&g_toast_mutex);
    g_toast_queue.clear();
    g_mutex_unlock(&g_toast_mutex);
    g_mutex_clear(&g_toast_mutex);
    g_toast_initialized = false;
}


/* ============================================================================
 * Destruktivní filter
 * ============================================================================ */

extern "C" bool action_toast_is_destructive(en_DBGAPI_CMD_ORIGIN origin,
                                            en_DBGAPI_CMD cmd)
{
    /* Filter 1: jen MCP klient. USER nepotřebuje toast - sám si akci
     * vyvolal. TEST / INTERNAL nezobrazujeme (= test framework + emu
     * interní = noise). */
    if (origin != DBGAPI_CMD_ORIGIN_MCP)
    {
        return false;
    }

    /* Filter 2: hardcoded whitelist destruktivních cmd. Read-only a
     * informativní operace (mem_read, bp_list, get_*) trigger nemají. */
    switch (cmd)
    {
        case DBGAPI_CMD_RESET:
        case DBGAPI_CMD_PAUSE:
        case DBGAPI_CMD_FORCE_PAUSE:
        case DBGAPI_CMD_RUN:
        case DBGAPI_CMD_SNAPSHOT_LOAD_FILE:
        case DBGAPI_CMD_SNAPSHOT_LOAD_BUFFER:
        case DBGAPI_CMD_PLATFORM_SET:
        case DBGAPI_CMD_PERIPH_ATTACH:
        case DBGAPI_CMD_PERIPH_DETACH:
        case DBGAPI_CMD_NMI_INJECT:
        case DBGAPI_CMD_IRQ_INJECT:
        case DBGAPI_CMD_MEM_WRITE_FORCE:
            return true;
        default:
            return false;
    }
}


/* ============================================================================
 * Show + render
 * ============================================================================ */

extern "C" void action_toast_show(en_DBGAPI_CMD_ORIGIN origin,
                                   en_DBGAPI_CMD cmd,
                                   const char *description)
{
    if (!g_toast_initialized)
    {
        return;
    }
    if (!action_toast_is_destructive(origin, cmd))
    {
        return;
    }

    g_mutex_lock(&g_toast_mutex);

    /* Drop-tail-old při dosažení kapacity. */
    while (g_toast_queue.size() >= ACTION_TOAST_MAX)
    {
        g_toast_queue.pop_front();
    }

    ToastEntry e;
    e.show_time_us = (uint64_t)g_get_monotonic_time();
    e.origin = origin;
    e.cmd = cmd;
    e.description = description ? description : "";
    g_toast_queue.push_back(std::move(e));

    g_mutex_unlock(&g_toast_mutex);
}


/**
 * @brief Vypočítá alpha pro fade-out efekt podle elapsed.
 *
 * Lineární klesání v posledních 0.7s před expirací. Před tím alpha=1.0.
 * Po expiraci 0.0 (= toast bude odstraněn).
 *
 * @param elapsed_s  Sekundy od show().
 * @return Alpha v [0.0, 1.0].
 */
static float toast_compute_alpha(float elapsed_s)
{
    const float fade_window = 0.7f;
    if (elapsed_s < 0.0f) return 1.0f;
    if (elapsed_s >= ACTION_TOAST_DURATION_SEC) return 0.0f;
    float remaining = ACTION_TOAST_DURATION_SEC - elapsed_s;
    if (remaining >= fade_window) return 1.0f;
    return remaining / fade_window;
}


extern "C" void action_toast_render(void)
{
    if (!g_toast_initialized)
    {
        return;
    }

    /* Snapshot copy + auto-remove expirovaných toastů. */
    std::deque<ToastEntry> snapshot;
    uint64_t now_us = (uint64_t)g_get_monotonic_time();
    g_mutex_lock(&g_toast_mutex);
    /* Trim expired z fronty. */
    while (!g_toast_queue.empty())
    {
        const ToastEntry &front = g_toast_queue.front();
        float elapsed_s = (float)((now_us - front.show_time_us) / 1000000.0);
        if (elapsed_s >= ACTION_TOAST_DURATION_SEC)
        {
            g_toast_queue.pop_front();
        }
        else
        {
            break;
        }
    }
    snapshot = g_toast_queue;
    g_mutex_unlock(&g_toast_mutex);

    if (snapshot.empty())
    {
        return;
    }

    /* Top-right viewport, vertical stack. */
    ImGuiViewport *vp = ImGui::GetMainViewport();
    const float toast_w = 320.0f;
    const float toast_h_estimate = 70.0f;
    const float margin = 10.0f;
    float y_offset = vp->WorkPos.y + margin;

    int idx = 0;
    for (const ToastEntry &e : snapshot)
    {
        float elapsed_s = (float)((now_us - e.show_time_us) / 1000000.0);
        float alpha = toast_compute_alpha(elapsed_s);
        if (alpha <= 0.0f) continue;

        ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - toast_w - margin, y_offset);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(toast_w, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.92f * alpha);

        char wname[64];
        snprintf(wname, sizeof(wname), "###action_toast_%d", idx);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                              | ImGuiWindowFlags_NoMove
                              | ImGuiWindowFlags_NoSavedSettings
                              | ImGuiWindowFlags_NoFocusOnAppearing
                              | ImGuiWindowFlags_NoNav
                              | ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        if (ImGui::Begin(wname, NULL, flags))
        {
            /* Header s ikonkou origin + "AI Action". */
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.20f, 1.0f),
                                "[A] %s", "AI Action");
            ImGui::Separator();
            if (!e.description.empty())
            {
                ImGui::TextWrapped("%s", e.description.c_str());
            }
            else
            {
                ImGui::TextDisabled("%s", "(no description)");
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        y_offset += toast_h_estimate + margin;
        idx++;
    }
}
