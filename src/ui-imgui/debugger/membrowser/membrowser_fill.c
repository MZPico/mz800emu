/**
 * @file membrowser_fill.c
 * @brief Pattern fill engine - implementace.
 *
 * Pure C, žádný emu / ImGui dependency, žádný malloc - vše stack/in-place.
 * Build je STANDALONE (kompiluje se i v testu bez MZ800EMU_CFG_DEBUGGER_ENABLED
 * guardu - test může includnout přímo .c).
 *
 * Random generator: xorshift32 (Marsaglia) - deterministický při fixed seed,
 * pro fill účely zcela dostatečný (ne kryptografický).
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#include "membrowser_fill.h"

#include <string.h>
#include <ctype.h>


/**
 * @brief Xorshift32 - rychlý, deterministický RNG (state mutuje).
 *
 * @param state  Pointer na 32-bit state (NESmí být NULL, NESmí být 0).
 * @return       Nová pseudo-random hodnota.
 */
static uint32_t xorshift32_step ( uint32_t *state )
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}


/**
 * @brief Bezpečné získání nibble z hex znaku.
 *
 * @param c  ASCII znak.
 * @return   0..15 pro hex digit, -1 pro non-hex.
 */
static int hex_nibble ( char c )
{
    if ( c >= '0' && c <= '9' ) return c - '0';
    if ( c >= 'a' && c <= 'f' ) return 10 + ( c - 'a' );
    if ( c >= 'A' && c <= 'F' ) return 10 + ( c - 'A' );
    return -1;
}


int membrowser_fill_apply ( const st_MB_FILL_PARAMS *params,
                            uint8_t *out, size_t len )
{
    if ( !params || !out ) return -1;
    if ( params->mode < 0 || params->mode >= MB_FILL_MODE__COUNT ) return -1;
    if ( len == 0 ) return 0;

    switch ( params->mode ) {
        case MB_FILL_MODE_ZERO:
            memset ( out, 0x00, len );
            return 0;

        case MB_FILL_MODE_FF:
            memset ( out, 0xFF, len );
            return 0;

        case MB_FILL_MODE_RAMP:
            for ( size_t i = 0; i < len; i++ ) {
                out[i] = ( uint8_t ) ( i & 0xFFu );
            }
            return 0;

        case MB_FILL_MODE_RANDOM: {
            /* Seed 0 je pro xorshift32 mrtvý - fallback na 0xDEADBEEF. */
            uint32_t st = ( params->random_seed != 0 )
                            ? params->random_seed
                            : 0xDEADBEEFu;
            for ( size_t i = 0; i < len; i++ ) {
                out[i] = ( uint8_t ) ( xorshift32_step ( &st ) & 0xFFu );
            }
            return 0;
        }

        case MB_FILL_MODE_PATTERN: {
            if ( params->pattern_len <= 0
                 || params->pattern_len > MB_FILL_PATTERN_MAX_BYTES ) {
                return -1;
            }
            int plen = params->pattern_len;
            for ( size_t i = 0; i < len; i++ ) {
                out[i] = params->pattern[i % ( size_t ) plen];
            }
            return 0;
        }

        case MB_FILL_MODE_INCREMENT: {
            uint8_t v = params->fixed_value;
            for ( size_t i = 0; i < len; i++ ) {
                out[i] = v;
                v = ( uint8_t ) ( ( v + 1 ) & 0xFFu );
            }
            return 0;
        }

        default:
            return -1;
    }
}


int membrowser_fill_parse_pattern ( const char *str,
                                     uint8_t *out, int max_bytes,
                                     int *out_count )
{
    if ( !str || !out || max_bytes <= 0 || !out_count ) return -1;
    *out_count = 0;

    int n_collected = 0;        /* Počet hotových bajtů. */
    int hi_nibble = -1;         /* Buffer pro horní nibble, -1 = čekáme na první. */

    for ( const char *p = str; *p != '\0'; p++ ) {
        char c = *p;
        /* Whitespace + běžné oddělovače ignorujeme (mezera/tab/čárka/dvojtečka). */
        if ( isspace ( ( unsigned char ) c ) || c == ',' || c == ':' || c == ';' ) {
            continue;
        }
        int nib = hex_nibble ( c );
        if ( nib < 0 ) {
            /* Neplatný znak. */
            return -1;
        }
        if ( hi_nibble < 0 ) {
            hi_nibble = nib;
        } else {
            if ( n_collected >= max_bytes ) {
                return -2;  /* Buffer plný. */
            }
            out[n_collected++] = ( uint8_t ) ( ( hi_nibble << 4 ) | nib );
            hi_nibble = -1;
        }
    }

    if ( hi_nibble >= 0 ) {
        /* Lichý počet hex digitů. */
        return -1;
    }

    *out_count = n_collected;
    return 0;
}


const char *membrowser_fill_mode_name ( int mode )
{
    switch ( mode ) {
        case MB_FILL_MODE_ZERO:      return "Zeros (0x00)";
        case MB_FILL_MODE_FF:        return "Ones (0xFF)";
        case MB_FILL_MODE_RAMP:      return "Ramp (00 01 02 ...)";
        case MB_FILL_MODE_RANDOM:    return "Random";
        case MB_FILL_MODE_PATTERN:   return "Pattern (hex)";
        case MB_FILL_MODE_INCREMENT: return "Increment from value";
        default:                     return "?";
    }
}
