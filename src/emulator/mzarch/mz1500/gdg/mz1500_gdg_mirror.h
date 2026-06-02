/**
 * @file mz1500_gdg_mirror.h
 * @brief Side-effect free read API pro GDG state MZ-1500 (gdg-panel F2).
 *
 * Viz mz800_gdg_mirror.h pro popis koncepce. Zde per-arch varianta pro
 * MZ-1500, který má (stejně jako MZ-700) menší field set než MZ-800:
 *
 *  - regDMD pole se liší: bit 0 = MODE (0 = MZ-700, 1 = MZ-1500 PCG),
 *    bit 1 = PRIORITY (BPF / BFP).
 *  - regBOR existuje, ale MZ-1500 nemá border port, hodnota je vždy 0.
 *  - Místo MZ-800 palette registrů má MZ-1500 `mode1500_color[8]`.
 *
 * Side-effect free invariant: `gdg_mirror_snapshot()` jen čte `g_gdg`,
 * `g_memory.map` a (pro pcg_active_cells) read-only sken VRAM PCGHI
 * regionu (0xC00-0xFFF). Žádný state mutation.
 *
 * F5 PCG state: PCG global stav nesídli v `st_GDG` (ten drží jen
 * regDMD MODE/PRIORITY a barevnou paletu `mode1500_color[8]`). Skutečné
 * "PCG enabled per pozice" je bit 3 v PCGHI VRAM byte (per-buňka),
 * žádný global enable flag neexistuje. PCG data jsou ve `g_memory.PCG`
 * = 3 banky po 8 KB (`MEMORY_SIZE_PCG_BANK`), namapování banky do CPU
 * adresního prostoru (D000-EFFF) řídí `g_memory.map` SPEC pole
 * (`MEMORY_MZ1500_SPEC_ID` = 0..4, 2-4 = PCG1-3).
 *
 * Co stále NENI součástí mirror snapshot:
 *  - Per-position attribute RAM (CHAR/ATTR/PCGLO/PCGHI) - patří VRAM
 *    okno (= 4 KB v `g_memory.VRAM`), tady neduplikujeme.
 *  - Dump PCG pattern dat - patří Memory Map / VRAM browser, ne global
 *    debug panel.
 *
 * License: GPLv3.
 */

#ifndef MZ1500_GDG_MIRROR_H
#define MZ1500_GDG_MIRROR_H

#include "mzarch/mzarch_config.h"

#if MZARCH == 1500

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Snapshot stavu MZ-1500 GDG určený pro UI debugger.
 *
 * POD struktura, žádné pointery na živá data. Viz mz800_gdg_mirror.h
 * pro identické invariantni pravidla.
 */
typedef struct st_GDG_MIRROR_MZ1500
{
    /* ---- DMD register (port 0xF0) -------------------------------------- */
    uint8_t  regDMD;        /**< Surová hodnota DMD registru (low 2 bity). */
    uint8_t  dmd_mode1500;  /**< Bit 0: 1 = MZ-1500 PCG mód, 0 = MZ-700 kompat. */
    uint8_t  dmd_pmode_bfp; /**< Bit 1: 0 = BPF, 1 = BFP priority. */

    /* ---- Border ------------------------------------------------------- */
    uint8_t  regBOR;        /**< Border color - MZ-1500 nemá border port,
                             *   vždy 0 (ponecháno pro symetrii). */

    /* ---- Palette ------------------------------------------------------- */
    int      mode1500_color[8]; /**< MZ-1500 paleta (8 indexů, port 0xF1). */

    /* ---- Raster pozice ------------------------------------------------- */
    unsigned beam_row;      /**< Aktuální řádek paprsku. */
    unsigned screen_ticks;  /**< Pozice v aktuálním screenu (pixel tick). */
    unsigned screens_done;  /**< Celkový počet vykreslených snímků. */

    uint8_t  hbln;          /**< HBLN signál: 0 = mimo screen, 1 = on. */
    uint8_t  vbln;          /**< VBLN signál: 0 = mimo řádek, 1 = on. */
    uint8_t  sts_hsync;     /**< STS HSYNC viditelný v Status registru. */
    uint8_t  sts_vsync;     /**< STS VSYNC viditelný v Status registru. */

    /* ---- Ostatní ------------------------------------------------------- */
    uint8_t  tempo;         /**< Tempo signál (1 Hz bit). */
    unsigned tempo_divider; /**< Aktuální stav děliče pro tempo. */
    uint8_t  regct53g7;     /**< Stav řízení GATE pro CTC0. */

    /* ---- PCG (Programmable Character Generator) ------------------------ */
    /**
     * @brief SPEC pole z `g_memory.map` - která PCG banka je namapovaná
     *        do CPU adresního prostoru D000-EFFF.
     *
     * Hodnoty: 0 = žádné mapování (VRAM/RAM), 1 = CGROM, 2 = PCG1,
     * 3 = PCG2, 4 = PCG3. Mapování je aktivní pouze pokud
     * `MEMORY_MZ1500_MAP_TEST_D000_SPEC` (= ROM_UPPER aktivní).
     */
    uint8_t  pcg_spec_id;
    /**
     * @brief Indikátor zda je D000-EFFF aktuálně v SPEC mappingu
     *        (= PCG nebo CGROM viditelné pro CPU).
     *
     * 1 = SPEC mapping aktivní (závisí na `pcg_spec_id`), 0 = VRAM/RAM.
     */
    uint8_t  pcg_spec_active;
    /**
     * @brief Velikost jedné PCG banky v bajtech.
     *
     * Konstanta `MEMORY_SIZE_PCG_BANK` (= 0x2000, 8 KB). Vystaveno
     * pro UI bez závislosti na include `memory.h`.
     */
    unsigned pcg_bank_size;
    /**
     * @brief Počet PCG bank.
     *
     * Konstanta 3 (PCG1, PCG2, PCG3). Vystaveno pro UI.
     */
    unsigned pcg_bank_count;
    /**
     * @brief Počet znakových pozic s aktivním PCG overlay.
     *
     * Spočítáno side-effect-free skenem PCGHI regionu VRAM
     * (0xC00-0xFFF, 1024 bajtů = 40x25 + padding) na bit 3
     * (`MZ1500_ATTRPCG_ENABLED = 0x08`). Rozsah 0..1024.
     *
     * Hodnota indikuje, zda aplikace PCG vůbec používá v aktuálním
     * snímku (= např. po POWER ON nebo v čistém MZ-700 modu by mělo
     * být 0).
     */
    unsigned pcg_active_cells;
} st_GDG_MIRROR_MZ1500;

/**
 * @brief Vyplní snapshot strukturu aktuálním stavem `g_gdg` (MZ-1500).
 *
 * Side-effect free: čte pouze raw fieldy `g_gdg`. Žádný state mutation.
 *
 * @param out  Pointer na cílovou strukturu (nesmí být NULL).
 *
 * @pre  `out != NULL`. Emulátor inicializován.
 * @post `*out` obsahuje aktuální snapshot.
 */
void gdg_mirror_snapshot(st_GDG_MIRROR_MZ1500 *out);

#ifdef __cplusplus
}
#endif

#endif /* MZARCH == 1500 */

#endif /* MZ1500_GDG_MIRROR_H */
