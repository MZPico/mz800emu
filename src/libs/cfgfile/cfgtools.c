/**
 * @file   cfgtools.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Implementace pomocnych nastroju pro parsovani cisel z textu
 *
 * Obsahuje funkce pro prevod retezcu na cisla (dec/hex) a parsovani
 * poli cisel s podporou ruznych separatoru.
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

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>


/**
 * @brief Overi, zda retezec obsahuje pouze desitkove cislice
 * @param str Vstupni retezec
 * @return EXIT_SUCCESS pokud je retezec cislo, EXIT_FAILURE jinak
 */
static int cfgtools_strisnum ( char *str ) {
    while ( *str != 0x00 ) {
        if ( 0 == isdigit ( *str++ ) ) return EXIT_FAILURE;
    };
    return EXIT_SUCCESS;
}


/**
 * @brief Overi, zda retezec je hexadecimalni cislo s prefixem 0x
 * @param str Vstupni retezec
 * @return EXIT_SUCCESS pokud je retezec hex cislo, EXIT_FAILURE jinak
 */
static int cfgtools_strishexnum ( char *str ) {
    if ( 0 != strncasecmp ( str, "0x", 2 ) ) return EXIT_FAILURE;
    str += 2 * sizeof ( char );
    while ( *str != 0x00 ) {
        if ( 0 == isxdigit ( *str++ ) ) return EXIT_FAILURE;
    };
    return EXIT_SUCCESS;
}


/**
 * @brief Prevede textovy retezec na long int (dec nebo hex s prefixem 0x)
 * @param str Vstupni textovy retezec s cislem
 * @param retcode Ukazatel na navratovy kod (EXIT_SUCCESS/EXIT_FAILURE)
 * @return Prevedena ciselna hodnota, nebo 0 pri chybe
 */
long int cfgtools_strtol ( char *str, int *retcode ) {
    *retcode = EXIT_SUCCESS;
    if ( EXIT_SUCCESS == cfgtools_strisnum ( str ) ) {
        return strtol ( str, NULL, 10 );
    } else if ( EXIT_SUCCESS == cfgtools_strishexnum ( str ) ) {
        return strtol ( str, NULL, 16 );
    };
    fprintf ( stderr, "\n\n%s(%d) - Error: parameter is not dec or hex number '%s'!\n\n", __func__, __LINE__, str );
    *retcode = EXIT_FAILURE;
    return 0;
}


/**
 * @brief Parsuje retezec obsahujici pole cisel oddeleny separatory
 * @param encoded_txt Vstupni text, napr. "10, 0x20, 30"
 * @param output_numbers_array Vystupni pole pro naparsovana cisla
 * @param max_elements Maximalni pocet prvku ve vystupnim poli
 * @param separators Oddelovace (NULL = " ,")
 * @param retcode Ukazatel na navratovy kod (EXIT_SUCCESS/EXIT_FAILURE)
 * @return Pocet naparsovanych prvku, nebo 0 pri chybe
 */
int cfgtools_strtol_array ( char *encoded_txt, long int *output_numbers_array, int max_elements, char *separators, int *retcode ) {

    if ( separators == NULL ) separators = " ,";

    int output_array_size = 0;

    int length = strlen ( encoded_txt );
    int i;
    for ( i = 0; i < length; i++ ) {
        if ( isdigit ( encoded_txt[i] ) ) {
            int j;
            for ( j = i; j < length; j++ ) {
                if ( !( ( isxdigit ( encoded_txt[j] ) ) || ( encoded_txt[j] == 'x' ) || ( encoded_txt[j] == 'X' ) ) ) {
                    break;
                };
            };

            int number_length = j - i;
            char *txt = malloc ( number_length + 1 );
            strncpy ( txt, &encoded_txt[i], ( j - i ) );
            txt[number_length] = 0x00;
            i = j;
            int sector_id = cfgtools_strtol ( txt, retcode );
            free ( txt );
            if ( EXIT_SUCCESS != *retcode ) {
                return 0;
            };

            if ( output_array_size >= max_elements ) {
                fprintf ( stderr, "\n\n%s(%d) - Error: parsed txt array is larger than %d elements!\n\n", __func__, __LINE__, max_elements );
                *retcode = EXIT_FAILURE;
                return 0;
            };

            output_numbers_array[output_array_size++] = sector_id;

        } else {

            char *sep = separators;

            while ( *sep != 0x00 ) {
                if ( *sep == encoded_txt[i] ) break;
                sep++;
            };

            if ( *sep == 0x00 ) {
                fprintf ( stderr, "\n\n%s(%d) - Error: invalid character '%c' in parsed txt array!\n\n", __func__, __LINE__, encoded_txt[i] );
                *retcode = EXIT_FAILURE;
                return 0;
            };
        };
    };

    return output_array_size;
}
