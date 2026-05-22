/*
 * File:   mzarch_platform_functions.h
 * Author: chaky
 *
 * Created on 9. března 2026, 16:16
 *
 * Description:
 *
 * Tento soubor obsahuje deklarace funkcí, které musí být implementovány pro každý port zvlášť.
 * Tyto funkce jsou volány z různých částí emulátoru a slouží k inicializaci, atp.
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
#ifndef MZARCH_PLATFORM_FUNCTIONS_H
#define MZARCH_PLATFORM_FUNCTIONS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Funkce z mzarch/mzXXX/mzXXX_main.c
     */
    void mzarch_platform_fn_reset_request(void);
    void mzarch_platform_fn_init(void);
    void mzarch_platform_fn_exit(void);

    /*
     * Funkce z debuggeru
     */
    void mzarch_platform_fn_debugger_state_changed(bool active);

#ifdef __cplusplus
}
#endif

#endif /* MZARCH_PLATFORM_FUNCTIONS_H */