/*
 * dbg_focus_to.h — Modální dialog "Focus To..." pro sekci Disassembled
 *
 * Umožňuje zadat hex adresu pro nastavení focusu dolní tabulky disassembly.
 * Obsahuje textový vstup (max 4 hex znaky) a roletku s historií
 * posledních unikátních hodnot seřazených vzestupně.
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

#ifndef DBG_FOCUS_TO_H
#define DBG_FOCUS_TO_H

/* Forward deklarace - dialog cílí na konkrétní instanci disassembly view. */
struct DisassembledView;

/**
 * @brief Otevře dialog Focus To pro konkrétní instanci disassembly view.
 *
 * Předvyplní se poslední použitá adresa (nebo 0000 pokud je historie
 * prázdná), text se celý selektuje. Po stisku Apply se cílová adresa
 * zapíše přes dbg_disasm_view_set_focus_addr() do předaného @p target,
 * čili do té instance, ze které byl dialog otevřen (hlavní okno NEBO
 * sekundární okno #2-#5).
 *
 * @param target Cílová instance disassembly view, do které se po
 *               Apply zapíše focus adresa. NESMÍ být NULL.
 *
 * @note Dialog je modální v rámci debuggeru, ale otevření probíhá
 *       lazy (s_should_open flag) - pointer je uložen jako file-static
 *       a používá se až ve frame, kdy dialog zobrazí Apply. Pokud je
 *       view zničeno před Apply, viz dbg_focus_to_invalidate_target_if_matches().
 */
void dbg_focus_to_open(struct DisassembledView *target);

/**
 * @brief Vykreslení modálního dialogu Focus To.
 *
 * Volat z debugger_window.cpp uvnitř Begin/End (za dbg_iasm_render).
 * Při Apply zapíše adresu do uloženého target view (viz dbg_focus_to_open).
 * Pokud target zmizel (= ukazatel byl invalidován přes
 * dbg_focus_to_invalidate_target_if_matches), Apply je no-op.
 */
void dbg_focus_to_render(void);

/**
 * @brief Invaliduje uložený target pointer pokud odkazuje na @p view.
 *
 * Volat z dbg_disasm_view_destroy() před uvolněním instance, aby
 * pending Focus To dialog (otevřený v této instanci, ale ještě
 * neapplied) neměl dangling pointer.
 *
 * @param view Instance, která se právě destruuje. Pokud se shoduje
 *             s aktuálně uloženým target, dialog se zruší a target
 *             se nastaví na NULL. Jinak no-op.
 */
void dbg_focus_to_invalidate_target_if_matches(const struct DisassembledView *view);

#endif /* DBG_FOCUS_TO_H */
