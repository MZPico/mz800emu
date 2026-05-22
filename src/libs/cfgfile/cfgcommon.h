/**
 * @file   cfgcommon.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Spolecne makro pro kontrolu alokace pameti v knihovne cfgfile
 *
 * Definuje makro CFGCOMMON_MALLOC_ERROR, ktere se pouziva ve vsech
 * souborech knihovny cfgfile pro kontrolu vysledku malloc/realloc.
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

#ifndef CFGCOMMON_H
#define CFGCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

/**
 * @brief Makro pro kontrolu alokace pameti
 *
 * Pokud alokace selze, vypise chybu a ukonci program — po selhani
 * malloc/realloc neni mozne bezpecne pokracovat.
 *
 * @param expr Vyraz k otestovani (typicky `ptr == NULL`)
 */
#define CFGCOMMON_MALLOC_ERROR(expr) do { \
    if ( expr ) { \
        fprintf ( stderr, "%s():%d - Could not allocate memory: %s\n", __func__, __LINE__, strerror ( errno ) ); \
        abort(); \
    } \
} while (0)


#ifdef __cplusplus
}
#endif

#endif /* CFGCOMMON_H */
