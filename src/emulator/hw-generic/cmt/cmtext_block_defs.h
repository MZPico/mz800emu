/* 
 * File:   cmtext_block_defs.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 20. května 2018, 7:52
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


#ifndef CMTEXT_BLOCK_DEFS_H
#define CMTEXT_BLOCK_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif


    typedef enum en_CMTEXT_BLOCK_TYPE {
        CMTEXT_BLOCK_TYPE_WAV = 0,
        CMTEXT_BLOCK_TYPE_MZF,
        CMTEXT_BLOCK_TYPE_TAPHEADER,
        CMTEXT_BLOCK_TYPE_TAPDATA,
    } en_CMTEXT_BLOCK_TYPE;


    typedef enum en_CMTEXT_BLOCK_SPEED {
        CMTEXT_BLOCK_SPEED_NONE = 0, // rychlost bloku neni mozne menit
        CMTEXT_BLOCK_SPEED_DEFAULT, // rychlost bloku se nastavuje podle cmt default speed
        CMTEXT_BLOCK_SPEED_SET, // rychlost bloku je pevne dana a nelze ji menit
    } en_CMTEXT_BLOCK_SPEED;


#ifdef __cplusplus
}
#endif

#endif /* CMTEXT_BLOCK_DEFS_H */

