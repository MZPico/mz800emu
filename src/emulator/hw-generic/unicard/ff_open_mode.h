/* 
 * File:   ff_open_mode.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 24. června 2018, 9:55
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


#ifndef FF_OPEN_MODE_H
#define FF_OPEN_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

#define FA_READ                 0x01
#define FA_OPEN_EXISTING        0x00

#define FA_WRITE                0x02
#define FA_CREATE_NEW           0x04
#define FA_CREATE_ALWAYS        0x08
#define FA_OPEN_ALWAYS          0x10



#ifdef __cplusplus
}
#endif

#endif /* FF_OPEN_MODE_H */

