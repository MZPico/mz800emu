/**
 * @file vram_sim_render.c
 * @brief Pixel extraction API pro VRAM Viewer canvas render.
 *
 * Implementuje `vram_sim_get_pixel_palette_index()` a `vram_sim_locate_pixel()`
 * - dvě komplementární funkce pro převod mezi canvas (x, y) a VRAM plane/byte/bit
 * adresou.
 *
 * KRITICKÉ: bit ordering je LSB-first (bit 0 = nejlevější pixel uvnitř bytu).
 * Ověřeno proti `mz800_framebuffer.c` switch case 0x00/0x04/0x06 - renderer
 * používá makro `BIT0POS(val, pos) = ((val & 1) << pos)` a po každém pixelu
 * `data >> 1`. Tedy první rendrovaný pixel je z bit 0.
 *
 * Pozn: rozbory/02-VRAM-SIMULATOR.md sekce 3 původně chybně tvrdily MSB-first.
 * Toto je opraveno - viz knowledge harvest `vram_pixel_bit_order_lsb_first.md`.
 *
 * Mode dispatch:
 *  - 320 modes (320@4 A/B, 320@16): byte_index = x / 8, bit = x & 7
 *  - 640 modes (640@2 A/B, 640@4): byte_index = x / 16 (sdílený byte-pair),
 *    plane select dle (x & 8): bit 0..7 jdou do "sudého" plane, 8..15 do "lichého"
 *  - undoc 0x03, 0x07, MZ-700: neimplementováno (vrátí -1 / 0)
 */

#include "vram_sim.h"
#include "vram_sim_modes.h"

#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Pomocné funkce
 * ------------------------------------------------------------------------- */

/**
 * @brief Extrakce 1 bitu z plane bytu.
 *
 * @param state         Snapshot
 * @param plane_idx     0..3
 * @param byte_offset   offset uvnitř plane
 * @param bit_in_byte   0..7 (LSB-first - bit 0 = nejlevější pixel)
 * @return 0 nebo 1
 */
static int extract_plane_bit(const st_VRAM_SIM_STATE *state, int plane_idx,
                             uint16_t byte_offset, uint8_t bit_in_byte)
{
    uint8_t b = vs_read_plane_byte(state, plane_idx, byte_offset);
    return (b >> bit_in_byte) & 0x01;
}

/* ---------------------------------------------------------------------------
 * Pixel palette index
 * ------------------------------------------------------------------------- */

int vram_sim_get_pixel_palette_index(const st_VRAM_SIM_STATE *state, int x, int y)
{
    en_VRAM_SIM_MODE mode = vram_sim_decode_mode(state->dmd);
    int canvas_w, canvas_h;
    vram_sim_canvas_dimensions(mode, &canvas_w, &canvas_h);

    if (canvas_w == 0 || canvas_h == 0) return -1;
    if (x < 0 || x >= canvas_w) return -1;
    if (y < 0 || y >= canvas_h) return -1;

    /* Bytes per row: 320/8 = 40, 640/8 = 80 ale 640 mod sdili byte-pair takze fakticky 40 byte-pairs. */
    /* Logical row offset v plane: 40 bytu per row v 320 modu i 640 modu (kazdy plane).
     * V 640 modu jeden byte-pair (2 plane bytes) pokryje 16 pixelu, takze byte_offset
     * = y * 40 + (x / 16). V 320 modu: byte_offset = y * 40 + (x / 8). */

    uint16_t byte_offset;
    uint8_t bit;

    switch (mode) {
        case VRAM_SIM_MODE_320_4_A:
            byte_offset = (uint16_t)(y * 40 + (x / 8));
            bit = (uint8_t)(x & 7);
            return (extract_plane_bit(state, 1, byte_offset, bit) << 1) |
                    extract_plane_bit(state, 0, byte_offset, bit);

        case VRAM_SIM_MODE_320_4_B:
            byte_offset = (uint16_t)(y * 40 + (x / 8));
            bit = (uint8_t)(x & 7);
            return (extract_plane_bit(state, 3, byte_offset, bit) << 1) |
                    extract_plane_bit(state, 2, byte_offset, bit);

        case VRAM_SIM_MODE_320_16:
            byte_offset = (uint16_t)(y * 40 + (x / 8));
            bit = (uint8_t)(x & 7);
            return (extract_plane_bit(state, 3, byte_offset, bit) << 3) |
                   (extract_plane_bit(state, 2, byte_offset, bit) << 2) |
                   (extract_plane_bit(state, 1, byte_offset, bit) << 1) |
                    extract_plane_bit(state, 0, byte_offset, bit);

        case VRAM_SIM_MODE_640_2_A: {
            /* 640 mod: byte_pair_index = x / 16 (= vram_addr increment). */
            uint16_t pair = (uint16_t)(y * 40 + (x / 16));
            bit = (uint8_t)((x & 7));
            if (x & 8) {
                /* lichá polovina pair → plane II */
                return extract_plane_bit(state, 1, pair, bit);
            } else {
                /* sudá polovina → plane I */
                return extract_plane_bit(state, 0, pair, bit);
            }
        }

        case VRAM_SIM_MODE_640_2_B: {
            uint16_t pair = (uint16_t)(y * 40 + (x / 16));
            bit = (uint8_t)((x & 7));
            if (x & 8) {
                return extract_plane_bit(state, 3, pair, bit);
            } else {
                return extract_plane_bit(state, 2, pair, bit);
            }
        }

        case VRAM_SIM_MODE_640_4: {
            uint16_t pair = (uint16_t)(y * 40 + (x / 16));
            bit = (uint8_t)((x & 7));
            if (x & 8) {
                /* lichá: plane II (lo bit) + plane IV (hi bit) */
                return (extract_plane_bit(state, 3, pair, bit) << 1) |
                        extract_plane_bit(state, 1, pair, bit);
            } else {
                /* sudá: plane I (lo bit) + plane III (hi bit) */
                return (extract_plane_bit(state, 2, pair, bit) << 1) |
                        extract_plane_bit(state, 0, pair, bit);
            }
        }

        case VRAM_SIM_MODE_320_UNDOC:
        case VRAM_SIM_MODE_640_UNDOC:
        case VRAM_SIM_MODE_MZ700:
        default:
            /* Neimplementováno v V-1d. */
            return -1;
    }
}

/* ---------------------------------------------------------------------------
 * Locate pixel (canvas → plane offsets)
 * ------------------------------------------------------------------------- */

int vram_sim_locate_pixel(const st_VRAM_SIM_STATE *state, int x, int y,
                          st_VRAM_PIXEL_LOC *out_locations)
{
    en_VRAM_SIM_MODE mode = vram_sim_decode_mode(state->dmd);
    int canvas_w, canvas_h;
    vram_sim_canvas_dimensions(mode, &canvas_w, &canvas_h);

    if (canvas_w == 0 || canvas_h == 0) return 0;
    if (x < 0 || x >= canvas_w) return 0;
    if (y < 0 || y >= canvas_h) return 0;
    if (!out_locations) return 0;

    uint16_t byte_offset;
    uint8_t bit;
    int count = 0;

    switch (mode) {
        case VRAM_SIM_MODE_320_4_A:
            byte_offset = (uint16_t)(y * 40 + (x / 8));
            bit = (uint8_t)(x & 7);
            out_locations[0].plane_index = 0; /* I (low bit) */
            out_locations[0].byte_offset = byte_offset;
            out_locations[0].bit_in_byte = bit;
            out_locations[1].plane_index = 1; /* II (high bit) */
            out_locations[1].byte_offset = byte_offset;
            out_locations[1].bit_in_byte = bit;
            count = 2;
            break;

        case VRAM_SIM_MODE_320_4_B:
            byte_offset = (uint16_t)(y * 40 + (x / 8));
            bit = (uint8_t)(x & 7);
            out_locations[0].plane_index = 2;
            out_locations[0].byte_offset = byte_offset;
            out_locations[0].bit_in_byte = bit;
            out_locations[1].plane_index = 3;
            out_locations[1].byte_offset = byte_offset;
            out_locations[1].bit_in_byte = bit;
            count = 2;
            break;

        case VRAM_SIM_MODE_320_16:
            byte_offset = (uint16_t)(y * 40 + (x / 8));
            bit = (uint8_t)(x & 7);
            for (int i = 0; i < 4; i++) {
                out_locations[i].plane_index = i;
                out_locations[i].byte_offset = byte_offset;
                out_locations[i].bit_in_byte = bit;
            }
            count = 4;
            break;

        case VRAM_SIM_MODE_640_2_A: {
            uint16_t pair = (uint16_t)(y * 40 + (x / 16));
            bit = (uint8_t)((x & 7));
            int plane = (x & 8) ? 1 : 0;
            out_locations[0].plane_index = plane;
            out_locations[0].byte_offset = pair;
            out_locations[0].bit_in_byte = bit;
            count = 1;
            break;
        }

        case VRAM_SIM_MODE_640_2_B: {
            uint16_t pair = (uint16_t)(y * 40 + (x / 16));
            bit = (uint8_t)((x & 7));
            int plane = (x & 8) ? 3 : 2;
            out_locations[0].plane_index = plane;
            out_locations[0].byte_offset = pair;
            out_locations[0].bit_in_byte = bit;
            count = 1;
            break;
        }

        case VRAM_SIM_MODE_640_4: {
            uint16_t pair = (uint16_t)(y * 40 + (x / 16));
            bit = (uint8_t)((x & 7));
            if (x & 8) {
                out_locations[0].plane_index = 1; /* II low bit */
                out_locations[1].plane_index = 3; /* IV high bit */
            } else {
                out_locations[0].plane_index = 0; /* I low bit */
                out_locations[1].plane_index = 2; /* III high bit */
            }
            out_locations[0].byte_offset = pair;
            out_locations[0].bit_in_byte = bit;
            out_locations[1].byte_offset = pair;
            out_locations[1].bit_in_byte = bit;
            count = 2;
            break;
        }

        case VRAM_SIM_MODE_320_UNDOC:
        case VRAM_SIM_MODE_640_UNDOC:
        case VRAM_SIM_MODE_MZ700:
        default:
            count = 0;
            break;
    }

    return count;
}
