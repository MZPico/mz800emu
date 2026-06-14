/*
 * File:   printer.h
 * Author: experiment centronics-printer (emu-experiments)
 *
 * Virtuální Centronics tiskárna napojená na Zilog Z80 PIO.
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

#ifndef PRINTER_H
#define PRINTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/** Maximální délka názvu capture souboru (vč. terminátoru). */
#define PRINTER_FILENAME_MAX 280

/**
 * @brief Stav virtuální Centronics tiskárny.
 *
 * Modul simuluje připojenou tiskárnu na Z80 PIO (porty 0FCh-0FFh): když je
 * @ref active, hlásí na vstupu PA0 (BUSY) připravenost (= 0) a zachytává bajty
 * zapsané na datovou bránu PB v okamžiku STROBE pulzu na PA7. Zachycené bajty
 * se ukládají syrově (1:1) do per-session souboru s prefixem "printer".
 *
 * Invarianty:
 *  - @ref fp != NULL  <=>  soubor je otevřen a @ref filename obsahuje jeho název
 *  - @ref byte_count je počet bajtů zapsaných do *aktuálního* souboru
 *  - @ref active == false  =>  handshake i capture jsou neaktivní (chování
 *    emulátoru je shodné s nepřipojenou tiskárnou)
 *
 * Stav je runtime-only a *neserializuje* se do snapshotu (file handle je
 * proces-specifický, capture je efemérní stream).
 */
typedef struct st_PRINTER {
    bool active;                          /**< Virtuální tiskárna zapnutá. */
    FILE *fp;                             /**< Capture soubor, NULL = neotevřen. */
    char filename[PRINTER_FILENAME_MAX]; /**< Název aktuálního souboru, "" = žádný. */
    uint32_t byte_count;                  /**< Bajtů v aktuálním souboru. */
    uint32_t total_byte_count;            /**< Bajtů za celou session. */
    uint32_t file_seq;                    /**< Pořadí souboru v session (pro unikátnost). */
    uint8_t last_strobe;                  /**< Poslední úroveň PA7 (STROBE) pro detekci hrany. */
} st_PRINTER;

/** Globální stav modulu (mirror pro UI). */
extern st_PRINTER g_printer;

/**
 * @brief Jednorázová inicializace modulu.
 *
 * Vynuluje stav, zaregistruje INI sekci [PRINTER] (klíče capture:bool default 0
 * a standard:keyword CENTRONICS|MZ default CENTRONICS) v g_cfgmain a aplikuje
 * případný CLI flag --printer. Klíč standard deleguje na pioz80_set_printer_std
 * (polarita řídicích signálů = zadní DIP SW2/SW3). Volá se z init sekvence
 * architektur, které mají Z80 PIO (mz800, mz1500), hned po @ref pioz80_init.
 * Idempotentní (opakované volání je no-op).
 *
 * @post g_printer je v klidovém stavu; soubor není otevřen.
 */
void printer_init ( void );

/**
 * @brief Reakce na reset stroje.
 *
 * Resynchronizuje baseline úrovně STROBE (PA7) podle aktuálního stavu PIO
 * (po resetu je výstupní registr 0 => PA7 = 0), aby se po resetu nevygenerovala
 * falešná hrana. Stávající capture soubor uzavře (reset stroje = konec tiskové
 * úlohy; další zachycený bajt založí nový soubor).
 */
void printer_reset ( void );

/**
 * @brief Ukončení modulu - zavře případně otevřený capture soubor.
 */
void printer_shutdown ( void );

/**
 * @brief Zapne/vypne virtuální tiskárnu.
 *
 * Při zapnutí se na PA0 (BUSY) začne hlásit ready a STROBE pulzy se zachytávají.
 * Při vypnutí se chování vrací k nepřipojené tiskárně; otevřený soubor zůstává
 * (lze ho zavřít přes @ref printer_close_file).
 *
 * @param active true = zapnout, false = vypnout
 */
void printer_set_active ( bool active );

/**
 * @brief Vrátí, zda je virtuální tiskárna aktivní.
 * @return true pokud aktivní
 */
bool printer_get_active ( void );

/**
 * @brief Uzavře aktuální capture soubor.
 *
 * Po uzavření se při dalším zachyceném bajtu založí nový jedinečný soubor.
 * Pokud žádný soubor otevřen není, je volání no-op.
 */
void printer_close_file ( void );

/**
 * @brief Capture hook volaný z PIO při zápisu na datovou bránu PA.
 *
 * Detekuje STROBE pulz: na náběžné hraně PA7 (0 -> 1) zachytí aktuální bajt
 * na datové bráně PB a zapíše ho do capture souboru (lazy-create při prvním
 * bajtu). Pokud tiskárna není aktivní, je volání no-op (jen se aktualizuje
 * baseline úrovně, aby po zapnutí nevznikla falešná hrana).
 *
 * Pozn.: STROBE je modelován jako aktivní HIGH (assert = PA7 jde na 1), což
 * odpovídá pozorovanému ovladači MRS. Signál BUSY (PA0) zrcadlí úroveň STROBE
 * (řešeno v pioz80_port_get_raw_input).
 *
 * @param pa7_level nová úroveň PA7 (0 nebo 1)
 * @param pb_data   aktuální bajt na datové bráně PB (output latch)
 */
void printer_pa_strobe_update ( uint8_t pa7_level, uint8_t pb_data );

#ifdef __cplusplus
}
#endif

#endif /* PRINTER_H */
