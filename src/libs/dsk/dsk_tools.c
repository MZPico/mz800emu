/**
 * @file   dsk_tools.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Implementace vyšších nástrojů pro Extended CPC DSK obrazy.
 *
 * Vytváření, modifikace, validace, identifikace formátu, iterace
 * přes stopy a sektory DSK diskových obrazů.
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
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "dsk.h"
#include "dsk_tools.h"
#include "../generic_driver/generic_driver.h"


/* Minimální velikost bloku pro I/O operace */
#define DSK_TOOLS_MIN_SECTOR_SIZE   128

/* Maximální délka jedné logovací zprávy */
#define DSK_LOG_BUF_SIZE            256


/* ========================================================================
 * Logování
 * ======================================================================== */

/* Globální logovací callback a user data */
static dsk_tools_log_cb_t s_log_cb = NULL;
static void *s_log_user_data = NULL;


void dsk_tools_set_log_cb ( dsk_tools_log_cb_t cb, void *user_data ) {
    s_log_cb = cb;
    s_log_user_data = user_data;
}


/* Interní pomocná funkce pro logování */
static void dsk_log ( int level, const char *fmt, ... ) {
    if ( s_log_cb == NULL ) return;

    char buf[DSK_LOG_BUF_SIZE];
    va_list ap;
    va_start ( ap, fmt );
    vsnprintf ( buf, sizeof ( buf ), fmt, ap );
    va_end ( ap );

    s_log_cb ( level, buf, s_log_user_data );
}


/* ========================================================================
 * Vytváření obrazů — popis geometrie
 * ======================================================================== */


/**
 * Přiřazení záznamu v existující struktuře st_DSK_DESCRIPTION.
 *
 * Záznamy musí být uloženy vzestupně podle absolute_track.
 *
 * @param dskdesc Odkaz na existující strukturu
 * @param rule Pořadové číslo záznamu
 * @param abs_track Absolutní stopa od které pravidlo platí
 * @param sectors Počet sektorů
 * @param ssize Kódovaná velikost sektoru
 * @param sector_order Typ řazení sektorů
 * @param sector_map Mapa sektorů (jen pro CUSTOM, jinak NULL)
 * @param default_value Filler byte
 */
void dsk_tools_assign_description ( st_DSK_DESCRIPTION *dskdesc, uint8_t rule, uint8_t abs_track, uint8_t sectors, en_DSK_SECTOR_SIZE ssize, en_DSK_SECTOR_ORDER_TYPE sector_order, uint8_t *sector_map, uint8_t default_value ) {
    if ( dskdesc == NULL || rule >= dskdesc->count_rules ) return;

    dskdesc->rules[rule].absolute_track = abs_track;
    dskdesc->rules[rule].sectors = sectors;
    dskdesc->rules[rule].ssize = ssize;
    dskdesc->rules[rule].sector_order = sector_order;
    dskdesc->rules[rule].sector_map = ( sector_order == DSK_SEC_ORDER_CUSTOM ) ? sector_map : NULL;
    dskdesc->rules[rule].filler = default_value;
}


/* ========================================================================
 * Interní pomocné funkce
 * ======================================================================== */


static int dsk_tools_create_tsizes ( uint8_t *tsize, st_DSK_DESCRIPTION *desc, uint8_t first_abs_track ) {

    if ( first_abs_track < desc->rules[0].absolute_track ) {
        return EXIT_FAILURE;
    }

    uint8_t sectors = desc->rules[0].sectors;
    en_DSK_SECTOR_SIZE ssize = desc->rules[0].ssize;
    uint8_t rule = 0;

    uint8_t abs_track = first_abs_track;
    uint8_t track;
    for ( track = ( first_abs_track / desc->sides ); track < desc->tracks; track++ ) {
        uint8_t side;
        for ( side = 0; side < desc->sides; side++ ) {
            if ( rule < desc->count_rules ) {
                if ( desc->rules[rule].absolute_track == abs_track ) {
                    sectors = desc->rules[rule].sectors;
                    ssize = desc->rules[rule].ssize;
                    rule++;
                }
            }
            tsize[abs_track] = dsk_encode_track_size ( sectors, ssize );
            if ( ( ssize == DSK_SECTOR_SIZE_128 ) && ( sectors & 1 ) ) {
                tsize[abs_track] += 1;
            }
            abs_track++;
        }
    }

    return EXIT_SUCCESS;
}


/* ========================================================================
 * Vytváření obrazů
 * ======================================================================== */


/**
 * Vytvoří DSK header podle description.
 *
 * @param h Handler
 * @param desc Popis geometrie disku
 * @return EXIT_FAILURE | EXIT_SUCCESS
 */
int dsk_tools_create_image_header ( st_HANDLER *h, st_DSK_DESCRIPTION *desc ) {

    if ( h == NULL || desc == NULL ) return EXIT_FAILURE;

    if ( ( !desc->count_rules ) || ( !desc->tracks ) ) {
        h->err = (en_HANDLER_ERROR) DSK_ERROR_NO_TRACKS;
        return EXIT_FAILURE;
    }

    st_DSK_HEADER dskhdr_buffer;
    st_DSK_HEADER *dskhdr = NULL;

    if ( EXIT_SUCCESS != generic_driver_prepare ( h, 0, (void**) &dskhdr, &dskhdr_buffer, sizeof ( st_DSK_HEADER ) ) ) return EXIT_FAILURE;

    memset ( dskhdr, 0x00, sizeof ( st_DSK_HEADER ) );
    memcpy ( dskhdr->file_info, DSK_DEFAULT_FILEINFO, DSK_FILEINFO_FIELD_LENGTH );
    memcpy ( dskhdr->creator, DSK_DEFAULT_CREATOR, DSK_CREATOR_FIELD_LENGTH );

    dskhdr->tracks = desc->tracks;
    dskhdr->sides = desc->sides;

    if ( EXIT_SUCCESS != dsk_tools_create_tsizes ( dskhdr->tsize, desc, 0 ) ) return EXIT_FAILURE;

    return generic_driver_ppwrite ( h, 0, dskhdr, sizeof ( st_DSK_HEADER ) );
}


/**
 * Vytvoření mapy sektorů podle definice sector order.
 *
 * @param sectors Počet sektorů
 * @param sector_order Typ řazení (CUSTOM se automaticky převede na NORMAL)
 * @param sector_map Výstupní pole o velikosti sectors
 */
void dsk_tools_make_sector_map ( uint8_t sectors, en_DSK_SECTOR_ORDER_TYPE sector_order, uint8_t *sector_map ) {

    if ( sectors == 0 || sector_map == NULL ) return;

    uint8_t sectors_to_order = ( sectors & 1 ) ? ( sectors + 1 ) : sectors;
    uint8_t sector_pos = 0;
    uint8_t i;

    if ( ( sector_order == DSK_SEC_ORDER_CUSTOM ) || ( sector_order > DSK_SEC_ORDER_INTERLACED_LEC_HD ) ) {
        sector_order = DSK_SEC_ORDER_NORMAL;
    }

    for ( i = 0; i < sectors; i += sector_order ) {
        uint8_t j = 0;
        while ( ( j < sector_order ) && ( ( i + j ) < sectors ) ) {
            uint8_t sector_id = 1 + ( i / sector_order ) + ( sectors_to_order / sector_order ) * ( j % sector_order );
            j++;
            sector_map[sector_pos] = sector_id;
            sector_pos++;
        }
    }
}


/**
 * Vytvoření hlavičky pro stopu.
 *
 * @param h Handler
 * @param dsk_offset Offset v souboru
 * @param track Číslo stopy
 * @param side Strana
 * @param sectors Počet sektorů
 * @param ssize Kódovaná velikost sektoru
 * @param sector_map Seznam ID jednotlivých sektorů
 * @return EXIT_FAILURE | EXIT_SUCCESS
 */
int dsk_tools_create_track_header ( st_HANDLER *h, uint32_t dsk_offset, uint8_t track, uint8_t side, uint8_t sectors, en_DSK_SECTOR_SIZE ssize, uint8_t *sector_map ) {

    st_DSK_TRACK_INFO trkhdr_buffer;
    st_DSK_TRACK_INFO *trkhdr = NULL;

    if ( EXIT_SUCCESS != generic_driver_prepare ( h, dsk_offset, (void**) &trkhdr, &trkhdr_buffer, sizeof ( st_DSK_TRACK_INFO ) ) ) return EXIT_FAILURE;

    memset ( trkhdr, 0x00, sizeof ( st_DSK_TRACK_INFO ) );
    memcpy ( trkhdr->track_info, DSK_DEFAULT_TRACKINFO, DSK_TRACKINFO_FIELD_LENGTH );
    trkhdr->track = track;
    trkhdr->side = side;
    trkhdr->sectors = sectors;
    trkhdr->ssize = ssize;

    int i;
    for ( i = 0; i < sectors; i++ ) {
        trkhdr->sinfo[i].track = track;
        trkhdr->sinfo[i].side = side;
        trkhdr->sinfo[i].sector = sector_map[i];
        trkhdr->sinfo[i].ssize = ssize;
    }

    return generic_driver_ppwrite ( h, dsk_offset, trkhdr, sizeof ( st_DSK_TRACK_INFO ) );
}


/**
 * Vyplní všechny sektory na stopě výchozí hodnotou.
 *
 * @param h Handler
 * @param dsk_offset Offset za hlavičkou stopy
 * @param sectors Počet sektorů
 * @param ssize Kódovaná velikost sektoru
 * @param default_value Filler byte
 * @param sectors_total_bytes Výstup: celková velikost zapsaných dat
 * @return EXIT_FAILURE | EXIT_SUCCESS
 */
int dsk_tools_create_track_sectors ( st_HANDLER *h, uint32_t dsk_offset, uint8_t sectors, en_DSK_SECTOR_SIZE ssize, uint8_t default_value, uint16_t *sectors_total_bytes ) {

    *sectors_total_bytes = 0;

    int my_ssize = dsk_decode_sector_size ( ssize ) / DSK_TOOLS_MIN_SECTOR_SIZE;

    uint8_t i;
    for ( i = 0; i < sectors; i++ ) {
        int j;
        for ( j = 0; j < my_ssize; j++ ) {

            uint8_t data_buffer [ DSK_TOOLS_MIN_SECTOR_SIZE ];
            uint8_t *sector_data = NULL;

            if ( EXIT_SUCCESS != generic_driver_prepare ( h, dsk_offset, (void**) &sector_data, &data_buffer, DSK_TOOLS_MIN_SECTOR_SIZE ) ) return EXIT_FAILURE;

            memset ( sector_data, default_value, DSK_TOOLS_MIN_SECTOR_SIZE );

            generic_driver_ppwrite ( h, dsk_offset, sector_data, DSK_TOOLS_MIN_SECTOR_SIZE );

            *sectors_total_bytes += DSK_TOOLS_MIN_SECTOR_SIZE;
            dsk_offset += DSK_TOOLS_MIN_SECTOR_SIZE;
        }
    }

    return EXIT_SUCCESS;
}


/**
 * Vytvoření jedné DSK stopy.
 *
 * @param h Handler
 * @param dsk_offset Offset v souboru
 * @param track Číslo stopy
 * @param side Strana
 * @param sectors Počet sektorů
 * @param ssize Kódovaná velikost sektoru
 * @param sector_map Seznam ID sektorů
 * @param default_value Filler byte
 * @param track_total_bytes Výstup: celková velikost zapsané stopy
 * @return EXIT_FAILURE | EXIT_SUCCESS
 */
int dsk_tools_create_track ( st_HANDLER *h, uint32_t dsk_offset, uint8_t track, uint8_t side, uint8_t sectors, en_DSK_SECTOR_SIZE ssize, uint8_t *sector_map, uint8_t default_value, uint32_t *track_total_bytes ) {

    *track_total_bytes = 0;

    if ( sectors != 0 ) {
        if ( EXIT_SUCCESS != dsk_tools_create_track_header ( h, dsk_offset, track, side, sectors, ssize, sector_map ) ) return EXIT_FAILURE;

        *track_total_bytes += sizeof ( st_DSK_TRACK_INFO );
        dsk_offset += sizeof ( st_DSK_TRACK_INFO );

        uint16_t sectors_total_size = 0;
        if ( EXIT_SUCCESS != dsk_tools_create_track_sectors ( h, dsk_offset, sectors, ssize, default_value, &sectors_total_size ) ) return EXIT_FAILURE;

        /* Padding pro 128B sektory s lichým počtem (zarovnání na 256B) */
        if ( ( ssize == DSK_SECTOR_SIZE_128 ) && ( sectors & 1 ) ) {

            uint8_t data_buffer [ DSK_TOOLS_MIN_SECTOR_SIZE ];
            uint8_t *sector_data = NULL;

            uint32_t zero_filling_offset = dsk_offset + sectors_total_size;

            if ( EXIT_SUCCESS != generic_driver_prepare ( h, zero_filling_offset, (void**) &sector_data, &data_buffer, DSK_TOOLS_MIN_SECTOR_SIZE ) ) return EXIT_FAILURE;

            memset ( sector_data, 0x00, DSK_TOOLS_MIN_SECTOR_SIZE );

            generic_driver_ppwrite ( h, zero_filling_offset, sector_data, DSK_TOOLS_MIN_SECTOR_SIZE );

            sectors_total_size += DSK_TOOLS_MIN_SECTOR_SIZE;
        }

        *track_total_bytes += sectors_total_size;
    }

    return EXIT_SUCCESS;
}


/**
 * Vytvoří postupně všechny stopy podle description.
 *
 * @param h Handler
 * @param desc Popis geometrie
 * @param first_abs_track První absolutní stopa
 * @param dsk_offset Offset v souboru (0 = sizeof(st_DSK_HEADER))
 * @return EXIT_FAILURE | EXIT_SUCCESS
 */
int dsk_tools_create_image_tracks ( st_HANDLER *h, st_DSK_DESCRIPTION *desc, uint8_t first_abs_track, uint32_t dsk_offset ) {

    if ( dsk_offset == 0 ) dsk_offset = sizeof ( st_DSK_HEADER );

    if ( first_abs_track < desc->rules[0].absolute_track ) {
        return EXIT_FAILURE;
    }

    uint8_t sectors = desc->rules[0].sectors;
    en_DSK_SECTOR_SIZE ssize = desc->rules[0].ssize;
    en_DSK_SECTOR_ORDER_TYPE sector_order = desc->rules[0].sector_order;
    uint8_t default_value = desc->rules[0].filler;
    uint8_t rule = 0;

    uint8_t abs_track = first_abs_track;
    uint8_t track;

    en_DSK_SECTOR_ORDER_TYPE last_sector_order = sector_order;
    uint8_t local_sector_map [ DSK_MAX_SECTORS ];
    uint8_t *sector_map;

    if ( ( sector_order == DSK_SEC_ORDER_CUSTOM ) && ( desc->rules[0].sector_map != NULL ) ) {
        sector_map = desc->rules[0].sector_map;
    } else {
        dsk_tools_make_sector_map ( sectors, sector_order, local_sector_map );
        sector_map = local_sector_map;
    }

    for ( track = ( first_abs_track / desc->sides ); track < desc->tracks; track++ ) {
        uint8_t side;
        for ( side = 0; side < desc->sides; side++ ) {
            if ( rule < desc->count_rules ) {
                if ( desc->rules[rule].absolute_track == abs_track ) {
                    sectors = desc->rules[rule].sectors;
                    ssize = desc->rules[rule].ssize;
                    sector_order = desc->rules[rule].sector_order;
                    default_value = desc->rules[rule].filler;

                    if ( ( sector_order == DSK_SEC_ORDER_CUSTOM ) && ( desc->rules[rule].sector_map != NULL ) ) {
                        sector_map = desc->rules[rule].sector_map;
                    } else {
                        if ( sector_order != last_sector_order ) {
                            dsk_tools_make_sector_map ( sectors, sector_order, local_sector_map );
                            sector_map = local_sector_map;
                        }
                    }
                    last_sector_order = sector_order;

                    rule++;
                }
            }

            /* vytvoření stopy */
            uint32_t track_total_bytes = 0;

            if ( EXIT_SUCCESS != dsk_tools_create_track ( h, dsk_offset, track, side, sectors, ssize, sector_map, default_value, &track_total_bytes ) ) return EXIT_FAILURE;
            dsk_offset += track_total_bytes;

            abs_track++;
        }
    }

    return EXIT_SUCCESS;
}


/**
 * Vytvoření DSK podle popisu v desc.
 *
 * @param h Handler
 * @param desc Popis geometrie disku
 * @return EXIT_FAILURE | EXIT_SUCCESS
 */
int dsk_tools_create_image ( st_HANDLER *h, st_DSK_DESCRIPTION *desc ) {
    if ( h == NULL || desc == NULL ) return EXIT_FAILURE;
    if ( EXIT_SUCCESS != dsk_tools_create_image_header ( h, desc ) ) return EXIT_FAILURE;
    return dsk_tools_create_image_tracks ( h, desc, 0, 0 );
}


/* ========================================================================
 * Modifikace existujícího obrazu
 * ======================================================================== */


/**
 * Změna parametrů a default obsahu konkrétní absolutní stopy.
 *
 * Pokud se změní velikost stopy, přesune data následujících stop.
 *
 * @param h Handler
 * @param short_image_info Informace o obrazu (NULL = načte se)
 * @param abstrack Absolutní stopa
 * @param sectors Nový počet sektorů
 * @param ssize Nová velikost sektoru
 * @param sector_map Nová mapa sektorů
 * @param default_value Filler byte
 * @return EXIT_FAILURE | EXIT_SUCCESS
 */
int dsk_tools_change_track ( st_HANDLER *h, st_DSK_SHORT_IMAGE_INFO *short_image_info, uint8_t abstrack, uint8_t sectors, en_DSK_SECTOR_SIZE ssize, uint8_t *sector_map, uint8_t default_value ) {

    st_DSK_SHORT_IMAGE_INFO local_short_image_info;
    st_DSK_SHORT_IMAGE_INFO *iinfo = short_image_info;

    if ( iinfo == NULL ) {
        if ( EXIT_SUCCESS != dsk_read_short_image_info ( h, &local_short_image_info ) ) return EXIT_FAILURE;
        iinfo = &local_short_image_info;
    }

    if ( abstrack >= ( iinfo->tracks * iinfo->sides ) ) {
        h->err = (en_HANDLER_ERROR) DSK_ERROR_TRACK_NOT_FOUND;
        return EXIT_FAILURE;
    }

    uint32_t track_offset = dsk_compute_track_offset ( abstrack, iinfo->tsize );
    uint16_t track_size = dsk_decode_track_size ( iinfo->tsize[abstrack] );

    uint16_t new_track_size = dsk_decode_track_size ( dsk_encode_track_size ( sectors, ssize ) );

    uint8_t last_track = ( iinfo->tracks * iinfo->sides ) - 1;
    uint32_t last_track_offset = dsk_compute_track_offset ( last_track, iinfo->tsize );
    uint16_t last_track_size = dsk_decode_track_size ( iinfo->tsize[last_track] );

    uint32_t last_image_byte = last_track_offset + last_track_size;

    if ( track_size != new_track_size ) {

        uint8_t buffer [ DSK_TOOLS_MIN_SECTOR_SIZE ];

        uint32_t next_track_offset = track_offset + track_size;
        uint32_t bytes_to_move = last_image_byte - next_track_offset;
        uint32_t chunks = bytes_to_move / sizeof ( buffer );

        uint32_t src_offset;
        uint32_t dst_offset;
        int32_t step;

        if ( track_size < new_track_size ) {
            /* Stopa se zvětšuje — přesouvat odzadu */
            uint16_t size_difference = new_track_size - track_size;
            src_offset = last_image_byte - sizeof ( buffer );
            dst_offset = src_offset + size_difference;
            step = -(int32_t) sizeof ( buffer );
        } else {
            /* Stopa se zmenšuje — přesouvat odpředu */
            uint16_t size_difference = track_size - new_track_size;
            src_offset = next_track_offset;
            dst_offset = src_offset - size_difference;
            step = (int32_t) sizeof ( buffer );
        }

        uint32_t chunk;
        for ( chunk = 0; chunk < chunks; chunk++ ) {
            if ( EXIT_SUCCESS != dsk_read_on_offset ( h, src_offset, &buffer, sizeof ( buffer ) ) ) return EXIT_FAILURE;
            if ( EXIT_SUCCESS != dsk_write_on_offset ( h, dst_offset, &buffer, sizeof ( buffer ) ) ) return EXIT_FAILURE;
            src_offset += step;
            dst_offset += step;
        }

        if ( track_size > new_track_size ) {
            uint32_t new_last_image_byte = last_image_byte - ( track_size - new_track_size );
            if ( EXIT_SUCCESS != generic_driver_truncate ( h, new_last_image_byte ) ) return EXIT_FAILURE;
        }

        iinfo->tsize[abstrack] = dsk_encode_track_size ( sectors, ssize );
        uint32_t offset = DSK_FILEINFO_FIELD_LENGTH + DSK_CREATOR_FIELD_LENGTH + 4 + abstrack;
        if ( EXIT_SUCCESS != dsk_write_on_offset ( h, offset, &iinfo->tsize[abstrack], 1 ) ) return EXIT_FAILURE;
    }

    uint8_t side = ( iinfo->sides == 1 ) ? 0 : ( abstrack & 1 );
    uint8_t track = abstrack / iinfo->sides;

    if ( sectors != 0 ) {
        if ( EXIT_SUCCESS != dsk_tools_create_track_header ( h, track_offset, track, side, sectors, ssize, sector_map ) ) return EXIT_FAILURE;

        uint16_t sectors_total_bytes;
        if ( EXIT_SUCCESS != dsk_tools_create_track_sectors ( h, track_offset + sizeof ( st_DSK_TRACK_INFO ), sectors, ssize, default_value, &sectors_total_bytes ) ) return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


int dsk_tools_add_tracks ( st_HANDLER *h, st_DSK_DESCRIPTION *desc ) {

    st_DSK_HEADER dskhdr_buffer;
    st_DSK_HEADER *dskhdr = NULL;

    if ( EXIT_SUCCESS != generic_driver_prepare ( h, 0, (void**) &dskhdr, &dskhdr_buffer, sizeof ( st_DSK_HEADER ) ) ) return EXIT_FAILURE;

    if ( EXIT_SUCCESS != generic_driver_ppread ( h, 0, dskhdr, sizeof ( st_DSK_HEADER ) ) ) return EXIT_FAILURE;

    dskhdr->tracks = desc->tracks;

    uint8_t first_abs_track = desc->rules[0].absolute_track;

    if ( EXIT_SUCCESS != dsk_tools_create_tsizes ( dskhdr->tsize, desc, first_abs_track ) ) return EXIT_FAILURE;

    if ( EXIT_SUCCESS != generic_driver_ppwrite ( h, 0, dskhdr, sizeof ( st_DSK_HEADER ) ) ) return EXIT_FAILURE;

    st_DSK_SHORT_IMAGE_INFO local_short_image_info;
    st_DSK_SHORT_IMAGE_INFO *iinfo;

    if ( EXIT_SUCCESS != dsk_read_short_image_info ( h, &local_short_image_info ) ) return EXIT_FAILURE;
    iinfo = &local_short_image_info;

    uint32_t track_offset = dsk_compute_track_offset ( first_abs_track, iinfo->tsize );

    return dsk_tools_create_image_tracks ( h, desc, first_abs_track, track_offset );
}


/**
 * Zmenší obraz odstraněním stop od konce.
 *
 * @param h Handler
 * @param short_image_info Informace o obrazu (NULL = načte se)
 * @param total_tracks Nový celkový počet absolutních stop
 * @return EXIT_FAILURE | EXIT_SUCCESS
 */
int dsk_tools_shrink_image ( st_HANDLER *h, st_DSK_SHORT_IMAGE_INFO *short_image_info, uint8_t total_tracks ) {

    st_DSK_SHORT_IMAGE_INFO local_short_image_info;
    st_DSK_SHORT_IMAGE_INFO *iinfo = short_image_info;
    st_DRIVER *d = h->driver;

    if ( iinfo == NULL ) {
        if ( EXIT_SUCCESS != dsk_read_short_image_info ( h, &local_short_image_info ) ) return EXIT_FAILURE;
        iinfo = &local_short_image_info;
    }

    if ( total_tracks == 0 ) {
        h->err = (en_HANDLER_ERROR) DSK_ERROR_NO_TRACKS;
        return EXIT_FAILURE;
    }

    if ( total_tracks >= ( iinfo->tracks * iinfo->sides ) ) {
        h->err = (en_HANDLER_ERROR) DSK_ERROR_TRACK_NOT_FOUND;
        return EXIT_FAILURE;
    }

    if ( ( iinfo->sides == 2 ) && ( total_tracks & 1 ) ) {
        h->err = (en_HANDLER_ERROR) DSK_ERROR_DOUBLE_SIDED;
        return EXIT_FAILURE;
    }

    uint32_t track_offset = dsk_compute_track_offset ( total_tracks, iinfo->tsize );

    /* Kontrola existence truncate callbacku PŘED voláním */
    if ( d->truncate_cb == NULL ) {
        d->err = GENERIC_DRIVER_ERROR_CB_NOT_EXIST;
        return EXIT_FAILURE;
    }

    if ( EXIT_SUCCESS != d->truncate_cb ( h, track_offset ) ) {
        return EXIT_FAILURE;
    }

    st_DSK_HEADER dskhdr_buffer;
    st_DSK_HEADER *dskhdr = NULL;

    if ( EXIT_SUCCESS != generic_driver_prepare ( h, 0, (void**) &dskhdr, &dskhdr_buffer, sizeof ( st_DSK_HEADER ) ) ) return EXIT_FAILURE;

    if ( EXIT_SUCCESS != generic_driver_ppread ( h, 0, dskhdr, sizeof ( st_DSK_HEADER ) ) ) return EXIT_FAILURE;

    dskhdr->tracks = total_tracks / dskhdr->sides;

    memset ( &dskhdr->tsize[total_tracks], 0x00, DSK_MAX_TOTAL_TRACKS - total_tracks );

    return generic_driver_ppwrite ( h, 0, dskhdr, sizeof ( st_DSK_HEADER ) );
}


/* ========================================================================
 * Validace a inspekce
 * ======================================================================== */


int dsk_tools_get_dsk_fileinfo ( st_HANDLER *h, uint8_t *dsk_fileinfo_buffer ) {
    uint32_t offset = 0;
    return dsk_read_on_offset ( h, offset, dsk_fileinfo_buffer, DSK_FILEINFO_FIELD_LENGTH );
}


int dsk_tools_check_dsk_fileinfo ( st_HANDLER *h ) {
    uint8_t dsk_fileinfo_buffer[DSK_FILEINFO_FIELD_LENGTH + 1];
    if ( EXIT_FAILURE == dsk_tools_get_dsk_fileinfo ( h, dsk_fileinfo_buffer ) ) return EXIT_FAILURE;
    dsk_fileinfo_buffer[DSK_FILEINFO_FIELD_LENGTH] = 0x00;
    if ( 0 != strcmp ( (char*) dsk_fileinfo_buffer, DSK_DEFAULT_FILEINFO ) ) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}


int dsk_tools_get_dsk_creator ( st_HANDLER *h, uint8_t *dsk_creator_buffer ) {
    uint32_t offset = DSK_FILEINFO_FIELD_LENGTH;
    return dsk_read_on_offset ( h, offset, dsk_creator_buffer, DSK_CREATOR_FIELD_LENGTH );
}


en_DSK_TOOLS_CHCKTRKINFO dsk_tools_check_dsk_trackinfo_on_offset ( st_HANDLER *h, uint32_t offset ) {
    uint8_t dsk_trackinfo_buffer[DSK_TRACKINFO_FIELD_LENGTH + 1];
    if ( EXIT_FAILURE == dsk_read_on_offset ( h, offset, dsk_trackinfo_buffer, DSK_TRACKINFO_FIELD_LENGTH ) ) return DSK_TOOLS_CHCKTRKINFO_READ_ERROR;
    dsk_trackinfo_buffer[DSK_TRACKINFO_FIELD_LENGTH] = 0x00;
    if ( 0 != strcmp ( (char*) dsk_trackinfo_buffer, DSK_DEFAULT_TRACKINFO ) ) return DSK_TOOLS_CHCKTRKINFO_FAILURE;
    return DSK_TOOLS_CHCKTRKINFO_SUCCESS;
}


/**
 * Validace DSK obrazu s volitelným autofixem.
 *
 * @param h Handler
 * @param print_info Nenulová = logovat detaily (přes logovací callback)
 * @param dsk_autofix Nenulová = opravit nalezené chyby
 * @return EXIT_FAILURE | EXIT_SUCCESS
 */
int dsk_tools_check_dsk ( st_HANDLER *h, int print_info, int dsk_autofix ) {

    if ( dsk_autofix != 0 ) {
        dsk_log ( DSK_LOG_INFO, "Checking DSK format (in autofix mode) ... " );
    } else {
        dsk_log ( DSK_LOG_INFO, "Checking DSK format ... " );
    }

    if ( EXIT_FAILURE == dsk_tools_check_dsk_fileinfo ( h ) ) {
        dsk_log ( DSK_LOG_ERROR, "DSK file info check failed" );
        return EXIT_FAILURE;
    } else {
        if ( print_info ) dsk_log ( DSK_LOG_INFO, "DSK fileinfo: OK" );
    }

    uint8_t dsk_creator_buffer[DSK_CREATOR_FIELD_LENGTH + 1];
    if ( EXIT_FAILURE == dsk_tools_get_dsk_creator ( h, dsk_creator_buffer ) ) {
        dsk_log ( DSK_LOG_ERROR, "can't get DSK creator info" );
        return EXIT_FAILURE;
    } else {
        dsk_creator_buffer[DSK_CREATOR_FIELD_LENGTH] = 0x00;
        if ( print_info ) {
            /* Extrahovat tisknutelnou část creator stringu */
            char creator_str[DSK_CREATOR_FIELD_LENGTH + 1];
            int ci = 0;
            while ( ci < DSK_CREATOR_FIELD_LENGTH && dsk_creator_buffer[ci] >= 0x20 ) {
                creator_str[ci] = dsk_creator_buffer[ci];
                ci++;
            }
            creator_str[ci] = '\0';
            dsk_log ( DSK_LOG_INFO, "DSK creator: %s", creator_str );
        }
    }

    st_DSK_SHORT_IMAGE_INFO sh_img_info;
    if ( EXIT_FAILURE == dsk_read_short_image_info ( h, &sh_img_info ) ) {
        dsk_log ( DSK_LOG_ERROR, "can't get DSK image info" );
        return EXIT_FAILURE;
    }

    if ( print_info ) {
        dsk_log ( DSK_LOG_INFO, "DSK header sides: %d", sh_img_info.sides );
        dsk_log ( DSK_LOG_INFO, "DSK header tracks: %d", sh_img_info.tracks );
        dsk_log ( DSK_LOG_INFO, "Analyzing tracks ... " );
    }

    uint8_t tsize [ DSK_MAX_TOTAL_TRACKS ];
    memset ( &tsize, 0x00, sizeof ( tsize ) );

    uint8_t expected_track = 0;
    uint8_t expected_side = 0;
    uint8_t abs_tracks = 0;
    uint8_t expected_total_tracks = sh_img_info.tracks * sh_img_info.sides;

    uint32_t offset = sizeof ( st_DSK_HEADER );

    while ( abs_tracks < expected_total_tracks ) {

        en_DSK_TOOLS_CHCKTRKINFO res = dsk_tools_check_dsk_trackinfo_on_offset ( h, offset );

        if ( res == DSK_TOOLS_CHCKTRKINFO_READ_ERROR ) break;

        if ( res == DSK_TOOLS_CHCKTRKINFO_FAILURE ) {
            dsk_log ( DSK_LOG_ERROR, "expected track info on 0x%08x, abstrack: %d", offset, abs_tracks );
            return EXIT_FAILURE;
        }

        st_DSK_SHORT_TRACK_INFO sh_trk_info;
        if ( EXIT_FAILURE == dsk_read_short_track_info_on_offset ( h, offset, &sh_trk_info ) ) {
            dsk_log ( DSK_LOG_ERROR, "can't get track info on 0x%08x, abstrack: %d", offset, abs_tracks );
            return EXIT_FAILURE;
        }

        if ( sh_trk_info.track != expected_track ) {
            dsk_log ( DSK_LOG_ERROR, "bad track '%d' (expected %d) on 0x%08x, abstrack: %d", sh_trk_info.track, expected_track, offset, abs_tracks );
            return EXIT_FAILURE;
        }

        if ( sh_trk_info.side != expected_side ) {
            dsk_log ( DSK_LOG_ERROR, "bad side '%d' (expected %d) on 0x%08x, abstrack: %d", sh_trk_info.side, expected_side, offset, abs_tracks );
            return EXIT_FAILURE;
        }

        if ( sh_img_info.sides == 1 ) {
            expected_track++;
        } else {
            expected_side = ( ~expected_side ) & 1;
            if ( sh_trk_info.side == 1 ) {
                expected_track++;
            }
        }

        if ( ( sh_trk_info.sectors < 1 ) || ( sh_trk_info.sectors >= DSK_MAX_SECTORS ) ) {
            dsk_log ( DSK_LOG_ERROR, "bad sectors count '%d' on 0x%08x, abstrack: %d", sh_trk_info.sectors, offset, abs_tracks );
            return EXIT_FAILURE;
        }

        if ( sh_trk_info.ssize > DSK_SECTOR_SIZE_1024 ) {
            dsk_log ( DSK_LOG_ERROR, "bad ssize '0x%02x' on 0x%08x, abstrack: %d", sh_trk_info.ssize, offset, abs_tracks );
            return EXIT_FAILURE;
        }

        uint16_t sectors_size = dsk_decode_sector_size ( sh_trk_info.ssize ) * sh_trk_info.sectors;

        if ( ( sh_trk_info.ssize == DSK_SECTOR_SIZE_128 ) && ( sh_trk_info.sectors & 1 ) ) {
            sectors_size += 0x80;
        }

        uint8_t sector_buffer[0x80];

        uint16_t read_offset;
        for ( read_offset = 0; read_offset < sectors_size; read_offset += sizeof ( sector_buffer ) ) {
            if ( EXIT_FAILURE == dsk_read_on_offset ( h, offset + sizeof ( st_DSK_TRACK_INFO ) + read_offset, sector_buffer, sizeof ( sector_buffer ) ) ) {
                dsk_log ( DSK_LOG_ERROR, "error when reading 0x%04x from track on 0x%04x, abstrack: %d", (unsigned) ( read_offset + sizeof ( st_DSK_TRACK_INFO ) ), (unsigned) offset, abs_tracks );
                return EXIT_FAILURE;
            }
        }

        uint16_t track_size = sectors_size + sizeof ( st_DSK_TRACK_INFO );
        tsize[abs_tracks++] = track_size / 0x0100;
        offset += track_size;
    }

    if ( print_info ) dsk_log ( DSK_LOG_INFO, "total tracks: %d", abs_tracks );

    int errors = 0;

    if ( abs_tracks != sh_img_info.tracks * sh_img_info.sides ) {
        dsk_log ( DSK_LOG_WARNING, "DSK BUG: Bad tracks info in DSK header." );
        errors++;
    }

    if ( ( sh_img_info.sides == 2 ) && ( abs_tracks & 1 ) ) {
        dsk_log ( DSK_LOG_WARNING, "DSK BUG: The disc is double-sided, but the count of tracks is odd." );
        errors++;
    }

    int tsize_differences = 0;

    int i;
    for ( i = 0; i < abs_tracks; i++ ) {
        if ( tsize[i] != sh_img_info.tsize[i] ) {
            tsize_differences++;
        }
    }

    if ( tsize_differences != 0 ) {
        dsk_log ( DSK_LOG_WARNING, "DSK BUG: Some tracks have bad size info in DSK header." );
        errors++;
    }

    if ( errors ) {
        if ( dsk_autofix ) {

            st_DSK_HEADER dhdr;

            if ( dsk_read_on_offset ( h, 0, &dhdr, sizeof ( dhdr ) ) ) {
                dsk_log ( DSK_LOG_ERROR, "can't get DSK image info for autofix" );
                return EXIT_FAILURE;
            }

            dhdr.tracks = abs_tracks / sh_img_info.sides;
            memcpy ( dhdr.tsize, tsize, sizeof ( dhdr.tsize ) );

            if ( dsk_write_on_offset ( h, 0, &dhdr, sizeof ( dhdr ) ) ) {
                dsk_log ( DSK_LOG_ERROR, "can't write DSK image info for autofix" );
                return EXIT_FAILURE;
            }

            dsk_log ( DSK_LOG_INFO, "Result: %d error(s) repaired. DSK is OK!", errors );

        } else {
            dsk_log ( DSK_LOG_WARNING, "Result: this DSK has %d repairable error(s).", errors );
            return EXIT_FAILURE;
        }
    } else {
        dsk_log ( DSK_LOG_INFO, "Result: DSK is OK!" );
    }

    return EXIT_SUCCESS;
}


/* ========================================================================
 * Analýza geometrie (pravidla stop)
 * ======================================================================== */


void dsk_tools_destroy_track_rules ( st_DSK_TOOLS_TRACKS_RULES_INFO *tracks_rules ) {
    if ( tracks_rules == NULL ) return;
    if ( ( tracks_rules->count_rules != 0 ) && ( tracks_rules->rule != NULL ) ) {
        free ( tracks_rules->rule );
    }
    free ( tracks_rules );
}


/**
 * Analyzuje geometrii DSK obrazu a extrahuje pravidla stop.
 *
 * @param h Handler
 * @return Alokovaná struktura (uvolnit přes dsk_tools_destroy_track_rules),
 *         nebo NULL při chybě
 */
st_DSK_TOOLS_TRACKS_RULES_INFO * dsk_tools_get_tracks_rules ( st_HANDLER * h ) {

    st_DSK_SHORT_IMAGE_INFO sh_img_info;
    if ( EXIT_FAILURE == dsk_read_short_image_info ( h, &sh_img_info ) ) {
        dsk_log ( DSK_LOG_ERROR, "can't get DSK image info" );
        return NULL;
    }

    st_DSK_TOOLS_TRACKS_RULES_INFO *tracks_rules = malloc ( sizeof ( st_DSK_TOOLS_TRACKS_RULES_INFO ) );
    if ( tracks_rules == NULL ) return NULL;

    tracks_rules->total_tracks = sh_img_info.tracks * sh_img_info.sides;
    tracks_rules->sides = sh_img_info.sides;
    tracks_rules->count_rules = 0;
    tracks_rules->rule = NULL;
    tracks_rules->mzboot_track = 0;

    int last_rule = -1;

    int track;
    for ( track = 0; track < tracks_rules->total_tracks; track++ ) {

        st_DSK_SHORT_TRACK_INFO sh_trk_info;

        if ( EXIT_FAILURE == dsk_read_short_track_info ( h, &sh_img_info, track, &sh_trk_info ) ) {
            dsk_log ( DSK_LOG_ERROR, "can't get DSK track info for track %d", track );
            dsk_tools_destroy_track_rules ( tracks_rules );
            return NULL;
        }

        if ( ( tracks_rules->count_rules == 0 ) || ( sh_trk_info.sectors != tracks_rules->rule[last_rule].sectors ) || ( sh_trk_info.ssize != tracks_rules->rule[last_rule].ssize ) ) {
            st_DSK_TOOLS_TRACK_RULE_INFO *new_rule = realloc ( tracks_rules->rule, ( tracks_rules->count_rules + 1 ) * sizeof ( st_DSK_TOOLS_TRACK_RULE_INFO ) );
            if ( new_rule == NULL ) {
                dsk_tools_destroy_track_rules ( tracks_rules );
                return NULL;
            }
            tracks_rules->rule = new_rule;
            tracks_rules->rule[tracks_rules->count_rules].from_track = track;
            tracks_rules->rule[tracks_rules->count_rules].count_tracks = 1;
            tracks_rules->rule[tracks_rules->count_rules].sectors = sh_trk_info.sectors;
            tracks_rules->rule[tracks_rules->count_rules].ssize = sh_trk_info.ssize;
            tracks_rules->count_rules++;
            last_rule++;
        } else {
            tracks_rules->rule[last_rule].count_tracks++;
        }

        if ( ( track == 1 ) && ( sh_trk_info.sectors == 16 ) && ( sh_trk_info.ssize == DSK_SECTOR_SIZE_256 ) ) {
            tracks_rules->mzboot_track = 1;
        }
    }

    return tracks_rules;
}


/* ========================================================================
 * Identifikace formátu
 * ======================================================================== */


en_DSK_TOOLS_IDENTFORMAT dsk_tools_identformat_from_tracks_rules ( const st_DSK_TOOLS_TRACKS_RULES_INFO *tracks_rules ) {

    if ( ( tracks_rules == NULL ) || ( tracks_rules->mzboot_track != 1 ) ) return DSK_TOOLS_IDENTFORMAT_UNKNOWN;

    if ( ( tracks_rules->count_rules == 1 ) && ( tracks_rules->rule[0].sectors == 16 ) && ( tracks_rules->rule[0].ssize == DSK_SECTOR_SIZE_256 ) ) {
        return DSK_TOOLS_IDENTFORMAT_MZBASIC;
    } else if ( ( tracks_rules->count_rules == 3 ) && ( tracks_rules->rule[0].sectors == tracks_rules->rule[2].sectors ) && ( tracks_rules->rule[0].ssize == tracks_rules->rule[2].ssize ) ) {
        if ( ( tracks_rules->rule[0].sectors == 9 ) && ( tracks_rules->rule[0].ssize == DSK_SECTOR_SIZE_512 ) ) {
            return DSK_TOOLS_IDENTFORMAT_MZCPM;
        } else if ( ( tracks_rules->rule[0].sectors == 18 ) && ( tracks_rules->rule[0].ssize == DSK_SECTOR_SIZE_512 ) ) {
            return DSK_TOOLS_IDENTFORMAT_MZCPMHD;
        }
    }

    return DSK_TOOLS_IDENTFORMAT_MZBOOT;
}


/**
 * Identifikuje formát DSK obrazu.
 *
 * @param h Handler
 * @param result Výstup: identifikovaný formát
 * @return EXIT_FAILURE | EXIT_SUCCESS
 */
int dsk_tools_identformat ( st_HANDLER *h, en_DSK_TOOLS_IDENTFORMAT *result ) {

    if ( result == NULL ) return EXIT_FAILURE;

    st_DSK_TOOLS_TRACKS_RULES_INFO *tracks_rules = dsk_tools_get_tracks_rules ( h );
    if ( tracks_rules == NULL ) {
        *result = DSK_TOOLS_IDENTFORMAT_UNKNOWN;
        return EXIT_FAILURE;
    }

    *result = dsk_tools_identformat_from_tracks_rules ( tracks_rules );
    dsk_tools_destroy_track_rules ( tracks_rules );
    return EXIT_SUCCESS;
}


st_DSK_TOOLS_TRACK_RULE_INFO* dsk_tools_get_rule_for_track ( const st_DSK_TOOLS_TRACKS_RULES_INFO *tracks_rules, uint8_t track ) {
    if ( tracks_rules == NULL || tracks_rules->count_rules == 0 ) return NULL;

    int i = tracks_rules->count_rules - 1;
    st_DSK_TOOLS_TRACK_RULE_INFO *rule = NULL;
    while ( i >= 0 ) {
        rule = &tracks_rules->rule[i--];
        if ( track >= rule->from_track ) break;
    }
    return rule;
}


/* ========================================================================
 * Iterace přes stopy a sektory
 * ======================================================================== */


/**
 * Iteruje přes všechny stopy v DSK obrazu a volá callback pro každou.
 *
 * @param h Handler
 * @param cb Callback volaný pro každou stopu
 * @param user_data Uživatelská data předávaná callbacku
 * @return EXIT_SUCCESS, EXIT_FAILURE (chyba I/O), nebo nenulová návratová
 *         hodnota z callbacku (předčasné ukončení)
 */
int dsk_for_each_track ( st_HANDLER *h, dsk_track_callback_t cb, void *user_data ) {

    if ( h == NULL || cb == NULL ) return EXIT_FAILURE;

    st_DSK_SHORT_IMAGE_INFO iinfo;
    if ( EXIT_SUCCESS != dsk_read_short_image_info ( h, &iinfo ) ) return EXIT_FAILURE;

    uint8_t total_tracks = iinfo.tracks * iinfo.sides;
    uint8_t abstrack;

    for ( abstrack = 0; abstrack < total_tracks; abstrack++ ) {
        st_DSK_SHORT_TRACK_INFO tinfo;
        if ( EXIT_SUCCESS != dsk_read_short_track_info ( h, &iinfo, abstrack, &tinfo ) ) return EXIT_FAILURE;

        int ret = cb ( h, abstrack, &tinfo, user_data );
        if ( ret != 0 ) return ret;
    }

    return EXIT_SUCCESS;
}


/**
 * Iteruje přes všechny sektory na zadané stopě a volá callback pro každý.
 *
 * @param h Handler
 * @param abstrack Absolutní číslo stopy
 * @param cb Callback volaný pro každý sektor
 * @param user_data Uživatelská data předávaná callbacku
 * @return EXIT_SUCCESS, EXIT_FAILURE, nebo nenulová návratová hodnota z cb
 */
int dsk_for_each_sector ( st_HANDLER *h, uint8_t abstrack, dsk_sector_callback_t cb, void *user_data ) {

    if ( h == NULL || cb == NULL ) return EXIT_FAILURE;

    st_DSK_SHORT_IMAGE_INFO iinfo;
    if ( EXIT_SUCCESS != dsk_read_short_image_info ( h, &iinfo ) ) return EXIT_FAILURE;

    if ( abstrack >= ( iinfo.tracks * iinfo.sides ) ) {
        h->err = (en_HANDLER_ERROR) DSK_ERROR_TRACK_NOT_FOUND;
        return EXIT_FAILURE;
    }

    uint32_t track_offset = dsk_compute_track_offset ( abstrack, iinfo.tsize );

    st_DSK_SHORT_TRACK_INFO tinfo;
    if ( EXIT_SUCCESS != dsk_read_short_track_info_on_offset ( h, track_offset, &tinfo ) ) return EXIT_FAILURE;

    uint32_t sector_data_offset = track_offset + sizeof ( st_DSK_TRACK_INFO );
    uint16_t default_ssize = dsk_decode_sector_size ( tinfo.ssize );

    uint8_t i;
    for ( i = 0; i < tinfo.sectors; i++ ) {
        uint16_t this_sector_size = ( tinfo.sector_sizes[i] != 0 )
            ? dsk_decode_sector_size ( tinfo.sector_sizes[i] )
            : default_ssize;

        int ret = cb ( h, abstrack, i, tinfo.sinfo[i], sector_data_offset, this_sector_size, user_data );
        if ( ret != 0 ) return ret;

        sector_data_offset += this_sector_size;
    }

    return EXIT_SUCCESS;
}
