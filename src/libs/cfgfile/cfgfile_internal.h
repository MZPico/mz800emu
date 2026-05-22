/**
 * @file   cfgfile_internal.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Interni hlavickovy soubor knihovny cfgfile
 *
 * Obsahuje deklarace privatnich funkci, ktere jsou pouzivany pouze
 * uvnitr knihovny a nejsou soucasti verejneho API.
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

#ifndef CFGFILE_INTERNAL_H
#define CFGFILE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cfgmodule.h"
#include "cfgelement.h"

    /**
     * @brief Nastavi textovy retezec s automatickou realokaci
     * @param mem_point Ukazatel na ukazatel na cilovy retezec
     * @param txt Zdrojovy textovy retezec k nakopirovani
     */
    extern void cfgcommon_set_text ( char **mem_point, char *txt );

    /**
     * @brief Vytvori novy konfiguracni modul
     * @param parent Ukazatel na nadrazeny root
     * @param module_name Nazev modulu (INI sekce)
     * @return Ukazatel na novy modul (nikdy NULL — pri selhani alokace abort)
     */
    extern st_CFGMODULE* cfgcommon_new_module ( struct st_CFGROOT *parent, char *module_name );

    /**
     * @brief Znici konfiguracni modul a uvolni vsechny jeho prostredky
     * @param m Ukazatel na modul k zniceni (bezpecne pro NULL)
     */
    extern void cfgcommon_destroy_module ( st_CFGMODULE *m );

    /**
     * @brief Vytvori novy konfiguracni element
     * @param parent Ukazatel na nadrazeny modul
     * @param element_name Nazev elementu (klic v INI)
     * @param type Typ elementu (KEYWORD, BOOL, TEXT, UNSIGNED, FLOAT)
     * @param args Variabilni parametry podle typu (default hodnota, rozsah, keywords...)
     * @return Ukazatel na novy element (nikdy NULL — pri selhani alokace abort)
     */
    extern st_CFGELEMENT* cfgcommon_new_element ( struct st_CFGMODULE *parent, char *element_name, en_CFGELEMENTTYPE type, va_list args );

    /**
     * @brief Znici konfiguracni element a uvolni vsechny jeho prostredky
     * @param e Ukazatel na element k zniceni (bezpecne pro NULL)
     */
    extern void cfgcommon_destroy_element ( st_CFGELEMENT *e );


#ifdef __cplusplus
}
#endif

#endif /* CFGFILE_INTERNAL_H */
