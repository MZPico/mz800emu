/**
 * @file membrowser_undo.c
 * @brief Per-region undo/redo stack - implementace.
 *
 * Datová struktura: pole "buckets" per (region_kind, sub_id), každý bucket
 * drží 2 ring buffery (undo + redo) o max @c MB_UNDO_MAX_LEVELS položkách.
 *
 * Hledání bucketu: lineární scan v poli (kapacita @c MAX_BUCKETS), insert
 * při prvním push pro nový region key. Counts jsou typicky pod 50 napříč
 * jednou session, lineární scan dostatečný.
 *
 * Threading: UI-thread only - žádný locking.
 *
 * Pamět: každý záznam = malloc(len bajtů); free při overwrite / pop /
 * @ref membrowser_undo_clear_all.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#include "membrowser_undo.h"

#include <stdlib.h>
#include <string.h>


/**
 * @brief Horní limit počtu unikátních (region_kind, sub_id) klíčů držených
 *        v undo storage.
 *
 * Typický emulátor session = pár desítek aktivních regionů; 64 je rezerva.
 * Po překročení nové push pro neexistující bucket vrací -2 (= alokační
 * fail) - uživatel se dozví, že je třeba clear all.
 */
#define MAX_BUCKETS 64


/**
 * @brief Jeden bucket = ring buffer undo + redo per region key.
 *
 * @c entries[head] = poslední přidaný (top stacku); @c entries[head-1] =
 * druhý odshora atd. Po překročení @c MB_UNDO_MAX_LEVELS položek se
 * nejstarší (= ten kam ukáže @c head + 1 mod cap) zahodí (free).
 *
 * Stack je modelován jako vector na poli: @c count drží počet platných
 * záznamů (0..MB_UNDO_MAX_LEVELS); push přidá na pozici @c count nebo
 * po overflow shiftuje (free dno, posun pozic).
 *
 * Pro jednoduchost implementujeme jako plain array s počitadlem - shift
 * při overflow je O(N) ale N je max 10.
 */
typedef struct st_BUCKET
{
    int used;                       /**< 0 = volný slot, 1 = obsazený. */
    int region_kind;                /**< en_REGION_KIND. */
    int region_sub_id;              /**< Disambiguator. */
    st_MB_UNDO_ENTRY undo[MB_UNDO_MAX_LEVELS];
    int undo_count;                 /**< 0..MB_UNDO_MAX_LEVELS. */
    st_MB_UNDO_ENTRY redo[MB_UNDO_MAX_LEVELS];
    int redo_count;
} st_BUCKET;


static st_BUCKET s_buckets[MAX_BUCKETS];


/**
 * @brief Najde existující bucket pro daný region key.
 *
 * @return Index v s_buckets[] nebo -1 pokud neexistuje.
 */
static int find_bucket ( int region_kind, int region_sub_id )
{
    for ( int i = 0; i < MAX_BUCKETS; i++ ) {
        if ( s_buckets[i].used
             && s_buckets[i].region_kind == region_kind
             && s_buckets[i].region_sub_id == region_sub_id ) {
            return i;
        }
    }
    return -1;
}


/**
 * @brief Najde nebo alokuje (= označí used) bucket pro region key.
 *
 * @return Index v s_buckets[] nebo -1 pokud je tabulka plná.
 */
static int find_or_alloc_bucket ( int region_kind, int region_sub_id )
{
    int idx = find_bucket ( region_kind, region_sub_id );
    if ( idx >= 0 ) return idx;
    for ( int i = 0; i < MAX_BUCKETS; i++ ) {
        if ( !s_buckets[i].used ) {
            memset ( &s_buckets[i], 0, sizeof ( s_buckets[i] ) );
            s_buckets[i].used = 1;
            s_buckets[i].region_kind = region_kind;
            s_buckets[i].region_sub_id = region_sub_id;
            return i;
        }
    }
    return -1;
}


/**
 * @brief Uvolní bytes záznamu (in-place).
 */
static void free_entry ( st_MB_UNDO_ENTRY *e )
{
    if ( !e ) return;
    if ( e->bytes ) {
        free ( e->bytes );
        e->bytes = NULL;
    }
    e->len = 0;
    e->offset = 0;
    e->region_kind = 0;
    e->region_sub_id = 0;
}


/**
 * @brief Push do daného pole (undo nebo redo) - common helper.
 */
static int push_into_stack ( st_MB_UNDO_ENTRY *stack, int *count,
                              int region_kind, int region_sub_id,
                              uint64_t offset, const uint8_t *bytes,
                              uint32_t len )
{
    if ( len == 0 || !bytes ) return -1;
    if ( len > MB_UNDO_MAX_BYTES ) return -1;

    uint8_t *dup = ( uint8_t * ) malloc ( len );
    if ( !dup ) return -2;
    memcpy ( dup, bytes, len );

    if ( *count >= MB_UNDO_MAX_LEVELS ) {
        /* Shift down - zahodit nejstarší (index 0). */
        free_entry ( &stack[0] );
        for ( int i = 1; i < MB_UNDO_MAX_LEVELS; i++ ) {
            stack[i - 1] = stack[i];
        }
        ( *count )--;
        /* Po shiftu je index *count volný. */
    }

    st_MB_UNDO_ENTRY *e = &stack[*count];
    e->region_kind = region_kind;
    e->region_sub_id = region_sub_id;
    e->offset = offset;
    e->len = len;
    e->bytes = dup;
    ( *count )++;
    return 0;
}


/**
 * @brief Pop z daného pole (top = index count-1).
 */
static int pop_from_stack ( st_MB_UNDO_ENTRY *stack, int *count,
                             st_MB_UNDO_ENTRY *out )
{
    if ( !out ) return -1;
    if ( *count <= 0 ) return -1;
    ( *count )--;
    *out = stack[*count];
    /* Vynulovat slot, ownership prešlo na callera. */
    memset ( &stack[*count], 0, sizeof ( stack[*count] ) );
    return 0;
}


/**
 * @brief Smaž všechny redo záznamy pro bucket (nový edit invaliduje redo).
 */
static void clear_redo ( st_BUCKET *b )
{
    for ( int i = 0; i < b->redo_count; i++ ) {
        free_entry ( &b->redo[i] );
    }
    b->redo_count = 0;
}


int membrowser_undo_push ( int region_kind, int region_sub_id,
                            uint64_t offset, const uint8_t *bytes_before,
                            uint32_t len )
{
    if ( len > MB_UNDO_MAX_BYTES ) return -1;
    int bi = find_or_alloc_bucket ( region_kind, region_sub_id );
    if ( bi < 0 ) return -2;
    st_BUCKET *b = &s_buckets[bi];
    /* Nový edit invaliduje existující redo. */
    clear_redo ( b );
    return push_into_stack ( b->undo, &b->undo_count,
                              region_kind, region_sub_id,
                              offset, bytes_before, len );
}


int membrowser_undo_pop ( int region_kind, int region_sub_id,
                           st_MB_UNDO_ENTRY *out )
{
    int bi = find_bucket ( region_kind, region_sub_id );
    if ( bi < 0 ) return -1;
    st_BUCKET *b = &s_buckets[bi];
    return pop_from_stack ( b->undo, &b->undo_count, out );
}


int membrowser_redo_push ( int region_kind, int region_sub_id,
                            uint64_t offset, const uint8_t *bytes_now,
                            uint32_t len )
{
    if ( len > MB_UNDO_MAX_BYTES ) return -1;
    int bi = find_or_alloc_bucket ( region_kind, region_sub_id );
    if ( bi < 0 ) return -2;
    st_BUCKET *b = &s_buckets[bi];
    return push_into_stack ( b->redo, &b->redo_count,
                              region_kind, region_sub_id,
                              offset, bytes_now, len );
}


int membrowser_redo_pop ( int region_kind, int region_sub_id,
                           st_MB_UNDO_ENTRY *out )
{
    int bi = find_bucket ( region_kind, region_sub_id );
    if ( bi < 0 ) return -1;
    st_BUCKET *b = &s_buckets[bi];
    return pop_from_stack ( b->redo, &b->redo_count, out );
}


void membrowser_undo_stats ( int region_kind, int region_sub_id,
                              int *undo_n, int *redo_n )
{
    int bi = find_bucket ( region_kind, region_sub_id );
    if ( bi < 0 ) {
        if ( undo_n ) *undo_n = 0;
        if ( redo_n ) *redo_n = 0;
        return;
    }
    st_BUCKET *b = &s_buckets[bi];
    if ( undo_n ) *undo_n = b->undo_count;
    if ( redo_n ) *redo_n = b->redo_count;
}


void membrowser_undo_clear_all ( void )
{
    for ( int i = 0; i < MAX_BUCKETS; i++ ) {
        if ( !s_buckets[i].used ) continue;
        for ( int j = 0; j < s_buckets[i].undo_count; j++ ) {
            free_entry ( &s_buckets[i].undo[j] );
        }
        for ( int j = 0; j < s_buckets[i].redo_count; j++ ) {
            free_entry ( &s_buckets[i].redo[j] );
        }
        memset ( &s_buckets[i], 0, sizeof ( s_buckets[i] ) );
    }
}


void membrowser_undo_free_entry ( st_MB_UNDO_ENTRY *e )
{
    free_entry ( e );
}
