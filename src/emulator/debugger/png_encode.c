/*
 * png_encode.c — implementace wrapperu nad stb_image_write.h
 *
 * Tento soubor je jediným translation unitem, který obsahuje
 * implementaci header-only knihovny stb_image_write.h (definuje
 * STB_IMAGE_WRITE_IMPLEMENTATION). Ostatní moduly includují jen
 * png_encode.h.
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

/**
 * @file png_encode.c
 * @brief Implementace stb_image_write.h + glib collector pro PNG enkód.
 *
 * stb_image_write.h (public domain, Sean Barrett, v1.02) zapisuje PNG
 * stream přes `stbi_write_png_to_func` do uživatelského callbacku. Náš
 * callback akumuluje bajty do glib-alokovaného bufferu (g_realloc), aby
 * výsledek mohl volající uvolnit symetricky přes g_free.
 */

#include <stdint.h>
#include <stddef.h>
#include <glib.h>

#include "png_encode.h"

/* Implementace header-only stb knihovny - právě v tomto TU.
 * Include přes cestu relativní k src/ (= include path z AddMzEmu.cmake). */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "libs/stb/stb_image_write.h"

/**
 * @brief Akumulační stav pro PNG collector callback.
 *
 * Drží glib-alokovaný rostoucí buffer, do kterého stb zapisuje PNG
 * bajty. Invariant: pokud `failed == FALSE`, pak `data` má kapacitu
 * alespoň `len` bajtů a `data != NULL` při `len > 0`.
 */
typedef struct st_PNG_COLLECTOR
{
    uint8_t *data;     /**< glib-alokovaný buffer s dosud zapsanými bajty. */
    size_t   len;      /**< Počet platných bajtů v `data`. */
    gboolean failed;   /**< TRUE pokud selhala alokace (g_realloc nikdy nevrací NULL, rezerva). */
} st_PNG_COLLECTOR;

/**
 * @brief stb_write_func callback - připojí blok dat do collector bufferu.
 *
 * @param context Pointer na st_PNG_COLLECTOR.
 * @param data    Blok bajtů od stb (PNG stream segment).
 * @param size    Délka bloku v bajtech.
 *
 * @note g_realloc při OOM volá abort() (glib chování), takže prakticky
 *       nevrací NULL; `failed` je defenzivní rezerva.
 */
static void png_collector_write ( void *context, void *data, int size )
{
    st_PNG_COLLECTOR *c = (st_PNG_COLLECTOR *) context;
    if ( c->failed || size <= 0 )
    {
        return;
    };
    c->data = (uint8_t *) g_realloc ( c->data, c->len + (size_t) size );
    memcpy ( c->data + c->len, data, (size_t) size );
    c->len += (size_t) size;
}

uint8_t *png_encode_rgba ( const uint8_t *rgba, uint32_t width,
                           uint32_t height, size_t *out_len )
{
    if ( out_len )
    {
        *out_len = 0;
    };
    if ( !rgba || width == 0 || height == 0 || !out_len )
    {
        return NULL;
    };

    st_PNG_COLLECTOR collector = { NULL, 0, FALSE };

    /* stride = width * 4 (RGBA8888, těsně uložené řádky). */
    int rc = stbi_write_png_to_func ( png_collector_write, &collector,
                                      (int) width, (int) height, 4,
                                      rgba, (int) ( width * 4 ) );
    if ( rc == 0 || collector.failed || collector.len == 0 )
    {
        g_free ( collector.data );
        return NULL;
    };

    *out_len = collector.len;
    return collector.data;
}
