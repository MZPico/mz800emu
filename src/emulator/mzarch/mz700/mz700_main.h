/*
 * File:   mz700_main.h
 * Author: chaky
 *
 * Created on 14. června 2015, 16:16
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

#ifndef MZ700_MAIN_H
#define MZ700_MAIN_H

#include "mzarch/mzarch.h"
#include "mzarch/mzarch_platform_functions.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
    void mzarch_run_to_temporary_breakpoint(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MZ700_MAIN_H */
