/*
 * File:   mz700_vramctrl.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 18. června 2015, 19:12
 *
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

#ifndef MZ700_VRAMCTRL_H
#define MZ700_VRAMCTRL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "debugger/debugger.h"

    typedef struct st_VRAMCTRL
    {
        unsigned mz700_wr_latch_is_used;
    } st_VRAMCTRL;

    extern st_VRAMCTRL g_vramctrl;

    extern void vramctrl_reset(void);

#include "mz700_gdg.h"
#include "mz700_framebuffer.h"
#include "memory/memory.h"
    /*******************************************************************************
     *
     * VRAM kontroler pro rezimy MZ-700
     *
     ******************************************************************************/

    /**
     * Cteni z MZ-700 VRAM bez ohledu na synchronizaci.
     *
     * @param addr
     * @return
     */
#define vramctrl_mz700_memop_read_byte(addr) g_memory.VRAM[addr]

    /**
     * Interni subrutina fyzickeho zapisu do MZ-700 VRAM - generuje vsak zmenu ve screenu.
     *
     * @param addr
     * @param value
     */
#define vramctrl_mz700_memop_write_byte_internal(addr, value)                                                     \
    {                                                                                                             \
        /* TODO: meli by jsme nastavovat jen pokud se zapisuje do CG-RAM, nebo do VRAM, ktera je opravdu videt */ \
        /* PCG makra MZ-700 pouzivaji offset presahujici VRAM do sousedniho PCG pole ve strukture */              \
        _Pragma("GCC diagnostic push")                                                                            \
            _Pragma("GCC diagnostic ignored \"-Wstringop-overflow\"")                                             \
                g_framebuffer.screen_changes = SCRSTS_THIS_IS_CHANGED;                                            \
        g_memory.VRAM[addr] = value;                                                                              \
        VRAMCTRL_DBG_NOTIFY_VRAM_TOUCH();                                                                         \
        _Pragma("GCC diagnostic pop")                                                                             \
    }

    /**
     * Zapis do MZ-700 VRAM bez synchronizace - generuje vsak zmenu ve screenu.
     *
     * @param addr
     * @param value
     */
#define vramctrl_mz700_memop_write_byte(addr, value) vramctrl_mz700_memop_write_byte_internal(addr, value)

    /**
     * One-shot WAIT logika pro MZ-700 mode VRAM/CG-RAM pristup.
     *
     * Pravidla z gate-level HDL simulace GDG (viz mz800-knowledge
     * reference/agent/hw/08-video-mz700-mode.md):
     *
     *   1. SIGNAL_GDG_HBLNK = 1 (visible canvas, vcetne 5 px pre-canvas zone):
     *      - Prvni R/W pristup na scanline projde bez WAIT (= "first free").
     *      - Kazdy dalsi R/W pristup na stejnem scanline dostane WAIT az do
     *        konce visible (~3 px pred koncem canvas, kdy fyrne HBLN_START
     *        event a deaktivuje SIGNAL_GDG_HBLNK).
     *      - Latch je sdileny pro READ i WRITE.
     *   2. SIGNAL_GDG_HBLNK = 0 (border / blanking): zadny WAIT bez ohledu
     *      na latch (= raster-chasing free okno).
     *   3. Latch reset na visible -> blank prechodu (= MZEVENT_GDG_HBLN_START
     *      event handler resetuje g_vramctrl.mz700_wr_latch_is_used na 0).
     *
     * Predpoklad: pred volanim doslo k mzarch_main_insideop_mreq() (sync
     * total_elapsed.ticks tak, ze SIGNAL_GDG_HBLNK odpovida current beam pos).
     */
    static inline void vramctrl_mz700_memop_oneshot_wait(void)
    {
        if (!SIGNAL_GDG_HBLNK) {
            return;     /* mimo visible - WAIT nikdy */
        }
        if (g_vramctrl.mz700_wr_latch_is_used) {
            /* dalsi pristup ve stejne visible periode - WAIT do konce visible */
            mzarch_main_insideop_mreq_mz700_vramctrl();
            return;
        }
        /* prvni pristup v aktualni visible periode - oznac latch, NO WAIT */
        g_vramctrl.mz700_wr_latch_is_used = 1;
    }

    /**
     * Cteni z MZ-700 VRAM se synchronizaci a HDL-spravnym WAIT.
     *
     * @param addr
     * @return
     */
    static inline uint8_t vramctrl_mz700_memop_read_byte_sync(uint16_t addr)
    {
        mzarch_main_insideop_mreq();
        vramctrl_mz700_memop_oneshot_wait();
        return vramctrl_mz700_memop_read_byte(addr);
    }

    /**
     * Zapis do MZ-700 VRAM se synchronizaci a HDL-spravnym WAIT.
     *
     * @param addr
     * @param value
     */
#define vramctrl_mz700_memop_write_byte_sync(addr, value)           \
    {                                                               \
        mzarch_main_insideop_mreq();                                \
        vramctrl_mz700_memop_oneshot_wait();                        \
        vramctrl_mz700_memop_write_byte_internal(addr, value);      \
    }

#ifdef __cplusplus
}
#endif

#endif /* MZ700_VRAMCTRL_H */
