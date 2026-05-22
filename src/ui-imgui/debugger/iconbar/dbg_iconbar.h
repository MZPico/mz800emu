/*
 * dbg_iconbar.h — Iconbar (toolbar) hlavního okna debuggeru
 *
 * Toolbar s tlačítky umístěný ve spodní části okna debuggeru.
 * Obsahuje 3 skupiny tlačítek oddělené separátory:
 *
 * 1. Řízení emulace: Continue (F5), Pause (Ctrl+F5)
 * 2. Krokování: Step Over (F8), Step Into (F7), Run to Cursor (F4)
 * 3. Další okna: Breakpoints (Alt+B), Memory Browser (Alt+E), Disassembler (Alt+I)
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

#ifndef DBG_ICONBAR_H
#define DBG_ICONBAR_H

/*
 * Vykreslení iconbar debuggeru.
 * Volá se uvnitř ImGui::Begin() hlavního okna debuggeru,
 * typicky jako poslední prvek (pod středovou částí).
 *
 * Iconbar zpracovává i klávesové zkratky (F4, F5, F7, F8, Ctrl+F5),
 * ale pouze pokud má okno debuggeru focus.
 */
void dbg_iconbar_render(void);

#endif /* DBG_ICONBAR_H */
