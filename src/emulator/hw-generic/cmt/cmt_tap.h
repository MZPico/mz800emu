/* 
 * File:   cmt_tap.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 21. května 2018, 16:36
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


#ifndef CMT_TAP_H
#define CMT_TAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "libs/zxtape/zxtape.h"

#include "cmtext.h"


    typedef enum en_CMTTAP_HEADER_CODE {
        CMTTAP_HEADER_CODE_PROGRAM = 0x00,
        CMTTAP_HEADER_CODE_NUMARRAY = 0x01,
        CMTTAP_HEADER_CODE_CHARARRAY = 0x02,
        CMTTAP_HEADER_CODE_FILE = 0x03,
    } en_CMTTAP_HEADER_CODE;


    typedef struct st_CMTTAP_BLOCKINFO {
        uint16_t size;
        uint8_t flag; // en_CMTTAP_BLOCK_FLAG
    } st_CMTTAP_BLOCKINFO;


    typedef struct st_CMTTAP_HEADER {
        uint8_t code;
        uint8_t name[10];
        uint16_t data_size;
        uint16_t param1;
        uint16_t param2;
        uint8_t chksum; // XOR
    } st_CMTTAP_HEADER;


    typedef union un_CMTTAP_FLAGSPEC {
        st_CMTEXT_TAPE_ITEM_TAPHDR hdr;
        st_CMTEXT_TAPE_ITEM_TAPDATA data;
    } un_CMTTAP_FLAGSPEC;


    typedef struct st_CMTTAP_BLOCKSPEC {
        uint8_t *data;
        uint16_t data_size;
        en_ZXTAPE_BLOCK_FLAG flag;
        un_CMTTAP_FLAGSPEC flspec;
        en_CMTSPEED cmtspeed; // bere se v potaz pouze pokud st_CMTEXT_BLOCK->block_speed = CMTEXT_BLOCK_SPEED_SET
    } st_CMTTAP_BLOCKSPEC;

    extern st_CMTEXT g_cmt_tap_extension;

    extern st_CMTEXT_BLOCK* cmttap_block_open ( st_CMTEXT *cmtext, int block_id );
    extern st_CMTEXT_TAPE_ITEM_TAPHDR* cmttap_block_get_spec_tapheader ( st_CMTEXT_BLOCK *block );
    extern st_CMTEXT_TAPE_ITEM_TAPDATA* cmttap_block_get_spec_tapdata ( st_CMTEXT_BLOCK *block );

    extern const char* cmttap_get_block_code_txt ( en_CMTTAP_HEADER_CODE code );

#define CMTTAP_DEFAULT_PAUSE_AFTER_HEADER (uint16_t) 350 // pocet milisekund
#define CMTTAP_DEFAULT_PAUSE_AFTER_DATA (uint16_t) 1000 // pocet milisekund

#ifdef __cplusplus
}
#endif

#endif /* CMT_TAP_H */

