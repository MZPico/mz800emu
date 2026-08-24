/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file z80_meta.h
 * @brief Metadata Z80 instrukcí pro debugger - popis a detail flag effects.
 * @version 0.1
 * @date 2026-05-09
 *
 * Doplňuje strukturovaný výstup z dasm-z80 (z80_dasm_inst_t) o lidsky
 * čitelný popis instrukce a detailní informaci, jak instrukce ovlivňuje
 * jednotlivé flagy registru F. T-states a bitová maska ovlivněných flagů
 * jsou již součástí z80_dasm_inst_t (z80_dasm.h).
 *
 * Tabulka je generována skriptem
 * `emu-experiments/disasm-upgrade/tools/z80_meta_gen/gen_z80_meta.py`
 * z explicitních dat (entries.py) a křížově ověřena proti
 * `mz800-knowledge/public/reference/agent/cpu/z80/06-flag-affection.md`.
 *
 * @note Knihovna je thread-safe: lookup nepoužívá globální stav.
 *       Vrácený ukazatel míří na statickou paměť (literály), nesmí se
 *       uvolňovat.
 *
 * @note Metadata jsou na úrovni **třídy instrukce** (např. ADD A,r),
 *       ne na úrovni jednotlivého opcode. To znamená, že popis je
 *       zobecněný (např. "ADD A,r: A = A + r" pokrývá ADD A,B i ADD A,L).
 */

#ifndef Z80_META_H
#define Z80_META_H

#include <stddef.h>
#include <stdint.h>

#include "libs/dasm-z80/z80_dasm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hodnota efektu na jeden flag registru F.
 *
 * Notace odpovídá souboru
 * `mz800-knowledge/public/reference/agent/cpu/z80/06-flag-affection.md`.
 * Pole `z80_meta_t::flags` má 8 prvků v pořadí
 * S, Z, F5, H, F3, P/V, N, C (od MSB k LSB registru F).
 */
typedef enum {
    Z80_FE_UNAFFECTED = 0,  /**< '-' Flag se nemění (zůstává předchozí). */
    Z80_FE_RESET,           /**< '0' Flag se vždy nuluje. */
    Z80_FE_SET,             /**< '1' Flag se vždy nastavuje. */
    Z80_FE_RESULT,          /**< '*' Flag dle výsledku operace
                                  (S, Z, H, F5, F3, C ze standardní ALU). */
    Z80_FE_PARITY,          /**< 'P' P/V flag = parita výsledku
                                  (logické operace, rotace). */
    Z80_FE_OVERFLOW,        /**< 'V' P/V flag = signed overflow
                                  (aritmetika ADD/SUB/INC/DEC). */
    Z80_FE_UNDEFINED,       /**< '?' Hodnota flagu není definována
                                  (např. blokové IN/OUT pro H/PV/N/C). */
    Z80_FE_SPECIAL,         /**< 'X' Speciální chování - viz description
                                  (např. F3/F5 z MEMPTR u BIT n,(HL),
                                  CCF kde H = staré C, LD A,I kde
                                  PV = IFF2). */
} z80_flag_effect_t;

/** Počet flagů v poli `z80_meta_t::flags`. */
#define Z80_META_FLAG_COUNT 8

/**
 * @brief Indexy flagů v poli `z80_meta_t::flags`.
 *
 * Pořadí je MSB → LSB registru F, tj. odpovídá fyzickému rozložení bitů
 * (S = bit 7, C = bit 0).
 */
typedef enum {
    Z80_META_F_S  = 0,  /**< Sign Flag (bit 7) */
    Z80_META_F_Z  = 1,  /**< Zero Flag (bit 6) */
    Z80_META_F_F5 = 2,  /**< F5 / YF (bit 5, undokumentovaný) */
    Z80_META_F_H  = 3,  /**< Half-carry Flag (bit 4) */
    Z80_META_F_F3 = 4,  /**< F3 / XF (bit 3, undokumentovaný) */
    Z80_META_F_PV = 5,  /**< Parity / Overflow Flag (bit 2) */
    Z80_META_F_N  = 6,  /**< Subtract Flag (bit 1) */
    Z80_META_F_C  = 7,  /**< Carry Flag (bit 0) */
} z80_meta_flag_index_t;

/**
 * @brief Metadata jedné třídy Z80 instrukce.
 *
 * Třída = jedinečná kombinace mnemoniky a typů operandů (např. třída
 * `ADD A,r` zahrnuje ADD A,B, ADD A,C, ..., ADD A,L). Tato struktura
 * popisuje záměr a flag effects společné pro celou třídu.
 *
 * @invariant `description` ukazuje na statický string literal.
 * @invariant `flags[i]` pro i = 0..7 obsahuje platnou hodnotu typu
 *            `z80_flag_effect_t`.
 *
 * @note Vrácená paměť patří knihovně (statická), volající ji nesmí
 *       uvolňovat ani modifikovat.
 */
typedef struct {
    const char *description;
        /**< Krátký lidsky čitelný popis (typicky 15-50 znaků), například
             "ADD A,r: A = A + r". */
    z80_flag_effect_t flags[Z80_META_FLAG_COUNT];
        /**< Detail flag effects v pořadí S, Z, F5, H, F3, P/V, N, C
             (= MSB → LSB registru F). */
} z80_meta_t;

/**
 * @brief Vyhledá metadata pro dekódovanou instrukci.
 *
 * Klíč lookupu je trojice (mnemonic, op1.type, op2.type) z `inst`,
 * volitelně doplněná o subtype (konkrétní hodnota op1/op2 - aktuálně
 * podporováno pro REG8). Specializované entries (např. LD A,I) mají
 * přednost před obecnými (LD r,r').
 *
 * @param[in] inst Dekódovaná instrukce z `z80_dasm()`. Nesmí být NULL.
 *                 Pole `inst->mnemonic` ukazuje na statický buffer
 *                 sdílený s `z80_dasm()`; lookup tento buffer čte v rámci
 *                 jednoho volání, takže je bezpečné volat
 *                 `z80_meta_lookup()` bezprostředně po `z80_dasm()`.
 * @return Ukazatel na statickou metadata strukturu, nebo NULL pokud
 *         instrukce nemá metadata (= invalid sequence bez známé třídy,
 *         neoficiální opcode bez popisu).
 *
 * @pre `inst != NULL`
 * @pre `inst->mnemonic != NULL`
 *
 * @post Vrácený ukazatel je platný po celou dobu života programu (statická
 *       paměť) - není třeba uvolňovat.
 *
 * @note Funkce je thread-safe (čistě read-only přístup ke statickým
 *       datům; nepoužívá globální mutable stav).
 */
const z80_meta_t *z80_meta_lookup(const z80_dasm_inst_t *inst);

/**
 * @brief Konverze hodnoty `z80_flag_effect_t` na ASCII znak.
 *
 * Užitečné pro vykreslování tooltipu (řádek typu "S=- Z=- 5=- ..." nebo
 * formát "--*0*-0*").
 *
 * @param fe Efekt na flag.
 * @return Jeden z znaků: '-', '0', '1', '*', 'P', 'V', '?', 'X'. Pro
 *         neznámou hodnotu mimo enum vrací '?'.
 */
char z80_flag_effect_to_char(z80_flag_effect_t fe);

#ifdef __cplusplus
}
#endif

#endif /* Z80_META_H */
