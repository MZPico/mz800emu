/*
 * dbg_topmenu.h — Top menu hlavního okna debuggeru
 *
 * Menu je umístěno v horní části okna debuggeru a obsahuje
 * 4 podmenu: File, Emulation, Screen, Debugger Settings.
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

#ifndef DBG_TOPMENU_H
#define DBG_TOPMENU_H

/*
 * Vykreslení menubar debuggeru.
 * Volá se uvnitř ImGui::Begin() hlavního okna debuggeru.
 * p_open — ukazatel na viditelnost okna (pro položku Hide).
 */
void dbg_topmenu_render(bool *p_open);

#endif /* DBG_TOPMENU_H */
