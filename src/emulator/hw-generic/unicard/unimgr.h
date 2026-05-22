/* 
 * File:   unimgr.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 22. června 2018, 10:00
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


#ifndef UNIMGR_H
#define UNIMGR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "unicard.h"


    typedef enum en_UNIMGR_ADDR {
        UNIMGR_ADDR_CMD = 0,
        UNIMGR_ADDR_DATA = 1
    } en_UNIMGR_ADDR;

    extern int unimgr_init ( void );

#ifdef UNICARD_EMULATED
    extern void unimgr_exit ( void );
    extern void unimgr_reset ( void );
#endif
    extern void unimgr_write_byte ( en_UNIMGR_ADDR addr, uint8_t data );
    extern uint8_t unimgr_read_byte ( en_UNIMGR_ADDR addr );

#ifdef __cplusplus
}
#endif

#endif /* UNIMGR_H */

