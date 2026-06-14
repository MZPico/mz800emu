/*
 * File:   printer.c
 * Author: experiment centronics-printer (emu-experiments)
 *
 * Virtuální tiskárna (capture) napojená na Zilog Z80 PIO.
 *
 * Princip:
 *  - Z80 PIO (porty 0FCh-0FFh) je v MZ-800/MZ-1500 vyhrazen primárně pro
 *    tiskárnu. Datová brána PB (0FFh) nese 8 datových bitů, řídicí brána PA
 *    (0FEh) signály BUSY (PA0, vstup) a STROBE/RDP (PA7, výstup).
 *  - Tento modul při zapnutém capture:
 *      (1) hlásí na PA0 (BUSY) připravenost (= 0), aby handshake (polling
 *          @PBYTE i přímý ASM) prošel - viz hook v pioz80_port_get_raw_input,
 *      (2) na náběžné hraně efektivního STROBE zachytí aktuální bajt na PB
 *          a zapíše ho syrově do per-session souboru s prefixem "printer".
 *
 * Pozn. k polaritě: úroveň STROBE předávaná sem (pa7_level) je už EFEKTIVNÍ
 * signál na konektoru po aplikaci zadního DIP SW2/SW3 (standard MZ/Centronics)
 * v pioz80. Capture tedy snímá na náběžné hraně efektivního STROBE bez ohledu
 * na zvolený standard (data na PB jsou stabilní po celou dobu pulzu).
 *
 * Omezení (V1):
 *  - Simuluje se polling handshake (statická připravenost). INT-driven
 *    bufferovaný tisk (ROM @INTP) vyžaduje toggle BUSY s generováním hrany,
 *    což V1 nedělá. [neověřeno] dopad na BASIC bufferovaný tisk.
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

#include "printer/printer.h"
#include "pioz80/pioz80.h"

#include "libs/sdlapp/sdlapp_options.h"
#include "libs/cfgfile/cfgmodule.h"
#include "cfgmain.h"

//#define DBGLEVEL (DBGNON | DBGERR | DBGWAR | DBGINF )
#include "debug.h"

st_PRINTER g_printer;

/** Idempotence flag pro @ref printer_init. */
static bool s_initialized = false;


/**
 * @brief Otevře nový jedinečný capture soubor.
 *
 * Název: "printer-<std>-YYYYMMDD-HHMMSS.bin" (std = mz|centronics dle zadního
 * DIP SW2/SW3) v aktuálním pracovním adresáři.
 * Při kolizi (více souborů ve stejné sekundě) se připojí pořadové číslo.
 * Po úspěchu je @ref g_printer.fp != NULL a @ref filename vyplněn.
 *
 * @return true při úspěchu, jinak false (soubor zůstane neotevřen)
 */
static bool printer_open_new_file ( void ) {

    time_t now = time ( NULL );
    struct tm tmv;
#if defined(_WIN32)
    localtime_s ( &tmv, &now );
#else
    localtime_r ( &now, &tmv );
#endif

    char stamp[32];
    strftime ( stamp, sizeof ( stamp ), "%Y%m%d-%H%M%S", &tmv );

    /* Prefix podle zvoleného standardu tiskárny (zadní DIP SW2/SW3) - význam
     * zachycených řídicích signálů na něm závisí, takže ho dáme do názvu. */
    const char *std = ( pioz80_get_printer_std ( ) == PIOZ80_PRINTER_STD_MZ )
                      ? "mz" : "centronics";

    char candidate[PRINTER_FILENAME_MAX];
    /* První pokus bez pořadového čísla, další s "-N" pro unikátnost. */
    unsigned seq = 0;
    FILE *fp = NULL;
    for ( ;; ) {
        if ( seq == 0 ) {
            snprintf ( candidate, sizeof ( candidate ), "printer-%s-%s.bin", std, stamp );
        } else {
            snprintf ( candidate, sizeof ( candidate ), "printer-%s-%s-%u.bin", std, stamp, seq );
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
        fprintf ( stderr, "printer: cannot create capture file \"%s\"\n", candidate );
        return false;
    };

    g_printer.fp = fp;
    snprintf ( g_printer.filename, sizeof ( g_printer.filename ), "%s", candidate );
    g_printer.byte_count = 0;
    g_printer.file_seq++;
    DBGPRINTF ( DBGINF, "printer: new capture file \"%s\"\n", g_printer.filename );
    return true;
}


/**
 * @brief Zapíše jeden zachycený bajt do capture souboru (lazy-create).
 *
 * @param data bajt z datové brány PB
 */
static void printer_capture_byte ( uint8_t data ) {
    if ( !g_printer.fp ) {
        if ( !printer_open_new_file ( ) ) return;
    };
    fputc ( data, g_printer.fp );
    fflush ( g_printer.fp );
    g_printer.byte_count++;
    g_printer.total_byte_count++;
}


/**
 * @brief Propagate cb pro [PRINTER] capture - aplikuje hodnotu z INI.
 */
static void printer_cfg_propagate_capture ( void *e, void *data ) {
    ( void ) data;
    st_CFGELEMENT *elm = ( st_CFGELEMENT * ) e;
    int v = cfgelement_get_bool_value ( elm );
    printer_set_active ( v ? true : false );
}


/**
 * @brief Save cb pro [PRINTER] capture - zapíše aktuální stav do INI.
 */
static void printer_cfg_save_capture ( void *e, void *data ) {
    ( void ) data;
    st_CFGELEMENT *elm = ( st_CFGELEMENT * ) e;
    cfgelement_set_bool_value ( elm, g_printer.active ? 1 : 0 );
}


/**
 * @brief Propagate cb pro [PRINTER] standard (zadní DIP SW2/SW3) z INI.
 *
 * Standard tiskárny (polarita řídicích signálů) je vlastnictvím obvodu
 * Z80 PIO (zde jen INI vrstva); deleguje na pioz80_set_printer_std.
 */
static void printer_cfg_propagate_standard ( void *e, void *data ) {
    ( void ) data;
    st_CFGELEMENT *elm = ( st_CFGELEMENT * ) e;
    int v = cfgelement_get_keyword_value ( elm );
    pioz80_set_printer_std ( ( en_PIOZ80_PRINTER_STD ) v );
}


/**
 * @brief Save cb pro [PRINTER] standard - zapíše aktuální stav do INI.
 */
static void printer_cfg_save_standard ( void *e, void *data ) {
    ( void ) data;
    st_CFGELEMENT *elm = ( st_CFGELEMENT * ) e;
    cfgelement_set_keyword_value ( elm, ( int ) pioz80_get_printer_std ( ) );
}


void printer_init ( void ) {
    if ( s_initialized ) return;
    s_initialized = true;

    memset ( &g_printer, 0x00, sizeof ( g_printer ) );

    /* cfgmain [PRINTER] sekce - klíče:
     *   - capture (bool, default 0) odposlech bajtů tiskárny ON/OFF při startu
     *   - standard (keyword CENTRONICS|MZ, default CENTRONICS) standard
     *     rozhraní tiskárny = polarita řídicích signálů (zadní DIP SW2/SW3) */
    CFGMOD *cmod = cfgroot_register_new_module ( g_cfgmain, "PRINTER" );
    if ( cmod ) {
        CFGELM *elm = cfgmodule_register_new_element ( cmod, "capture",
                                                       CFGENTYPE_BOOL, 0 );
        cfgelement_set_propagate_cb ( elm, printer_cfg_propagate_capture, NULL );
        cfgelement_set_save_cb      ( elm, printer_cfg_save_capture,      NULL );

        CFGELM *elm_std = cfgmodule_register_new_element ( cmod, "standard",
                                                           CFGENTYPE_KEYWORD,
                                                           PIOZ80_PRINTER_STD_CENTRONICS,
                                                           PIOZ80_PRINTER_STD_CENTRONICS, "CENTRONICS",
                                                           PIOZ80_PRINTER_STD_MZ,         "MZ",
                                                           -1 );
        cfgelement_set_propagate_cb ( elm_std, printer_cfg_propagate_standard, NULL );
        cfgelement_set_save_cb      ( elm_std, printer_cfg_save_standard,      NULL );

        cfgmodule_parse ( cmod );
        cfgmodule_propagate ( cmod );
    };

    /* CLI --printer je FLAG (bez hodnoty). Pokud přítomen, vynutí zapnutí
     * capture nad INI hodnotou. Vzor: profiler_apply_cli_options. */
    if ( sdlapp_option_present ( "--printer" ) ) {
        printer_set_active ( true );
    };
}


void printer_reset ( void ) {
    /* Po resetu stroje je výstupní registr PIO 0 => PA7 = 0. Resync baseline,
     * aby první idle-write (typicky PA7 = 1) nevypadal jako sestupná hrana. */
    g_printer.last_strobe = 0;

    /* Reset stroje (nebo plotteru) = konec aktuální tiskové úlohy: uzavři
     * stávající capture soubor (má-li obsah). Další bajt založí nový. */
    printer_close_file ( );
}


void printer_shutdown ( void ) {
    printer_close_file ( );
    s_initialized = false;
}


void printer_set_active ( bool active ) {
    g_printer.active = active;
    DBGPRINTF ( DBGINF, "printer: capture active = %d\n", ( int ) active );
}


bool printer_get_active ( void ) {
    return g_printer.active;
}


void printer_close_file ( void ) {
    if ( g_printer.fp ) {
        fclose ( g_printer.fp );
        g_printer.fp = NULL;
        DBGPRINTF ( DBGINF, "printer: closed file \"%s\" (%u bytes)\n",
                    g_printer.filename, g_printer.byte_count );
    };
    g_printer.filename[0] = 0x00;
    g_printer.byte_count = 0;
}


void printer_pa_strobe_update ( uint8_t pa7_level, uint8_t pb_data ) {
    uint8_t prev = g_printer.last_strobe;
    uint8_t now = pa7_level ? 1 : 0;
    g_printer.last_strobe = now;

    if ( !g_printer.active ) return;

    /* Náběžná hrana PA7 (0 -> 1) = aktivace STROBE = host právě položil platná
     * data na PB a žádá jejich převzetí. Sbíráme bajt zde (= okamžik, kdy ho
     * tiskárna "přebírá"). Pozorováno v ovladači MRS: STROBE je aktivní HIGH
     * (assert = PA7 jde na 1, data na PB nastavena těsně předtím). */
    if ( ( prev == 0 ) && ( now == 1 ) ) {
        printer_capture_byte ( pb_data );
    };
}
