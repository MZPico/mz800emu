/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file symdb_bridge.c
 * @brief Implementace mostu sym_db ↔ z80_symtab_t.
 *
 * Sémantika beze změny oproti původní implementaci v
 * @c dbg_disassembled.cpp:543-634. Extrakce byla provedena ve fázi
 * F3 disassembler-window-v1, aby Disassembler V1 okno mohlo
 * konzumovat stejnou bridge cestu jako inline disasm view sekce.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#include "symdb_bridge.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "emulator/debugger/symbols/sym_db.h"


/**
 * @brief Cached z80_symtab_t naplněný symboly z sym_db.
 *
 * Lifetime: lazy alokace v prvním symdb_bridge_get_symtab() volání,
 * žije do @ref symdb_bridge_destroy. Cleared a re-fillovaný kdykoliv
 * je @c sym_db_get_version() jiné než @c s_dasm_symtab_version.
 */
static z80_symtab_t *s_dasm_symtab = NULL;

/**
 * @brief Cached version sym_db (= last sync). Inicializováno na 0,
 *        při prvním sync se aktualizuje na aktuální verzi DB.
 *
 * Hodnota 0 zároveň znamená "ještě nikdy nesyncováno" (nebot sym_db
 * po init má version 0, ale taky před prvním rebuild máme version 0
 * → bezpečně se v prvním volání spustí inicializace přes
 * @c s_dasm_symtab_initialized flag).
 */
static unsigned s_dasm_symtab_version = 0;

/**
 * @brief Indikátor, že lazy create proběhl alespoň jednou.
 *
 * Slouží k odlišení "úplně poprvé" od "už máme symtab + aktuální
 * verze 0" - bez toho by věčně 0==0 znamenalo "nemusíš nic dělat",
 * ale fakticky bychom neměli alokovaný symtab.
 */
static bool s_dasm_symtab_initialized = false;


/**
 * @brief Synchronizuje cached symtab s aktuálním stavem sym_db.
 *
 * Pokud verze sym_db neodpovídá cachované, vyčistí symtab a znovu
 * naplní ze všech záznamů s bank_id == 0 (= CPU view).
 *
 * @post @c s_dasm_symtab je platný handle nebo NULL pokud alokace
 *       selhala. NULL handle je akceptovatelný - z80_dasm_to_str_sym()
 *       degraduje na bez-symbol fmt.
 */
static void sync_dasm_symtab(void)
{
    unsigned cur_ver = sym_db_get_version();

    if (!s_dasm_symtab_initialized) {
        s_dasm_symtab = z80_symtab_create();
        s_dasm_symtab_initialized = true;
        s_dasm_symtab_version = (unsigned)~0u; /* force rebuild níže */
    }

    if (cur_ver == s_dasm_symtab_version && s_dasm_symtab != NULL) {
        return; /* up-to-date */
    }

    if (s_dasm_symtab != NULL) {
        z80_symtab_clear(s_dasm_symtab);
    } else {
        s_dasm_symtab = z80_symtab_create();
        if (s_dasm_symtab == NULL) {
            s_dasm_symtab_version = cur_ver; /* zabraň busy-loop rebuild */
            return;
        }
    }

    size_t n = sym_db_count();
    for (size_t i = 0; i < n; i++) {
        const st_SYMBOL *s = sym_db_get_by_index(i);
        if (s == NULL || s->name == NULL) continue;
        /* V1 limit: jen globální (CPU view) symboly. */
        if (s->bank_id != 0) continue;
        z80_symtab_add(s_dasm_symtab, (uint16_t)(s->addr & 0xFFFFu),
                       s->name);
    }

    s_dasm_symtab_version = cur_ver;
}


const z80_symtab_t *symdb_bridge_get_symtab(void)
{
    sync_dasm_symtab();
    return s_dasm_symtab;
}


void symdb_bridge_destroy(void)
{
    if (s_dasm_symtab != NULL) {
        z80_symtab_destroy(s_dasm_symtab);
        s_dasm_symtab = NULL;
    }
    s_dasm_symtab_initialized = false;
    s_dasm_symtab_version = 0;
}

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
