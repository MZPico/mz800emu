/* 
 * File:   debug.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 4. srpna 2015, 11:55
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

#ifndef DEBUG_H
#define	DEBUG_H

#ifdef	__cplusplus
extern "C" {
#endif

    
#include <stdio.h>
#define USE_DBG_PRN     printf

    
#define DBGNON      0
#define DBGERR      ( 1 << 0 )
#define DBGWAR      ( 1 << 1 )
#define DBGINF      ( 1 << 2 )

#ifndef DBGLEVEL
#define DBGLEVEL    DBGNON
#endif

    
#ifndef USE_DBG_PRN
#warning Using standard printf function, replace it by #define USE_DBG_PRN your_prinft
#include <stdio.h>
#define USE_DBG_PRN     printf
#endif

    
#define DBGNON__DEBUGPRINTF__(...)
    

#define __DEBUGPRINTF__X(level,...)  { USE_DBG_PRN( "%s: %s():%d - ", #level, __func__, __LINE__); USE_DBG_PRN(__VA_ARGS__); }


#if (DBGLEVEL & DBGERR)
#define DBGERR__DEBUGPRINTF__(...)      __DEBUGPRINTF__X(DBGERR,__VA_ARGS__)
#else
#define DBGERR__DEBUGPRINTF__(...)
#endif

    
#if (DBGLEVEL & DBGWAR)
#define DBGWAR__DEBUGPRINTF__(...)      __DEBUGPRINTF__X(DBGWAR,__VA_ARGS__)
#else
#define DBGWAR__DEBUGPRINTF__(...)
#endif

    
#if (DBGLEVEL & DBGINF)
#define DBGINF__DEBUGPRINTF__(...)      __DEBUGPRINTF__X(DBGINF,__VA_ARGS__)
#else
#define DBGINF__DEBUGPRINTF__(...)
#endif

    
#define DBGPRINTF(level,...)            level##__DEBUGPRINTF__(__VA_ARGS__)
    
    
#ifdef	__cplusplus
}
#endif

#endif	/* DEBUG_H */

