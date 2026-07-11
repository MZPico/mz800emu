/*
 * File:   bp_zone.h
 *
 * Banking-zone awareness pro smart breakpointy (fáze D.5).
 *
 * Zone awareness umožňuje BP cílit konkrétní paměťovou zónu (ROM_LOWER,
 * ROM_UPPER, RAM, VRAM_FB, PCG, MMEXT_BANK), nezávisle na momentálním
 * banking stavu CPU view. Bez zone awareness BP používá CPU adresu a
 * fire jen když je požadovaný region paged-in (= legacy chování,
 * BP_ZONE_CPU_VIEW).
 *
 * Příklad use case (mzdos overlay):
 *   - PEHU memext bank 9 obsahuje ccp.ovl, mapuje se do okna 0x2000-0x3FFF
 *   - BP `type=PC_EXEC, addr=0x2080, zone=MMEXT_BANK, bank_id=9` fire
 *     jen když PEHU bank 9 je aktuálně v okně 0x2000 (= addr_point=2,
 *     map[2] == 9*2 nebo 9*2+1)
 *   - Když uživatel přepne overlay na bank 10, stejný BP nesmí fire
 *
 * Pokrytí platforem: MZ-700 + MZ-800 + MZ-1500.
 * MZ-800 používá vlastní MEMORY_MZ800_MAP_TEST_* makra (DMD + banking
 * flagy). MZ-700 a MZ-1500 sdílí kód nad platformně-neutrálním
 * memmap_query() helperem (memory.h). MMEXT_BANK detekce je
 * platformně neutrální a sdílí se přes statický helper
 * zone_addr_in_mmext_bank() v bp_zone.c.
 *
 * ----------------------------- License -------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef BP_ZONE_H
#define BP_ZONE_H

#include <stdint.h>
#include <stdbool.h>

#include "breakpoints.h"        /* en_BP_ZONE */

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Vrátí, zda daná zóna pokrývá addr a je aktuálně paged-in.
 *
 * Rozhoduje o tom, zda BP s daným zone+addr má fire pro aktuální banking
 * stav. Pro BP_ZONE_CPU_VIEW vždy true (= co PC vidí teď). Pro ostatní
 * zóny závisí na per-arch banking stavu (MZ-800: MEMORY_MZ800_MAP_TEST_*
 * makra; MZ-700/1500: memmap_query() z hw-generic/memory/memory.h).
 *
 * Sémantika per zóna (společná všem 3 archům, není-li uvedeno jinak):
 *   - BP_ZONE_CPU_VIEW   - vždy true (= legacy CPU adresa)
 *   - BP_ZONE_ROM_LOWER  - dolní Monitor ROM mapped
 *                          (MZ-800: 0x0000-0x1FFF přes ROM_0000/ROM_1000;
 *                          MZ-700/1500: kind == MEMMAP_KIND_ROM_LOW)
 *   - BP_ZONE_ROM_UPPER  - horní ROM mapped (MZ-800: 0xE000-0xFFFF +
 *                          ROM_E000; MZ-700/1500: kind == ROM_HIGH ||
 *                          MAPPED_PORTS - vč. 0xE000-0xE00F mapped ports)
 *   - BP_ZONE_RAM        - addr je v RAM mapování (= ne ROM, ne VRAM,
 *                          ne PCG, ne mapped ports)
 *   - BP_ZONE_VRAM_FB    - addr je v VRAM-mapped okně (MZ-800:
 *                          8000/A000/D000 podle DMD; MZ-700/1500:
 *                          0xD000-0xDFFF)
 *   - BP_ZONE_PCG        - MZ-800 v MZ-700 modu: CGRAM 0xC000-0xCFFF;
 *                          MZ-1500: PCG bank 1/2/3 v 0xD000-0xEFFF
 *                          (SPEC=2/3/4); MZ-700: vždy false (HW chybí)
 *   - BP_ZONE_MMEXT_BANK - addr_point (= addr >> 12) je aktuálně
 *                          mapovaný na bank, který odpovídá pravidlu
 *                          (bank_mode + bank_id + bank_id_end + bank_id_mask)
 *                          přes bp_match8(). Vyžaduje memext PEHU connected,
 *                          jinak false. Detekce je platformně neutrální.
 *
 * CGROM (MZ-1500 SPEC=1), PROHIBITED a UNMAPPED kategorie nemají
 * odpovídající BP_ZONE konstantu - bp_zone_active_at_pc() pro tyto
 * případy vrací BP_ZONE_CPU_VIEW (= fallback).
 *
 * V1.5.E - signature rozšířena o bank match mode/end/mask. Pro non-
 * MMEXT_BANK zóny jsou tyto parametry ignorovány. Caller musí předat
 * konzistentní hodnoty z bpt->bank_match_mode / bpt->bank_id /
 * bpt->bank_id_end / bpt->bank_id_mask.
 *
 * @param zone           Cílová zóna BP.
 * @param addr           CPU 16-bit adresa (hit_addr z enforce hooku).
 * @param bank_mode      Match mode pro bank (BP_MATCH_SINGLE/RANGE/MASK).
 * @param bank_id        Memext bank index (= ref hodnota pro match).
 * @param bank_id_end    RANGE upper bound (relevant jen pro BP_MATCH_RANGE).
 * @param bank_id_mask   AND mask (relevant jen pro BP_MATCH_MASK).
 * @return true, pokud zóna pokrývá addr a je aktuálně paged-in.
 *
 * Side effects: žádné (čte jen globální stav g_memory.map / g_memext.map).
 * Threading: jen z EMU vlákna (= caller je enforce hook).
 */
extern bool bp_zone_is_active_at ( en_BP_ZONE zone, uint16_t addr,
                                    en_BP_MATCH_MODE bank_mode,
                                    uint8_t bank_id, uint8_t bank_id_end,
                                    uint8_t bank_id_mask );


/**
 * @brief Vrátí nejspecifičtější aktuálně paged-in zónu pro danou addr.
 *
 * Použití: debugger UI pro zobrazení "BP na 0x0042 = ROM_LOWER" / "RAM"
 * apod. Také naplnění bp_expr_ctx_t::BankPC / BankAddr (= výraz
 * `BankPC == 1` v condition).
 *
 * Priorita (od nejspecifičtější):
 *   1. ROM_LOWER pokud je dolní Monitor ROM mapped
 *   2. ROM_UPPER pokud je horní ROM mapped (vč. MAPPED_PORTS na MZ-700/1500)
 *   3. VRAM_FB / PCG (= mode-dependent okno; PCG jen MZ-800/1500)
 *   4. MMEXT_BANK (pokud memext PEHU connected a addr je v mapovaném
 *      windows; na MZ-700/1500 jen pokud kind == RAM)
 *   5. RAM (default fallback pro adresy které nejsou ROM/VRAM/PCG)
 *   6. CPU_VIEW (= fallback pro CGROM / PROHIBITED / UNMAPPED kategorie,
 *      které nemají odpovídající BP_ZONE)
 *
 * @param addr  CPU 16-bit adresa.
 * @return en_BP_ZONE - zóna aktuálně paged-in pro addr.
 */
extern en_BP_ZONE bp_zone_active_at_pc ( uint16_t addr );


/**
 * @brief Přepočte Z80 write adresu na offset v rámci PEHU banky (feature D).
 *
 * PEHU bank = 8 KB = dvě 4 KB stránky. rawbank sudý = dolní půl banky
 * (offset 0x000-0xFFF), lichý = horní půl (0x1000-0x1FFF).
 *
 * @param addr Z80 adresa 0x0000-0xFFFF.
 * @return offset 0x0000-0x1FFF, nebo -1 když addr nemíří do platné PEHU
 *         banky (PEHU nepřipojen nebo rawbank>=128).
 */
extern int32_t mmext_pehu_offset_from_addr ( uint16_t addr );


#ifdef __cplusplus
}
#endif

#endif /* BP_ZONE_H */
