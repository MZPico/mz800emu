/**
 * @file vram_sim_modes.h
 * @brief Interní hlavička pro per-mode dispatch v vram_sim knihovně.
 *
 * Není určeno pro public consumption - interní detail mezi vram_sim.c a
 * vram_sim_modes.c. Veřejné API je výhradně ve `vram_sim.h`.
 *
 * Per-mode dispatch tabulky umožňují čistší kód než velký nested switch
 * v emu reference - každý mód má vlastní helper funkci pro read/write,
 * snazší testování + debug per-mode.
 */

#ifndef VRAM_SIM_MODES_H
#define VRAM_SIM_MODES_H

#include <stddef.h>             /* NULL (Linux: stdint.h nezahrnuje tranzitivně) */

#include "vram_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Bitové masky plane (interní)
 * ------------------------------------------------------------------------- */

/** Maska plane I v rf_plane / wf_plane / avlb_plane bitmasce. */
#define VS_PLANE1 (1u << 0)
/** Maska plane II. */
#define VS_PLANE2 (1u << 1)
/** Maska plane III. */
#define VS_PLANE3 (1u << 2)
/** Maska plane IV. */
#define VS_PLANE4 (1u << 3)
/** Maska všech 4 plane. */
#define VS_PLANE_ALL (VS_PLANE1 | VS_PLANE2 | VS_PLANE3 | VS_PLANE4)

/* ---------------------------------------------------------------------------
 * DMD bit test makra (interní)
 * ------------------------------------------------------------------------- */

/** Test DMD bit 0 (VBANK) - používáno jen pro display-side, ne CPU R/W. */
#define VS_DMD_TEST_VBANK_DMD(s)  ((s)->dmd & 0x01)
/** Test DMD bit 1 (HICOLOR) - 1 = 4/16 barev, 0 = 2 barvy. */
#define VS_DMD_TEST_HICOLOR(s)    ((s)->dmd & 0x02)
/** Test DMD bit 2 (SCRW640) - 1 = 640x200, 0 = 320x200. */
#define VS_DMD_TEST_SCRW640(s)    ((s)->dmd & 0x04)
/** Test DMD bit 3 (MZ700) - 1 = MZ-700 charakterový mód. */
#define VS_DMD_TEST_MZ700(s)      ((s)->dmd & 0x08)

/* ---------------------------------------------------------------------------
 * Interní helpers
 * ------------------------------------------------------------------------- */

/**
 * @brief Vrátí ukazatel na plane (read-only), nebo NULL.
 *
 * Aplikuje pravidla `has_exvram`: pokud `idx >= 2` (plane III/IV) a
 * `has_exvram == 0`, vrací NULL = simuluje "plane neexistuje".
 *
 * @param[in] state
 * @param[in] idx 0..3
 * @return Pointer nebo NULL
 */
static inline const uint8_t *vs_plane_ro(const st_VRAM_SIM_STATE *state, int idx)
{
    if (idx < 0 || idx >= VRAM_SIM_PLANE_COUNT) return NULL;
    if (idx >= 2 && !state->has_exvram) return NULL;
    return state->plane_data[idx];
}

/**
 * @brief Vrátí ukazatel na plane (writable), nebo NULL.
 *
 * Stejná pravidla jako `vs_plane_ro()`, ale na `plane_data_writable[]`.
 * Write je tichý skip pokud výsledek NULL.
 */
static inline uint8_t *vs_plane_rw(st_VRAM_SIM_STATE *state, int idx)
{
    if (idx < 0 || idx >= VRAM_SIM_PLANE_COUNT) return NULL;
    if (idx >= 2 && !state->has_exvram) return NULL;
    return state->plane_data_writable[idx];
}

/**
 * @brief Pomocná funkce - čte bajt z plane se safety checkem.
 *
 * Pokud plane není dostupný (NULL nebo idx>=2 bez exVRAM), vrací 0xFF
 * (matching emu chování pro chybějící exVRAM).
 *
 * @param[in] state
 * @param[in] idx        0..3
 * @param[in] phy_offset 0..0x1FFF (caller responsible za mask)
 * @return Hodnota bajtu nebo 0xFF
 */
static inline uint8_t vs_read_plane_byte(const st_VRAM_SIM_STATE *state, int idx,
                                         uint16_t phy_offset)
{
    const uint8_t *p = vs_plane_ro(state, idx);
    if (!p) return 0xFF;
    return p[phy_offset];
}

/* ---------------------------------------------------------------------------
 * Per-mode read/write dispatch (implementováno v vram_sim_modes.c)
 * ------------------------------------------------------------------------- */

/**
 * @brief Interní read entry point - per-mode dispatch pro READ mode (RF_SEARCH=0).
 */
extern uint8_t vs_read_normal(const st_VRAM_SIM_STATE *state,
                              uint16_t addr, uint16_t phy);

/**
 * @brief Interní read entry point - per-mode dispatch pro SEARCH mode (RF_SEARCH=1).
 */
extern uint8_t vs_read_search(const st_VRAM_SIM_STATE *state,
                              uint16_t addr, uint16_t phy);

/**
 * @brief Interní write entry point - per-mode WE compute + per-plane WMD aplikace.
 */
extern void vs_write_byte(st_VRAM_SIM_STATE *state,
                          uint16_t addr, uint16_t phy, uint8_t value);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VRAM_SIM_MODES_H */
