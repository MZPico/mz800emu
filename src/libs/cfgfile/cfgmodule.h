/**
 * @file   cfgmodule.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Konfiguracni modul — jedna sekce [JMENO] v INI souboru
 *
 * Obsahuje kolekci elementu (klic=hodnota) a volitelne callbacky
 * pro propagate a save. Poskytuje by_name convenience wrappery
 * pro pristup k hodnotam elementu podle jmena.
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

#ifndef CFGMODULE_H
#define CFGMODULE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cfgelement.h"

    /* Forward deklarace — modul odkazuje na svuj nadrazeny root */
    struct st_CFGROOT;


    /** @brief Typ callbacku volaneho pri propagaci modulu */
    typedef void (*cfgmodule_propagate_cb_t ) (void *m, void *data );

    /** @brief Typ callbacku volaneho pri ukladani modulu */
    typedef void (*cfgmodule_save_cb_t ) (void *m, void *data );


    /** @brief Struktura callbacku pro propagaci modulu */
    typedef struct st_CFGMODPROPCB {
        cfgmodule_propagate_cb_t exec;  /**< Ukazatel na callback funkci */
        void *data;                     /**< Uzivatelska data predana callbacku */
    } st_CFGMODPROPCB;


    /** @brief Struktura callbacku pro ukladani modulu */
    typedef struct st_CFGMODSAVECB {
        cfgmodule_save_cb_t exec;       /**< Ukazatel na callback funkci */
        void *data;                     /**< Uzivatelska data predana callbacku */
    } st_CFGMODSAVECB;


    /** @brief Konfiguracni modul — jedna sekce [JMENO] v INI souboru */
    typedef struct st_CFGMODULE {
        char *name;                     /**< Nazev sekce v INI souboru */

        struct st_CFGROOT *parent;      /**< Ukazatel na nadrazeny root */

        int elements_count;             /**< Pocet registrovanych elementu */
        st_CFGELEMENT **elements;       /**< Pole ukazatelu na elementy */

        st_CFGMODPROPCB propagate_cb;   /**< Callback volany po propagate celeho modulu */
        st_CFGMODSAVECB save_cb;        /**< Callback volany pred save celeho modulu */

    } st_CFGMODULE;

    /** @brief Zkraceny typovy alias pro st_CFGMODULE */
    typedef st_CFGMODULE CFGMOD;


    /**
     * @brief Registruje callback volany po propagaci vsech elementu modulu
     * @param m Ukazatel na modul
     * @param cbexec Ukazatel na callback funkci
     * @param cbdata Uzivatelska data predana callbacku
     */
    extern void cfgmodule_set_propagate_cb ( st_CFGMODULE *m, cfgmodule_propagate_cb_t cbexec, void *cbdata );

    /**
     * @brief Registruje callback volany pred ulozenim vsech elementu modulu
     * @param m Ukazatel na modul
     * @param cbexec Ukazatel na callback funkci
     * @param cbdata Uzivatelska data predana callbacku
     */
    extern void cfgmodule_set_save_cb ( st_CFGMODULE *m, cfgmodule_save_cb_t cbexec, void *cbdata );

    /**
     * @brief Resetuje vsechny elementy modulu na vychozi hodnoty
     * @param m Ukazatel na modul
     */
    extern void cfgmodule_reset ( st_CFGMODULE *m );

    /**
     * @brief Registruje novy element v modulu
     *
     * Variabilni parametry podle typu:
     * - CFGENTYPE_KEYWORD:  default_value, pak seznam paru (int hodnota, char* keyword), ukonceny -1
     * - CFGENTYPE_BOOL:     default_value (int, 0 nebo 1)
     * - CFGENTYPE_TEXT:     default_value (char*)
     * - CFGENTYPE_UNSIGNED: default_value (int), min (int), max (int)
     * - CFGENTYPE_FLOAT:    default_value (double!), min (double!), max (double!)
     *
     * POZNAMKA: pro CFGENTYPE_FLOAT pouzivejte double literaly (0.5, ne 0.5f),
     * protoze C variadic promotion automaticky konvertuje float na double.
     *
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu (klic v INI)
     * @param type Typ elementu
     * @param ... Variabilni parametry podle typu
     * @return Ukazatel na novy element, nebo NULL pokud jmeno jiz existuje
     */
    extern st_CFGELEMENT* cfgmodule_register_new_element ( st_CFGMODULE *m, char *element_name, en_CFGELEMENTTYPE type, ... );

    /**
     * @brief Vyhleda element podle jmena (case-insensitive)
     * @param m Ukazatel na modul
     * @param element_name Hledany nazev elementu
     * @return Ukazatel na element, nebo NULL pokud neexistuje
     */
    extern st_CFGELEMENT* cfgmodule_get_element_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati aktualni unsigned hodnotu elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Aktualni unsigned hodnota
     */
    extern unsigned cfgmodule_get_element_unsigned_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati aktualni textovou hodnotu elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Ukazatel na textovy retezec (vlastneny elementem)
     */
    extern char* cfgmodule_get_element_text_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati aktualni bool hodnotu elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Aktualni hodnota (0 nebo 1)
     */
    extern int cfgmodule_get_element_bool_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati aktualni keyword hodnotu elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Aktualni ciselna hodnota keyword
     */
    extern int cfgmodule_get_element_keyword_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati aktualni float hodnotu elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Aktualni float hodnota
     */
    extern float cfgmodule_get_element_float_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati vychozi unsigned hodnotu elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Vychozi unsigned hodnota
     */
    extern unsigned cfgmodule_get_element_unsigned_default_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati vychozi textovou hodnotu elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Ukazatel na vychozi textovy retezec (vlastneny elementem)
     */
    extern char* cfgmodule_get_element_text_default_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati vychozi bool hodnotu elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Vychozi hodnota (0 nebo 1)
     */
    extern int cfgmodule_get_element_bool_default_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati vychozi keyword hodnotu elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Vychozi ciselna hodnota keyword
     */
    extern int cfgmodule_get_element_keyword_default_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati vychozi float hodnotu elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Vychozi float hodnota
     */
    extern float cfgmodule_get_element_float_default_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati textovy keyword aktualni hodnoty elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Textovy keyword, nebo NULL
     */
    extern char* cfgmodule_get_element_keyword_by_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Vrati textovy keyword vychozi hodnoty elementu podle jmena
     * @param m Ukazatel na modul
     * @param element_name Nazev elementu
     * @return Textovy keyword, nebo NULL
     */
    extern char* cfgmodule_get_element_keyword_by_default_value_by_name ( st_CFGMODULE *m, char *element_name );

    /**
     * @brief Prenese hodnoty vsech elementu modulu do aplikace
     * @param m Ukazatel na modul
     */
    extern void cfgmodule_propagate ( st_CFGMODULE *m );

    /**
     * @brief Ulozi vsechny elementy modulu do INI souboru
     * @param m Ukazatel na modul
     */
    extern void cfgmodule_save ( st_CFGMODULE *m );

    /**
     * @brief Parsuje INI soubor a nastavi hodnoty elementu modulu
     * @param m Ukazatel na modul
     * @return EXIT_SUCCESS pri uspechu, EXIT_FAILURE pri chybe
     */
    extern int cfgmodule_parse ( st_CFGMODULE *m );


#ifdef __cplusplus
}
#endif

#endif /* CFGMODULE_H */
