/*
 * File:   owner_badge.h
 *
 * Sdílený UI helper pro vykreslení "owner badge" (= origin štítku) v
 * tabulkách BP / Watch / Symbol / Bookmark a v MCP Activity okně.
 *
 * Badge je krátký textový kód v hranatých závorkách, který informuje
 * uživatele, kdo daný záznam vytvořil:
 *
 *   USER     -> "[U]"    (modrá)  - GUI uživatel (default GUI tvorba)
 *   MCP      -> "[A]"    (oranžová) - AI agent / MCP klient
 *   TEST     -> "[T]"    (šedá)   - test framework
 *   INTERNAL -> "[S]"    (tlumená) - emulátor sám / legacy entries
 *
 * Hover tooltip zobrazí plný popis.
 *
 * Threading: helper se volá výhradně z UI vlákna v rámci ImGui frame.
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

#ifndef OWNER_BADGE_H
#define OWNER_BADGE_H

#include "emulator/debugger/dbgapi_cmdrq.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Vrátí krátký kód badge (= max 3 znaky bez závorek).
 *
 * Statický string, lifetime = celý program. Hodí se pro řazení nebo
 * filtrování bez nutnosti renderu. Pro neznámou hodnotu vrátí "?".
 *
 * Mapování:
 *   USER     -> "U"
 *   MCP      -> "A"
 *   TEST     -> "T"
 *   INTERNAL -> "S"
 *
 * @param origin Hodnota enum en_DBGAPI_CMD_ORIGIN.
 * @return Read-only statický řetězec.
 */
const char *owner_badge_short_code(en_DBGAPI_CMD_ORIGIN origin);


/**
 * @brief Vrátí bracketed badge string ve tvaru "[X]".
 *
 * Statický string, lifetime = celý program. Slouží pro rychlé
 * `TextUnformatted` v ImGui tabulce bez nutnosti per-frame formátování.
 *
 * @param origin Hodnota enum en_DBGAPI_CMD_ORIGIN.
 * @return Read-only statický řetězec.
 */
const char *owner_badge_bracketed(en_DBGAPI_CMD_ORIGIN origin);


/**
 * @brief Vykreslí badge v aktuální ImGui buňce/řádku.
 *
 * Renderuje barevný `Text("[X]")` přes ImGui PushStyleColor + Text.
 * Po renderu hodnota tooltip se zobrazí při hoveru.
 *
 * Funkce nevolá `TableNextColumn()` ani jiné table primitivy - caller
 * musí mít aktivní table cell připravenou před voláním.
 *
 * @param origin Hodnota enum en_DBGAPI_CMD_ORIGIN.
 *
 * @pre Aktivní ImGui kontext + frame, volání v UI vlákně.
 * @post Aktuální cursor v ImGui je posunut za vykreslený text (= další
 *       widget může pokračovat na SameLine / nový řádek dle layoutu).
 */
void owner_badge_render(en_DBGAPI_CMD_ORIGIN origin);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OWNER_BADGE_H */
