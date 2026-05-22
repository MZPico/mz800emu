/**
 * @file   cfgroot.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Korenova konfiguracni struktura — predstavuje jeden INI soubor
 *
 * Obsahuje kolekci modulu (sekci), kazdy s vlastnimi elementy.
 * Poskytuje operace pro vytvoreni/destrukci konfiguracniho stromu,
 * registraci modulu, propagate, save a reset.
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

#ifndef CFGROOT_H
#define CFGROOT_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stdio.h>

#include "cfgmodule.h"

    /** @brief Korenova konfiguracni struktura — predstavuje jeden INI soubor */
    typedef struct st_CFGROOT {
        char *name;                 /**< Nazev konfigurace (volitelny) */
        int modules_count;          /**< Pocet registrovanych modulu */
        st_CFGMODULE **modules;     /**< Pole ukazatelu na moduly */

        /* INI soubor */
        const char *filename;       /**< Cesta k INI souboru */
        FILE *ini_fp;               /**< Souborovy deskriptor pro cteni/zapis */
    } st_CFGROOT;

    /** @brief Zkraceny typovy alias pro st_CFGROOT */
    typedef st_CFGROOT CFGR;

    /**
     * @brief Vytvori novou korenovou konfiguracni strukturu
     * @param filename Cesta k INI souboru (ulozi se ukazatel, nekopiruje se)
     * @return Ukazatel na novou strukturu (nikdy NULL — pri selhani alokace abort)
     */
    extern st_CFGROOT* cfgroot_new ( const char *filename );

    /**
     * @brief Znici celý konfiguracni strom — moduly, elementy, keyword tabulky
     * @param r Ukazatel na root k zniceni (bezpecne pro NULL)
     */
    extern void cfgroot_destroy ( st_CFGROOT *r );

    /**
     * @brief Resetuje vsechny moduly a elementy na vychozi hodnoty
     * @param r Ukazatel na root
     */
    extern void cfgroot_reset ( st_CFGROOT *r );

    /**
     * @brief Registruje novy modul (sekci [JMENO] v INI)
     * @param r Ukazatel na root
     * @param module_name Nazev modulu
     * @return Ukazatel na novy modul, nebo NULL pokud modul se stejnym jmenem jiz existuje
     */
    extern st_CFGMODULE* cfgroot_register_new_module ( st_CFGROOT *r, char *module_name );

    /**
     * @brief Vyhleda modul podle jmena (case-insensitive)
     * @param r Ukazatel na root
     * @param module_name Hledany nazev modulu
     * @return Ukazatel na modul, nebo NULL pokud neexistuje
     */
    extern st_CFGMODULE* cfgroot_get_module_by_name ( st_CFGROOT *r, char *module_name );

    /**
     * @brief Prenese hodnoty vsech elementu do aplikace (pres handlery/pointery/callbacky)
     * @param r Ukazatel na root
     */
    extern void cfgroot_propagate ( st_CFGROOT *r );

    /**
     * @brief Ulozi aktualni hodnoty vsech elementu do INI souboru
     * @param r Ukazatel na root
     */
    extern void cfgroot_save ( st_CFGROOT *r );


#ifdef __cplusplus
}
#endif

#endif /* CFGROOT_H */
