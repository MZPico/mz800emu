/*
 * dbg_inline_asm.h — Modální dialog Inline Assembleru
 *
 * Umožňuje uživateli editovat Z80 instrukce přímo v debuggeru.
 * Dialog se otevře double-clickem na řádek, klávesou Enter,
 * nebo zahájením psaní validního znaku nad tabulkou disassembly.
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

#ifndef DBG_INLINE_ASM_H
#define DBG_INLINE_ASM_H

#include <stdint.h>

/* Režim otevření inline assembleru */
typedef enum {
    IASM_OPEN_DOUBLECLICK,  /* Double click / Enter — prefill addr + mnemonic, select all */
    IASM_OPEN_TYPING,       /* Psaní znaku — prefill addr, vložit první znak */
} IasmOpenMode;

/*
 * Otevření inline assembler dialogu.
 *
 * addr       — adresa instrukce (zobrazí se v Addr poli)
 * mnemonic   — existující mnemonic (prefill), může být NULL
 * first_char — první znak instrukce (pro IASM_OPEN_TYPING), 0 pokud se nepoužívá
 * mode       — režim otevření
 */
void dbg_iasm_open(uint16_t addr, const char *mnemonic, char first_char, IasmOpenMode mode);

/*
 * Vykreslení modálního dialogu inline assembleru.
 * Volat z debugger_window.cpp uvnitř Begin/End (za render_pause_info_modal).
 */
void dbg_iasm_render(void);

#endif /* DBG_INLINE_ASM_H */
