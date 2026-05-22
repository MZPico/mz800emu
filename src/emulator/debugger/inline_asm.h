/*
 * File:   inline_asm.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 16. září 2015, 8:46
 *
 * Kompatibilní wrapper nad knihovnou libs/iasm.
 * Přidává zobrazení chybových hlášek přes baseui.
 *
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

#ifndef INLINE_ASM_H
#define INLINE_ASM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "libs/iasm/iasm.h"

    /* zpětná kompatibilita — původní typ */
    typedef iasm_bin_t st_IASMBIN;

    /*
     * Kompatibilní wrapper: zkompiluje instrukci a při chybě zobrazí
     * hlášku přes baseui_show_error_message().
     * Vrací počet bajtů, nebo 0 při chybě.
     */
    extern unsigned debugger_iasm_assemble_line ( uint16_t addr, const char *assemble_txt, st_IASMBIN *compiled_output );


#ifdef __cplusplus
}
#endif

#endif /* INLINE_ASM_H */
