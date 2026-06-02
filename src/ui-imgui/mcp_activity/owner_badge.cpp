/*
 * File:   owner_badge.cpp
 *
 * Implementace sdíleného UI helperu pro owner badge - viz owner_badge.h
 * pro architekturu a kontrakty.
 *
 * Designová rozhodnutí:
 *
 *   1. Krátký kód [U]/[A]/[T]/[S] je preferovaný před emoji (= dnešní
 *      ImGui font neobsahuje plnou Unicode tabulku) a před delším
 *      [USER]/[MCP]/[TEST]/[SYS] (= šetří horizontální místo v tabulkách
 *      vedle hex adres a labelů).
 *
 *   2. Barvy jsou konstanty zaokrouhlené z přístupového UI palette
 *      schématu (= modrá pro user, oranžová pro AI, šedá pro test,
 *      ztlumená pro system). Není to čerpáno z ImGui style colors,
 *      aby badge byl viditelně odlišný i pod custom theme.
 *
 *   3. Tooltip text je čistě anglický literál (= runtime hláška MCP
 *      Activity okna; per zadání 2026-05-29 se nelokalizuje, jen menu a
 *      setup window MCP serveru jdou do gettextu).
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
#include "owner_badge.h"

/* ============================================================================
 * Statické mapy (lifetime = program)
 * ============================================================================ */

/**
 * @brief Vrátí barvu badge pro daný origin.
 *
 * Hodnoty jsou pevné RGBA float, ne čerpané z ImGui::GetStyle().Colors,
 * aby zůstaly konzistentní napříč user theme změnami.
 */
static ImVec4 origin_to_color(en_DBGAPI_CMD_ORIGIN origin)
{
    switch (origin)
    {
        case DBGAPI_CMD_ORIGIN_USER:
            /* Modrá - primary user akce. */
            return ImVec4(0.45f, 0.70f, 1.00f, 1.00f);
        case DBGAPI_CMD_ORIGIN_MCP:
            /* Oranžová - AI / external accent. */
            return ImVec4(1.00f, 0.65f, 0.20f, 1.00f);
        case DBGAPI_CMD_ORIGIN_TEST:
            /* Šedá - test framework, neutral. */
            return ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        case DBGAPI_CMD_ORIGIN_INTERNAL:
            /* Tlumená - emu interní / legacy. */
            return ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
    }
    /* Defenzivní fallback - nemělo by nastat. */
    return ImVec4(1.00f, 0.00f, 1.00f, 1.00f);
}


/* ============================================================================
 * Veřejné API
 * ============================================================================ */

extern "C" const char *owner_badge_short_code(en_DBGAPI_CMD_ORIGIN origin)
{
    switch (origin)
    {
        case DBGAPI_CMD_ORIGIN_USER:     return "U";
        case DBGAPI_CMD_ORIGIN_MCP:      return "A";
        case DBGAPI_CMD_ORIGIN_TEST:     return "T";
        case DBGAPI_CMD_ORIGIN_INTERNAL: return "S";
    }
    return "?";
}


extern "C" const char *owner_badge_bracketed(en_DBGAPI_CMD_ORIGIN origin)
{
    switch (origin)
    {
        case DBGAPI_CMD_ORIGIN_USER:     return "[U]";
        case DBGAPI_CMD_ORIGIN_MCP:      return "[A]";
        case DBGAPI_CMD_ORIGIN_TEST:     return "[T]";
        case DBGAPI_CMD_ORIGIN_INTERNAL: return "[S]";
    }
    return "[?]";
}


extern "C" void owner_badge_render(en_DBGAPI_CMD_ORIGIN origin)
{
    const char *text = owner_badge_bracketed(origin);
    ImVec4 col = origin_to_color(origin);

    ImGui::TextColored(col, "%s", text);

    if (ImGui::IsItemHovered())
    {
        const char *tip = NULL;
        switch (origin)
        {
            case DBGAPI_CMD_ORIGIN_USER:
                tip = "USER (GUI)";
                break;
            case DBGAPI_CMD_ORIGIN_MCP:
                tip = "AI (MCP client)";
                break;
            case DBGAPI_CMD_ORIGIN_TEST:
                tip = "TEST framework";
                break;
            case DBGAPI_CMD_ORIGIN_INTERNAL:
                tip = "SYSTEM (internal)";
                break;
        }
        if (tip)
        {
            ImGui::SetTooltip("%s", tip);
        }
    }
}
