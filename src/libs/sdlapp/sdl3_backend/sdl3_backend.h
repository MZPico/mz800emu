/**
 * @file   sdl3_backend.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  SDL3 backend — inicializace a ukončení video, audio, joystick a OpenGL subsystémů.
 *
 * Deklaruje funkce pro inicializaci a ukončení jednotlivých SDL3 subsystémů
 * (video, audio, joystick, OpenGL) a pomocnou funkci pro převod SDL událostí
 * na textové názvy.
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
#ifndef SDL3_BACKEND_H
#define SDL3_BACKEND_H

#include <glib.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C"
{
#endif
    /** @brief Inicializuje SDL3 video subsystém. */
    gboolean sdl3_backend_video_init(void);

    /** @brief Inicializuje SDL3 video subsystém s podporou OpenGL. */
    gboolean sdl3_backend_video_opengl_init(void);

    /** @brief Ukončí SDL3 video subsystém. */
    void sdl3_backend_video_quit(void);

    /** @brief Inicializuje SDL3 joystick subsystém. */
    gboolean sdl3_backend_joy_init(void);

    /** @brief Ukončí SDL3 joystick subsystém. */
    void sdl3_backend_joy_quit(void);

    /** @brief Inicializuje SDL3 audio subsystém. */
    gboolean sdl3_backend_audio_init(void);

    /** @brief Ukončí SDL3 audio subsystém. */
    void sdl3_backend_audio_quit(void);

    /** @brief Ukončí všechny SDL3 subsystémy. */
    void sdl3_backend_quit(void);

    /** @brief Vrátí textový název SDL události pro ladící účely. */
    const char *sdl3_backend_GetEventName(Uint32 event_type);
#ifdef __cplusplus
}
#endif

#endif /* SDL3_BACKEND_H */
