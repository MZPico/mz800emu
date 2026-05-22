/**
 * @file   dsk_tools.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Vyšší nástroje pro práci s Extended CPC DSK obrazy.
 *
 * Poskytuje API pro vytváření, modifikace, validaci, identifikaci
 * formátu a iteraci přes stopy/sektory DSK diskových obrazů.
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


#ifndef DSK_TOOLS_H
#define DSK_TOOLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "dsk.h"


    /* ─── Logování ─── */

    /**
     * @brief Callback pro logování.
     *
     * Knihovna je ve výchozím stavu tichá — loguje pouze pokud je nastaven
     * callback přes dsk_tools_set_log_cb().
     *
     * @param level Úroveň logování: 0 = info, 1 = warning, 2 = error.
     * @param msg Formátovaná zpráva.
     * @param user_data Uživatelská data předaná při registraci callbacku.
     */
    typedef void (*dsk_tools_log_cb_t)( int level, const char *msg, void *user_data );

    /** @brief Úroveň logování: informační zpráva. */
#define DSK_LOG_INFO    0
    /** @brief Úroveň logování: varování. */
#define DSK_LOG_WARNING 1
    /** @brief Úroveň logování: chyba. */
#define DSK_LOG_ERROR   2

    /**
     * @brief Nastaví logovací callback.
     *
     * NULL = žádné logování (výchozí). user_data se předá callbacku při každém volání.
     *
     * @param cb Logovací callback, nebo NULL pro vypnutí logování.
     * @param user_data Uživatelská data předávaná callbacku.
     */
    extern void dsk_tools_set_log_cb ( dsk_tools_log_cb_t cb, void *user_data );


    /* ─── Popis geometrie pro vytváření obrazů ─── */

    /** @brief Typ řazení sektorů na stopě. */
    typedef enum en_DSK_SECTOR_ORDER_TYPE {
        DSK_SEC_ORDER_CUSTOM = 0,           /**< Uživatelská mapa (vyžaduje přiložený sector_map) */
        DSK_SEC_ORDER_NORMAL = 1,           /**< Sekvenční řazení: 1, 2, 3, ... */
        DSK_SEC_ORDER_INTERLACED_LEC = 2,   /**< 1x prokládané řazení */
        DSK_SEC_ORDER_INTERLACED_LEC_HD = 3 /**< 2x prokládané řazení */
    } en_DSK_SECTOR_ORDER_TYPE;


    /** @brief Jedno pravidlo popisující geometrii stop v rozsahu. */
    typedef struct st_DSK_DESCRIPTION_RULE {
        uint8_t absolute_track;             /**< Absolutní stopa od které pravidlo platí */
        uint8_t sectors;                    /**< Počet sektorů na stopě */
        en_DSK_SECTOR_SIZE ssize;           /**< Kódovaná velikost sektoru */
        en_DSK_SECTOR_ORDER_TYPE sector_order; /**< Typ řazení sektorů */
        uint8_t *sector_map;                /**< Mapa sektorů (jen pro CUSTOM, jinak NULL) */
        uint8_t filler;                     /**< Filler byte pro nové sektory */
    } st_DSK_DESCRIPTION_RULE;


    /**
     * @brief Popis geometrie disku pro vytvoření obrazu.
     *
     * Alokuje se přes malloc(dsk_tools_compute_description_size(n)),
     * pravidla jsou za strukturou jako C99 flexible array member.
     */
    typedef struct st_DSK_DESCRIPTION {
        uint16_t count_rules;               /**< Počet pravidel */
        uint8_t tracks;                     /**< Počet stop (per strana) */
        uint8_t sides;                      /**< Počet stran (1 nebo 2) */
        st_DSK_DESCRIPTION_RULE rules[];    /**< Pravidla geometrie (C99 flexible array member) */
    } st_DSK_DESCRIPTION;


    /**
     * @brief Výpočet velikosti alokace pro st_DSK_DESCRIPTION s daným počtem pravidel.
     * @param rules Počet pravidel.
     * @return Velikost v bajtech pro malloc().
     */
    static inline size_t dsk_tools_compute_description_size ( uint8_t rules ) {
        return ( sizeof ( st_DSK_DESCRIPTION ) + sizeof ( st_DSK_DESCRIPTION_RULE ) * rules );
    }


    /* ─── Vytváření a modifikace obrazů ─── */

    /**
     * @brief Přiřadí jedno pravidlo do popisu geometrie disku.
     *
     * Pravidla musí být přiřazována vzestupně podle abs_track.
     *
     * @param dskdesc Odkaz na existující strukturu popisu. Může být NULL (NOP).
     * @param rule Pořadové číslo záznamu (index do rules[]).
     * @param abs_track Absolutní stopa od které pravidlo platí.
     * @param sectors Počet sektorů na stopě.
     * @param ssize Kódovaná velikost sektoru.
     * @param sector_order Typ řazení sektorů.
     * @param sector_map Mapa sektorů (jen pro CUSTOM, jinak NULL).
     * @param default_value Filler byte pro nové sektory.
     */
    extern void dsk_tools_assign_description ( st_DSK_DESCRIPTION *dskdesc, uint8_t rule, uint8_t abs_track, uint8_t sectors, en_DSK_SECTOR_SIZE ssize, en_DSK_SECTOR_ORDER_TYPE sector_order, uint8_t *sector_map, uint8_t default_value );

    /**
     * @brief Vygeneruje mapu ID sektorů podle typu řazení.
     *
     * CUSTOM se automaticky převede na NORMAL.
     *
     * @param sectors Počet sektorů.
     * @param sector_order Typ řazení sektorů.
     * @param sector_map Výstupní pole o velikosti sectors.
     */
    extern void dsk_tools_make_sector_map ( uint8_t sectors, en_DSK_SECTOR_ORDER_TYPE sector_order, uint8_t *sector_map );

    /**
     * @brief Vytvoří kompletní DSK obraz podle popisu geometrie.
     * @param h Handler.
     * @param desc Popis geometrie disku.
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_create_image ( st_HANDLER *h, st_DSK_DESCRIPTION *desc );

    /**
     * @brief Vytvoří DSK hlavičku podle popisu geometrie.
     * @param h Handler.
     * @param desc Popis geometrie disku.
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_create_image_header ( st_HANDLER *h, st_DSK_DESCRIPTION *desc );

    /**
     * @brief Vytvoří postupně všechny stopy podle popisu geometrie.
     * @param h Handler.
     * @param desc Popis geometrie.
     * @param first_abs_track První absolutní stopa.
     * @param dsk_offset Offset v souboru (0 = sizeof(st_DSK_HEADER)).
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_create_image_tracks ( st_HANDLER *h, st_DSK_DESCRIPTION *desc, uint8_t first_abs_track, uint32_t dsk_offset );

    /**
     * @brief Vytvoří jednu kompletní DSK stopu (hlavičku + sektory).
     * @param h Handler.
     * @param dsk_offset Offset v souboru.
     * @param track Číslo stopy.
     * @param side Strana (0/1).
     * @param sectors Počet sektorů.
     * @param ssize Kódovaná velikost sektoru.
     * @param sector_map Seznam ID sektorů.
     * @param default_value Filler byte.
     * @param track_total_bytes Výstup: celková velikost zapsané stopy.
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_create_track ( st_HANDLER *h, uint32_t dsk_offset, uint8_t track, uint8_t side, uint8_t sectors, en_DSK_SECTOR_SIZE ssize, uint8_t *sector_map, uint8_t default_value, uint32_t *track_total_bytes );

    /**
     * @brief Vytvoří hlavičku pro jednu stopu.
     * @param h Handler.
     * @param dsk_offset Offset v souboru.
     * @param track Číslo stopy.
     * @param side Strana (0/1).
     * @param sectors Počet sektorů.
     * @param ssize Kódovaná velikost sektoru.
     * @param sector_map Seznam ID jednotlivých sektorů.
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_create_track_header ( st_HANDLER *h, uint32_t dsk_offset, uint8_t track, uint8_t side, uint8_t sectors, en_DSK_SECTOR_SIZE ssize, uint8_t *sector_map );

    /**
     * @brief Vyplní všechny sektory na stopě výchozí hodnotou.
     * @param h Handler.
     * @param dsk_offset Offset za hlavičkou stopy.
     * @param sectors Počet sektorů.
     * @param ssize Kódovaná velikost sektoru.
     * @param default_value Filler byte.
     * @param sectors_total_bytes Výstup: celková velikost zapsaných dat.
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_create_track_sectors ( st_HANDLER *h, uint32_t dsk_offset, uint8_t sectors, en_DSK_SECTOR_SIZE ssize, uint8_t default_value, uint16_t *sectors_total_bytes );

    /**
     * @brief Změní geometrii a obsah jedné stopy v existujícím obrazu.
     *
     * Pokud se změní velikost stopy, přesune data následujících stop.
     *
     * @param h Handler.
     * @param short_image_info Informace o obrazu (NULL = načte se automaticky).
     * @param abstrack Absolutní stopa.
     * @param sectors Nový počet sektorů.
     * @param ssize Nová kódovaná velikost sektoru.
     * @param sector_map Nová mapa sektorů.
     * @param default_value Filler byte.
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_change_track ( st_HANDLER *h, st_DSK_SHORT_IMAGE_INFO *short_image_info, uint8_t abstrack, uint8_t sectors, en_DSK_SECTOR_SIZE ssize, uint8_t *sector_map, uint8_t default_value );

    /**
     * @brief Přidá stopy na konec existujícího DSK obrazu.
     * @param h Handler.
     * @param desc Popis geometrie s novými stopami.
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_add_tracks ( st_HANDLER *h, st_DSK_DESCRIPTION *desc );

    /**
     * @brief Zmenší obraz odstraněním stop od konce.
     * @param h Handler.
     * @param short_image_info Informace o obrazu (NULL = načte se automaticky).
     * @param total_tracks Nový celkový počet absolutních stop.
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_shrink_image ( st_HANDLER *h, st_DSK_SHORT_IMAGE_INFO *short_image_info, uint8_t total_tracks );


    /* ─── Validace a inspekce ─── */

    /**
     * @brief Přečte pole file_info z hlavičky DSK.
     * @param h Handler.
     * @param dsk_fileinfo_buffer Výstupní buffer (min DSK_FILEINFO_FIELD_LENGTH bajtů).
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_get_dsk_fileinfo ( st_HANDLER *h, uint8_t *dsk_fileinfo_buffer );

    /**
     * @brief Ověří, že pole file_info v hlavičce odpovídá Extended CPC DSK formátu.
     * @param h Handler.
     * @return EXIT_SUCCESS pokud je platný, EXIT_FAILURE pokud ne.
     */
    extern int dsk_tools_check_dsk_fileinfo ( st_HANDLER *h );

    /**
     * @brief Přečte pole creator z hlavičky DSK.
     * @param h Handler.
     * @param dsk_creator_buffer Výstupní buffer (min DSK_CREATOR_FIELD_LENGTH bajtů).
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_get_dsk_creator ( st_HANDLER *h, uint8_t *dsk_creator_buffer );


    /** @brief Výsledek kontroly track info identifikátoru. */
    typedef enum en_DSK_TOOLS_CHCKTRKINFO {
        DSK_TOOLS_CHCKTRKINFO_SUCCESS = 0,      /**< Track info je platný */
        DSK_TOOLS_CHCKTRKINFO_READ_ERROR,        /**< Chyba čtení */
        DSK_TOOLS_CHCKTRKINFO_FAILURE,           /**< Track info není platný */
    } en_DSK_TOOLS_CHCKTRKINFO;

    /**
     * @brief Ověří identifikátor track info na zadaném offsetu.
     * @param h Handler.
     * @param offset Offset v souboru kde se očekává track info.
     * @return Výsledek kontroly (en_DSK_TOOLS_CHCKTRKINFO).
     */
    extern en_DSK_TOOLS_CHCKTRKINFO dsk_tools_check_dsk_trackinfo_on_offset ( st_HANDLER *h, uint32_t offset );

    /**
     * @brief Kompletní validace DSK obrazu s volitelným automatickým opravením.
     * @param h Handler.
     * @param print_info Nenulová = logovat detaily přes logovací callback.
     * @param dsk_autofix Nenulová = opravit nalezené chyby v hlavičce.
     * @return EXIT_SUCCESS pokud je obraz v pořádku (nebo opraven), EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_check_dsk ( st_HANDLER *h, int print_info, int dsk_autofix );


    /* ─── Analýza geometrie (pravidla stop) ─── */

    /** @brief Kompaktní pravidlo popisující rozsah stop se stejnou geometrií. */
    typedef struct st_DSK_TOOLS_TRACK_RULE_INFO {
        uint8_t from_track;         /**< Absolutní stopa od které pravidlo platí */
        uint8_t count_tracks;       /**< Počet stop pokrytých tímto pravidlem */
        uint8_t sectors;            /**< Počet sektorů na stopě */
        en_DSK_SECTOR_SIZE ssize;   /**< Kódovaná velikost sektoru */
    } st_DSK_TOOLS_TRACK_RULE_INFO;


    /** @brief Výsledek analýzy geometrie disku — sada pravidel stop. */
    typedef struct st_DSK_TOOLS_TRACKS_RULES_INFO {
        uint8_t total_tracks;                   /**< Celkový počet absolutních stop */
        uint8_t sides;                          /**< Počet stran */
        uint8_t count_rules;                    /**< Počet pravidel */
        uint8_t mzboot_track;                   /**< 1 pokud stopa 1 je MZ boot (16x256B) */
        st_DSK_TOOLS_TRACK_RULE_INFO *rule;     /**< Pole pravidel (dynamicky alokované) */
    } st_DSK_TOOLS_TRACKS_RULES_INFO;


    /**
     * @brief Uvolní paměť alokovanou pro pravidla stop.
     * @param tracks_rules Struktura k uvolnění (může být NULL).
     */
    extern void dsk_tools_destroy_track_rules ( st_DSK_TOOLS_TRACKS_RULES_INFO *tracks_rules );

    /**
     * @brief Analyzuje geometrii DSK obrazu a extrahuje pravidla stop.
     * @param h Handler.
     * @return Alokovaná struktura (uvolnit přes dsk_tools_destroy_track_rules()), nebo NULL při chybě.
     */
    extern st_DSK_TOOLS_TRACKS_RULES_INFO* dsk_tools_get_tracks_rules ( st_HANDLER *h );


    /* ─── Identifikace formátu ─── */

    /** @brief Rozpoznané formáty MZ disket. */
    typedef enum en_DSK_TOOLS_IDENTFORMAT {
        DSK_TOOLS_IDENTFORMAT_UNKNOWN = 0,  /**< Neznámý formát */
        DSK_TOOLS_IDENTFORMAT_MZBASIC,      /**< Sharp MZ-BASIC disketa (16 x 256 B na všech stopách) */
        DSK_TOOLS_IDENTFORMAT_MZCPM,        /**< Sharp LEC CP/M SD (9 x 512 B + boot 16 x 256 B) */
        DSK_TOOLS_IDENTFORMAT_MZCPMHD,      /**< Sharp LEC CP/M HD (18 x 512 B + boot 16 x 256 B) */
        DSK_TOOLS_IDENTFORMAT_MZBOOT,       /**< Bootovatelný disk (1. stopa je MZ-BASIC, zbývající formát neznámý) */
    } en_DSK_TOOLS_IDENTFORMAT;


    /**
     * @brief Identifikuje formát z existujících pravidel stop.
     * @param tracks_rules Pravidla stop (může být NULL -> UNKNOWN).
     * @return Identifikovaný formát.
     */
    extern en_DSK_TOOLS_IDENTFORMAT dsk_tools_identformat_from_tracks_rules ( const st_DSK_TOOLS_TRACKS_RULES_INFO *tracks_rules );

    /**
     * @brief Identifikuje formát DSK obrazu.
     * @param h Handler.
     * @param result Výstup: identifikovaný formát.
     * @return EXIT_SUCCESS při úspěchu, EXIT_FAILURE při chybě.
     */
    extern int dsk_tools_identformat ( st_HANDLER *h, en_DSK_TOOLS_IDENTFORMAT *result );

    /**
     * @brief Vrátí pravidlo platné pro zadanou stopu.
     * @param tracks_rules Pravidla stop (může být NULL).
     * @param track Absolutní číslo stopy.
     * @return Ukazatel na pravidlo, nebo NULL pokud neexistuje.
     */
    extern st_DSK_TOOLS_TRACK_RULE_INFO* dsk_tools_get_rule_for_track ( const st_DSK_TOOLS_TRACKS_RULES_INFO *tracks_rules, uint8_t track );


    /* ─── Iterace přes stopy a sektory ─── */

    /**
     * @brief Callback pro iteraci přes stopy.
     *
     * @param h Handler.
     * @param abstrack Absolutní číslo stopy.
     * @param tinfo Informace o stopě.
     * @param user_data Uživatelská data z dsk_for_each_track.
     * @return 0 = pokračovat, nenulová = zastavit iteraci (vrátí se volajícímu).
     */
    typedef int (*dsk_track_callback_t)( st_HANDLER *h, uint8_t abstrack, const st_DSK_SHORT_TRACK_INFO *tinfo, void *user_data );

    /**
     * @brief Callback pro iteraci přes sektory na stopě.
     *
     * @param h Handler.
     * @param abstrack Absolutní číslo stopy.
     * @param sector_idx Index sektoru v rámci stopy (0-based).
     * @param sector_id ID sektoru (z hlavičky).
     * @param sector_offset Absolutní offset dat sektoru v souboru.
     * @param sector_size Velikost sektoru v bajtech.
     * @param user_data Uživatelská data z dsk_for_each_sector.
     * @return 0 = pokračovat, nenulová = zastavit iteraci.
     */
    typedef int (*dsk_sector_callback_t)( st_HANDLER *h, uint8_t abstrack, uint8_t sector_idx, uint8_t sector_id, uint32_t sector_offset, uint16_t sector_size, void *user_data );

    /**
     * @brief Iteruje přes všechny stopy v DSK obrazu a volá callback pro každou.
     * @param h Handler.
     * @param cb Callback volaný pro každou stopu.
     * @param user_data Uživatelská data předávaná callbacku.
     * @return EXIT_SUCCESS, EXIT_FAILURE (chyba I/O), nebo nenulová návratová hodnota z callbacku.
     */
    extern int dsk_for_each_track ( st_HANDLER *h, dsk_track_callback_t cb, void *user_data );

    /**
     * @brief Iteruje přes všechny sektory na zadané stopě a volá callback pro každý.
     * @param h Handler.
     * @param abstrack Absolutní číslo stopy.
     * @param cb Callback volaný pro každý sektor.
     * @param user_data Uživatelská data předávaná callbacku.
     * @return EXIT_SUCCESS, EXIT_FAILURE, nebo nenulová návratová hodnota z callbacku.
     */
    extern int dsk_for_each_sector ( st_HANDLER *h, uint8_t abstrack, dsk_sector_callback_t cb, void *user_data );

#ifdef __cplusplus
}
#endif

#endif /* DSK_TOOLS_H */
