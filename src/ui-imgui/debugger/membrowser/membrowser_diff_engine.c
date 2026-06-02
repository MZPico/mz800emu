/**
 * @file membrowser_diff_engine.c
 * @brief Diff engine - implementace (V5).
 *
 * Pure C - kompiluje se nezávisle na ImGui/dbgapi/SDL. Smí volat jen
 * stdint + stdbool. Žádný stdio/stdlib.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#include "membrowser_diff_engine.h"

#include <stddef.h>

uint64_t membrowser_diff_compute ( const uint8_t *a, uint64_t size_a,
                                    const uint8_t *b, uint64_t size_b,
                                    membrowser_diff_byte_cb_t cb,
                                    void *user,
                                    st_MB_DIFF_STATS *stats )
{
    if ( !stats ) return 0;

    stats->size_a = size_a;
    stats->size_b = size_b;
    stats->compared_bytes = ( size_a < size_b ) ? size_a : size_b;
    stats->changed = 0;
    stats->size_mismatch = ( size_a != size_b );

    /* Společný prefix - per-byte porovnání + callback. */
    if ( stats->compared_bytes > 0 && a && b ) {
        for ( uint64_t i = 0; i < stats->compared_bytes; i++ ) {
            if ( a[i] != b[i] ) {
                stats->changed++;
                if ( cb ) {
                    if ( !cb ( user, i, a[i], b[i] ) ) {
                        /* Iterace přerušena - changed obsahuje jen
                         * bajty doiterované; tail mismatch i tak připočteme
                         * níže (= konsistentní celkový pohled). */
                        break;
                    }
                }
            }
        }
    }

    /* Tail mismatch - delší strana se celá počítá jako changed. */
    if ( stats->size_mismatch ) {
        uint64_t tail = ( size_a > size_b )
            ? ( size_a - size_b ) : ( size_b - size_a );
        stats->changed += tail;
    }

    return stats->changed;
}


bool membrowser_diff_is_changed ( const uint8_t *a, uint64_t size_a,
                                   const uint8_t *b, uint64_t size_b,
                                   uint64_t offset )
{
    bool in_a = ( a != NULL ) && ( offset < size_a );
    bool in_b = ( b != NULL ) && ( offset < size_b );

    if ( in_a && in_b ) {
        return a[offset] != b[offset];
    }
    if ( !in_a && !in_b ) {
        /* Offset mimo oba buffery - "neexistuje" v obou, neoznačujeme. */
        return false;
    }
    /* Jen jedna strana má bajt - tail mismatch. */
    return true;
}
