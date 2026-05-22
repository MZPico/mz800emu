/*
 * File:   bp_zone.c
 *
 * Implementace banking-zone awareness pro smart BP (D.5).
 *
 * MZ-800 build (MZARCH == 800): plná implementace 7 zón nad
 * MEMORY_MZ800_MAP_TEST_* makry + PEHU bank detekce přes g_memext.map[].
 *
 * MZ-1500 build (MZARCH == 1500): rozšířené pokrytí 6 zón
 * (ROM_LOWER, ROM_UPPER, RAM, VRAM_FB, PCG, MMEXT_BANK) přes platformně
 * neutrální `memmap_query()`. CGROM (SPEC=1) + UNMAPPED + PROHIBITED
 * spadají na CPU_VIEW (= nejsou v BP_ZONE enumeraci).
 *
 * MZ-700 build (MZARCH == 700): identický rozsah 5 zón
 * (ROM_LOWER, ROM_UPPER, RAM, VRAM_FB, MMEXT_BANK). PCG MZ-700 nemá -
 * `BP_ZONE_PCG` zde vrací false.
 *
 * Detekce Memext PEHU banky je platformně neutrální (= memext sedí pod
 * bankingem; když banking mapuje RAM, memext jen určuje KTERÁ RAM bank).
 * Sdílí se přes `zone_addr_in_mmext_bank()` mimo `#if MZARCH` bloky.
 *
 * Doxygen: viz bp_zone.h.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPLv3, viz bp_zone.h.
 *
 * ---------------------------------------------------------------------------
 */

#include "mzarch/mzarch_config.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include "bp_zone.h"

#if MZARCH == 800
/* mz800_memory.h používá GDG_MZ800_DMD_TEST_* makra (g_gdg), takže
 * musíme includovat mz800_gdg.h před ním. memory/memory.h dává
 * deklaraci g_memory (= map flagy). */
#include "memory/memory.h"
#include "mzarch/mz800/gdg/mz800_gdg.h"
#include "mzarch/mz800/memory/mz800_memory.h"
#elif MZARCH == 1500
#include "memory/memory.h"
#include "mzarch/mz1500/memory/mz1500_memory.h"
#elif MZARCH == 700
#include "memory/memory.h"
#include "mzarch/mz700/memory/mz700_memory.h"
#endif

/* Memext je platformně neutrální (sedí pod bankingem), proto se
 * include dělá pro všechny architektury. */
#include "memory/memext.h"


/* ========================================================================= */
/*  Sdílené helpery (platformně neutrální)                                    */
/* ========================================================================= */

/**
 * @brief Zjistí, zda je addr v aktivně namapovaném Memext PEHU banku,
 *        který vyhovuje match pravidlu (SINGLE/RANGE/MASK).
 *
 * Memext PEHU sedí pod bankingem - pokud banking mapuje RAM na danou
 * 4 kB stránku, PEHU určuje, KTERÁ RAM bank (`g_memext.map[ap]`).
 * Detekce je proto platformně nezávislá (= funguje stejně na
 * MZ-700/800/1500).
 *
 * Mapping logika z `memext_map_pwrite()` pro PEHU:
 *   ap = addr_point & 0xFE (= sudý addr_point, PEHU bank = 8 KB)
 *   map[ap]   = bank_id * 2
 *   map[ap+1] = bank_id * 2 + 1
 *
 * Reverz: `pehu_bank = map[addr_point] >> 1` (pokud rawbank < 128;
 * vyšší hodnoty značí FLASH bit / non-PEHU bank).
 *
 * @param addr        Z80 adresa 0x0000-0xFFFF.
 * @param bank_mode   Match mód (SINGLE/RANGE/MASK).
 * @param bank_id     Reference bank.
 * @param bank_id_end Konec rozsahu pro RANGE.
 * @param bank_id_mask Maska pro MASK.
 * @return            true pokud Memext PEHU connected a aktuální bank
 *                    na stránce `addr>>12` vyhovuje match pravidlu.
 */
static bool zone_addr_in_mmext_bank ( uint16_t addr,
                                       en_BP_MATCH_MODE bank_mode,
                                       uint8_t bank_id,
                                       uint8_t bank_id_end,
                                       uint8_t bank_id_mask ) {
    if ( !MEMEXT_TEST_CONNECTED_PEHU ) return false;
    int addr_point = ( addr >> 12 ) & 0x0F;
    uint32_t rawbank = g_memext.map[ addr_point ];
    /* Pro PEHU rawbank je v rozsahu 0..127 (= 64 bank * 2).
     * pehu_bank = rawbank >> 1. */
    uint8_t actual = (uint8_t) ( rawbank >> 1 );
    return bp_match8 ( bank_mode, actual, bank_id, bank_id_end, bank_id_mask );
}


/**
 * @brief Zjistí, zda je addr v okně, kde aktuálně visí libovolný PEHU
 *        bank (= bez match check). Použito v `bp_zone_active_at_pc()`
 *        pro určení dominantní zóny.
 *
 * @param addr  Z80 adresa.
 * @return      true pokud Memext PEHU connected a rawbank < 128 (= valid
 *              PEHU bank, ne FLASH bit).
 */
static bool zone_addr_in_any_mmext_bank ( uint16_t addr ) {
    if ( !MEMEXT_TEST_CONNECTED_PEHU ) return false;
    int addr_point = ( addr >> 12 ) & 0x0F;
    uint32_t rawbank = g_memext.map[ addr_point ];
    return ( rawbank < 128 );
}


/* ========================================================================= */
/*  MZ-800 implementace                                                       */
/* ========================================================================= */

#if MZARCH == 800

/**
 * @brief Zjistí, zda je addr v aktuálně mapovaném VRAM okně (MZ-800).
 *
 * Sjednocuje 3 různá VRAM okna podle DMD:
 *   - MZ-800 320x200 / 320x200x16 / 640x200: 0x8000-0x9FFF (+ 0xA000-0xBFFF
 *     pro SCRW640 mód = 16K VRAM)
 *   - MZ-800 v MZ-700 modu (text): 0xD000-0xD7FF (CGRAM/atributová VRAM)
 */
static bool zone_addr_in_vram_window ( uint16_t addr ) {
    /* 0x8000-0x9FFF v non-MZ-700 modu pokud VRAM mapping aktivní. */
    if ( addr >= 0x8000 && addr < 0xA000 ) {
        if ( MEMORY_MZ800_MAP_TEST_VRAM_8000 ) return true;
    };
    /* 0xA000-0xBFFF v non-MZ-700 modu pokud SCRW640 (= 16K VRAM). */
    if ( addr >= 0xA000 && addr < 0xC000 ) {
        if ( MEMORY_MZ800_MAP_TEST_VRAM_A000 ) return true;
    };
    /* 0xD000-0xDFFF v MZ-700 modu pokud ROM_E000 unmap (= VRAM/atributy). */
    if ( addr >= 0xD000 && addr < 0xE000 ) {
        if ( MEMORY_MZ800_MAP_TEST_VRAM_D000 ) return true;
    };
    return false;
}


/**
 * @brief Zjistí, zda je addr v aktuálně mapovaném CGRAM okně (MZ-800).
 *
 * CGRAM (PCG) na MZ-800 v MZ-700 modu: 0xC000-0xCFFF. Detekce přes
 * MEMORY_MZ800_MAP_TEST_CGRAM (= MZ-700 mode + VRAM mapping flag).
 */
static bool zone_addr_in_cgram_window ( uint16_t addr ) {
    if ( addr >= 0xC000 && addr < 0xD000 ) {
        if ( MEMORY_MZ800_MAP_TEST_CGRAM ) return true;
    };
    return false;
}


bool bp_zone_is_active_at ( en_BP_ZONE zone, uint16_t addr,
                             en_BP_MATCH_MODE bank_mode,
                             uint8_t bank_id, uint8_t bank_id_end,
                             uint8_t bank_id_mask ) {
    switch ( zone ) {

        case BP_ZONE_CPU_VIEW:
            /* Default: vždy fire (= legacy CPU adresa, banking-agnostic). */
            return true;

        case BP_ZONE_ROM_LOWER:
            /* MZ-800 monitor ROM 0x0000-0x0FFF (ROM_0000) +
             * 0x1000-0x1FFF (ROM_1000). */
            if ( addr < 0x1000 ) return ( MEMORY_MZ800_MAP_TEST_ROM_0000 != 0 );
            if ( addr < 0x2000 ) return ( MEMORY_MZ800_MAP_TEST_ROM_1000 != 0 );
            return false;

        case BP_ZONE_ROM_UPPER:
            /* MZ-800 horní ROM 0xE000-0xFFFF (Sharp BASIC/disk loader).
             * V MZ-700 modu unmap ROM_E000 znamená VRAM/atributy na
             * D000 - tj. ROM_UPPER je aktivní jen pokud ROM_E000 = 1. */
            if ( addr >= 0xE000 ) return ( MEMORY_MZ800_MAP_TEST_ROM_E000 != 0 );
            return false;

        case BP_ZONE_RAM:
            /* RAM zone = "addr není v aktuálně mapované ROM/VRAM/PCG".
             * Backstop: pokud addr není ROM-mapped a není VRAM-mapped,
             * je to RAM (= g_memory.RAM[addr]). */
            if ( addr < 0x1000 && MEMORY_MZ800_MAP_TEST_ROM_0000 ) return false;
            if ( addr < 0x2000 && addr >= 0x1000 && MEMORY_MZ800_MAP_TEST_ROM_1000 ) return false;
            if ( addr >= 0xE000 && MEMORY_MZ800_MAP_TEST_ROM_E000 ) return false;
            if ( zone_addr_in_vram_window ( addr ) ) return false;
            if ( zone_addr_in_cgram_window ( addr ) ) return false;
            return true;

        case BP_ZONE_VRAM_FB:
            /* VRAM "RF" = read/write framebuffer přes RF okno. Rozsahy
             * dle aktuálního DMD - viz zone_addr_in_vram_window. */
            return zone_addr_in_vram_window ( addr );

        case BP_ZONE_PCG:
            /* PCG (programmable character generator) = CGRAM 0xC000-0xCFFF
             * v MZ-700 modu. */
            return zone_addr_in_cgram_window ( addr );

        case BP_ZONE_MMEXT_BANK:
            /* PEHU overlay bank: detekce je platformně neutrální -
             * viz `zone_addr_in_mmext_bank()` v hlavičce souboru. */
            return zone_addr_in_mmext_bank ( addr, bank_mode, bank_id,
                                              bank_id_end, bank_id_mask );

        case BP_ZONE_COUNT:
        default:
            return false;
    };
}


en_BP_ZONE bp_zone_active_at_pc ( uint16_t addr ) {
    /* Priority pořadí:
     *   ROM_LOWER (0x0000-0x1FFF) > ROM_UPPER (0xE000-0xFFFF) >
     *   VRAM_FB (mode-dependent) > PCG > PEHU_BANK > RAM > CPU_VIEW.
     *
     * Vrátíme nejspecifičtější zónu, která je aktuálně paged-in. */

    /* ROM_LOWER */
    if ( addr < 0x1000 && MEMORY_MZ800_MAP_TEST_ROM_0000 ) return BP_ZONE_ROM_LOWER;
    if ( addr >= 0x1000 && addr < 0x2000 && MEMORY_MZ800_MAP_TEST_ROM_1000 ) return BP_ZONE_ROM_LOWER;

    /* ROM_UPPER */
    if ( addr >= 0xE000 && MEMORY_MZ800_MAP_TEST_ROM_E000 ) return BP_ZONE_ROM_UPPER;

    /* VRAM_FB */
    if ( zone_addr_in_vram_window ( addr ) ) return BP_ZONE_VRAM_FB;

    /* PCG */
    if ( zone_addr_in_cgram_window ( addr ) ) return BP_ZONE_PCG;

    /* PEHU_BANK - vrátíme jen pokud Memext PEHU connected a addr_point
     * je mapovaný na nějaký valid PEHU bank. POZN: nevracíme jako "RAM"
     * protože uživatel chce vědět "tohle teď cílí na PEHU overlay". */
    if ( zone_addr_in_any_mmext_bank ( addr ) ) {
        return BP_ZONE_MMEXT_BANK;
    };

    /* RAM = default pro vše ostatní (= addr je v RAM mapování). */
    return BP_ZONE_RAM;
}

#endif  /* MZARCH == 800 */


/* ========================================================================= */
/*  MZ-1500 / MZ-700 implementace - kompletní zone resolve přes memmap_query  */
/* ========================================================================= */

#if (MZARCH == 1500) || (MZARCH == 700)

/**
 * @brief Zjistí, zda je addr v aktuálně mapovaném VRAM okně (MZ-700/1500).
 *
 * MZ-700: text VRAM + atributy na 0xD000-0xDFFF, mapování přes
 * ROM_E000 flag (= ROM_E000 unmapped == VRAM_D000 mapped).
 * MZ-1500: identický rozsah 0xD000-0xDFFF, detekce přes vlastní map
 * flag (MEMORY_MZ1500_MAP_TEST_D000_VRAM = ROM_UPPER + SPEC=0).
 */
static bool zone_addr_in_vram_window ( uint16_t addr ) {
    if ( addr >= 0xD000 && addr < 0xE000 ) {
#if MZARCH == 700
        if ( MEMORY_MZ700_MAP_TEST_VRAM_D000 ) return true;
#elif MZARCH == 1500
        if ( MEMORY_MZ1500_MAP_TEST_D000_VRAM ) return true;
#endif
    }
    return false;
}


#if MZARCH == 1500
/**
 * @brief Zjistí, zda je addr v aktuálně mapovaném PCG (Programmable
 *        Character Generator) okně (jen MZ-1500).
 *
 * MZ-1500 specifická feature - 3 PCG banky (PCG1/2/3) přepínané přes
 * SPEC bity (porty $CF/$CE). Aktivní PCG bank zabírá celých
 * 0xD000-0xEFFF (= 2 stránky 0x0D a 0x0E současně) když ROM_UPPER set.
 *
 * Detekce přes `memmap_query()` per stránku - vrátí
 * MEMMAP_KIND_PCG_1/2/3 pokud je SPEC=2/3/4 a ROM_UPPER set.
 */
static bool zone_addr_in_pcg_window ( uint16_t addr ) {
    if ( addr < 0xD000 || addr >= 0xF000 ) return false;
    en_MEMMAP_REGION_KIND kind = memmap_query ( (uint8_t) ( addr >> 12 ) );
    return ( kind == MEMMAP_KIND_PCG_1
          || kind == MEMMAP_KIND_PCG_2
          || kind == MEMMAP_KIND_PCG_3 );
}
#endif  /* MZARCH == 1500 */


bool bp_zone_is_active_at ( en_BP_ZONE zone, uint16_t addr,
                             en_BP_MATCH_MODE bank_mode,
                             uint8_t bank_id, uint8_t bank_id_end,
                             uint8_t bank_id_mask ) {
    /* Pro nestížené zóny (ROM_*, RAM, VRAM_FB, PCG) jsou bank_*
     * parametry ignorovány - používá je jen MMEXT_BANK case. */
    (void) bank_mode;
    (void) bank_id;
    (void) bank_id_end;
    (void) bank_id_mask;

    /* `memmap_query()` mapuje 4 kB stránku (= addr >> 12) na konkrétní
     * druh regionu reflektující aktuální banking. */
    en_MEMMAP_REGION_KIND kind = memmap_query ( (uint8_t) ( addr >> 12 ) );

    switch ( zone ) {
        case BP_ZONE_CPU_VIEW:
            return true;

        case BP_ZONE_ROM_LOWER:
            /* MZ-700/1500: ROM_LOW = stránka $0000-$0FFF s mapovanou
             * Monitor ROM. (POZN: na rozdíl od MZ-800 zde není
             * ROM_1000 = horní polovina Monitor ROM, ta neexistuje.) */
            return ( kind == MEMMAP_KIND_ROM_LOW );

        case BP_ZONE_ROM_UPPER:
            /* MZ-700/1500: horní ROM 0xE000-0xFFFF zahrnuje
             * 0xE000-0xE00F mapped ports + 0xE800-0xFFFF Monitor ROM.
             * Oba regiony jsou dostupné jen s ROM_E000/ROM_UPPER set,
             * takže obě kategorie z memmap_query (MAPPED_PORTS,
             * ROM_HIGH) značí ROM_UPPER. */
            return ( kind == MEMMAP_KIND_ROM_HIGH
                  || kind == MEMMAP_KIND_MAPPED_PORTS );

        case BP_ZONE_RAM:
            return ( kind == MEMMAP_KIND_RAM );

        case BP_ZONE_VRAM_FB:
            return zone_addr_in_vram_window ( addr );

        case BP_ZONE_PCG:
#if MZARCH == 1500
            return zone_addr_in_pcg_window ( addr );
#else
            /* MZ-700 nemá PCG ani CG-RAM (PCG je MZ-1500 specifická
             * feature; CG-RAM je MZ-800 v MZ-700 modu). */
            return false;
#endif

        case BP_ZONE_MMEXT_BANK:
            /* Memext PEHU detekce sdílí platformně-neutrální helper -
             * viz `zone_addr_in_mmext_bank()` v hlavičce souboru. */
            return zone_addr_in_mmext_bank ( addr, bank_mode, bank_id,
                                              bank_id_end, bank_id_mask );

        case BP_ZONE_COUNT:
        default:
            return false;
    }
}


en_BP_ZONE bp_zone_active_at_pc ( uint16_t addr ) {
    /* Priority pořadí (analogicky MZ-800):
     *   ROM_LOWER > ROM_UPPER > VRAM_FB > PCG (MZ-1500) > MMEXT_BANK >
     *   RAM > CPU_VIEW.
     *
     * CGROM (MZ-1500 SPEC=1), PROHIBITED, UNMAPPED nemají odpovídající
     * BP zone - spadnou na CPU_VIEW (= fallback bez specifické zone
     * klasifikace). */

    en_MEMMAP_REGION_KIND kind = memmap_query ( (uint8_t) ( addr >> 12 ) );

    /* ROM_LOWER */
    if ( kind == MEMMAP_KIND_ROM_LOW ) return BP_ZONE_ROM_LOWER;

    /* ROM_UPPER (= ROM_HIGH | MAPPED_PORTS) */
    if ( kind == MEMMAP_KIND_ROM_HIGH || kind == MEMMAP_KIND_MAPPED_PORTS )
        return BP_ZONE_ROM_UPPER;

    /* VRAM_FB */
    if ( zone_addr_in_vram_window ( addr ) ) return BP_ZONE_VRAM_FB;

#if MZARCH == 1500
    /* PCG (jen MZ-1500) */
    if ( zone_addr_in_pcg_window ( addr ) ) return BP_ZONE_PCG;
#endif

    /* MMEXT_BANK - jen pokud RAM stránka a Memext PEHU connected.
     * (PEHU sedí pod bankingem = mapuje konkrétní bank do RAM regionu,
     * proto kontrolujeme jen pokud kind == RAM.) */
    if ( kind == MEMMAP_KIND_RAM && zone_addr_in_any_mmext_bank ( addr ) )
        return BP_ZONE_MMEXT_BANK;

    /* RAM = běžná DRAM stránka. */
    if ( kind == MEMMAP_KIND_RAM ) return BP_ZONE_RAM;

    /* CGROM / PROHIBITED / UNMAPPED nemají BP_ZONE odpovídající
     * kategorii. Fallback na CPU_VIEW. */
    return BP_ZONE_CPU_VIEW;
}

#endif  /* MZARCH == 1500 || 700 */

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
