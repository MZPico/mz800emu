/**
 * @file   sdlapp_int.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Interní hlavičkový soubor knihovny SdlApp — struktura timeru.
 *
 * Definuje interní strukturu SdlAppTimer používanou pro správu timerů
 * v hlavní smyčce aplikace. Není součástí veřejného API.
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
#ifndef SDLAPP_INT_H
#define SDLAPP_INT_H

#include <glib.h>
#include "sdlapp.h"

/**
 * @brief Interní struktura reprezentující jeden timer.
 *
 * Propojuje sdlapp timer ID s SDL timer ID a uchovává uživatelský callback
 * spolu se zpětným ukazatelem na aplikaci pro doručení eventu.
 */
typedef struct SdlAppTimer
{
    guint id;              /**< Identifikátor timeru na úrovni sdlapp */
    SDL_TimerID sdl_id;    /**< SDL timer ID vrácené z SDL_AddTimer */
    guint interval_ms;     /**< Interval opakování v milisekundách */
    SdlAppTimerCb callback; /**< Uživatelský callback volaný v hlavním vlákně */
    gpointer user_data;    /**< Uživatelská data pro callback */
    SdlApp *app;           /**< Zpětný ukazatel na aplikaci */
} SdlAppTimer;

#endif /* SDLAPP_INT_H */
