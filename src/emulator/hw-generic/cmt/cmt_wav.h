/* 
 * File:   cmt_wav.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 19. května 2018, 8:22
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


#ifndef CMT_WAV_H
#define CMT_WAV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cmtext.h"

    extern st_CMTEXT g_cmt_wav_extension;

    st_CMTEXT_BLOCK* cmtwav_block_open ( st_HANDLER *h, uint32_t offset, int block_id, int pause_after );
    void cmtwav_block_close ( st_CMTEXT_BLOCK *block );

#ifdef __cplusplus
}
#endif

#endif /* CMT_WAV_H */


