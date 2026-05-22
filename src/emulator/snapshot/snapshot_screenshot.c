/**
 * @file snapshot_screenshot.c
 * @brief Generování PNG screenshotu z framebufferu pro snapshot systém
 *
 * Vytvoří PNG obrázek z aktuálního stavu framebufferu (INDEX8 paleta → RGBA)
 * a uloží ho do ZIP archivu jako screenshot.png.
 *
 * Používá SDL3_image pro PNG enkódování a SDL_IOFromDynamicMem()
 * pro zápis do paměti (bez temp souboru).
 */

#include "snapshot_screenshot.h"
#include "snapshot_xml.h"

#include "hw-generic/gdg/framebuffer.h"
#include "hw-generic/gdg/video.h"
#include "display.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>


en_SNAPSHOT_RESULT snapshot_screenshot_save(snapshot_io_t *io)
{
    /* 1. Vytvořit SDL_Surface ve formátu INDEX8 */
    SDL_Surface *surface = SDL_CreateSurface(VIDEO_DISPLAY_WIDTH,
                                              VIDEO_DISPLAY_HEIGHT,
                                              SDL_PIXELFORMAT_INDEX8);
    if (!surface) {
        SNAP_ERR("screenshot", "SDL_CreateSurface failed: %s", SDL_GetError());
        return SNAPSHOT_ERR_ALLOC;
    }

    /* 2. Nastavit paletu ze stávajícího barevného schématu */
    uint32_t *colormap = display_get_default_color_schema();
    SDL_Color colors[DISPLAY_MZCOLORS];
    for (int i = 0; i < DISPLAY_MZCOLORS; i++) {
        colors[i].r = (colormap[i] >> 16) & 0xff;
        colors[i].g = (colormap[i] >> 8) & 0xff;
        colors[i].b = colormap[i] & 0xff;
        colors[i].a = 255;
    }

    SDL_Palette *palette = SDL_CreatePalette(DISPLAY_MZCOLORS);
    if (!palette) {
        SNAP_ERR("screenshot", "SDL_CreatePalette failed: %s", SDL_GetError());
        SDL_DestroySurface(surface);
        return SNAPSHOT_ERR_ALLOC;
    }

    SDL_SetPaletteColors(palette, colors, 0, DISPLAY_MZCOLORS);
    SDL_SetSurfacePalette(surface, palette);

    /* 3. Kopírovat pixel data z aktuálního framebufferu */
    SDL_LockSurface(surface);
    memcpy(surface->pixels, g_framebuffer.pixels,
           VIDEO_DISPLAY_WIDTH * VIDEO_DISPLAY_HEIGHT);
    SDL_UnlockSurface(surface);

    /* 4. Konvertovat na RGBA32 (PNG kodér potřebuje RGB/RGBA) */
    SDL_Surface *rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    SDL_DestroyPalette(palette);

    if (!rgba) {
        SNAP_ERR("screenshot", "SDL_ConvertSurface to RGBA32 failed: %s", SDL_GetError());
        return SNAPSHOT_ERR_ALLOC;
    }

    /* 5. Uložit PNG do paměti přes SDL_IOFromDynamicMem */
    SDL_IOStream *mem_io = SDL_IOFromDynamicMem();
    if (!mem_io) {
        SNAP_ERR("screenshot", "SDL_IOFromDynamicMem failed: %s", SDL_GetError());
        SDL_DestroySurface(rgba);
        return SNAPSHOT_ERR_ALLOC;
    }

    if (!IMG_SavePNG_IO(rgba, mem_io, false)) {
        SNAP_ERR("screenshot", "IMG_SavePNG_IO failed: %s", SDL_GetError());
        SDL_CloseIO(mem_io);
        SDL_DestroySurface(rgba);
        return SNAPSHOT_ERR_IO;
    }

    SDL_DestroySurface(rgba);

    /* 6. Získat PNG data z dynamické paměti */
    Sint64 png_size = SDL_TellIO(mem_io);
    SDL_PropertiesID props = SDL_GetIOProperties(mem_io);
    void *png_data = SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);

    if (!png_data || png_size <= 0) {
        SNAP_ERR("screenshot", "failed to obtain PNG data from memory");
        SDL_CloseIO(mem_io);
        return SNAPSHOT_ERR_IO;
    }

    /* 7. Zapsat PNG data do ZIP archivu */
    en_SNAPSHOT_RESULT result = snapshot_io_write_bin(io, "screenshot.png",
                                                      (const uint8_t *)png_data,
                                                      (size_t)png_size);

    SDL_CloseIO(mem_io);

    if (result != SNAPSHOT_OK) {
        SNAP_ERR("screenshot", "cannot write screenshot.png to archive");
    }

    return result;
}
