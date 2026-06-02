/**
 * @file sharpmz_koi8cs.h
 * @brief KOI8-CS a display kód konverze pro Memory Browser ASCII column.
 *
 * Samostatná knihovna, paralelní k existujícímu modulu sharpmz_ascii/
 * sharpmz_utf8. Cíleně NEPOUŽÍVÁ symboly z sharpmz_ascii.h aby nevznikla
 * kolize - jiná sémantika starého API (sharpmz_utf8_table_eu mapuje
 * CG-ROM display kódy přímo na znaky, zatímco upstream sharpmz_ascii
 * 2.2.0 interpretuje vstup jako Sharp MZ ASCII).
 *
 * API poskytuje:
 *
 * - KOI8-CS - 8-bit kódování s českou/slovenskou diakritikou používané
 *   v Sharp MZ-800 CP/M ekosystému (LEC CP/M 1.4/4.1, NIPOS, LuckySoft).
 *   Bytes 0x00-0x7F shodné s ASCII, bytes 0x80-0xFF obsahují diakritiku.
 *   Zdroj tabulky: Cstools-3.44 (Jan Pazdziora, Perl/Artistic dual).
 *
 * - DISPLAY_EU/JP - display kódy (CG-ROM indexy zapisované do VRAM 0xD000).
 *   Konverze přes inverzi tabulky !ADCN z ROM monitoru MZ-800/MZ-1500.
 *   Adapted from mzdisk mz_vcode.c.
 *
 * @author Michal Hucik <hucik@ordoz.com>
 *
 * ----------------------------- License -------------------------------------
 *
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2017-2026 Michal Hucik <hucik@ordoz.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef SHARPMZ_KOI8CS_H
#define SHARPMZ_KOI8CS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/** @brief Verze knihovny sharpmz_koi8cs. */
#define SHARPMZ_KOI8CS_VERSION "1.0.0"

/**
 * @brief Volba kódování pro Memory Browser ASCII column.
 *
 * @invariant Číselné hodnoty zachovat - mohou se persistovat v configu.
 */
typedef enum {
    MZKOI8CS_CHARSET_KOI8CS = 0,     /**< KOI8-CS (čeština/slovenština pod CP/M) */
    MZKOI8CS_CHARSET_DISPLAY_EU = 1, /**< Display kód (CG-ROM index), EU varianta */
    MZKOI8CS_CHARSET_DISPLAY_JP = 2  /**< Display kód (CG-ROM index), JP varianta */
} mzkoi8cs_charset_t;

/**
 * @brief Vrátí řetězec s verzí knihovny.
 * @return Statický řetězec s verzí (např. "1.0.0").
 */
extern const char *mzkoi8cs_version(void);

/* ======================================================================== */
/* KOI8-CS API                                                              */
/* ======================================================================== */

/**
 * @brief Konvertuje jeden KOI8-CS byte na UTF-8 řetězec.
 *
 * Vrací ukazatel na statický UTF-8 řetězec odpovídající KOI8-CS bytu.
 * Bytes 0x00-0x7F mapují 1:1 na ASCII. Bytes 0x80-0xFF s diakritikou
 * mapují na příslušný Unicode codepoint (U+00B0, U+010D, ...).
 * Digrafy "ch"/"CH" (0xC7/0xE7) se vrací jako dvouznaková UTF-8 sekvence
 * "ch"/"CH" (input 1 byte -> output 2 bytes).
 *
 * Nedefinované bytes (mimo ASCII a mimo specifikované KOI8-CS znaky):
 * converted=0, printable=0, vrací mezeru " ".
 *
 * @param[in]  c         KOI8-CS byte (0x00-0xFF).
 * @param[out] converted 1 pokud konverze úspěšná, 0 pokud ne.
 * @param[out] printable 1 pokud znak reprezentuje tisknutelný glyf.
 * @return Ukazatel na statický UTF-8 řetězec (null-terminated). Nikdy
 *         nevrací NULL. Platný do dalšího volání této funkce.
 *
 * @pre converted nesmí být NULL.
 * @pre printable nesmí být NULL.
 *
 * @note Funkce není reentrantní (statický buffer).
 */
extern const char *mzkoi8cs_koi8cs_to_utf8(uint8_t c, int *converted, int *printable);

/**
 * @brief Konvertuje jeden UTF-8 znak na KOI8-CS byte.
 *
 * Pokud je vstupní znak ASCII (1 byte, 0x00-0x7F), vrací ho beze změny.
 * Pokud je vícebajtový, hledá odpovídající KOI8-CS byte v inverzní tabulce.
 * Digrafy: vstup "ch" / "CH" vrací 0xC7 / 0xE7.
 *
 * @param[in] utf8 Ukazatel na null-terminated UTF-8 řetězec. Zpracuje
 *                  první znak (1-3 bytes pro KOI8-CS rozsah U+0000-U+FFFF).
 * @return KOI8-CS byte (0x00-0xFF) nebo 0x20 pokud znak není v KOI8-CS.
 *
 * @pre utf8 nesmí být NULL a musí ukazovat na validní UTF-8 sekvenci.
 */
extern uint8_t mzkoi8cs_utf8_to_koi8cs(const char *utf8);

/**
 * @brief Konvertuje KOI8-CS byte na ASCII transliteraci (s odstraněním diakritiky).
 *
 * Bytes 0x00-0x7F passthrough. Bytes 0x80-0xFF s diakritikou se přepíší
 * na nejbližší ASCII písmeno (á->'a', č->'c', ...). Digrafy ch/CH ->
 * 'c'/'C' (první písmeno). Nedefinované bytes -> ' '.
 *
 * @param[in]  c         KOI8-CS byte (0x00-0xFF).
 * @param[out] converted 1 pokud konverze úspěšná, 0 pokud ne.
 * @param[out] printable 1 pokud znak reprezentuje tisknutelný glyf.
 * @return ASCII znak nebo ' ' při neúspěchu.
 *
 * @pre converted nesmí být NULL.
 * @pre printable nesmí být NULL.
 */
extern uint8_t mzkoi8cs_koi8cs_to_ascii(uint8_t c, int *converted, int *printable);

/* ======================================================================== */
/* Display kódy (CG-ROM index) API                                          */
/* ======================================================================== */

/**
 * @brief Konvertuje Sharp MZ ASCII na display kód (CG-ROM index).
 *
 * Přímé použití tabulky !ADCN. Pro JP variantu se aplikuje korekce
 * na pozicích 0x6C a 0x6D (MZ-1500 CG-ROM rozdíl).
 *
 * @param[in] ascii   Sharp MZ ASCII byte (0x00-0xFF).
 * @param[in] charset MZKOI8CS_CHARSET_DISPLAY_EU nebo _DISPLAY_JP.
 * @return Display kód (0x00-0xFF). Hodnota 0xF0 = nezobrazitelný znak.
 *
 * @pre charset musí být MZKOI8CS_CHARSET_DISPLAY_EU nebo _DISPLAY_JP.
 */
extern uint8_t mzkoi8cs_ascii_to_display(uint8_t ascii, mzkoi8cs_charset_t charset);

/**
 * @brief Konvertuje display kód (CG-ROM index) na Sharp MZ ASCII.
 *
 * Inverze tabulky !ADCN přes lineární scan. Pro display kódy bez
 * Sharp MZ ASCII ekvivalentu vrací ' '.
 *
 * @param[in] display_code Display kód (0x00-0xFF).
 * @param[in] charset      MZKOI8CS_CHARSET_DISPLAY_EU nebo _DISPLAY_JP.
 * @return Sharp MZ ASCII (0x00-0xFF) nebo ' '.
 *
 * @pre charset musí být MZKOI8CS_CHARSET_DISPLAY_EU nebo _DISPLAY_JP.
 */
extern uint8_t mzkoi8cs_display_to_ascii(uint8_t display_code, mzkoi8cs_charset_t charset);

/* ======================================================================== */
/* Charset-dispatchovaný wrapper                                            */
/* ======================================================================== */

/**
 * @brief Konvertuje jeden byte (libovolné podporované kódování) na UTF-8.
 *
 * Dispatch wrapper podle parametru charset:
 * - KOI8CS: přímo mzkoi8cs_koi8cs_to_utf8
 * - DISPLAY_EU/JP: nejprve display -> Sharp MZ ASCII -> UTF-8 (přes
 *   existující sharpmz_to_utf8 z libs/sharpmz_ascii pokud k dispozici;
 *   tato verze vrací ASCII-only UTF-8 bez speciálních znaků)
 *
 * @param[in] code     Vstupní byte (0x00-0xFF).
 * @param[in] charset  Volba kódování.
 * @return Ukazatel na statický UTF-8 řetězec. Nikdy nevrací NULL.
 *
 * @pre charset musí být platná hodnota mzkoi8cs_charset_t.
 * @note Funkce není reentrantní.
 */
extern const char *mzkoi8cs_to_utf8(uint8_t code, mzkoi8cs_charset_t charset);

#ifdef __cplusplus
}
#endif

#endif /* SHARPMZ_KOI8CS_H */
