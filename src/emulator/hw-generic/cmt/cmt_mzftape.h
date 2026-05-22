/* 
 * File:   cmt_mzftape.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 11. května 2018, 7:27
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

#ifndef CMT_MZFTAPE_H
#define CMT_MZFTAPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cmtext.h"

    extern st_CMTEXT g_cmt_mzftape_extension;

    extern st_CMTEXT_BLOCK* cmtmzftape_block_open ( int block_id );

#define CMTMZFTAPE_DEFAULT_PAUSE_AFTER (uint16_t) 1000 // pocet milisekund

#ifdef __cplusplus
}
#endif

#endif /* CMT_MZFTAPE_H */
