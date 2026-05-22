/**
 * @file   sdlapp_winmanager.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Veřejné API správce aplikačních oken (SdlAppWindowManager).
 *
 * Definuje strukturu SdlAppWindowManager a veřejné funkce pro vytvoření,
 * zničení, vyhledávání oken (podle jména, SDL WindowID, regulárního výrazu),
 * iteraci, pozicování a event/render dispatch.
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
#ifndef SDLAPP_WINMANAGER_H
#define SDLAPP_WINMANAGER_H

#include "sdlapp.h"

typedef struct SdlApp SdlApp;
typedef struct SdlAppWindow SdlAppWindow;

/**
 * @brief Struktura spravující seznam aplikačních oken.
 *
 * Udržuje seznam oken (GList) a dvě hash tabulky pro rychlé vyhledávání
 * podle jména a SDL WindowID. Přístup je synchronizován RW zámkem.
 */
typedef struct SdlAppWindowManager
{
    SdlApp *app;              /**< Rodičovská SdlApp instance */
    GList *windows;            /**< Seznam registrovaných oken (SdlAppWindow *) */
    GHashTable *window_names;  /**< Hash tabulka: jméno okna -> SdlAppWindow * */
    GHashTable *window_ids;    /**< Hash tabulka: SDL_WindowID -> SdlAppWindow * */
    GRWLock rwlock;            /**< RW zámek pro synchronizaci přístupu */
    gboolean destroyed;        /**< TRUE pokud je správce v režimu likvidace */
} SdlAppWindowManager;

#ifdef __cplusplus
extern "C"
{
#endif

    /** @brief Vytvoří novou instanci správce oken. */
    SdlAppWindowManager *sdlapp_winmanager_new(SdlApp *app);

    /** @brief Zničí správce oken a uvolní všechny prostředky. */
    void sdlapp_winmanager_destroy(SdlAppWindowManager *wm);

    /** @brief Najde nejlepší pozici pro nové okno (bez překrývání). */
    void sdlapp_winmanager_good_place_for(SdlAppWindowManager *wm, SdlAppWindow *new_window, int *x, int *y);

    /** @brief Rozešle SDL událost všem registrovaným oknům. */
    void sdlapp_winmanager_process_SDL_event_for_all(SdlAppWindowManager *wm, SDL_Event *event);

    /** @brief Zavolá renderovací callback pro všechna aktivní okna. */
    void sdlapp_winmanager_process_render_for_all(SdlAppWindowManager *wm);

    /** @brief Zavolá callback pro každé registrované okno. */
    void sdlapp_winmanager_windows_foreach(SdlAppWindowManager *wm, void (*callback)(SdlAppWindow *, void *), void *user_data, gboolean reverse);

    /** @brief Vyhledá okno podle SDL WindowID. */
    SdlAppWindow *sdlapp_winmanager_get_window_by_SDL_WindowID(SdlAppWindowManager *wm, SDL_WindowID win_id);

    /** @brief Vyhledá okno podle jména. */
    SdlAppWindow *sdlapp_winmanager_get_window_by_name(SdlAppWindowManager *wm, const gchar *name);

    /** @brief Vyhledá okna podle regulárního výrazu. */
    SdlAppWindow **sdlapp_winmanager_get_window_list_by_regex(SdlAppWindowManager *wm, const gchar *pattern, guint *count, GError **error);

    /** @brief Vrátí počet registrovaných oken. */
    guint sdlapp_winmanager_get_window_count(SdlAppWindowManager *wm);

#ifdef __cplusplus
}
#endif

#endif /* SDLAPP_WINMANAGER_H */
