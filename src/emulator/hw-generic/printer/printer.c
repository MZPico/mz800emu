/*
 * File:   centronics.c
 * Author: experiment centronics-printer (emu-experiments)
 *
 * Virtuální Centronics tiskárna napojená na Zilog Z80 PIO.
 *
 * Princip:
 *  - Z80 PIO (porty 0FCh-0FFh) je v MZ-800/MZ-1500 vyhrazen primárně pro
 *    tiskárnu. Datová brána PB (0FFh) nese 8 datových bitů, řídicí brána PA
 *    (0FEh) signály BUSY (PA0, vstup) a STROBE/RDP (PA7, výstup).
 *  - Tento modul při zapnuté tiskárně:
 *      (1) hlásí na PA0 (BUSY) připravenost (= 0), aby handshake (polling
 *          @PBYTE i přímý ASM) prošel - viz hook v pioz80_port_get_raw_input,
 *      (2) na sestupné hraně PA7 (STROBE) zachytí aktuální bajt na PB a zapíše
 *          ho syrově do per-session souboru s prefixem "centronics".
 *
 * Omezení (V1):
 *  - Simuluje se polling handshake (statická připravenost). INT-driven
 *    bufferovaný tisk (ROM @INTP) vyžaduje toggle BUSY s generováním hrany,
 *    což V1 nedělá. [neověřeno] dopad na BASIC bufferovaný tisk.
 *  - STROBE se snímá na sestupné hraně PA7 (1 -> 0). Data na PB jsou stabilní
 *    po celou dobu pulzu, takže sběr na jedné hraně dá 1 bajt/pulz bez ohledu
 *    na polaritu RDP (INIT "LPT:" parametr Q bit 2).
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
 * ---------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "centronics/centronics.h"

#include "libs/sdlapp/sdlapp_options.h"
#include "libs/cfgfile/cfgmodule.h"
#include "cfgmain.h"

//#define DBGLEVEL (DBGNON | DBGERR | DBGWAR | DBGINF )
#include "debug.h"

st_CENTRONICS g_centronics;

/** Idempotence flag pro @ref centronics_init. */
static bool s_initialized = false;


/**
 * @brief Otevře nový jedinečný capture soubor.
 *
 * Název: "centronics-YYYYMMDD-HHMMSS.bin" v aktuálním pracovním adresáři.
 * Při kolizi (více souborů ve stejné sekundě) se připojí pořadové číslo.
 * Po úspěchu je @ref g_centronics.fp != NULL a @ref filename vyplněn.
 *
 * @return true při úspěchu, jinak false (soubor zůstane neotevřen)
 */
static bool centronics_open_new_file ( void ) {

    time_t now = time ( NULL );
    struct tm tmv;
#if defined(_WIN32)
    localtime_s ( &tmv, &now );
#else
    localtime_r ( &now, &tmv );
#endif

    char stamp[32];
    strftime ( stamp, sizeof ( stamp ), "%Y%m%d-%H%M%S", &tmv );

    char candidate[CENTRONICS_FILENAME_MAX];
    /* První pokus bez pořadového čísla, další s "-N" pro unikátnost. */
    unsigned seq = 0;
    FILE *fp = NULL;
    for ( ;; ) {
        if ( seq == 0 ) {
            snprintf ( candidate, sizeof ( candidate ), "centronics-%s.bin", stamp );
        } else {
            snprintf ( candidate, sizeof ( candidate ), "centronics-%s-%u.bin", stamp, seq );
        };
        /* Existuje už? (zabráníme přepsání souboru ze stejné sekundy.) */
        FILE *test = fopen ( candidate, "rb" );
        if ( test ) {
            fclose ( test );
            seq++;
            if ( seq > 9999 ) break; /* pojistka proti nekonečné smyčce */
            continue;
        };
        fp = fopen ( candidate, "wb" );
        break;
    };

    if ( !fp ) {
        fprintf ( stderr, "centronics: cannot create capture file \"%s\"\n", candidate );
        return false;
    };

    g_centronics.fp = fp;
    snprintf ( g_centronics.filename, sizeof ( g_centronics.filename ), "%s", candidate );
    g_centronics.byte_count = 0;
    g_centronics.file_seq++;
    DBGPRINTF ( DBGINF, "centronics: new capture file \"%s\"\n", g_centronics.filename );
    return true;
}


/**
 * @brief Zapíše jeden zachycený bajt do capture souboru (lazy-create).
 *
 * @param data bajt z datové brány PB
 */
static void centronics_capture_byte ( uint8_t data ) {
    if ( !g_centronics.fp ) {
        if ( !centronics_open_new_file ( ) ) return;
    };
    fputc ( data, g_centronics.fp );
    fflush ( g_centronics.fp );
    g_centronics.byte_count++;
    g_centronics.total_byte_count++;
}


/**
 * @brief Propagate cb pro [CENTRONICS] active - aplikuje hodnotu z INI.
 */
static void centronics_cfg_propagate_active ( void *e, void *data ) {
    ( void ) data;
    st_CFGELEMENT *elm = ( st_CFGELEMENT * ) e;
    int v = cfgelement_get_bool_value ( elm );
    centronics_set_active ( v ? true : false );
}


/**
 * @brief Save cb pro [CENTRONICS] active - zapíše aktuální stav do INI.
 */
static void centronics_cfg_save_active ( void *e, void *data ) {
    ( void ) data;
    st_CFGELEMENT *elm = ( st_CFGELEMENT * ) e;
    cfgelement_set_bool_value ( elm, g_centronics.active ? 1 : 0 );
}


void centronics_init ( void ) {
    if ( s_initialized ) return;
    s_initialized = true;

    memset ( &g_centronics, 0x00, sizeof ( g_centronics ) );

    /* cfgmain [CENTRONICS] sekce - klíč:
     *   - active (bool, default 0) virtuální tiskárna ON/OFF při startu */
    CFGMOD *cmod = cfgroot_register_new_module ( g_cfgmain, "CENTRONICS" );
    if ( cmod ) {
        CFGELM *elm = cfgmodule_register_new_element ( cmod, "active",
                                                       CFGENTYPE_BOOL, 0 );
        cfgelement_set_propagate_cb ( elm, centronics_cfg_propagate_active, NULL );
        cfgelement_set_save_cb      ( elm, centronics_cfg_save_active,      NULL );
        cfgmodule_parse ( cmod );
        cfgmodule_propagate ( cmod );
    };

    /* CLI --centronics je FLAG (bez hodnoty). Pokud přítomen, vynutí aktivaci
     * nad INI hodnotou. Vzor: profiler_apply_cli_options. */
    if ( sdlapp_option_present ( "--centronics" ) ) {
        centronics_set_active ( true );
    };
}


void centronics_reset ( void ) {
    /* Po resetu stroje je výstupní registr PIO 0 => PA7 = 0. Resync baseline,
     * aby první idle-write (typicky PA7 = 1) nevypadal jako sestupná hrana.
     * Capture soubor zůstává - reset stroje není nová session. */
    g_centronics.last_strobe = 0;
}


void centronics_shutdown ( void ) {
    centronics_close_file ( );
    s_initialized = false;
}


void centronics_set_active ( bool active ) {
    g_centronics.active = active;
    DBGPRINTF ( DBGINF, "centronics: active = %d\n", ( int ) active );
}


bool centronics_get_active ( void ) {
    return g_centronics.active;
}


void centronics_close_file ( void ) {
    if ( g_centronics.fp ) {
        fclose ( g_centronics.fp );
        g_centronics.fp = NULL;
        DBGPRINTF ( DBGINF, "centronics: closed file \"%s\" (%u bytes)\n",
                    g_centronics.filename, g_centronics.byte_count );
    };
    g_centronics.filename[0] = 0x00;
    g_centronics.byte_count = 0;
}


void centronics_pa_strobe_update ( uint8_t pa7_level, uint8_t pb_data ) {
    uint8_t prev = g_centronics.last_strobe;
    uint8_t now = pa7_level ? 1 : 0;
    g_centronics.last_strobe = now;

    if ( !g_centronics.active ) return;

    /* Náběžná hrana PA7 (0 -> 1) = aktivace STROBE = host právě položil platná
     * data na PB a žádá jejich převzetí. Sbíráme bajt zde (= okamžik, kdy ho
     * tiskárna "přebírá"). Pozorováno v ovladači MRS: STROBE je aktivní HIGH
     * (assert = PA7 jde na 1, data na PB nastavena těsně předtím). */
    if ( ( prev == 0 ) && ( now == 1 ) ) {
        centronics_capture_byte ( pb_data );
    };
}
