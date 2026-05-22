/*
 * File:   mzarch_config.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 16. srpna 2016, 9:50
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

#ifndef MZARCH_CONFIG_H
#define MZARCH_CONFIG_H

#ifdef __cplusplus
extern "C"
{
#endif

#if MZARCH == 800
#include "mz800/mz800_config.h"
#else
#if MZARCH == 1500
#include "mz1500/mz1500_config.h"
#else
#if MZARCH == 700
#include "mz700/mz700_config.h"
#else
#error "Unknown MZARCH value"
#endif
#endif
#endif

/*
 * Konfiguracni vypnuti periferii
 * ==============================
 *
 * Nastavuje se 1, nebo 0
 *
 */
#ifndef CFG_HWEXT_HAVE_FDC
#define CFG_HWEXT_HAVE_FDC 0
#endif
#ifndef CFG_HWEXT_HAVE_IDE8
#define CFG_HWEXT_HAVE_IDE8 0
#endif
#ifndef CFG_HWEXT_HAVE_RAMDISK
#define CFG_HWEXT_HAVE_RAMDISK 0
#endif
#ifndef CFG_HWEXT_HAVE_QDISK
#define CFG_HWEXT_HAVE_QDISK 0
#endif
#ifndef HAVE_JOY
#define HAVE_JOY 0
#endif

    /*
     * HAVE_PSG: max pocet PSG instanci (SN76489) na platforme - compile-time
     * =====================================================================
     *   0 = bez PSG, fyzicky nikdy (MZ-700)
     *   1 = max 1 PSG (mono jen)
     *   2 = max 2 PSG (mono nebo stereo - rozhoduje runtime g_psg_module.stereo)
     *
     * Pouziti:
     *  - HAVE_PSG = 0  →  MZ-700: PSG cesta cele audio pipeline je
     *                     compile-time eliminovana
     *  - HAVE_PSG = 2  →  MZ-800: nativne mono, ale HW experiment
     *                     allow_psg1 muze za behu zapnout stereo
     *  - HAVE_PSG = 2  →  MZ-1500: nativne stereo
     *
     * Pocet kanalu audio logu = 1 + 4 * HAVE_PSG (CTC0 + 4 kanaly per PSG).
     * Runtime stereo flag (g_psg_module.stereo) urci, zda PSG1 kanaly
     * (5..8) budou skutecne zpracovany v audio_log_fill_psg.
     */
#ifndef HAVE_PSG
#define HAVE_PSG 0
#endif


    /*
     * Konfiguracni vypnuti modulu Gtk UI
     * =======================================
     */

#ifndef MZ800EMU_CFG_UI_ENABLED
#if defined(USE_GTK3_UI) && defined(USE_GTK3)
#define MZ800EMU_CFG_UI_ENABLED
#endif
#endif
#undef MZ800EMU_CFG_UI_ENABLED

    /*
     * Konfiguracni vypnuti modulu MZ-800 debugger
     * ===========================================
     *
     * Debugger lze vypnout dvema zpusoby:
     *
     *  1. Build-time prepinac z prikazove radky:
     *     make NO_DEBUGGER=1   (Makefile to predava CMake jako
     *                           -DMZ_NO_DEBUGGER=ON, ktery definuje
     *                           MZ800EMU_NO_DEBUGGER pres globalni
     *                           add_compile_definitions)
     *
     *  2. Lokalni edit tohoto souboru - zakomentovat radek `#define
     *     MZ800EMU_CFG_DEBUGGER_ENABLED` nize.
     *
     * Bez debuggeru je binarka cca o 15 % mensi a emulace by mela
     * byt rychlejsi (presne hodnoty neoverene).
     */

#ifndef MZ800EMU_NO_DEBUGGER
#define MZ800EMU_CFG_DEBUGGER_ENABLED
#endif

    /*
     * Konfigurace pro emulaci CLK 1.1 MHz (CTC8253-CTC0 a CMT )
     * =========================================================
     *
     * !!! Pouzit jednu z voleb:
     *
     * MZ800EMU_CFG_CLK1M1_SLOW - presna varianta pri ktere se vola CTC0_step a CMT_step
     * pri kazdem 16 pixelclk
     *
     * MZ800EMU_CFG_CLK1M1_FAST - prozatim nedokoncena varianta pri ktere se CLK 1M1
     * step() zavola jen tehdy, pokud ma nastat konkretni event
     *
     */
#if 1
#define MZ800EMU_CFG_CLK1M1_FAST
#else
#define MZ800EMU_CFG_CLK1M1_SLOW
#endif

    /*
     * Vypnuti audio modulu
     * ====================
     *
     * Pokud neni definovano pouziti SDL nebo Gstreamer Audio, tak se vypne audio modul.
     * Sznchronizace 20ms bude nahrazena synchronizaci podle get_ticks_ns().
     *
     */

// #define MZ800EMU_CFG_AUDIO_DISABLED

// Vypnuti audio modulu, pokud neni definovano pouziti SDL nebo Gstreamer Audio
#ifndef MZ800EMU_CFG_AUDIO_DISABLED
#if !defined(USE_SDL_AUDIO) && !defined(USE_SDL2_AUDIO) && !defined(USE_SDL3_AUDIO) && !defined(USE_GSTREAMER_AUDIO)
#define MZ800EMU_CFG_AUDIO_DISABLED
#endif /* !USE_SDL_AUDIO && !USE_GSTREAM_AUDIO */
#endif /* !MZ800EMU_CFG_AUDIO_DISABLED */

    /*
     * Memory statistics
     * =================
     *
     * Chci zkusit zjistit, jak nejlepe optimalizovat umisteni podminek pro cteni a zapis do pameti.
     * Tato volba vytvori aditivni soubor se statistikou pro cteni a zapis pameti.
     *
     */
    // #define MEMORY_MAKE_STATISTICS

#ifdef __cplusplus
}
#endif

#endif /* MZARCH_CONFIG_H */
