/**
 * @file   cfgtools.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Pomocne nastroje pro parsovani cisел z textu
 *
 * Poskytuje funkce pro prevod textovych retezcu na cisla (dec/hex)
 * a parsovani poli cisel oddeleny separatory.
 *
 * @par Changelog:
 * - 2026-03-14: Proběhla kompletní revize a refaktorizace. Vytvořeny unit testy.
 *
 * @par Licence:
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
 */


#ifndef CFGTOOLS_H
#define CFGTOOLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

    /**
     * @brief Prevede textovy retezec na long int (dec nebo hex s prefixem 0x)
     * @param str Vstupni textovy retezec s cislem
     * @param retcode Ukazatel na navratovy kod (EXIT_SUCCESS/EXIT_FAILURE)
     * @return Prevedena ciselna hodnota, nebo 0 pri chybe
     */
    extern long int cfgtools_strtol ( char *str, int *retcode );

    /**
     * @brief Parsuje retezec obsahujici pole cisel oddeleny separatory
     * @param encoded_txt Vstupni text, napr. "10, 0x20, 30"
     * @param output_numbers_array Vystupni pole pro naparsovana cisla
     * @param max_elements Maximalni pocet prvku ve vystupnim poli
     * @param separators Oddelovace (NULL = " ,")
     * @param retcode Ukazatel na navratovy kod (EXIT_SUCCESS/EXIT_FAILURE)
     * @return Pocet naparsovanych prvku, nebo 0 pri chybe
     */
    extern int cfgtools_strtol_array ( char *encoded_txt, long int *output_numbers_array, int max_elements, char *separators, int *retcode );


#ifdef __cplusplus
}
#endif

#endif /* CFGTOOLS_H */
