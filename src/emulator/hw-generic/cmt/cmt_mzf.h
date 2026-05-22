/* 
 * File:   cmt_mzf.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 19. května 2018, 16:54
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


#ifndef CMT_MZF_H
#define CMT_MZF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cmtext.h"
#include "libs/mztape/mztape.h"

    extern st_CMTEXT g_cmt_mzf_extension;


    typedef struct st_CMTMZF_BLOCKSPEC {
        st_MZF_HEADER *hdr;
        st_MZTAPE_MZF *mztmzf;
        en_CMTSPEED cmtspeed; // bere se v potaz pouze pokud st_CMTEXT_BLOCK->block_speed = CMTEXT_BLOCK_SPEED_SET
    } st_CMTMZF_BLOCKSPEC;

    extern void cmtmzf_blockspec_destroy ( st_CMTMZF_BLOCKSPEC *blspec );
    extern st_CMTMZF_BLOCKSPEC* cmtmzf_blockspec_new ( st_HANDLER *h, uint32_t offset, en_CMTSPEED cmtspeed );

    extern st_CMTEXT_BLOCK* cmtmzf_block_open ( st_HANDLER *h, uint32_t offset, int block_id, int pause_after, en_CMTEXT_BLOCK_SPEED block_speed, en_CMTSPEED cmtspeed );
    extern void cmtmzf_block_close ( st_CMTEXT_BLOCK *block );

    extern st_MZF_HEADER* cmtmzf_block_get_spec_mzfheader ( st_CMTEXT_BLOCK *block );

#ifdef __cplusplus
}
#endif

#endif /* CMT_MZF_H */

