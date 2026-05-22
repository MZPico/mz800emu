/**
 * @file   cfgroot.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Implementace korenove konfiguracni struktury (INI soubor)
 *
 * Obsahuje logiku pro vytvoreni/destrukci konfiguracniho stromu,
 * registraci modulu, save do INI souboru, propagate a reset.
 * Implementuje take pomocnou funkci cfgcommon_set_text.
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
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <errno.h>

#include "cfgroot.h"
#include "cfgcommon.h"
#include "cfgfile_internal.h"

#include "baseui/baseui.h"


/**
 * @brief Vytvori novou korenovou konfiguracni strukturu
 * @param filename Cesta k INI souboru (ulozi se ukazatel, nekopiruje se)
 * @return Ukazatel na novou strukturu (nikdy NULL — pri selhani alokace abort)
 */
st_CFGROOT* cfgroot_new ( const char *filename ) {
    st_CFGROOT *r = malloc ( sizeof ( st_CFGROOT ) );
    CFGCOMMON_MALLOC_ERROR ( r == NULL );
    memset ( r, 0x00, sizeof ( st_CFGROOT ) );

    r->filename = filename;
    return r;
}


/**
 * @brief Znici celý konfiguracni strom — moduly, elementy, keyword tabulky
 * @param r Ukazatel na root k zniceni (bezpecne pro NULL)
 */
void cfgroot_destroy ( st_CFGROOT *r ) {

    if ( r == NULL ) return;

    int i;
    for ( i = 0; i < r->modules_count; i++ ) {
        st_CFGMODULE *m = r->modules[i];
        cfgcommon_destroy_module ( m );
    };

    if ( r->modules != NULL ) {
        free ( r->modules );
    };

    free ( r );
}


/**
 * @brief Registruje novy modul (sekci [JMENO] v INI)
 * @param r Ukazatel na root
 * @param module_name Nazev modulu
 * @return Ukazatel na novy modul, nebo NULL pokud modul se stejnym jmenem jiz existuje
 */
st_CFGMODULE* cfgroot_register_new_module ( st_CFGROOT *r, char *module_name ) {

    assert ( r != NULL );

    if ( NULL != cfgroot_get_module_by_name ( r, module_name ) ) return NULL;

    st_CFGMODULE *m = cfgcommon_new_module ( r, module_name );
    if ( NULL == m ) return NULL;

    unsigned modules_count = r->modules_count + 1;

    r->modules = realloc ( r->modules, modules_count * sizeof ( st_CFGMODULE* ) );
    CFGCOMMON_MALLOC_ERROR ( r->modules == NULL );

    r->modules[ r->modules_count++ ] = m;

    return m;
}


/**
 * @brief Vyhleda modul podle jmena (case-insensitive)
 * @param r Ukazatel na root
 * @param module_name Hledany nazev modulu
 * @return Ukazatel na modul, nebo NULL pokud neexistuje
 */
st_CFGMODULE* cfgroot_get_module_by_name ( st_CFGROOT *r, char *module_name ) {

    assert ( r != NULL );

    int i;
    for ( i = 0; i < r->modules_count; i++ ) {
        st_CFGMODULE *m = r->modules[i];
        if ( 0 == strcasecmp ( m->name, module_name ) ) return m;
    };
    return NULL;
}


/**
 * @brief Resetuje vsechny moduly a elementy na vychozi hodnoty
 * @param r Ukazatel na root
 */
void cfgroot_reset ( st_CFGROOT *r ) {

    assert ( r != NULL );

    int i;
    for ( i = 0; i < r->modules_count; i++ ) {
        st_CFGMODULE *m = r->modules[i];
        cfgmodule_reset ( m );
    };
}


/**
 * @brief Prenese hodnoty vsech elementu do aplikace (pres handlery/pointery/callbacky)
 * @param r Ukazatel na root
 */
void cfgroot_propagate ( st_CFGROOT *r ) {

    assert ( r != NULL );

    int i;
    for ( i = 0; i < r->modules_count; i++ ) {
        st_CFGMODULE *m = r->modules[i];
        cfgmodule_propagate ( m );
    };
}


/**
 * @brief Ulozi aktualni hodnoty vsech elementu do INI souboru
 * @param r Ukazatel na root
 */
void cfgroot_save ( st_CFGROOT *r ) {

    assert ( r != NULL );

    printf ( "Save config into: %s\n", r->filename );


    if ( !( r->ini_fp = baseui_tools_file_open ( r->filename, "w" ) ) ) {
        baseui_error ( "Can't open file '%s' to write: %s", r->filename, strerror ( errno ) );
        return;
    };

    fprintf ( r->ini_fp, "\n" );

    int i;
    for ( i = 0; i < r->modules_count; i++ ) {
        st_CFGMODULE *m = r->modules[i];
        cfgmodule_save ( m );
    };

    fclose ( r->ini_fp );
}


/**
 * @brief Nastavi textovy retezec s automatickou realokaci
 *
 * Centralni funkce pro kopirovani textovych retezcu v knihovne cfgfile.
 * Pokud cilovy ukazatel jiz obsahuje alokovany retezec, provede realloc.
 *
 * @param mem_point Ukazatel na ukazatel na cilovy retezec
 * @param txt Zdrojovy textovy retezec k nakopirovani
 */
void cfgcommon_set_text ( char **mem_point, char *txt ) {

    if ( *mem_point == txt ) {
        return;
    };

    unsigned length = strlen ( txt ) + 1;
    *mem_point = realloc ( *mem_point, length );
    CFGCOMMON_MALLOC_ERROR ( *mem_point == NULL );
    strcpy ( *mem_point, txt );
}
