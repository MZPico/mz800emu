/*
 * dbgapi_regions_arch.h - per-arch interface pro REGION_LIST enumeraci
 *
 * Internal header (= jen pro dbgapi_regions.c + per-arch implementace v
 * src/emulator/mzarch/<arch>/debugger/<arch>_regions.c). Veřejný kontrakt
 * je v dbgapi_regions.h.
 *
 * Per-arch implementace musí definovat funkci:
 *   int <arch>_regions_collect(st_REGION_DESC *out, int max_count);
 *
 * která naplní `out[0..count-1]` s arch-specific regiony. Společné jádro
 * (dbgapi_regions.c) volá tuto funkci přes compile-time dispatch dle
 * MZARCH a uloží snapshot do interní cache pro následující read/write.
 *
 * Licence: GPLv3
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

#ifndef DBGAPI_REGIONS_ARCH_H
#define DBGAPI_REGIONS_ARCH_H

#include "dbgapi_regions.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Per-arch helper: přidá jeden region do výstupního pole.
 *
 * Společná inline funkce sdílená všemi per-arch implementacemi. Použití
 * minimalizuje boilerplate a zajišťuje konzistentní postup (snprintf jména,
 * memset zbytku st_REGION_DESC).
 *
 * Inkrementuje *p_count při úspěšném zápisu. Pokud max_count překročen,
 * pouze inkrementuje counter bez zápisu (caller pak vidí kolik regionů
 * by se naplnilo, kdyby bylo větší pole - užitečné pro detekci truncation).
 *
 * @param[in,out] out          Výstupní pole st_REGION_DESC.
 * @param[in,out] p_count      Aktuální count (read+write).
 * @param[in]     max_count    Velikost pole `out`.
 * @param[in]     kind         Druh regionu.
 * @param[in]     sub_id       Disambiguator.
 * @param[in]     name         ASCII jméno (anglicky, max 63 znaků).
 * @param[in]     logical_base Z80 adresa nebo 0xFFFFFFFF.
 * @param[in]     size         Velikost v bajtech.
 * @param[in]     writable     1/0.
 * @param[in]     connected    1/0.
 * @param[in]     mapped_now   1/0.
 */
static inline void dbgapi_regions_add(
    st_REGION_DESC *out, int *p_count, int max_count,
    en_REGION_KIND kind, int sub_id, const char *name,
    uint32_t logical_base, uint32_t size,
    int writable, int connected, int mapped_now)
{
    int idx = *p_count;
    (*p_count)++;
    if (idx >= max_count) return;
    st_REGION_DESC *d = &out[idx];
    d->id = idx;
    d->kind = kind;
    d->sub_id = sub_id;
    /* Bezpečná kopie jména s '\0'-termination. */
    int i;
    for (i = 0; i < (int)(sizeof(d->name) - 1) && name[i]; i++) {
        d->name[i] = name[i];
    }
    d->name[i] = '\0';
    /* Zbytek bufferu vynulujeme (deterministická paměť pro snapshot diff). */
    for (; i < (int)sizeof(d->name); i++) {
        d->name[i] = '\0';
    }
    d->logical_base = logical_base;
    d->size = size;
    d->writable = writable;
    d->connected = connected;
    d->mapped_now = mapped_now;
}

/**
 * @brief Per-arch enumerate (MZ-800).
 *
 * Naplní `out` regiony specifickými pro MZ-800: LOGICAL, RAM, ROM_LOWER,
 * ROM_UPPER, CGROM, CGRAM_700 (jen MZ-700 mode), VRAM_700_CHAR/ATTR (jen
 * MZ-700 mode), 4× VRAM_PHYS_PLANE (plane III/IV vždy enumerované -
 * EXVRAM pole je vždy alokované v st_MEMORY [neověřeno emu detection
 * exVRAM connected flagu]), MEMEXT_RAM/FLASH banks (pokud connected;
 * Luftner = 128 RAM + 128 FLASH, PEHU = 64 RAM + 0 FLASH, bank size 4 KB),
 * RAMDISK_STD banks (pokud connected, 64 KB každá),
 * RAMDISK_PEZIK per-bank (8 banek × 2 instance = až 16 regionů po 64 KB;
 * sub_id encoding = pezik_instance * 8 + bank_idx),
 * PROHIBITED_SHADOW (pokud aktivní).
 *
 * @return Skutečný počet regionů (může přesáhnout max_count - viz pozn.
 *         u dbgapi_regions_add).
 */
int mz800_regions_collect(st_REGION_DESC *out, int max_count);

/**
 * @brief Per-arch enumerate (MZ-700).
 *
 * LOGICAL, RAM, ROM_LOWER, ROM_UPPER, VRAM_700_CHAR/ATTR,
 * MEMEXT_RAM/FLASH banks (pokud connected; port 0xE7), RAMDISK_STD
 * (pokud connected, port 0xEA), RAMDISK_PEZIK pi=0 (pokud connected,
 * port 0x68 - PEZIK 0xE8 není na MZ-700 v IORQ namapován),
 * PROHIBITED_SHADOW. Bez CG-RAM (na MZ-700 standalone neexistuje, je
 * to artefakt MZ-800 v 700 compat módu) a bez VRAM plane.
 */
int mz700_regions_collect(st_REGION_DESC *out, int max_count);

/**
 * @brief Per-arch enumerate (MZ-1500).
 *
 * LOGICAL, RAM, ROM_LOWER, ROM_UPPER, CGROM, VRAM_700_CHAR/ATTR, 3× PCG_1500,
 * MEMEXT_RAM/FLASH banks (pokud connected; MZ-1500 má port 0xE7 stejný jako
 * MZ-800), RAMDISK_STD (pokud connected, port 0xEA), RAMDISK_PEZIK pi=0
 * (pokud connected, port 0x68 - PEZIK 0xE8 není na MZ-1500 v IORQ
 * namapován, port 0xE9 koliduje s PSG). Bez PROHIBITED (MZ-1500 koncept
 * nemá).
 */
int mz1500_regions_collect(st_REGION_DESC *out, int max_count);

#ifdef __cplusplus
}
#endif

#endif /* DBGAPI_REGIONS_ARCH_H */
