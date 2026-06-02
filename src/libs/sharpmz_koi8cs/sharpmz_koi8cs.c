/**
 * @file sharpmz_koi8cs.c
 * @brief Implementace KOI8-CS a display kód konverze.
 *
 * Viz sharpmz_koi8cs.h pro popis API a sémantiky.
 *
 * @author Michal Hucik <hucik@ordoz.com>
 *
 * ----------------------------- License -------------------------------------
 *
 * GNU General Public License v3 (GPLv3)
 *
 * Copyright (C) 2017-2026 Michal Hucik <hucik@ordoz.com>
 *
 * ---------------------------------------------------------------------------
 */

#include <stdint.h>
#include <string.h>
#include "sharpmz_koi8cs.h"

/* Tabulka koi8cs_to_unicode[256], generovaná tools/gen_koi8cs.pl. */
#include "koi8cs_table.h"

const char *mzkoi8cs_version(void)
{
    return SHARPMZ_KOI8CS_VERSION;
}

/* ======================================================================== */
/* KOI8-CS - transliterační tabulka                                         */
/* ======================================================================== */

/**
 * @brief Inverzní tabulka pro ASCII transliteraci KOI8-CS znaků.
 *
 * Index = KOI8-CS byte (0x80-0xFF). Hodnota = nejbližší ASCII písmeno
 * bez diakritiky. 0 = nedefinováno / ne písmeno (pak vrátíme ' ').
 */
static const uint8_t koi8cs_to_ascii_translit[256] = {
    [0xC1] = 'a', [0xC3] = 'c', [0xC4] = 'd', [0xC5] = 'e',
    [0xC6] = 'r', [0xC7] = 'c', /* ch -> 'c' (první písmeno digrafa) */
    [0xC8] = 'u', [0xC9] = 'i', [0xCA] = 'u', [0xCB] = 'l',
    [0xCC] = 'l', [0xCD] = 'o', [0xCE] = 'n', [0xCF] = 'o',
    [0xD0] = 'o', [0xD1] = 'a', [0xD2] = 'r', [0xD3] = 's',
    [0xD4] = 't', [0xD5] = 'u', [0xD7] = 'e', [0xD8] = 'a',
    [0xD9] = 'y', [0xDA] = 'z',
    [0xE1] = 'A', [0xE3] = 'C', [0xE4] = 'D', [0xE5] = 'E',
    [0xE6] = 'R', [0xE7] = 'C', /* CH -> 'C' */
    [0xE8] = 'U', [0xE9] = 'I', [0xEA] = 'U', [0xEB] = 'L',
    [0xEC] = 'L', [0xED] = 'O', [0xEE] = 'N', [0xEF] = 'O',
    [0xF0] = 'O', [0xF1] = 'A', [0xF2] = 'R', [0xF3] = 'S',
    [0xF4] = 'T', [0xF5] = 'U', [0xF7] = 'E', [0xF8] = 'A',
    [0xF9] = 'Y', [0xFA] = 'Z',
};

/**
 * @brief Zakóduje Unicode codepoint do UTF-8 sekvence (statický buffer).
 *
 * Podporuje rozsah U+0000 - U+FFFF (1-3 bajty UTF-8).
 *
 * @param[in] cp Unicode codepoint (0x0000-0xFFFF).
 * @return Ukazatel na statický null-terminated UTF-8 řetězec.
 *
 * @note Funkce není reentrantní (statický buffer).
 */
static const char *encode_utf8(uint16_t cp)
{
    static char buf[4];
    if (cp == 0) { buf[0] = 0; return buf; }
    if (cp < 0x80) {
        buf[0] = (char)cp;
        buf[1] = 0;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        buf[2] = 0;
    } else {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        buf[3] = 0;
    }
    return buf;
}

uint8_t mzkoi8cs_koi8cs_to_ascii(uint8_t c, int *converted, int *printable)
{
    if (c < 0x20) {
        *printable = 0;
        *converted = 1;
        return ' ';
    }
    if (c < 0x7F) {
        *printable = 1;
        *converted = 1;
        return c;
    }
    if (c == 0x7F) {
        *printable = 0;
        *converted = 1;
        return ' ';
    }
    uint8_t a = koi8cs_to_ascii_translit[c];
    if (a != 0) {
        *printable = 1;
        *converted = 1;
        return a;
    }
    *printable = 0;
    *converted = 0;
    return ' ';
}

const char *mzkoi8cs_koi8cs_to_utf8(uint8_t c, int *converted, int *printable)
{
    static char ascii_buf[2] = {0, 0};

    if (c < 0x20) {
        *printable = 0;
        *converted = 1;
        ascii_buf[0] = (char)c;
        return ascii_buf;
    }
    if (c < 0x7F) {
        *printable = 1;
        *converted = 1;
        ascii_buf[0] = (char)c;
        return ascii_buf;
    }
    if (c == 0x7F) {
        *printable = 0;
        *converted = 1;
        return " ";
    }

    uint16_t cp = koi8cs_to_unicode[c];

    if (cp == 0xFFFE) {
        /* Digraph - 2-znaková UTF-8 sekvence */
        *printable = 1;
        *converted = 1;
        if (c == 0xC7) return "ch";
        if (c == 0xE7) return "CH";
        return " ";
    }

    if (cp == 0x0000) {
        *printable = 0;
        *converted = 0;
        return " ";
    }

    *printable = 1;
    *converted = 1;
    return encode_utf8(cp);
}

uint8_t mzkoi8cs_utf8_to_koi8cs(const char *c)
{
    if (!c) return 0x20;
    const unsigned char *s = (const unsigned char *)c;

    /* ASCII (1 byte) - KOI8-CS sdílí 0x00-0x7F.
     * Speciální handling digrafů: "ch" -> 0xC7, "CH" -> 0xE7. */
    if (s[0] < 0x80) {
        if (s[0] == 'c' && s[1] == 'h') return 0xC7;
        if (s[0] == 'C' && s[1] == 'H') return 0xE7;
        return s[0];
    }

    /* Vícebajtový UTF-8 - dekódujeme codepoint a hledáme v tabulce */
    uint16_t cp = 0;
    if (s[0] < 0xC0) return 0x20;
    else if (s[0] < 0xE0) {
        cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    } else if (s[0] < 0xF0) {
        cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    } else {
        return 0x20;
    }

    for (int i = 0x80; i < 0x100; i++) {
        if (koi8cs_to_unicode[i] == cp && cp != 0 && cp != 0xFFFE) {
            return (uint8_t)i;
        }
    }
    return 0x20;
}

/* ======================================================================== */
/* Display kódy (CG-ROM index) - tabulka !ADCN                              */
/* ======================================================================== */

/**
 * @brief Převodní tabulka Sharp MZ ASCII -> display kód (EU varianta).
 *
 * Adapted from mzdisk mz_vcode.c (Michal Hucik, GPLv3). Zdrojová data:
 * ROM MZ-800, dolní monitor, tabulka !ADCN na adrese 0x0A92.
 */
static const uint8_t adcn_eu[256] = {
    /* 0x00 */ 0xF0, 0xF0, 0xF0, 0xF3, 0xF0, 0xF5, 0xF0, 0xF0,
    /* 0x08 */ 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
    /* 0x10 */ 0xF0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xF0,
    /* 0x18 */ 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
    /* 0x20 */ 0x00, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    /* 0x28 */ 0x68, 0x69, 0x6B, 0x6A, 0x2F, 0x2A, 0x2E, 0x2D,
    /* 0x30 */ 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    /* 0x38 */ 0x28, 0x29, 0x4F, 0x2C, 0x51, 0x2B, 0x57, 0x49,
    /* 0x40 */ 0x55, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    /* 0x48 */ 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    /* 0x50 */ 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    /* 0x58 */ 0x18, 0x19, 0x1A, 0x52, 0x59, 0x54, 0x50, 0x45,
    /* 0x60 */ 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE,
    /* 0x68 */ 0xCF, 0xDF, 0xE7, 0xE8, 0xE5, 0xE9, 0xEC, 0xED,
    /* 0x70 */ 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    /* 0x78 */ 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xC0,
    /* 0x80 */ 0x80, 0xBD, 0x9D, 0xB1, 0xB5, 0xB9, 0xB4, 0x9E,
    /* 0x88 */ 0xB2, 0xB6, 0xBA, 0xBE, 0x9F, 0xB3, 0xB7, 0xBB,
    /* 0x90 */ 0xBF, 0xA3, 0x85, 0xA4, 0xA5, 0xA6, 0x94, 0x87,
    /* 0x98 */ 0x88, 0x9C, 0x82, 0x98, 0x84, 0x92, 0x90, 0x83,
    /* 0xA0 */ 0x91, 0x81, 0x9A, 0x97, 0x93, 0x95, 0x89, 0xA1,
    /* 0xA8 */ 0xAF, 0x8B, 0x86, 0x96, 0xA2, 0xAB, 0xAA, 0x8A,
    /* 0xB0 */ 0x8E, 0xB0, 0xAD, 0x8D, 0xA7, 0xA8, 0xA9, 0x8F,
    /* 0xB8 */ 0x8C, 0xAE, 0xAC, 0x9B, 0xA0, 0x99, 0xBC, 0xB8,
    /* 0xC0 */ 0x40, 0x3B, 0x3A, 0x70, 0x3C, 0x71, 0x5A, 0x3D,
    /* 0xC8 */ 0x43, 0x56, 0x3F, 0x1E, 0x4A, 0x1C, 0x5D, 0x3E,
    /* 0xD0 */ 0x5C, 0x1F, 0x5F, 0x5E, 0x37, 0x7B, 0x7F, 0x36,
    /* 0xD8 */ 0x7A, 0x7E, 0x33, 0x4B, 0x4C, 0x1D, 0x6C, 0x5B,
    /* 0xE0 */ 0x78, 0x41, 0x35, 0x34, 0x74, 0x30, 0x38, 0x75,
    /* 0xE8 */ 0x39, 0x4D, 0x6F, 0x6E, 0x32, 0x77, 0x76, 0x72,
    /* 0xF0 */ 0x73, 0x47, 0x7C, 0x53, 0x31, 0x4E, 0x6D, 0x48,
    /* 0xF8 */ 0x46, 0x7D, 0x44, 0x1B, 0x58, 0x79, 0x42, 0x60
};

uint8_t mzkoi8cs_ascii_to_display(uint8_t ascii, mzkoi8cs_charset_t charset)
{
    if (charset == MZKOI8CS_CHARSET_DISPLAY_JP) {
        if (ascii == 0x6C) return 0xE9;
        if (ascii == 0x6D) return 0xEA;
    }
    return adcn_eu[ascii];
}

uint8_t mzkoi8cs_display_to_ascii(uint8_t display_code, mzkoi8cs_charset_t charset)
{
    for (int i = 0; i < 256; i++) {
        if (mzkoi8cs_ascii_to_display((uint8_t)i, charset) == display_code) {
            return (uint8_t)i;
        }
    }
    return ' ';
}

/* ======================================================================== */
/* Dispatch wrapper                                                          */
/* ======================================================================== */

const char *mzkoi8cs_to_utf8(uint8_t code, mzkoi8cs_charset_t charset)
{
    int conv, prn;
    static char buf[2] = {0, 0};

    switch (charset) {
        case MZKOI8CS_CHARSET_KOI8CS:
            return mzkoi8cs_koi8cs_to_utf8(code, &conv, &prn);

        case MZKOI8CS_CHARSET_DISPLAY_EU:
        case MZKOI8CS_CHARSET_DISPLAY_JP: {
            /* display -> Sharp MZ ASCII -> printable ASCII UTF-8.
             * Pro znaky bez ASCII ekvivalentu (grafické symboly v CG-ROM)
             * vrací mezeru. */
            uint8_t ascii = mzkoi8cs_display_to_ascii(code, charset);
            if (ascii >= 0x20 && ascii < 0x7F) {
                buf[0] = (char)ascii;
                return buf;
            }
            return " ";
        }
    }
    return " ";
}
