/*
 * dbg_iasm_help.h — Help popup Inline Assembleru
 *
 * Help je implementován jako vnořený modální popup uvnitř
 * IASM dialogu (dbg_inline_asm.cpp). Tento header obsahuje
 * prázdné stuby pro zpětnou kompatibilitu — samotná
 * implementace help okna je přímo v dbg_inline_asm.cpp.
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

#ifndef DBG_IASM_HELP_H
#define DBG_IASM_HELP_H

/*
 * Stub — help je implementován přímo v dbg_inline_asm.cpp
 * jako vnořený modální popup (render_help_modal()).
 * Tato funkce nedělá nic.
 */
void dbg_iasm_help_open(void);

/*
 * Stub — help je vnořený popup uvnitř IASM dialogu,
 * renderuje se automaticky z dbg_iasm_render().
 * Tato funkce nedělá nic.
 */
void dbg_iasm_help_render(void);

#endif /* DBG_IASM_HELP_H */
