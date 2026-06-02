/**
 * @file mz700_gdg_mirror.h
 * @brief Side-effect free read API pro GDG state MZ-700 (gdg-panel F2).
 *
 * Viz mz800_gdg_mirror.h pro popis koncepce. Zde per-arch varianta pro
 * MZ-700, který má jiný field set než MZ-800:
 *
 *  - regDMD pole má jiný význam (bit 0 = MODE, bit 1 = PRIORITY).
 *  - regBOR existuje, ale MZ-700 nemá border port, hodnota je vždy 0.
 *  - Místo MZ-800 palette registrů má MZ-700 `mode700_color[8]` - paleta
 *    je 8-položková (port 0xF1).
 *  - Žádný regPALGRP / regPAL0..3 / cksw / vram800 hot phase.
 *
 * Side-effect free invariant: `gdg_mirror_snapshot()` jen čte `g_gdg`,
 * žádný state mutation.
 *
 * Co NENI součástí mirror snapshot (TODO [F4-resolve]):
 *  - VRAM split (text vs attribute) - žije v memory/vramctrl modulu, NE
 *    v `st_GDG`. F4 musí dohledat.
 *  - Attribute RAM snapshot - sídlí ve VRAM regionu, ne v GDG struct.
 *    F4 si bude číst z `g_memory` přes existující memory API.
 *  - NE556 cursor blink - cursor_timer je v `g_mzarch_main`, NE v GDG.
 *    F4 si dotáhne přes mzarch API nebo přidá samostatný getter.
 *
 * License: GPLv3.
 */

#ifndef MZ700_GDG_MIRROR_H
#define MZ700_GDG_MIRROR_H

#include "mzarch/mzarch_config.h"

#if MZARCH == 700

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Snapshot stavu MZ-700 GDG určený pro UI debugger.
 *
 * Struktura je čistě hodnotová (POD), bez pointerů na živá data. Viz
 * popis invariantů v mz800_gdg_mirror.h - identický kontrakt.
 */
typedef struct st_GDG_MIRROR_MZ700
{
    /* ---- DMD register (port 0xF0) -------------------------------------- */
    uint8_t  regDMD;        /**< Surová hodnota DMD registru (low 2 bity). */
    uint8_t  dmd_mode700;   /**< Bit 0: 1 = MZ-700 mód (vs MZ-700 default 0). */
    uint8_t  dmd_pmode_bfp; /**< Bit 1: 0 = BPF, 1 = BFP priority. */

    /* ---- Border ------------------------------------------------------- */
    uint8_t  regBOR;        /**< Border color - MZ-700 nemá border port,
                             *   hodnota je vždy 0 (ponecháno pro symetrii
                             *   s MZ-800 mirror). */

    /* ---- Palette ------------------------------------------------------- */
    int      mode700_color[8]; /**< MZ-700 paleta (8 indexů, port 0xF1). */

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
} st_GDG_MIRROR_MZ700;

/**
 * @brief Vyplní snapshot strukturu aktuálním stavem `g_gdg` (MZ-700).
 *
 * Side-effect free: čte pouze raw fieldy `g_gdg`. Žádný state mutation.
 *
 * @param out  Pointer na cílovou strukturu (nesmí být NULL).
 *
 * @pre  `out != NULL`. Emulátor inicializován.
 * @post `*out` obsahuje aktuální snapshot.
 */
void gdg_mirror_snapshot(st_GDG_MIRROR_MZ700 *out);

#ifdef __cplusplus
}
#endif

#endif /* MZARCH == 700 */

#endif /* MZ700_GDG_MIRROR_H */
