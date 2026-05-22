/* 
 * File:   cmt_tap.c
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 21. května 2018, 16:36
 * 
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * ---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <string.h>

#include "libs/endianity/endianity.h"
#include "libs/generic_driver/generic_driver.h"
#include "generic_driver/memory_driver.h"
#include "libs/zxtape/zxtape.h"
#include "baseui/baseui.h"

#include "cmt.h"
#include "cmtext.h"
#include "cmt_tap.h"

static st_DRIVER *g_driver = &g_memory_driver_static;
//static st_DRIVER *g_driver_realloc = &g_ui_memory_driver_realloc;

char *g_cmt_tap_fileext[] = {
                             "tap",
                             NULL
};

st_CMTEXT_INFO g_cmt_tap_info = {
                                 "TAP",
                                 g_cmt_tap_fileext,
                                 "TAP cmt extension",
                                 CMTEXT_TYPE_PLAYABLE
};

extern st_CMTEXT *g_cmt_tap;


st_CMTEXT_TAPE_ITEM_TAPHDR* cmttap_block_get_spec_tapheader ( st_CMTEXT_BLOCK *block ) {
    assert ( block != NULL );
    assert ( block->spec != NULL );
    st_CMTTAP_BLOCKSPEC *blspec = (st_CMTTAP_BLOCKSPEC*) block->spec;
    assert ( blspec->flag == ZXTAPE_BLOCK_FLAG_HEADER );
    return &blspec->flspec.hdr;
}


st_CMTEXT_TAPE_ITEM_TAPDATA* cmttap_block_get_spec_tapdata ( st_CMTEXT_BLOCK *block ) {
    assert ( block != NULL );
    assert ( block->spec != NULL );
    st_CMTTAP_BLOCKSPEC *blspec = (st_CMTTAP_BLOCKSPEC*) block->spec;
    assert ( blspec->flag == ZXTAPE_BLOCK_FLAG_DATA );
    return &blspec->flspec.data;
}


void cmttap_blockspec_destroy ( st_CMTTAP_BLOCKSPEC *blspec ) {
    if ( !blspec ) return;
    if ( blspec->data ) baseui_tools_mem_free ( blspec->data );
    if ( ( blspec->flag == ZXTAPE_BLOCK_FLAG_HEADER ) && ( blspec->flspec.hdr.fname ) ) baseui_tools_mem_free ( blspec->flspec.hdr.fname );
    baseui_tools_mem_free ( blspec );
}


st_CMTTAP_BLOCKSPEC* cmttap_blockspec_new ( st_HANDLER *h, uint32_t offset, en_ZXTAPE_BLOCK_FLAG flag, un_CMTTAP_FLAGSPEC *flspec, en_CMTSPEED cmtspeed ) {

    uint16_t data_size;
    char *fname = NULL;

    if ( flag == ZXTAPE_BLOCK_FLAG_HEADER ) {
        data_size = flspec->hdr.size;
        printf ( "%s fname: %s\n", cmtext_get_name ( g_cmt_tap ), flspec->hdr.fname );
        printf ( "%s code: 0x%02x\n", cmtext_get_name ( g_cmt_tap ), flspec->hdr.code );
        printf ( "%s fsize: 0x%04x\n", cmtext_get_name ( g_cmt_tap ), flspec->hdr.data_size );

        int fname_size = strlen ( flspec->hdr.fname );
        fname = (char*) baseui_tools_mem_alloc0 ( fname_size + 1 );
        if ( !fname ) {
            fprintf ( stderr, "%s():%d - Can't alocate memory (%d)\n", __func__, __LINE__, fname_size + 1 );
            return NULL;
        };
        strncpy ( fname, flspec->hdr.fname, fname_size );

    } else if ( flag == ZXTAPE_BLOCK_FLAG_DATA ) {
        data_size = flspec->data.size;
        printf ( "%s data block fsize: 0x%04x\n", cmtext_get_name ( g_cmt_tap ), flspec->data.size - 2 );
    } else {
        fprintf ( stderr, "%s():%d - Unknown TAP block flag 0x%02x\n", __func__, __LINE__, flag );
        return NULL;
    };

    uint8_t *data = (uint8_t*) baseui_tools_mem_alloc ( data_size );
    if ( !data ) {
        fprintf ( stderr, "%s():%d - Can't alocate memory (%d)\n", __func__, __LINE__, data_size );
        if ( fname ) baseui_tools_mem_free ( fname );
        return NULL;
    };

    if ( EXIT_SUCCESS != generic_driver_read ( h, offset, data, data_size ) ) {
        fprintf ( stderr, "%s():%d - Can't read block data\n", __func__, __LINE__ );
        if ( fname ) baseui_tools_mem_free ( fname );
        baseui_tools_mem_free ( data );
        return NULL;
    };

    st_CMTTAP_BLOCKSPEC *blspec = (st_CMTTAP_BLOCKSPEC*) baseui_tools_mem_alloc0 ( sizeof ( st_CMTTAP_BLOCKSPEC ) );
    if ( !blspec ) {
        fprintf ( stderr, "%s():%d - Can't alocate memory (%d)\n", __func__, __LINE__, (int) sizeof ( st_CMTTAP_BLOCKSPEC ) );
        if ( fname ) baseui_tools_mem_free ( fname );
        baseui_tools_mem_free ( data );
        return NULL;
    };

    blspec->data = data;
    blspec->data_size = data_size;
    blspec->flag = flag;
    memcpy ( &blspec->flspec, flspec, sizeof (blspec->flspec ) );
    blspec->cmtspeed = cmtspeed;
    if ( flag == ZXTAPE_BLOCK_FLAG_HEADER ) blspec->flspec.hdr.fname = fname;

    return blspec;
}


void cmttap_block_close ( st_CMTEXT_BLOCK *block ) {
    if ( !block ) return;
    cmttap_blockspec_destroy ( (st_CMTTAP_BLOCKSPEC*) block->spec );
    block->spec = (void*) NULL;
    cmtext_block_destroy ( block );
}


static void cmttap_container_close ( st_CMTEXT_CONTAINER *container ) {
    cmtext_container_destroy ( container );
}


static void cmttap_eject ( void ) {
    cmttap_block_close ( g_cmt_tap->block );
    g_cmt_tap->block = (st_CMTEXT_BLOCK*) NULL;
    cmttap_container_close ( g_cmt_tap->container );
    g_cmt_tap->container = (st_CMTEXT_CONTAINER*) NULL;
}


static st_CMTEXT_TAPE_INDEX* cmttap_container_index_new ( st_HANDLER *h, uint32_t offset, en_CMTSPEED cmtspeed, int *count_blocks ) {

    *count_blocks = 0;

    st_CMTTAP_BLOCKINFO tapblinfo;
    st_CMTTAP_HEADER taphdr;

    st_CMTEXT_TAPE_INDEX *index = NULL;
    int i = 0;

    while ( offset < h->spec.memspec.size ) {

        if ( EXIT_SUCCESS != generic_driver_read ( h, offset, &tapblinfo.size, 2 ) ) {
            fprintf ( stderr, "%s():%d - Can't read block info\n", __func__, __LINE__ );
            cmtext_container_tapeindex_destroy ( index, i );
            return NULL;
        };

        tapblinfo.size = endianity_bswap16_LE ( tapblinfo.size );
        offset += 2;

        if ( EXIT_SUCCESS != generic_driver_read ( h, offset, &tapblinfo.flag, 1 ) ) {
            fprintf ( stderr, "%s():%d - Can't read block info\n", __func__, __LINE__ );
            cmtext_container_tapeindex_destroy ( index, i );
            return NULL;
        };

        if ( tapblinfo.flag == ZXTAPE_BLOCK_FLAG_HEADER ) {

            index = cmtext_container_tape_index_aloc ( index, i );
            if ( !index ) {
                return NULL;
            };

            st_CMTEXT_TAPE_INDEX *idx = &index[i];

            idx->block_id = i++;
            idx->offset = offset;
            offset += 1;
            idx->blspeed = CMTEXT_BLOCK_SPEED_SET;
            idx->bltype = CMTEXT_BLOCK_TYPE_TAPHEADER;
            idx->pause_after = CMTTAP_DEFAULT_PAUSE_AFTER_HEADER;

            if ( EXIT_SUCCESS != generic_driver_read ( h, offset, &taphdr.code, 1 ) ) {
                fprintf ( stderr, "%s():%d - Can't read TAP header block\n", __func__, __LINE__ );
                cmtext_container_tapeindex_destroy ( index, i );
                return NULL;
            };
            offset += 1;

            if ( EXIT_SUCCESS != generic_driver_read ( h, offset, &taphdr.name, sizeof ( taphdr.name ) ) ) {
                fprintf ( stderr, "%s():%d - Can't read TAP header block\n", __func__, __LINE__ );
                cmtext_container_tapeindex_destroy ( index, i );
                return NULL;
            };
            offset += sizeof ( taphdr.name );

            if ( EXIT_SUCCESS != generic_driver_read ( h, offset, &taphdr.data_size, 2 ) ) {
                fprintf ( stderr, "%s():%d - Can't read TAP header block\n", __func__, __LINE__ );
                cmtext_container_tapeindex_destroy ( index, i );
                return NULL;
            };
            offset += 2;
            taphdr.data_size = endianity_bswap16_LE ( taphdr.data_size );

            if ( EXIT_SUCCESS != generic_driver_read ( h, offset, &taphdr.param1, 2 ) ) {
                fprintf ( stderr, "%s():%d - Can't read TAP header block\n", __func__, __LINE__ );
                cmtext_container_tapeindex_destroy ( index, i );
                return NULL;
            };
            offset += 2;
            taphdr.param1 = endianity_bswap16_LE ( taphdr.param1 );

            if ( EXIT_SUCCESS != generic_driver_read ( h, offset, &taphdr.param2, 2 ) ) {
                fprintf ( stderr, "%s():%d - Can't read TAP header block\n", __func__, __LINE__ );
                cmtext_container_tapeindex_destroy ( index, i );
                return NULL;
            };
            offset += 2;
            taphdr.param2 = endianity_bswap16_LE ( taphdr.param2 );

            if ( EXIT_SUCCESS != generic_driver_read ( h, offset, &taphdr.chksum, 1 ) ) {
                fprintf ( stderr, "%s():%d - Can't read TAP header block\n", __func__, __LINE__ );
                cmtext_container_tapeindex_destroy ( index, i );
                return NULL;
            };
            offset += 1;

            st_CMTEXT_TAPE_ITEM_TAPHDR *taphdritem = &idx->item.taphdr;

            taphdritem->size = tapblinfo.size;
            taphdritem->code = taphdr.code;
            taphdritem->data_size = taphdr.data_size;
            taphdritem->param1 = taphdr.param1;
            taphdritem->param2 = taphdr.param2;
            taphdritem->cmtspeed = cmtspeed;

            taphdritem->fname = (char*) baseui_tools_mem_alloc0 ( sizeof ( taphdr.name ) + 1 );
            if ( !taphdritem->fname ) {
                fprintf ( stderr, "%s():%d - Can't alocate memory (%d)\n", __func__, __LINE__, (int) ( sizeof ( taphdr.name ) + 1 ) );
                cmtext_container_tapeindex_destroy ( index, i );
                return NULL;
            };

            memcpy ( taphdritem->fname, &taphdr.name, sizeof ( taphdr.name ) );

        } else if ( tapblinfo.flag == ZXTAPE_BLOCK_FLAG_DATA ) {

            if ( !( ( offset + tapblinfo.size ) <= h->spec.memspec.size ) ) {
                fprintf ( stderr, "%s():%d - Bad TAP block size '%d' for block '%d'\n", __func__, __LINE__, tapblinfo.size, i );
                cmtext_container_tapeindex_destroy ( index, i );
                return NULL;
            };

            index = cmtext_container_tape_index_aloc ( index, i );
            if ( !index ) {
                return NULL;
            };

            st_CMTEXT_TAPE_INDEX *idx = &index[i];

            idx->block_id = i++;
            idx->offset = offset;
            idx->blspeed = CMTEXT_BLOCK_SPEED_SET;
            idx->bltype = CMTEXT_BLOCK_TYPE_TAPDATA;
            idx->pause_after = CMTTAP_DEFAULT_PAUSE_AFTER_DATA;

            st_CMTEXT_TAPE_ITEM_TAPDATA *tapdataitem = &idx->item.tapdata;

            tapdataitem->size = tapblinfo.size;
            tapdataitem->cmtspeed = cmtspeed;

            offset += tapblinfo.size;

        } else {
            fprintf ( stderr, "%s():%d - Unknown TAP block (%d) flag '0x%02x' - offset= 0x%04x\n", __func__, __LINE__, i, tapblinfo.flag, offset );
            cmtext_container_tapeindex_destroy ( index, i );
            return NULL;
        };
    };

    if ( i != 0 ) {
        st_CMTEXT_TAPE_INDEX *idx = &index[i - 1];
        idx->pause_after = 0;
    };

    *count_blocks = i;
    return index;
}


st_CMT_STREAM* cmttap_generate_stream ( en_ZXTAPE_BLOCK_FLAG flag, uint8_t *data, uint16_t data_size, en_CMTSPEED cmtspeed, en_CMT_STREAM_TYPE type ) {

    st_CMT_STREAM *stream = zxtape_create_stream_from_tapblock ( flag, data, data_size, cmtspeed, CMTSTREAM_DEFAULT_RATE, type );
    if ( !stream ) {
        return NULL;
    };

    char buff[100];
    cmtspeed_get_speedtxt ( buff, sizeof ( buff ), cmtspeed, ZXTAPE_DEFAULT_BDSPEED );
    printf ( "%s speed: %s\n", cmtext_get_name ( g_cmt_tap ), buff );
    printf ( "%s stream type: %s\n", cmtext_get_name ( g_cmt_tap ), cmt_stream_get_stream_type_txt ( stream ) );
    printf ( "%s stream size: %0.2f kB\n", cmtext_get_name ( g_cmt_tap ), (float) cmt_stream_get_size ( stream ) / 1024 );
    printf ( "%s rate: %d Hz\n", cmtext_get_name ( g_cmt_tap ), cmt_stream_get_rate ( stream ) );
    printf ( "%s length: %1.2f s\n", cmtext_get_name ( g_cmt_tap ), cmt_stream_get_length ( stream ) );

    return stream;
}


static int cmttap_set_speed ( void *cmtext, en_CMTSPEED cmtspeed ) {
    assert ( cmtext != NULL );
    st_CMTEXT *cext = (st_CMTEXT*) cmtext;
    st_CMTEXT_BLOCK *block = cext->block;
    assert ( block != NULL );
    if ( block->block_speed != CMTEXT_BLOCK_SPEED_DEFAULT ) return EXIT_FAILURE;
    st_CMTTAP_BLOCKSPEC *blspec = (st_CMTTAP_BLOCKSPEC*) block->spec;
    if ( blspec->cmtspeed == cmtspeed ) return EXIT_SUCCESS;

    assert ( block->stream != NULL );

    st_CMT_STREAM *stream = cmttap_generate_stream ( blspec->flag, blspec->data, blspec->data_size, cmtspeed, block->stream->stream_type );
    if ( !stream ) return EXIT_FAILURE;
    cmt_stream_destroy ( block->stream );
    block->stream = stream;

    blspec->cmtspeed = cmtspeed;
    return EXIT_SUCCESS;
}


static uint16_t cmttap_get_bdspeed ( void *cmtext ) {
    assert ( cmtext != NULL );
    st_CMTEXT *cext = (st_CMTEXT*) cmtext;
    st_CMTTAP_BLOCKSPEC *blspec = (st_CMTTAP_BLOCKSPEC*) cext->block->spec;
    assert ( blspec != NULL );
    return cmtspeed_get_bdspeed ( blspec->cmtspeed, ZXTAPE_DEFAULT_BDSPEED );
}


static const char* cmttap_block_get_playname ( void *cmtext ) {
    assert ( cmtext != NULL );
    st_CMTEXT *cext = (st_CMTEXT*) cmtext;
    assert ( cext->block != NULL );
    st_CMTTAP_BLOCKSPEC *spec = (st_CMTTAP_BLOCKSPEC*) cext->block->spec;
    assert ( spec != NULL );
    if ( spec->flag != ZXTAPE_BLOCK_FLAG_DATA ) return "";
    return "*** TAP data block ***";
}



#define CMTTAP_DEFAULT_STREAM_BITSTREAM
//#define CMTTAP_DEFAULT_STREAM_VSTREAM


st_CMTEXT_BLOCK* cmttap_block_open ( st_CMTEXT *cmtext, int block_id ) {

    assert ( cmtext->container != NULL );
    assert ( cmtext->container->tape != NULL );
    assert ( cmtext->container->tape->h != NULL );
    assert ( cmtext->container->tape->index != NULL );
    (void) cmtext;

    if ( ( block_id < 0 ) || ( block_id >= g_cmt_tap->container->count_blocks ) ) {
        baseui_error ( "%s: Can't open block '%d' (blocks = %d)\n", cmtext_get_description ( g_cmt_tap ), block_id, g_cmt_tap->container->count_blocks );
        return NULL;
    };

    st_CMTEXT_TAPE_INDEX *idx = &g_cmt_tap->container->tape->index[block_id];
    uint32_t offset = idx->offset;
    en_CMTEXT_BLOCK_SPEED blspeed = idx->blspeed;
    uint16_t pause_after = idx->pause_after;

    en_CMTSPEED cmtspeed;
    en_ZXTAPE_BLOCK_FLAG flag;

    if ( idx->bltype == CMTEXT_BLOCK_TYPE_TAPHEADER ) {
        st_CMTEXT_TAPE_ITEM_TAPHDR *item = &idx->item.taphdr;
        cmtspeed = item->cmtspeed;
        flag = ZXTAPE_BLOCK_FLAG_HEADER;
    } else if ( idx->bltype == CMTEXT_BLOCK_TYPE_TAPDATA ) {
        st_CMTEXT_TAPE_ITEM_TAPDATA *item = &idx->item.tapdata;
        cmtspeed = item->cmtspeed;
        flag = ZXTAPE_BLOCK_FLAG_DATA;
    } else {
        fprintf ( stderr, "%s():%d - Unknown block type '%d'\n", __func__, __LINE__, idx->bltype );
        return NULL;
    };

    st_HANDLER *h = g_cmt_tap->container->tape->h;

    printf ( "%s block id: %d\n", cmtext_get_name ( g_cmt_tap ), block_id );

    st_CMTTAP_BLOCKSPEC *blspec = cmttap_blockspec_new ( h, offset, flag, ( un_CMTTAP_FLAGSPEC* ) & idx->item, cmtspeed );
    if ( !blspec ) {
        return NULL;
    }


#ifdef CMTTAP_DEFAULT_STREAM_BITSTREAM
    st_CMT_STREAM *stream = cmttap_generate_stream ( blspec->flag, blspec->data, blspec->data_size, cmtspeed, CMT_STREAM_TYPE_BITSTREAM );
#if 0
    if ( EXIT_FAILURE == cmt_bitstream_create_wav ( hwav, bitstream ) ) {
        fprintf ( stderr, "%s():%d - Can't create WAV from bitstream\n", __func__, __LINE__ );
        generic_driver_close ( hwav );
    } else {
        char filename[100];
        snprintf ( filename, sizeof (filename ), "tap2wav_block_%d.wav", block_id );
        printf ( "Save WAV: %s\n", filename );
        if ( EXIT_FAILURE == generic_driver_save_memory ( hwav, filename ) ) {
            fprintf ( stderr, "%s():%d - Can't write WAV\n", __func__, __LINE__ );
        };
        generic_driver_close ( hwav );
    };
#endif
#endif
#ifdef CMTTAP_DEFAULT_STREAM_VSTREAM
    st_CMT_STREAM *stream = cmttap_generate_stream ( blspec->flag, blspec->data, blspec->data_size, cmtspeed, CMT_STREAM_TYPE_VSTREAM );
#endif
    if ( !stream ) {
        cmttap_blockspec_destroy ( blspec );
        return NULL;
    };

    st_CMTEXT_BLOCK *block = cmtext_block_new ( block_id, idx->bltype, stream, blspeed, pause_after, blspec );
    if ( !block ) {
        cmttap_blockspec_destroy ( blspec );
        cmt_stream_destroy ( stream );
    };

    block->cb_play = cmtext_block_play;
    block->cb_get_playname = cmttap_block_get_playname;
    block->cb_set_polarity = (cmtext_block_cb_set_polarity) NULL;
    block->cb_set_speed = cmttap_set_speed;
    block->cb_get_bdspeed = cmttap_get_bdspeed;

    return block;
}


static int cmttap_container_next_block ( void ) {
    assert ( g_cmt_tap->container != NULL );
    assert ( g_cmt_tap->block != NULL );
    int next_block_id = g_cmt_tap->block->block_id + 1;
    if ( next_block_id < g_cmt_tap->container->count_blocks ) {
        st_CMTEXT_BLOCK *block = cmttap_block_open ( g_cmt_tap, next_block_id );
        if ( !block ) return EXIT_FAILURE;
        cmttap_block_close ( g_cmt_tap->block );
        g_cmt_tap->block = block;
        return EXIT_SUCCESS;
    };
    return EXIT_FAILURE;
}


static int cmttap_container_previous_block ( void ) {
    assert ( g_cmt_tap->container != NULL );
    assert ( g_cmt_tap->block != NULL );
    if ( g_cmt_tap->block->block_id == 0 ) return EXIT_FAILURE;
    int prev_block_id = g_cmt_tap->block->block_id - 1;
    st_CMTEXT_BLOCK * block = cmttap_block_open ( g_cmt_tap, prev_block_id );
    if ( !block ) return EXIT_FAILURE;
    cmttap_block_close ( g_cmt_tap->block );
    g_cmt_tap->block = block;
    return EXIT_SUCCESS;
}


static int cmttap_container_open_block ( int block_id ) {
    assert ( g_cmt_tap->container != NULL );
    assert ( g_cmt_tap->block != NULL );
    if ( block_id < g_cmt_tap->container->count_blocks ) {
        st_CMTEXT_BLOCK *block = cmttap_block_open ( g_cmt_tap, block_id );
        if ( !block ) return EXIT_FAILURE;
        cmttap_block_close ( g_cmt_tap->block );
        g_cmt_tap->block = block;
        return EXIT_SUCCESS;
    };
    return EXIT_FAILURE;
}


static int cmttap_container_open ( char *filename ) {

    cmttap_eject ( );

    printf ( "%s\nOpen: %s\n", cmtext_get_description ( g_cmt_tap ), filename );

    st_HANDLER *h = generic_driver_open_memory_from_file ( NULL, g_driver, filename );

    if ( !h ) {
        baseui_error ( "%s: Can't open file '%s'\n", cmtext_get_description ( g_cmt_tap ), filename );
        return EXIT_FAILURE;
    };

    int count_blocks = 0;

    st_CMTEXT_TAPE_INDEX *index = cmttap_container_index_new ( h, 0, CMTSPEED_1_1, &count_blocks );

    if ( !index ) {
        generic_driver_close ( h );
        return EXIT_FAILURE;
    };

    if ( !count_blocks ) {
        baseui_error ( "%s: Empty container '%s'\n", cmtext_get_description ( g_cmt_tap ), filename );
        generic_driver_close ( h );
        cmtext_container_tapeindex_destroy ( index, count_blocks );
        return EXIT_FAILURE;
    };

    st_CMTEXT_CONTAINER_TAPE *tape = cmtext_container_tape_new ( h, index );
    if ( !tape ) {
        generic_driver_close ( h );
        cmtext_container_tapeindex_destroy ( index, count_blocks );
        return EXIT_FAILURE;
    };

    st_CMTEXT_CONTAINER *container = cmtext_container_new ( CMTEXT_CONTAINER_TYPE_SIMPLE_TAPE, filename, count_blocks, tape, cmttap_container_next_block, cmttap_container_previous_block, cmttap_container_open_block );
    if ( !container ) {
        cmtext_container_tape_destroy ( tape, count_blocks );
        return EXIT_FAILURE;
    };

    g_cmt_tap->container = container;

    st_CMTEXT_BLOCK *block = cmttap_block_open ( g_cmt_tap, 0 );
    if ( !block ) {
        baseui_error ( "%s: Can't create block\n", cmtext_get_description ( g_cmt_tap ) );
        cmttap_container_close ( container );
        g_cmt_tap->container = NULL;
        return EXIT_FAILURE;
    };

    g_cmt_tap->block = block;

    return EXIT_SUCCESS;
}


static void cmttap_init ( void ) {


    return;
}


static void cmttap_exit ( void ) {
    cmttap_eject ( );
}


st_CMTEXT g_cmt_tap_extension = {
                                 &g_cmt_tap_info,
                                 (st_CMTEXT_CONTAINER*) NULL,
                                 (st_CMTEXT_BLOCK*) NULL,
                                 cmttap_init,
                                 cmttap_exit,
                                 cmttap_container_open,
                                 (cmtext_cb_stop) NULL,
                                 cmttap_eject,
                                 (cmtext_cb_write_data) NULL,
};

st_CMTEXT *g_cmt_tap = &g_cmt_tap_extension;


const char* cmttap_get_block_code_txt ( en_CMTTAP_HEADER_CODE code ) {
    char *txt;
    switch ( code ) {
        case CMTTAP_HEADER_CODE_PROGRAM:
            txt = "Program Header";
            break;

        case CMTTAP_HEADER_CODE_NUMARRAY:
            txt = "Num Array Header";
            break;

        case CMTTAP_HEADER_CODE_CHARARRAY:
            txt = "Char Array Header";
            break;

        case CMTTAP_HEADER_CODE_FILE:
            txt = "File Header";
            break;

        default:
            txt = "Data";

    };
    return txt;
}
