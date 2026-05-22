/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file z80_meta.c
 * @brief Lookup logika nad statickou tabulkou metadat Z80 instrukcí.
 *
 * Tabulka je definována v z80_meta_table.c (auto-generated). Tento soubor
 * obsahuje veřejnou API funkci `z80_meta_lookup()` a pomocný převodník
 * `z80_flag_effect_to_char()`.
 *
 * Výkon: lookup je linear search nad ~170 záznamy. Není to hot path -
 * volá se jen při zobrazení tooltipu v debuggeru. Pokud by to vadilo,
 * lze přejít na dvouúrovňový hash (mnemonic -> bucket -> sub-search),
 * ale aktuální měření zatím nebylo provedeno.
 */

#include "libs/z80meta/z80_meta.h"

#include <stddef.h>

/* Forward deklarace pomocné funkce z auto-generated z80_meta_table.c.
 * Ta zná konkrétní strukturu z80_meta_entry_t a přístup k tabulce. */
extern const z80_meta_t *z80_meta_table_lookup(
    const char *mnemonic,
    z80_operand_type_t op1_type, z80_operand_type_t op2_type,
    int op1_subtype_val, int op2_subtype_val);


/**
 * @brief Získá hodnotu subtype pro REG8 / REG16 / CONDITION operand.
 *
 * Pro typy které nemají subtype (např. IMM8, IMM16), vrací 0 (= bude
 * matchovat wildcard -1 v lookup).
 *
 * @param op Operand z dekódované instrukce.
 * @return Číselná hodnota podtypu (val.reg8 / val.reg16 / val.condition),
 *         nebo 0 pokud typ nemá smysluplný subtype.
 */
static int extract_subtype(const z80_operand_t *op)
{
    switch (op->type) {
    case Z80_OP_REG8:
        return (int)op->val.reg8;
    case Z80_OP_REG16:
    case Z80_OP_MEM_REG16:
        return (int)op->val.reg16;
    case Z80_OP_CONDITION:
        return (int)op->val.condition;
    default:
        return 0;
    }
}


const z80_meta_t *z80_meta_lookup(const z80_dasm_inst_t *inst)
{
    if (inst == NULL || inst->mnemonic[0] == '\0') {
        return NULL;
    }

    int sub1 = extract_subtype(&inst->op1);
    int sub2 = extract_subtype(&inst->op2);

    return z80_meta_table_lookup(
        inst->mnemonic,
        inst->op1.type, inst->op2.type,
        sub1, sub2);
}


char z80_flag_effect_to_char(z80_flag_effect_t fe)
{
    switch (fe) {
    case Z80_FE_UNAFFECTED: return '-';
    case Z80_FE_RESET:      return '0';
    case Z80_FE_SET:        return '1';
    case Z80_FE_RESULT:     return '*';
    case Z80_FE_PARITY:     return 'P';
    case Z80_FE_OVERFLOW:   return 'V';
    case Z80_FE_UNDEFINED:  return '?';
    case Z80_FE_SPECIAL:    return 'X';
    default:                return '?';
    }
}
