/**
 * @file   iasm.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Implementace Z80 inline assembleru — parsování, vyhledání opkódu, generování binárního kódu.
 *
 * Zpracování instrukce probíhá ve třech fázích:
 * 1. Parsování textu na tokeny (iasm_parse_line)
 * 2. Vyhledání odpovídajícího opkódu v tabulce (iasm_lookup_opcode)
 * 3. Generování binárního kódu z opkódového řetězce (iasm_generate_binary)
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

#include <stdint.h>
#include <string.h>

#include "iasm.h"
#include "iasm_opcodes.h"


/**
 * @brief Stavy stavového automatu parseru.
 *
 * Parser prochází vstupní text znak po znaku a rozlišuje 6 stavů.
 * Viz architektura.md pro detailní popis přechodů.
 */
typedef enum en_IASMSTATE {
    INLASM_CMD = 0,  /**< Příkaz (mnemonic), např. "LD". */
    INLASM_PAR1,     /**< Parametr před 1. proměnnou, např. "A,(IX+". */
    INLASM_VAR1,     /**< 1. proměnná (číselná hodnota), např. "0x42". */
    INLASM_PAR2,     /**< Parametr mezi/za proměnnými, např. ")". */
    INLASM_VAR2,     /**< 2. proměnná (jen u LD (IX+$),#). */
    INLASM_PAR3,     /**< Koncový parametr za 2. proměnnou. */
    INLASM_COUNT     /**< Počet stavů — slouží jako délka polí. */
} en_IASMSTATE;


/**
 * @brief Interní struktura pro předávání rozparsovaných dat mezi fázemi.
 */
typedef struct {
    char text[IASM_MNEMONICS_MAXLEN + 1]; /**< Normalizovaný vstupní text (uppercase, ořezané mezery). */
    char *pos[INLASM_COUNT];              /**< Ukazatele na začátky sekcí v text[]. */
    int length[INLASM_COUNT];             /**< Délky jednotlivých sekcí. */
    uint16_t var[IASM_MAX_VARIABLES];     /**< Extrahované číselné proměnné. */
    int variables_count;                  /**< Počet nalezených proměnných (0–2). */
} iasm_parsed_t;


/** @defgroup iasm_helpers Pomocné funkce
 *  @{ */


/**
 * @brief Odstraní úvodní mezery z řetězce (in-place).
 * @param text Řetězec k úpravě.
 */
static void iasm_trim_lspaces ( char *text ) {
    char *first_non_space = text;
    while (*first_non_space == ' ') {
        ++first_non_space;
    };
    if (first_non_space != text) {
        memmove(text, first_non_space, strlen(first_non_space) + 1);
    };
}


/**
 * @brief Odstraní koncové mezery z řetězce (in-place).
 * @param text Řetězec k úpravě.
 */
static void iasm_trim_rspaces ( char *text ) {
    unsigned i = strlen ( text );
    if ( i ) {
        while ( text [ i - 1 ] == ' ' ) {
            text [ i - 1 ] = 0x00;
            i--;
            if ( !i ) {
                return;
            };
        };
    };
}


/**
 * @brief Převede hexadecimální číslici (0-9, A-F) na hodnotu 0-15.
 *
 * Vstup MUSÍ být platná hex číslice (uppercase).
 *
 * @param c Hexadecimální číslice ('0'-'9', 'A'-'F').
 * @return Hodnota 0–15, nebo 0 jako fallback při neplatném vstupu.
 */
static uint8_t iasm_hex_digit_to_value ( char c ) {
    if ( ( c >= '0' ) && ( c <= '9' ) ) {
        return c - '0';
    };
    if ( ( c >= 'A' ) && ( c <= 'F' ) ) {
        return c - 'A' + 0x0a;
    };
    /* neplatný vstup — vrátíme 0 jako fallback */
    return 0;
}


/**
 * @brief Převede bajt na hex řetězec "0xNN" (uppercase).
 *
 * Buffer musí mít minimálně 5 bajtů (prefix "0x" + 2 hex + terminator).
 *
 * @param value Bajt k převodu.
 * @param buf   Výstupní buffer (min. 5 bajtů).
 */
static void iasm_byte_to_hex_str ( uint8_t value, char *buf ) {
    static const char hex_digits[] = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    buf[2] = hex_digits[(value >> 4) & 0x0F];
    buf[3] = hex_digits[value & 0x0F];
    buf[4] = 0x00;
}


/**
 * @brief Dekóduje hexadecimální řetězec na číselnou hodnotu.
 *
 * Podporované formáty číselných hodnot (proměnných):
 * - nnn — desítkové číslo
 * - n[a-f] — hex (auto-detekce)
 * - 0xnnnn — hex s prefixem
 * - #nnnn — hex s hash prefixem
 * - %nnnnnnnn — binární číslo
 *
 * @param c     Ukazatel na začátek hex řetězce (znaky se normalizují na uppercase).
 * @param value Výstupní dekódovaná hodnota.
 * @return Počet zpracovaných znaků, nebo 0 pokud nebyla nalezena platná hex číslice.
 */
static int iasm_decode_hex_txt ( char *c, uint16_t *value ) {
    int scan_pos = 0;
    int bit_shift = 0;
    int length;

    *value = 0;

    while ( ( c [ scan_pos ] != 0x00 ) && (
            ( ( c [ scan_pos ] >= '0' ) && ( c [ scan_pos ] <= '9' ) ) ||
            ( ( c [ scan_pos ] >= 'a' ) && ( c [ scan_pos ] <= 'f' ) ) ||
            ( ( c [ scan_pos ] >= 'A' ) && ( c [ scan_pos ] <= 'F' ) ) ) ) {
        if ( ( c [ scan_pos ] >= 'a' ) && ( c [ scan_pos ] <= 'f' ) ) {
            c [ scan_pos ] -= ( 'a' - 'A' );
        };
        scan_pos++;
    };

    if ( scan_pos == 0 ) {
        return 0;
    };
    length = scan_pos;
    scan_pos--;

    while ( scan_pos >= 0 ) {
        *value += iasm_hex_digit_to_value ( c [ scan_pos ] ) << bit_shift;
        bit_shift += 4;
        scan_pos--;
    };

    return length;
}


/**
 * @brief Dekóduje desítkový řetězec na číselnou hodnotu.
 *
 * Pokud po dekadických číslicích následuje hex písmeno (a-f/A-F),
 * automaticky přepne na hex dekódování.
 *
 * @param c     Ukazatel na začátek desítkového řetězce.
 * @param value Výstupní dekódovaná hodnota.
 * @return Počet zpracovaných znaků, nebo 0 pokud nebyla nalezena platná číslice.
 */
static int iasm_decode_dec_txt ( char *c, uint16_t *value ) {
    int scan_pos = 0;
    int j;
    int multiplier = 1;
    int length;

    *value = 0;

    while ( ( c [ scan_pos ] != 0x00 ) && ( ( c [ scan_pos ] >= '0' ) && ( c [ scan_pos ] <= '9' ) ) ) {
        scan_pos++;
    };
    /* pokud po dekadických číslicích následuje hex písmeno, je to hex číslo */
    if ( ( ( c [ scan_pos ] >= 'a' ) && ( c [ scan_pos ] <= 'f' ) ) || ( ( c [ scan_pos ] >= 'A' ) && ( c [ scan_pos ] <= 'F' ) ) ) {
        return iasm_decode_hex_txt ( c, value );
    };
    if ( scan_pos == 0 ) {
        return 0;
    };
    length = scan_pos;
    scan_pos--;

    while ( scan_pos >= 0 ) {
        j = c [ scan_pos ] - '0';
        *value += j * multiplier;
        multiplier = multiplier * 10;
        scan_pos--;
    };

    return length;
}


/**
 * @brief Dekóduje binární řetězec (znaky '0' a '1') na číselnou hodnotu.
 *
 * @param c     Ukazatel na začátek binárního řetězce (bez prefixu '%').
 * @param value Výstupní dekódovaná hodnota.
 * @return Počet zpracovaných znaků, nebo 0 pokud nebyl nalezen platný binární znak.
 */
static int iasm_decode_bin_txt ( char *c, uint16_t *value ) {

    int scan_pos = 0;
    int bit_value = 1;
    int length;

    *value = 0;

    while ( ( c [ scan_pos ] != 0x00 ) && ( ( c [ scan_pos ] == '0' ) || ( c [ scan_pos ] == '1' ) ) ) {
        scan_pos++;
    };

    if ( scan_pos == 0 ) {
        return 0;
    };
    length = scan_pos;
    scan_pos--;

    while ( scan_pos >= 0 ) {
        if ( c [ scan_pos-- ] == '1' ) {
            *value += bit_value;
        };
        bit_value = bit_value << 1;
    };

    return length;
}


/** @} */ /* konec skupiny iasm_helpers */


/** @defgroup iasm_phase1 Fáze 1: Parsování textu na tokeny
 *  @{ */


/**
 * @brief Rozparsuje textovou Z80 instrukci na strukturované tokeny.
 *
 * Normalizuje text (uppercase, odstraní nadbytečné mezery),
 * extrahuje číselné proměnné, řeší IX/IY offset se znaménkem
 * a korekci RST adres.
 *
 * @param assemble_txt Vstupní text instrukce.
 * @param parsed       Výstupní struktura s rozparsovanými daty.
 * @return 1 při úspěchu, 0 při chybě (např. přetečení pole proměnných).
 */
static int iasm_parse_line ( const char *assemble_txt, iasm_parsed_t *parsed ) {

    int is_legal_space, is_literal_operand, ix_iy_sign;
    int var_length, var_prefix_length;
    en_IASMSTATE inlasm_state;
    uint16_t value;
    char *par1_start;

    strncpy ( parsed->text, assemble_txt, sizeof ( parsed->text ) );
    parsed->text [ IASM_MNEMONICS_MAXLEN ] = 0x00;

    /* korekce vstupních dat */
    iasm_trim_lspaces ( parsed->text );
    iasm_trim_rspaces ( parsed->text );

    memset ( &parsed->length, 0x00, sizeof ( parsed->length ) );
    memset ( &parsed->pos, 0x00, sizeof ( parsed->pos ) );

    int cmd_is_ld = 0;
    int cmd_is_rst = 0;
    int flag_ld_set_res = 0;

    parsed->variables_count = 0;
    ix_iy_sign = 0;

    inlasm_state = INLASM_CMD;
    parsed->pos [ inlasm_state ] = &parsed->text[0];

    int scan_pos = 0;
    while ( ( parsed->text [ scan_pos ] != 0x00 ) ) {
        if ( ( parsed->text [ scan_pos ] >= 'a' ) && ( parsed->text [ scan_pos ] <= 'z' ) ) {
            parsed->text [ scan_pos ] -= ( 'a' - 'A' );
        };

        if ( inlasm_state == INLASM_CMD ) {
            if ( parsed->text [ scan_pos ] == ' ' ) {
                parsed->length [ INLASM_CMD ] = scan_pos;
                inlasm_state++;
                parsed->pos [ inlasm_state ] = &parsed->text [ scan_pos + 1 ];

                /* "LD " má 3 znaky (příkaz + mezera), "RST " má 4 */
                if ( 0 == strncmp ( parsed->pos [ INLASM_CMD ], "LD ", 3 ) ) {
                    cmd_is_ld = 1;
                } else if ( 0 == strncmp ( parsed->pos [ INLASM_CMD ], "RST ", 4 ) ) {
                    cmd_is_rst = 1;
                };
            };


        } else {

            /* odstraníme nepovolené mezery */
            if ( parsed->text [ scan_pos ] == ' ' ) {

                is_legal_space = 0;

                /*
                 * Undocumented instrukce tvaru LD r,OP (IX+d):
                 * Mezera za čárkou je legální, protože následuje rotace/bitová operace.
                 * Pozice scan_pos závisí na délce: "LD " (3) + registr (1) + "," (1) + operace.
                 * scan_pos==7: LD r,RL/RR (2-znakové operace: 3+1+1+2=7)
                 * scan_pos==8: LD r,RLC/RRC/SLA/SLL/SRA/SRL (3-znakové) nebo LD r,RES/SET
                 */
                if ( cmd_is_ld ) {
                    if ( inlasm_state == INLASM_PAR1 ) {
                        par1_start = parsed->pos [ inlasm_state ];
                        if ( ( par1_start [ 0 ] == 'A' ) || ( par1_start [ 0 ] == 'B' ) || ( par1_start [ 0 ] == 'C' ) || ( par1_start [ 0 ] == 'D' ) || ( par1_start [ 0 ] == 'E' ) || ( par1_start [ 0 ] == 'H' ) || ( par1_start [ 0 ] == 'L' ) ) {
                            if ( scan_pos == 7 ) {
                                /* pozice 7 = "LD " (3) + "r," (2) + "RL"/"RR" (2) */
                                if ( ( 0 == strncmp ( &par1_start [ 1 ], ",RL", 3 ) ) || ( 0 == strncmp ( &par1_start [ 1 ], ",RR", 3 ) ) ) {
                                    is_legal_space = 1;
                                };
                            } else if ( scan_pos == 8 ) {
                                /* pozice 8 = "LD " (3) + "r," (2) + "RES"/"SET" (3) nebo "RLC"/"RRC"/... (3) */
                                if ( ( 0 == strncmp ( &par1_start [ 1 ], ",RES", 4 ) ) || ( 0 == strncmp ( &par1_start [ 1 ], ",SET", 4 ) ) ) {
                                    is_legal_space = 1;
                                    flag_ld_set_res = 1;

                                } else if ( ( 0 == strncmp ( &par1_start [ 1 ], ",RLC", 4 ) ) || ( 0 == strncmp ( &par1_start [ 1 ], ",RRC", 4 ) ) || ( 0 == strncmp ( &par1_start [ 1 ], ",SLA", 4 ) ) || ( 0 == strncmp ( &par1_start [ 1 ], ",SLL", 4 ) ) || ( 0 == strncmp ( &par1_start [ 1 ], ",SRA", 4 ) ) || ( 0 == strncmp ( &par1_start [ 1 ], ",SRL", 4 ) ) ) {
                                    is_legal_space = 1;
                                };
                            };
                        };
                    };
                };


                if ( !is_legal_space ) {
                    iasm_trim_lspaces ( &parsed->text [ scan_pos ] );
                    if ( ( parsed->text [ scan_pos ] >= 'a' ) && ( parsed->text [ scan_pos ] <= 'z' ) ) {
                        parsed->text [ scan_pos ] -= ( 'a' - 'A' );
                    };
                };
            };


            if ( inlasm_state != INLASM_PAR3 ) {

                if ( ( ix_iy_sign == 0 ) && ( ( parsed->text [ scan_pos ] == '+' ) || ( parsed->text [ scan_pos ] == '-' ) ) ) {

                    if ( ( 0 == strncmp ( &parsed->text [ scan_pos - 3 ], "(IX", 3 ) ) || ( 0 == strncmp ( &parsed->text [ scan_pos - 3 ], "(IY", 3 ) ) ) {
                        if ( parsed->text [ scan_pos ] == '+' ) {
                            ix_iy_sign = 1;
                        } else {
                            ix_iy_sign = -1;
                        };
                        parsed->text [ scan_pos ] = '+';
                    };

                } else {

                    var_length = 0;

                    if ( ( parsed->text [ scan_pos ] >= '0' ) && ( parsed->text [ scan_pos ] <= '9' ) ) {

                        is_literal_operand = 0;
                        /*
                         * Číslice, které NEJSOU proměnné — jsou součástí parametru:
                         * - BIT/RES/SET n,... — číslo bitu (pozice 4 = "BIT " (4))
                         * - IM n — mód přerušení (pozice 3 = "IM " (3))
                         * - OUT (C),n — výstupní hodnota (pozice 8 = "OUT (C)," (8))
                         * - LD r,SET/RES n,... — číslo bitu v undoc (pozice 9 = "LD r,SET " (9))
                         */
                        if ( inlasm_state == INLASM_PAR1 ) {
                            char *cmd_start = parsed->pos [ INLASM_CMD ];
                            if ( ( ( flag_ld_set_res ) && ( scan_pos == 9 ) ) ||
                                    ( ( scan_pos == 4 ) && ( ( 0 == strncmp ( cmd_start, "BIT", parsed->length [ INLASM_CMD ] ) ) || ( 0 == strncmp ( cmd_start, "RES", parsed->length [ INLASM_CMD ] ) ) || ( 0 == strncmp ( cmd_start, "SET", parsed->length [ INLASM_CMD ] ) ) ) ) ||
                                    ( ( scan_pos == 3 ) && ( 0 == strncmp ( cmd_start, "IM", parsed->length [ INLASM_CMD ] ) ) ) ||
                                    ( ( scan_pos == 8 ) && ( 0 == strncmp ( parsed->text, "OUT (C),", 8 ) ) ) ) {
                                is_literal_operand = 1;
                            };
                        };


                        if ( !is_literal_operand ) {
                            if ( ( parsed->text [ scan_pos ] == '0' ) && ( ( parsed->text [ scan_pos + 1 ] == 'x' ) || ( parsed->text [ scan_pos + 1 ] == 'X' ) ) ) {
                                var_prefix_length = 2;
                                var_length = iasm_decode_hex_txt ( &parsed->text [ scan_pos + var_prefix_length ], &value );
                            } else {
                                var_prefix_length = 0;
                                var_length = iasm_decode_dec_txt ( &parsed->text [ scan_pos ], &value );
                            };
                        };

                    } else if ( parsed->text [ scan_pos ] == '#' ) {
                        var_prefix_length = 1;
                        var_length = iasm_decode_hex_txt ( &parsed->text [ scan_pos + var_prefix_length ], &value );

                    } else if ( parsed->text [ scan_pos ] == '%' ) {
                        var_prefix_length = 1;
                        var_length = iasm_decode_bin_txt ( &parsed->text [ scan_pos + var_prefix_length ], &value );
                    };


                    /* našli jsme platnou proměnnou? */
                    if ( var_length ) {

                        if ( ix_iy_sign != 0 ) {
                            if ( value <= 0xff ) { /* pokud je hodnota větší, tak na znaménko nesaháme — chyba se ohlásí jinde */
                                if ( ( ix_iy_sign == -1 ) && ( value < 128 ) && ( value > 0 ) ) { /* pokud bylo vloženo (IX-nn), kde nn < 128, tak sahneme na znaménko */
                                    value = 0x100 - value;
                                };
                            };
                        };

                        /* kontrola přetečení pole proměnných */
                        if ( parsed->variables_count >= IASM_MAX_VARIABLES ) {
                            return 0;
                        };

                        parsed->var [ parsed->variables_count++ ] = value;
                        parsed->length [ inlasm_state ] = ( &parsed->text [ scan_pos ] - parsed->pos [ inlasm_state ] ) / sizeof ( char );
                        inlasm_state++;
                        parsed->pos [ inlasm_state ] = &parsed->text [ scan_pos ];
                        var_length += var_prefix_length;
                        parsed->length [ inlasm_state ] = var_length;
                        inlasm_state++;
                        parsed->pos [ inlasm_state ] = &parsed->text [ scan_pos + var_length ];
                        scan_pos += var_length - 1;
                    };

                    ix_iy_sign = 0;
                };
            };
        };

        scan_pos++;
    };
    if ( !parsed->length [ INLASM_CMD ] ) {
        parsed->length [ INLASM_CMD ] = scan_pos;
    };

    /* NULL check — pos[inlasm_state] může být NULL pokud text neobsahuje danou sekci */
    if ( parsed->pos [ inlasm_state ] != NULL ) {
        if ( !parsed->length [ inlasm_state ] ) {
            if ( parsed->pos [ inlasm_state ] [ 0 ] != 0x00 ) {
                parsed->length [ inlasm_state ] = ( &parsed->text [ scan_pos ] - parsed->pos [ inlasm_state ] ) / sizeof ( char );
            };
        };
    };

    /* zakončíme sekce proměnných nulou, aby se neúčastnily porovnávání */
    if ( parsed->pos [ INLASM_VAR1 ] != NULL ) {
        parsed->pos [ INLASM_VAR1 ] [ 0 ] = 0x00;
    };

    if ( parsed->pos [ INLASM_VAR2 ] != NULL ) {
        parsed->pos [ INLASM_VAR2 ] [ 0 ] = 0x00;
    };

    char *cmd_name = parsed->pos [ INLASM_CMD ];
    cmd_name [ parsed->length [ INLASM_CMD ] ] = 0x00;


    /* usnadnění pro instrukce RST nn — korekce dekadických restart adres na hex */
    if ( ( cmd_is_rst ) && ( parsed->variables_count == 1 ) && ( ( parsed->length [ INLASM_PAR1 ] | parsed->length [ INLASM_PAR2 ] ) == 0 ) ) {
        switch ( parsed->var [ 0 ] ) {
            case 10:
                parsed->var [ 0 ] = 0x10;
                break;
            case 18:
                parsed->var [ 0 ] = 0x18;
                break;
            case 20:
                parsed->var [ 0 ] = 0x20;
                break;
            case 28:
                parsed->var [ 0 ] = 0x28;
                break;
            case 30:
                parsed->var [ 0 ] = 0x30;
                break;
            case 38:
                parsed->var [ 0 ] = 0x38;
                break;
        };
        /* zapíšeme hex hodnotu zpět do parametru — RST potřebuje textový parametr */
        iasm_byte_to_hex_str ( (uint8_t) parsed->var [ 0 ], parsed->pos [ INLASM_PAR1 ] );
        parsed->length [ INLASM_PAR1 ] = strlen ( parsed->pos [ INLASM_PAR1 ] );
        parsed->variables_count = 0;
    };

    return 1;
}


/** @} */ /* konec skupiny iasm_phase1 */


/** @defgroup iasm_phase2 Fáze 2: Vyhledání v tabulce opkódů
 *  @{ */


/**
 * @brief Vyhledá odpovídající opkódový řetězec v tabulce opkódů.
 *
 * Prohledá hierarchii: iasm_chars -> iasm_commands -> iasm_opts.
 * Porovnává příkaz, počet proměnných, délky parametrů a textové parametry.
 *
 * @param parsed Ukazatel na rozparsovaná data instrukce.
 * @return Ukazatel na opkódový řetězec (např. "DD$8E"), nebo NULL při nenalezení.
 */
static const char * iasm_lookup_opcode ( const iasm_parsed_t *parsed ) {

    const iasm_commands_t *asm_commands = NULL;
    const iasm_opts_t *asm_opts = NULL;
    const char *cmd_opts = NULL;

    char *cmd_name = parsed->pos [ INLASM_CMD ];

    /* hledáme sadu instrukcí podle prvního písmene */
    int char_idx = 0;
    while ( iasm_chars [ char_idx ] . c != 0x00 ) {
        if ( iasm_chars [ char_idx ] . c == cmd_name [ 0 ] ) {
            asm_commands = iasm_chars [ char_idx ] . commands;
            break;
        };
        char_idx++;
    };

    int cmd_idx = 0;
    if ( asm_commands != NULL ) {
        while ( asm_commands [ cmd_idx ] . command != NULL ) {
            if ( 0 == strcmp ( cmd_name, asm_commands [ cmd_idx ] . command ) ) {
                asm_opts = asm_commands [ cmd_idx ] . opts;
                break;
            };
            cmd_idx++;
        };

        int opt_idx = 0;
        if ( asm_opts != NULL ) {
            while ( asm_opts [ opt_idx ] . opcode != NULL ) {
                /* pokud souhlasí počet variables, počet znaků před 1. variable a počet za ním, tak hledáme dál */
                if ( ( asm_opts [ opt_idx ] . variables == parsed->variables_count ) && ( asm_opts [ opt_idx ] . llen == parsed->length [ INLASM_PAR1 ] ) && ( asm_opts [ opt_idx ] . rlen == parsed->length [ INLASM_PAR2 ] ) ) {
                    if ( 0 == parsed->variables_count ) {
                        if ( !parsed->length [ INLASM_PAR1 ] ) {
                            /* NALEZENO: instrukce nemá žádné parametry */
                            cmd_opts = asm_opts [ opt_idx ] . opcode;
                            break;
                        };
                        if ( 0 == strcmp ( parsed->pos [ INLASM_PAR1 ], asm_opts [ opt_idx ] . param ) ) {
                            /* NALEZENO: instrukce má shodný parametr a nemá žádné variables */
                            cmd_opts = asm_opts [ opt_idx ] . opcode;
                            break;
                        };

                    } else if ( parsed->length [ INLASM_PAR1 ] == 0 ) {
                        /* NALEZENO: instrukce nemá žádný parametr, ale má jednu proměnnou */
                        cmd_opts = asm_opts [ opt_idx ] . opcode;
                        break;
                    } else {
                        if ( 0 == strncmp ( parsed->pos [ INLASM_PAR1 ], asm_opts [ opt_idx ] . param, parsed->length [ INLASM_PAR1 ] ) ) {

                            /* souhlasí parametr před proměnnou */

                            if ( parsed->length [ INLASM_PAR2 ] == 0 ) {
                                /* NALEZENO: za proměnnou už není žádný parametr a instrukce je ukončena proměnnou */
                                cmd_opts = asm_opts [ opt_idx ] . opcode;
                                break;

                            } else {
                                if ( parsed->variables_count == 1 ) {
                                    if ( 0 == strcmp ( parsed->pos [ INLASM_PAR2 ], &asm_opts [ opt_idx ] . param [ parsed->length [ INLASM_PAR1 ] + 1 ] ) ) {
                                        /* NALEZENO: souhlasí parametr před i za proměnnou */
                                        cmd_opts = asm_opts [ opt_idx ] . opcode;
                                        break;
                                    };
                                } else {
                                    /* 2 proměnné mají jen tyto instrukce: LD (IX+$),# */
                                    if ( ( 0 == strcmp ( parsed->pos [ INLASM_PAR2 ], ")," ) ) && ( parsed->length [ INLASM_PAR3 ] == 0 ) ) {
                                        /* NALEZENO */
                                        cmd_opts = asm_opts [ opt_idx ] . opcode;
                                        break;
                                    };
                                };
                            };
                        };
                    };
                };
                opt_idx++;
            };
        };
    };

    return cmd_opts;
}


/** @} */ /* konec skupiny iasm_phase2 */


/** @defgroup iasm_phase3 Fáze 3: Generování binárního kódu
 *  @{ */


/**
 * @brief Vygeneruje binární kód z opkódového řetězce a proměnných.
 *
 * Opkódový řetězec obsahuje hex číslice (přímé bajty) a speciální znaky:
 *   - '@' = 16-bit word (little-endian)
 *   - '#' = 8-bit byte
 *   - '$' = 8-bit IX/IY offset
 *   - '%' = relativní adresa (pro JR, DJNZ)
 *
 * @param opcode_str Opkódový řetězec z tabulky (např. "DD$5E").
 * @param var        Pole extrahovaných číselných proměnných.
 * @param addr       Adresa instrukce (pro výpočet relativních skoků).
 * @param output     Výstupní binární data.
 * @return IASM_OK při úspěchu, jinak chybový kód.
 */
static iasm_error_t iasm_generate_binary ( const char *opcode_str, const uint16_t *var, uint16_t addr, iasm_bin_t *output ) {

    memset ( output, 0x00, sizeof ( *output ) );

    int src_pos = 0;         /* pozice ve zdrojovém opkódovém řetězci */
    int dst_pos = 0;         /* pozice ve výstupním binárním poli */
    int nibble_index = 0;    /* 0 = horní nibble, 1 = dolní nibble */
    int var_idx = 0;         /* index do pole proměnných */

    while ( opcode_str [ src_pos ] != 0x00 ) {
        switch ( opcode_str [ src_pos ] ) {
            case '@':
                /* 16-bit word, little-endian */
                output->byte[ dst_pos++ ] = var [ var_idx ] & 0xff;
                output->byte[ dst_pos++ ] = ( var [ var_idx ] >> 8 ) & 0xff;
                var_idx++;
                break;
            case '#':
            case '$':
                /* 8-bit byte — musí se vejít do jednoho bajtu */
                if ( var [ var_idx ] <= 0xff ) {
                    output->byte[ dst_pos++ ] = var [ var_idx++ ];
                } else {
                    return IASM_ERR_VALUE_OUT_OF_RANGE;
                };
                break;
            case '%':
            {
                /* relativní adresa — všechny instrukce s relativní hodnotou mají 2 bajty */
                int rel_addr = var [ var_idx ] - ( addr + 2 );
                if ( ( rel_addr >= -128 ) && ( ( rel_addr <= 127 ) ) ) {
                    output->byte[ dst_pos++ ] = (uint8_t) rel_addr;
                } else {
                    return IASM_ERR_VALUE_OUT_OF_RANGE;
                };
                break;
            }
            default:
                /* hexadecimální číslice — skládáme po nibblech */
                output->byte[ dst_pos ] |= iasm_hex_digit_to_value ( opcode_str [ src_pos ] ) << ( ( ( ~nibble_index ) & 1 ) * 4 );
                if ( nibble_index & 1 ) {
                    dst_pos++;
                };
                nibble_index++;
                break;
        };
        src_pos++;
    };

    output->length = dst_pos;
    return IASM_OK;
}


/** @} */ /* konec skupiny iasm_phase3 */


/** @defgroup iasm_api Veřejné API
 *  @{ */


unsigned iasm_assemble_line ( uint16_t addr, const char *assemble_txt, iasm_bin_t *compiled_output, iasm_error_t *error ) {

    iasm_parsed_t parsed;

    /* Fáze 1: parsování */
    if ( !iasm_parse_line ( assemble_txt, &parsed ) ) {
        if ( error != NULL ) {
            *error = IASM_ERR_INVALID_INSTRUCTION;
        };
        return 0;
    };

    /* Fáze 2: vyhledání opkódu */
    const char *opcode_str = iasm_lookup_opcode ( &parsed );
    if ( opcode_str == NULL ) {
        if ( error != NULL ) {
            *error = IASM_ERR_INVALID_INSTRUCTION;
        };
        return 0;
    };

    /* Fáze 3: generování binárního kódu */
    iasm_bin_t compiled;
    iasm_error_t gen_err = iasm_generate_binary ( opcode_str, parsed.var, addr, &compiled );
    if ( gen_err != IASM_OK ) {
        if ( error != NULL ) {
            *error = gen_err;
        };
        return 0;
    };

    memcpy ( compiled_output, &compiled, sizeof ( compiled ) );

    if ( error != NULL ) {
        *error = IASM_OK;
    };

    return compiled.length; /* vrátíme počet zkompilovaných bajtů */
}

/** @} */ /* konec skupiny iasm_api */
