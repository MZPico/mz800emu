/*
 * File:   mz1500_gdgclk.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 19. června 2015, 15:37
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

#ifndef MZ1500_GDGCLK_H
#define MZ1500_GDGCLK_H

#include "hw-generic/gdg/video.h"

#define GDGCLK_REAL_BASE 14318180
#define GDGCLK_BASE (VIDEO_SCREEN_TICKS * VIDEO_SCREENS_PER_SEC) /* 262 * 912 * 60 = 14 336 640 */
#define GDGCLK2CPU_DIVIDER 4                                     /* 3,579545 MHz */
/* MZ-1500 CTC0 dělička je 16, NE 13 (na rozdíl od MZ-700 NTSC kde 13 = true 1M1).
 * Empiricky overeno boot beep frekvenci: ROM zapisuje preset 0x03F8 = 1016
 * (vs MZ-700 0x04EC = 1260), takze pro stejny ~880 Hz boot beep musi byt
 * input clock 1016 * 882 = 896 kHz = 14336640 / 16. Shoduje se s referencnim
 * SVN emulatorem. Predtim chybne nastaveno na 13 (pri MZ-700 mzarch refactoru
 * commit 2aa107f s nespravnym predpokladem MZ-1500 = MZ-700 NTSC). */
#define GDGCLK_CTC0_DIVIDER 16                                   /* 896,040 kHz */

#endif /* MZ1500_GDGCLK_H */
