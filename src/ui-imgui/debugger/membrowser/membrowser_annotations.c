/**
 * @file membrowser_annotations.c
 * @brief Annotation storage + perzistence - implementace.
 *
 * Pure C, žádný emu / ImGui dependency. UI vrstva (membrowser_annot_dialog)
 * volá tyto API.
 *
 * Storage je fixed-size pole st_MB_ANNOTATION[MB_ANNOT_MAX_ENTRIES] -
 * žádný malloc, žádné resize. Lineární scan pro find/remove - počty
 * položek typicky < 100 v jedné session, akceptovatelné.
 *
 * Threading: UI-thread only.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#include "membrowser_annotations.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


static st_MB_ANNOTATION s_storage[MB_ANNOT_MAX_ENTRIES];


/**
 * @brief Najde existující slot pro daný klíč.
 *
 * @return Index nebo -1 pokud nenalezen.
 */
static int find_slot ( int region_kind, int region_sub_id, uint32_t addr )
{
    for ( int i = 0; i < MB_ANNOT_MAX_ENTRIES; i++ ) {
        if ( !s_storage[i].used ) continue;
        if ( s_storage[i].region_kind == region_kind
             && s_storage[i].region_sub_id == region_sub_id
             && s_storage[i].addr == addr ) {
            return i;
        }
    }
    return -1;
}


/**
 * @brief Najde první volný slot.
 *
 * @return Index nebo -1 pokud storage plný.
 */
static int find_free_slot ( void )
{
    for ( int i = 0; i < MB_ANNOT_MAX_ENTRIES; i++ ) {
        if ( !s_storage[i].used ) return i;
    }
    return -1;
}


int membrowser_annotations_set ( int region_kind, int region_sub_id,
                                  uint32_t addr,
                                  const char *text, uint32_t color_rgba )
{
    int idx = find_slot ( region_kind, region_sub_id, addr );
    if ( idx < 0 ) {
        idx = find_free_slot ( );
        if ( idx < 0 ) return -1;
    }
    s_storage[idx].used = true;
    s_storage[idx].region_kind = region_kind;
    s_storage[idx].region_sub_id = region_sub_id;
    s_storage[idx].addr = addr;
    s_storage[idx].color_rgba = color_rgba;
    if ( text ) {
        strncpy ( s_storage[idx].text, text, MB_ANNOT_TEXT_MAX - 1 );
        s_storage[idx].text[MB_ANNOT_TEXT_MAX - 1] = '\0';
    } else {
        s_storage[idx].text[0] = '\0';
    }
    return 0;
}


const st_MB_ANNOTATION *membrowser_annotations_find ( int region_kind,
                                                       int region_sub_id,
                                                       uint32_t addr )
{
    int idx = find_slot ( region_kind, region_sub_id, addr );
    return ( idx >= 0 ) ? &s_storage[idx] : NULL;
}


bool membrowser_annotations_remove ( int region_kind, int region_sub_id,
                                      uint32_t addr )
{
    int idx = find_slot ( region_kind, region_sub_id, addr );
    if ( idx < 0 ) return false;
    memset ( &s_storage[idx], 0, sizeof ( s_storage[idx] ) );
    return true;
}


int membrowser_annotations_count ( void )
{
    int n = 0;
    for ( int i = 0; i < MB_ANNOT_MAX_ENTRIES; i++ ) {
        if ( s_storage[i].used ) n++;
    }
    return n;
}


const st_MB_ANNOTATION *membrowser_annotations_at ( int idx )
{
    if ( idx < 0 ) return NULL;
    int seen = 0;
    for ( int i = 0; i < MB_ANNOT_MAX_ENTRIES; i++ ) {
        if ( !s_storage[i].used ) continue;
        if ( seen == idx ) return &s_storage[i];
        seen++;
    }
    return NULL;
}


void membrowser_annotations_clear_all ( void )
{
    memset ( s_storage, 0, sizeof ( s_storage ) );
}


/**
 * @brief Escape text pro persist: \n -> \\n, \t -> \\t, \\ -> \\\\.
 *
 * @return Délka zapsaná do out (bez \0); -1 pokud out nestačí.
 */
static int escape_text ( const char *src, char *out, size_t out_cap )
{
    size_t i = 0;
    for ( const char *p = src; *p != '\0'; p++ ) {
        if ( i + 2 >= out_cap ) return -1;
        if ( *p == '\\' ) { out[i++] = '\\'; out[i++] = '\\'; }
        else if ( *p == '\n' ) { out[i++] = '\\'; out[i++] = 'n'; }
        else if ( *p == '\t' ) { out[i++] = '\\'; out[i++] = 't'; }
        else { out[i++] = *p; }
    }
    out[i] = '\0';
    return ( int ) i;
}


/**
 * @brief Inverse escape: \\n -> \n, \\t -> \t, \\\\ -> \\.
 */
static void unescape_text ( const char *src, char *out, size_t out_cap )
{
    size_t i = 0;
    for ( const char *p = src; *p != '\0' && i + 1 < out_cap; p++ ) {
        if ( *p == '\\' && *( p + 1 ) != '\0' ) {
            char c = *( p + 1 );
            if ( c == 'n' ) { out[i++] = '\n'; p++; }
            else if ( c == 't' ) { out[i++] = '\t'; p++; }
            else if ( c == '\\' ) { out[i++] = '\\'; p++; }
            else { out[i++] = *p; }
        } else {
            out[i++] = *p;
        }
    }
    out[i] = '\0';
}


int membrowser_annotations_load_from_file ( const char *filename )
{
    if ( !filename ) return 0;
    FILE *f = fopen ( filename, "rb" );
    if ( !f ) return 0;

    int loaded = 0;
    char line[1024];
    while ( fgets ( line, sizeof ( line ), f ) ) {
        /* Skip empty / comment lines. */
        if ( line[0] == '#' || line[0] == '\n' || line[0] == '\r' ) continue;

        /* Parse "kind\tsub_id\taddr\tcolor\ttext". */
        int kind, sub_id;
        unsigned addr_u, color_u;
        char escaped[1024];
        int nf = sscanf ( line, "%d\t%d\t%x\t%x\t%1023[^\r\n]",
                          &kind, &sub_id, &addr_u, &color_u, escaped );
        if ( nf < 4 ) continue;  /* Bad line. */
        if ( nf == 4 ) escaped[0] = '\0';

        char decoded[MB_ANNOT_TEXT_MAX];
        unescape_text ( escaped, decoded, sizeof ( decoded ) );

        if ( membrowser_annotations_set ( kind, sub_id, addr_u,
                                            decoded, color_u ) == 0 ) {
            loaded++;
        }
    }
    fclose ( f );
    return loaded;
}


int membrowser_annotations_save_to_file ( const char *filename )
{
    if ( !filename ) return -1;
    FILE *f = fopen ( filename, "wb" );
    if ( !f ) return -1;

    fprintf ( f, "# Memory Browser annotations - auto-generated\n" );
    fprintf ( f, "# Format: kind<TAB>sub_id<TAB>addr_hex<TAB>color_rgba_hex<TAB>text\n" );
    fprintf ( f, "# Text escapes: \\\\n \\\\t \\\\\\\\\n" );

    int saved = 0;
    for ( int i = 0; i < MB_ANNOT_MAX_ENTRIES; i++ ) {
        if ( !s_storage[i].used ) continue;
        char escaped[1024];
        if ( escape_text ( s_storage[i].text, escaped, sizeof ( escaped ) ) < 0 ) {
            /* Příliš dlouhé po escape - skip. */
            continue;
        }
        fprintf ( f, "%d\t%d\t%X\t%08X\t%s\n",
                  s_storage[i].region_kind,
                  s_storage[i].region_sub_id,
                  ( unsigned ) s_storage[i].addr,
                  ( unsigned ) s_storage[i].color_rgba,
                  escaped );
        saved++;
    }
    fclose ( f );
    return 0;
}
