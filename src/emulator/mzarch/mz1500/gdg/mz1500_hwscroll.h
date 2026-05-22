/*
 * File:   mz1500_hwscroll.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 18. června 2015, 19:45
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

/*
 * MZ-1500 nema HW scroll — tento soubor poskytuje prazdne stuby
 * pro zpetnou kompatibilitu s kodem, ktery je sdileny s MZ-800.
 */

#ifndef MZ1500_HWSCROLL_H
#define MZ1500_HWSCROLL_H

#ifdef __cplusplus
extern "C"
{
#endif

    static inline void hwscroll_init(void) {}
    static inline void hwscroll_reset(void) {}

#ifdef __cplusplus
}
#endif

#endif /* MZ1500_HWSCROLL_H */
