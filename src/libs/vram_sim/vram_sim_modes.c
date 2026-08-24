/**
 * @file vram_sim_modes.c
 * @brief Per-mode read/write dispatch pro MZ-800 VRAM simulator.
 *
 * Port logiky z `mz800_vramctrl.c` (read: ř. 203-594, write: ř. 658-1180).
 * Strukturováno per-DMD mód místo jednoho velkého nested switche pro snazší
 * debug + testovatelnost. Funkčně 1:1 ekvivalentní emu reference.
 *
 * KRITICKÉ: bit-for-bit identita s emu `vramctrl_mz800_memop_read_byte()` a
 * `vramctrl_mz800_memop_write_byte()` je akceptační kritérium. Změny v této
 * logice MUSÍ být ověřeny proti emu (smoke identity test).
 *
 * Undokumentované módy 0x03 a 0x07:
 *  - Emu kód implementuje skrz dispatch fall-through (case 0x03 je obecně
 *    320x200@16 dispatch s VBANK ignorovaným, case 0x07 je 640x200@4
 *    dispatch). Toto chování je hypotéza C-emulátoru, ne ověřeno proti
 *    HDL netlistu (viz mz800-knowledge public/reference/agent/hw/09a-undoc-dmd-modes.md).
 *  - Sim port = stejná hypotéza. Identity test je ověří proti emu (oba
 *    mají stejnou hypotézu), ne proti realitě HW.
 */

#include "vram_sim.h"
#include "vram_sim_modes.h"

#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Lokální helpers
 * ------------------------------------------------------------------------- */

/**
 * @brief Test zda adresa je lichá (relevant jen v 640 modu).
 */
#define VS_ADDR_IS_ODD(addr) ((addr) & 0x01)

/* ---------------------------------------------------------------------------
 * READ - normální (RF_SEARCH = 0)
 *
 * Port z mz800_vramctrl.c:513-570. Logika:
 *  - regRF_PLANE == 0: vraci 0xFF
 *  - pro kazdou vybranou plane (bit v regRF_PLANE):
 *      retval &= plane[phy] (s podle 320/640 mode + sudy/lichy addr)
 *  - plane III/IV bez exVRAM: skip (= AND s 0xFF = no-op)
 *
 * Pozn: emu kod ma 'retval = 0xff' initial a pak '&= plane', takze AND vsech
 * vybranych plane. Pro jednu plane je to identicky jako plane hodnota.
 * Pro vice plane je to vlastne SEARCH-like behavior (ale bez 'notsearch').
 * ------------------------------------------------------------------------- */

uint8_t vs_read_normal(const st_VRAM_SIM_STATE *state, uint16_t addr, uint16_t phy)
{
    uint8_t retval;
    int scrw640 = VS_DMD_TEST_SCRW640(state) ? 1 : 0;
    int addr_odd = VS_ADDR_IS_ODD(addr);

    if (state->rf_plane == 0) {
        return 0xFF;
    }

    retval = 0xFF;

    /* PLANE I: v 320 modu cti plane I, v 640 modu lichy addr cti II, sudy I. */
    if (state->rf_plane & VS_PLANE1) {
        if (scrw640 && addr_odd) {
            retval &= vs_read_plane_byte(state, 1, phy); /* plane II */
        } else {
            retval &= vs_read_plane_byte(state, 0, phy); /* plane I */
        }
    }

    /* PLANE II: jen v 320 modu (v 640 modu plane II je "secondary" pro plane I bit). */
    if (state->rf_plane & VS_PLANE2) {
        if (!scrw640) {
            retval &= vs_read_plane_byte(state, 1, phy); /* plane II */
        }
        /* v 640 modu: skip - emu ignoruje rf_plane bit 1 v 640 modu pro 'plane II' */
    }

    /* PLANE III: v 320 modu cti plane III, v 640 modu lichy addr cti IV, sudy III. */
    if (state->rf_plane & VS_PLANE3) {
        if (scrw640 && addr_odd) {
            retval &= vs_read_plane_byte(state, 3, phy); /* plane IV */
        } else {
            retval &= vs_read_plane_byte(state, 2, phy); /* plane III */
        }
    }

    /* PLANE IV: jen v 320 modu. */
    if (state->rf_plane & VS_PLANE4) {
        if (!scrw640) {
            retval &= vs_read_plane_byte(state, 3, phy); /* plane IV */
        }
    }

    return retval;
}

/* ---------------------------------------------------------------------------
 * READ - search mode (RF_SEARCH = 1)
 *
 * Port z mz800_vramctrl.c:221-512. Vraci bitovou masku pixelu ktere matchuji
 * RF_PLANE pattern napric vsemi plane.
 *
 * Logika: pro kazdou plane P dostupnou v aktualnim rezimu:
 *   if (regRF_PLANE & P) search &= plane[phy];
 *   else                 notsearch |= plane[phy];
 * Pak: return search & ~notsearch.
 *
 * Per-mode dispatch:
 *   HICOLOR + SCRW640: 640@4 - plane I (sudy)/II (lichy) + plane III/IV
 *   HICOLOR + !SCRW640: 320@16 - vsechny 4 plane
 *   !HICOLOR + SCRW640 + VBANK=0: 640@2 A - plane I (sudy)/II (lichy)
 *   !HICOLOR + SCRW640 + VBANK=1: 640@2 B - plane III (sudy)/IV (lichy)
 *   !HICOLOR + !SCRW640 + VBANK=0: 320@4 A - plane I + II
 *   !HICOLOR + !SCRW640 + VBANK=1: 320@4 B - plane III + IV
 * ------------------------------------------------------------------------- */

uint8_t vs_read_search(const st_VRAM_SIM_STATE *state, uint16_t addr, uint16_t phy)
{
    uint8_t search = 0xFF;
    uint8_t notsearch = 0x00;
    int hicolor = VS_DMD_TEST_HICOLOR(state) ? 1 : 0;
    int scrw640 = VS_DMD_TEST_SCRW640(state) ? 1 : 0;
    int vbank = state->wfrf_vbank ? 1 : 0;
    int addr_odd = VS_ADDR_IS_ODD(addr);

    if (hicolor) {
        if (scrw640) {
            /* 640x200@4: rf_plane bit 0 = I/II per addr_odd, bit 2 = III/IV. */
            /* Pozn: emu ignoruje bity 1, 3 v 640@4 search dispatchu. */

            /* I/II (bit 0 v rf_plane) */
            if (state->rf_plane & VS_PLANE1) {
                if (addr_odd) {
                    search = vs_read_plane_byte(state, 1, phy);
                } else {
                    search = vs_read_plane_byte(state, 0, phy);
                }
            } else {
                if (addr_odd) {
                    notsearch = vs_read_plane_byte(state, 1, phy);
                } else {
                    notsearch = vs_read_plane_byte(state, 0, phy);
                }
            }

            /* III/IV (bit 2 v rf_plane) */
            if (state->rf_plane & VS_PLANE3) {
                if (state->has_exvram) {
                    if (addr_odd) {
                        search &= vs_read_plane_byte(state, 3, phy);
                    } else {
                        search &= vs_read_plane_byte(state, 2, phy);
                    }
                }
                /* bez exVRAM: search &= 0xff = no-op */
            } else {
                if (state->has_exvram) {
                    if (addr_odd) {
                        notsearch |= vs_read_plane_byte(state, 3, phy);
                    } else {
                        notsearch |= vs_read_plane_byte(state, 2, phy);
                    }
                } else {
                    notsearch |= 0xFF;
                }
            }
        } else {
            /* 320x200@16: vsechny 4 plane. */

            /* Plane I */
            if (state->rf_plane & VS_PLANE1) {
                search = vs_read_plane_byte(state, 0, phy);
            } else {
                notsearch = vs_read_plane_byte(state, 0, phy);
            }

            /* Plane II */
            if (state->rf_plane & VS_PLANE2) {
                search &= vs_read_plane_byte(state, 1, phy);
            } else {
                notsearch |= vs_read_plane_byte(state, 1, phy);
            }

            /* Plane III */
            if (state->rf_plane & VS_PLANE3) {
                if (state->has_exvram) {
                    search &= vs_read_plane_byte(state, 2, phy);
                }
            } else {
                if (state->has_exvram) {
                    notsearch |= vs_read_plane_byte(state, 2, phy);
                } else {
                    notsearch |= 0xFF;
                }
            }

            /* Plane IV */
            if (state->rf_plane & VS_PLANE4) {
                if (state->has_exvram) {
                    search &= vs_read_plane_byte(state, 3, phy);
                }
            } else {
                if (state->has_exvram) {
                    notsearch |= vs_read_plane_byte(state, 3, phy);
                } else {
                    notsearch |= 0xFF;
                }
            }
        }
    } else if (scrw640) {
        if (vbank) {
            /* 640x200@2 B: jen plane III (sudy) / IV (lichy). */
            if (state->rf_plane & VS_PLANE3) {
                if (state->has_exvram) {
                    if (addr_odd) {
                        search = vs_read_plane_byte(state, 3, phy);
                    } else {
                        search = vs_read_plane_byte(state, 2, phy);
                    }
                }
            } else {
                if (state->has_exvram) {
                    if (addr_odd) {
                        notsearch = vs_read_plane_byte(state, 3, phy);
                    } else {
                        notsearch = vs_read_plane_byte(state, 2, phy);
                    }
                } else {
                    notsearch = 0xFF;
                }
            }
        } else {
            /* 640x200@2 A: jen plane I (sudy) / II (lichy). */
            if (state->rf_plane & VS_PLANE1) {
                if (addr_odd) {
                    search = vs_read_plane_byte(state, 1, phy);
                } else {
                    search = vs_read_plane_byte(state, 0, phy);
                }
            } else {
                if (addr_odd) {
                    notsearch = vs_read_plane_byte(state, 1, phy);
                } else {
                    notsearch = vs_read_plane_byte(state, 0, phy);
                }
            }
        }
    } else {
        if (vbank) {
            /* 320x200@4 B: plane III + IV. */
            if (state->rf_plane & VS_PLANE3) {
                if (state->has_exvram) {
                    search = vs_read_plane_byte(state, 2, phy);
                }
            } else {
                if (state->has_exvram) {
                    notsearch = vs_read_plane_byte(state, 2, phy);
                } else {
                    notsearch = 0xFF;
                }
            }

            if (state->rf_plane & VS_PLANE4) {
                if (state->has_exvram) {
                    search &= vs_read_plane_byte(state, 3, phy);
                }
            } else {
                if (state->has_exvram) {
                    notsearch |= vs_read_plane_byte(state, 3, phy);
                } else {
                    notsearch |= 0xFF;
                }
            }
        } else {
            /* 320x200@4 A: plane I + II. */
            if (state->rf_plane & VS_PLANE1) {
                search = vs_read_plane_byte(state, 0, phy);
            } else {
                notsearch = vs_read_plane_byte(state, 0, phy);
            }

            if (state->rf_plane & VS_PLANE2) {
                search &= vs_read_plane_byte(state, 1, phy);
            } else {
                notsearch |= vs_read_plane_byte(state, 1, phy);
            }
        }
    }

    return (uint8_t)(search & (~notsearch));
}

/* ---------------------------------------------------------------------------
 * WRITE - per-mode WE compute + per-plane WMD aplikace
 *
 * Port z mz800_vramctrl.c:658-1180. Logika:
 *  1. Compute avlb_plane + avlb_plane_s (per HICOLOR/VBANK/SCRW640).
 *  2. Compute WE (Write Enable) per plane podle WF_PLANE + WF_MODE + avlb.
 *  3. Per plane v WE: aplikuj WF_MODE (SINGLE/EXOR/OR/RESET/REPLACE/PSET)
 *     a zapis vypocitanou wrvalue do plane[phy].
 *
 * WMD = WF_MODE bit 2: pokud 1 => REPLACE/PSET (vyzaduje avlb_plane test),
 * jinak SINGLE/EXOR/OR/RESET (pouziva avlb_plane_s).
 *
 * Pravidla pro WE flag per plane:
 *
 *  SINGLE/EXOR/OR/RESET (WF_MODE bit 2 = 0):
 *    WE_PX = (WF_PLANE & PX) & avlb_plane_s
 *
 *  REPLACE/PSET (WF_MODE bit 2 = 1):
 *    if (avlb_plane & PX):
 *      WE_PX = 1 (vzdy - zapise se bud value pro vybranou nebo 0/RESET
 *               pro nevybranou)
 *
 *  640 mod + sudy addr: PLANE I + III (cti se z bytu LO half do plane I/III).
 *  640 mod + lichy addr: PLANE II + IV (cti se z bytu HI half do plane II/IV).
 *  V 320 modu: vsechny 4 plane (per WF_PLANE bits).
 *
 * Pozn: pro 640 mod lichy addr, "WF_PLANE bit 0" (plane I) ovlivnuje WE
 * pro plane II (ne plane I). Stejne tak bit 2 (III) ovlivnuje IV.
 * ------------------------------------------------------------------------- */

/**
 * @brief Aplikuje WF_MODE na danou plane + zapise vysledek.
 *
 * Per-plane aplikace WMD. Pro REPLACE/PSET je dulezity 'is_selected' flag -
 * v 640 modu pro plane II/IV se pouziva 'selected' z plane I/III bitu
 * (ne z plane II/IV bitu) - emu kod (mz800_vramctrl.c:989-1037, 1111-1158).
 *
 * @param[in,out] state
 * @param[in]     plane_idx  0..3
 * @param[in]     phy        plane offset
 * @param[in]     value      novy bajt
 * @param[in]     wf_mode    en_VRAM_SIM_WF_MODE
 * @param[in]     is_selected_for_replace  pro REPLACE/PSET: zda je plane
 *                "vybran" (zapise value) nebo "nevybran" (REPLACE=0, PSET=RESET)
 */
static void vs_apply_wmd(st_VRAM_SIM_STATE *state, int plane_idx, uint16_t phy,
                         uint8_t value, uint8_t wf_mode, int is_selected_for_replace)
{
    uint8_t *p = vs_plane_rw(state, plane_idx);
    if (!p) return; /* read-only nebo neexistujici plane (e.g. III/IV bez exVRAM) */

    uint8_t current = p[phy];
    uint8_t wrvalue = 0;

    switch (wf_mode) {
        case VRAM_SIM_WF_SINGLE:
            wrvalue = value;
            break;
        case VRAM_SIM_WF_EXOR:
            wrvalue = (uint8_t)(value ^ current);
            break;
        case VRAM_SIM_WF_OR:
            wrvalue = (uint8_t)(value | current);
            break;
        case VRAM_SIM_WF_RESET:
            wrvalue = (uint8_t)((~value) & current);
            break;
        case VRAM_SIM_WF_REPLACE:
            wrvalue = is_selected_for_replace ? value : (uint8_t)0;
            break;
        case VRAM_SIM_WF_PSET:
            if (is_selected_for_replace) {
                wrvalue = (uint8_t)(value | current);
            } else {
                wrvalue = (uint8_t)((~value) & current);
            }
            break;
        default:
            /* Neznamy wf_mode - tichy skip. */
            return;
    }

    p[phy] = wrvalue;
}

void vs_write_byte(st_VRAM_SIM_STATE *state, uint16_t addr, uint16_t phy, uint8_t value)
{
    int scrw640 = VS_DMD_TEST_SCRW640(state) ? 1 : 0;
    int addr_odd = VS_ADDR_IS_ODD(addr);
    uint8_t wf_mode = state->wf_mode;
    uint8_t wf_plane = state->wf_plane;

    /* Vypocet avlb_plane / avlb_plane_s - port z mz800_vramctrl.c:702-711. */
    int avlb_plane = VS_DMD_TEST_HICOLOR(state) ? VS_PLANE_ALL : 0;
    avlb_plane |= state->wfrf_vbank ? (VS_PLANE3 | VS_PLANE4) : (VS_PLANE1 | VS_PLANE2);
    avlb_plane &= scrw640 ? (VS_PLANE1 | VS_PLANE3) : VS_PLANE_ALL;

    int avlb_plane_s = scrw640 ? (VS_PLANE1 | VS_PLANE3) : VS_PLANE_ALL;

    /* WE per plane podle WF_MODE + WF_PLANE + avlb. */
    int we = 0;
    int is_replace_or_pset = (wf_mode & (1 << 2)) ? 1 : 0;

    /*
     * PLANE I (320 mod nebo 640 mod sudy addr).
     * V 640 mod lichy addr je 'plane I bit' urceni pouzity pro plane II/IV
     * (sekce dale). Tady jen 320 mod nebo 640 mod sudy.
     */
    if (!(scrw640 && addr_odd)) {
        if (!is_replace_or_pset) {
            if ((wf_plane & VS_PLANE1) & avlb_plane_s) {
                we |= VS_PLANE1;
            }
        } else if (avlb_plane & VS_PLANE1) {
            we |= VS_PLANE1;
            /* WE se nastavi vzdy kdyz je avlb - REPLACE/PSET pak rozhodne mezi
             * 'selected' (zapise value) a 'nevybrano' (REPLACE=0, PSET=RESET).
             * Emu kod (ř. 731-745) ma slozitejsi if/elif strukturu, ale efekt
             * je stejny: vzdy WE pokud avlb. */
        }
    }

    /*
     * PLANE II:
     *  - 320 mod: per WF_PLANE bit 1.
     *  - 640 mod lichy addr: per WF_PLANE bit 0 (= plane I bit, protoze plane II je
     *    'secondary' pro plane I v 640 modu).
     */
    if (scrw640) {
        if (addr_odd) {
            /* 640 mod, lichy bajt - plane II je 'mirror' plane I bitu */
            if (!is_replace_or_pset) {
                if ((wf_plane & VS_PLANE1) & avlb_plane_s) {
                    we |= VS_PLANE2;
                }
            } else if (avlb_plane & VS_PLANE1) {
                we |= VS_PLANE2;
            }
        }
        /* 640 mod sudy bajt: plane II se nezapisuje */
    } else {
        /* 320 mod: plane II per WF_PLANE bit 1 */
        if (!is_replace_or_pset) {
            if ((wf_plane & VS_PLANE2) & avlb_plane_s) {
                we |= VS_PLANE2;
            }
        } else if (avlb_plane & VS_PLANE2) {
            we |= VS_PLANE2;
        }
    }

    /*
     * PLANE III (320 mod nebo 640 mod sudy addr) - jen pokud has_exvram.
     * exVRAM check je v has_exvram, ale emu kod ma DEF_USE_EXTVRAM = 1 vzdy.
     * Pokud sim ma has_exvram = 0, WE bit se nastavi, ale apply_wmd skoncí
     * na NULL pointer (vs_plane_rw vrátí NULL pro III/IV bez exVRAM).
     */
    if (state->has_exvram) {
        if (!(scrw640 && addr_odd)) {
            if (!is_replace_or_pset) {
                if ((wf_plane & VS_PLANE3) & avlb_plane_s) {
                    we |= VS_PLANE3;
                }
            } else if (avlb_plane & VS_PLANE3) {
                we |= VS_PLANE3;
            }
        }

        if (scrw640) {
            if (addr_odd) {
                /* 640 mod lichy: plane IV mirror plane III bitu */
                if (!is_replace_or_pset) {
                    if ((wf_plane & VS_PLANE3) & avlb_plane_s) {
                        we |= VS_PLANE4;
                    }
                } else if (avlb_plane & VS_PLANE3) {
                    we |= VS_PLANE4;
                }
            }
            /* 640 mod sudy: plane IV neaktivni */
        } else {
            /* 320 mod: plane IV per WF_PLANE bit 3 */
            if (!is_replace_or_pset) {
                if ((wf_plane & VS_PLANE4) & avlb_plane_s) {
                    we |= VS_PLANE4;
                }
            } else if (avlb_plane & VS_PLANE4) {
                we |= VS_PLANE4;
            }
        }
    }

    /* Per-plane zapis dle WE + wf_mode. */

    if (we & VS_PLANE1) {
        /* PLANE I: REPLACE/PSET 'selected' = (wf_plane & PLANE1). */
        int sel = (wf_plane & VS_PLANE1) ? 1 : 0;
        vs_apply_wmd(state, 0, phy, value, wf_mode, sel);
    }

    if (we & VS_PLANE2) {
        /* PLANE II:
         *  - 320 mod: selected = (wf_plane & PLANE2)
         *  - 640 mod: selected = (wf_plane & PLANE1) (mirror plane I)
         */
        int sel;
        if (scrw640) {
            sel = (wf_plane & VS_PLANE1) ? 1 : 0;
        } else {
            sel = (wf_plane & VS_PLANE2) ? 1 : 0;
        }
        vs_apply_wmd(state, 1, phy, value, wf_mode, sel);
    }

    if (we & VS_PLANE3) {
        int sel = (wf_plane & VS_PLANE3) ? 1 : 0;
        vs_apply_wmd(state, 2, phy, value, wf_mode, sel);
    }

    if (we & VS_PLANE4) {
        /* PLANE IV:
         *  - 320 mod: selected = (wf_plane & PLANE4)
         *  - 640 mod: selected = (wf_plane & PLANE3) (mirror plane III)
         */
        int sel;
        if (scrw640) {
            sel = (wf_plane & VS_PLANE3) ? 1 : 0;
        } else {
            sel = (wf_plane & VS_PLANE4) ? 1 : 0;
        }
        vs_apply_wmd(state, 3, phy, value, wf_mode, sel);
    }
}
