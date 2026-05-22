/*
 * File:   sym_import_sym.c
 *
 * Importér sjasmplus .sym formátu.
 *
 * Format (per řádek):
 *   <NAME> EQU <value>
 *
 * Hodnota může být:
 *   - hex s '$' prefixem: $4042
 *   - hex s '0x' prefixem: 0x4042  (= akceptujeme pro robustnost)
 *   - decimální:   16450
 *
 * Příklady:
 *   print_char EQU $4042
 *   some_const EQU 0x0010
 *   counter    EQU 100
 *
 * Liché řádky (komentáře, prázdné, makra) se přeskočí.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later, viz licence header v breakpoints.h.
 *
 * ---------------------------------------------------------------------------
 */

#include "mzarch/mzarch_config.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "sym_db.h"


extern int sym_db_add_internal ( const char *name, uint32_t addr,
                                 uint8_t bank_id, en_SYM_SOURCE source,
                                 const char *comment, const char *module );


/**
 * @brief Pokusí se naparsovat hodnotu (hex/dec).
 *
 * @return 1 při úspěchu, 0 při chybě
 */
static int parse_value ( const char *s, uint32_t *out ) {
    if ( !s || !*s ) return 0;
    while ( *s && isspace ( (unsigned char) *s ) ) s++;
    if ( !*s ) return 0;

    unsigned int v = 0;
    int consumed = 0;

    if ( s [ 0 ] == '$' ) {
        if ( sscanf ( s + 1, "%X%n", &v, &consumed ) != 1 || consumed == 0 ) return 0;
        *out = (uint32_t) v;
        return 1;
    };
    if ( s [ 0 ] == '0' && ( s [ 1 ] == 'x' || s [ 1 ] == 'X' ) ) {
        if ( sscanf ( s + 2, "%X%n", &v, &consumed ) != 1 || consumed == 0 ) return 0;
        *out = (uint32_t) v;
        return 1;
    };
    /* decimal default */
    if ( sscanf ( s, "%u%n", &v, &consumed ) != 1 || consumed == 0 ) return 0;
    *out = (uint32_t) v;
    return 1;
}


/**
 * @brief Načte sjasmplus .sym soubor.
 *
 * @return počet načtených symbolů, -1 při I/O chybě.
 */
int sym_db_load_sym ( const char *path ) {
    if ( !path ) return -1;
    FILE *fp = fopen ( path, "r" );
    if ( !fp ) return -1;

    char buf [ 1024 ];
    int loaded = 0;

    while ( fgets ( buf, sizeof ( buf ), fp ) ) {
        size_t len = strlen ( buf );
        while ( len > 0 && ( buf [ len - 1 ] == '\n' || buf [ len - 1 ] == '\r' ) ) {
            buf [ --len ] = '\0';
        };

        const char *p = buf;
        while ( *p && isspace ( (unsigned char) *p ) ) p++;
        if ( !*p || *p == ';' || *p == '#' ) continue;

        /* identifier */
        char name [ 256 ];
        size_t ni = 0;
        while ( *p && !isspace ( (unsigned char) *p ) && ni < sizeof ( name ) - 1 ) {
            name [ ni++ ] = *p++;
        };
        name [ ni ] = '\0';
        if ( ni == 0 ) continue;

        /* whitespace */
        while ( *p && isspace ( (unsigned char) *p ) ) p++;

        /* "EQU" keyword (case-insensitive) */
        if ( !( ( p [ 0 ] == 'E' || p [ 0 ] == 'e' ) &&
                ( p [ 1 ] == 'Q' || p [ 1 ] == 'q' ) &&
                ( p [ 2 ] == 'U' || p [ 2 ] == 'u' ) &&
                ( p [ 3 ] == '\0' || isspace ( (unsigned char) p [ 3 ] ) ) ) ) {
            continue;
        };
        p += 3;
        while ( *p && isspace ( (unsigned char) *p ) ) p++;

        uint32_t addr = 0;
        if ( !parse_value ( p, &addr ) ) continue;

        if ( sym_db_add_internal ( name, addr, 0,
                                   SYM_SOURCE_SJASMPLUS, NULL, NULL ) ) {
            loaded++;
        };
    };

    fclose ( fp );
    return loaded;
}

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
