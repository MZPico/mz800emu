/**
 * @file   sdlapp_winmanager.c
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Implementace správce aplikačních oken pro SdlApp.
 *
 * Správce oken (SdlAppWindowManager) zajišťuje registraci, vyhledávání,
 * iteraci a event/render dispatch přes všechna aplikační okna. Přístup
 * ke sdíleným datům je chráněn RW zámkem (GRWLock).
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

#include "sdlapp_winmanager_int.h"
#include "sdlapp_window.h"
#include <stdlib.h>
#include <glib.h>
#include <glib/gregex.h>
#include <SDL3/SDL.h>

/**
 * @brief Vytvoří novou instanci správce oken.
 *
 * Alokuje a inicializuje SdlAppWindowManager včetně hash tabulek
 * pro vyhledávání oken podle jména a SDL WindowID.
 *
 * @param app Ukazatel na rodičovskou SdlApp instanci.
 * @return Nově alokovaný SdlAppWindowManager, nebo NULL při chybě.
 */
SdlAppWindowManager *sdlapp_winmanager_new(SdlApp *app)
{
    SdlAppWindowManager *wm = g_new0(SdlAppWindowManager, 1);
    g_rw_lock_init(&wm->rwlock);
    wm->app = app;
    wm->windows = NULL;
    wm->window_names = g_hash_table_new(g_str_hash, g_str_equal);
    wm->window_ids = g_hash_table_new(g_direct_hash, g_direct_equal);
    wm->destroyed = FALSE;
    return wm;
}
// sdl_app_destroy_window

/**
 * @brief Zničí správce oken a uvolní všechny přidružené prostředky.
 *
 * Projde všechna registrovaná okna od konce a zavolá na každé
 * sdlapp_window_destroy. Poté uvolní hash tabulky a RW zámek.
 *
 * @param wm Ukazatel na SdlAppWindowManager (může být NULL).
 */
void sdlapp_winmanager_destroy(SdlAppWindowManager *wm)
{
    if (!wm)
        return;

    g_rw_lock_writer_lock(&wm->rwlock);

    /* Uvolnění všech oken - udelame foreach od konce a postupne zavolame destroy */
    wm->destroyed = TRUE;
    GList *iter = g_list_last(wm->windows);
    while (iter)
    {
        SdlAppWindow *win = (SdlAppWindow *)iter->data;
        sdlapp_window_destroy(wm, win);
        iter = iter->prev;
    };

    g_list_free(wm->windows);

    g_hash_table_remove_all(wm->window_names);
    g_hash_table_destroy(wm->window_names);

    g_hash_table_remove_all(wm->window_ids);
    g_hash_table_destroy(wm->window_ids);

    g_rw_lock_writer_unlock(&wm->rwlock);
    g_rw_lock_clear(&wm->rwlock);
    g_free(wm);
}

/**
 * @brief Změní jméno registrovaného okna ve správci oken.
 *
 * Ověří, že nové jméno není prázdné, není duplicitní a že okno
 * je skutečně registrováno pod původním jménem. Při úspěchu
 * aktualizuje hash tabulku i interní jméno okna.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 * @param win Ukazatel na okno, jehož jméno se mění.
 * @param name Nové jméno okna.
 * @return TRUE při úspěchu, FALSE při chybě.
 */
gboolean sdlapp_winmanager_change_window_name(SdlAppWindowManager *wm, SdlAppWindow *win, const gchar *name)
{
    if (!wm || !win || !name)
        return false;

    if (g_strcmp0(name, "") == 0)
    {
        WARN("Window name is empty, cannot change window name");
        return false;
    };

    if (g_strcmp0(win->name, name) == 0)
    {
        return true;
    };

    g_rw_lock_writer_lock(&wm->rwlock);
    if (g_hash_table_lookup(wm->window_names, name))
    {
        WARN("Window with name %s already exists, cannot change window name", name);
        g_rw_lock_writer_unlock(&wm->rwlock);
        return false;
    };

    if (!g_hash_table_lookup(wm->window_names, win->name))
    {
        WARN("Window with name %s not found, cannot change window name", win->name);
        g_rw_lock_writer_unlock(&wm->rwlock);
        return false;
    };

    g_hash_table_remove(wm->window_names, win->name);
    g_free(win->name);
    win->name = g_strdup(name);
    g_hash_table_insert(wm->window_names, win->name, win);
    g_rw_lock_writer_unlock(&wm->rwlock);

    return true;
}

/**
 * @brief Zaregistruje okno ve správci oken.
 *
 * Přidá okno do seznamu oken a do obou hash tabulek (jméno, SDL WindowID).
 * Odmítne registraci, pokud jméno okna je prázdné nebo duplicitní.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 * @param win Ukazatel na registrované okno.
 * @return TRUE při úspěchu, FALSE při chybě.
 */
gboolean sdlapp_winmanager_register_window(SdlAppWindowManager *wm, SdlAppWindow *win)
{
    if (!wm || !win)
        return false;

    if ((!win->name) || (g_strcmp0(win->name, "") == 0))
    {
        WARN("Window name is empty, cannot register window");
    };

    g_rw_lock_writer_lock(&wm->rwlock);
    if (g_hash_table_lookup(wm->window_names, win->name))
    {
        WARN("Window with name %s already exists, cannot register window", win->name);
        g_rw_lock_writer_unlock(&wm->rwlock);
        return false;
    };
    wm->windows = g_list_append(wm->windows, win);
    g_hash_table_insert(wm->window_names, win->name, win);
    g_hash_table_insert(wm->window_ids, GINT_TO_POINTER(SDL_GetWindowID(win->sdl_window)), win);
    g_rw_lock_writer_unlock(&wm->rwlock);

    return true;
}

/**
 * @brief Odregistruje okno ze správce oken.
 *
 * Odebere okno ze seznamu a z obou hash tabulek. Pokud je správce
 * oken v režimu likvidace (destroyed=TRUE), nic nedělá.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 * @param win Ukazatel na odregistrovávané okno.
 */
void sdlapp_winmanager_unregister_window(SdlAppWindowManager *wm, SdlAppWindow *win)
{
    if (!wm || !win)
        return;

    if (wm->destroyed) // Pokud je správce oken v likvidaci, tak uz není potřeba nic mazat
        return;

    g_rw_lock_writer_lock(&wm->rwlock);
    wm->windows = g_list_remove(wm->windows, win);
    g_hash_table_remove(wm->window_names, win->name);
    g_hash_table_remove(wm->window_ids, GINT_TO_POINTER(SDL_GetWindowID(win->sdl_window)));
    g_rw_lock_writer_unlock(&wm->rwlock);
}

/**
 * @brief Zavolá callback pro každé registrované okno.
 *
 * Iteruje přes seznam oken (vpřed nebo vzad) a pro každé zavolá
 * daný callback. Přístup je chráněn reader zámkem.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 * @param callback Funkce volaná pro každé okno.
 * @param user_data Uživatelská data předaná callbacku.
 * @param reverse TRUE pro iteraci od konce, FALSE od začátku.
 */
void sdlapp_winmanager_windows_foreach(SdlAppWindowManager *wm, void (*callback)(SdlAppWindow *, void *), void *user_data, gboolean reverse)
{
    if (!wm || !callback)
        return;

    g_rw_lock_reader_lock(&wm->rwlock);
    GList *iter = reverse ? g_list_last(wm->windows) : wm->windows;
    while (iter)
    {
        callback((SdlAppWindow *)iter->data, user_data);
        iter = reverse ? iter->prev : iter->next;
    }
    g_rw_lock_reader_unlock(&wm->rwlock);
}

/**
 * @brief Vyhledá okno podle SDL WindowID.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 * @param win_id SDL WindowID hledaného okna.
 * @return Ukazatel na nalezené SdlAppWindow, nebo NULL.
 */
SdlAppWindow *sdlapp_winmanager_get_window_by_SDL_WindowID(SdlAppWindowManager *wm, SDL_WindowID win_id)
{
    if (!wm)
        return NULL;

    g_rw_lock_reader_lock(&wm->rwlock);
    SdlAppWindow *win = (SdlAppWindow *)g_hash_table_lookup(wm->window_ids, GINT_TO_POINTER(win_id));
    g_rw_lock_reader_unlock(&wm->rwlock);
    return win;
}

/**
 * @brief Vyhledá okno podle jména.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 * @param name Jméno hledaného okna.
 * @return Ukazatel na nalezené SdlAppWindow, nebo NULL.
 */
SdlAppWindow *sdlapp_winmanager_get_window_by_name(SdlAppWindowManager *wm, const gchar *name)
{
    if (!wm || !name)
        return NULL;

    g_rw_lock_reader_lock(&wm->rwlock);
    SdlAppWindow *win = (SdlAppWindow *)g_hash_table_lookup(wm->window_names, name);
    g_rw_lock_reader_unlock(&wm->rwlock);
    return win;
}

#include <glib.h>
#include <glib/gregex.h>

/**
 * @brief Prohledá okna ve správci oken podle regulárního výrazu.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 * @param pattern Regulární výraz pro hledání.
 * @param count Počet nalezených oken (výstupní parametr).
 * @param error Chybový výstup (NULL pokud není chyba).
 * @return NULL-terminated seznam nalezených oken (SdlAppWindow **), nebo NULL při chybě.
 */
SdlAppWindow **sdlapp_winmanager_get_window_list_by_regex(SdlAppWindowManager *wm, const gchar *pattern, guint *count, GError **error)
{
    if (!wm || !pattern || !count)
    {
        g_set_error(error, G_REGEX_ERROR, G_REGEX_ERROR_COMPILE, "Invalid input parameter.");
        return NULL;
    };

    g_rw_lock_reader_lock(&wm->rwlock);

    GRegex *regex = g_regex_new(pattern,
                                (GRegexCompileFlags)(G_REGEX_OPTIMIZE | G_REGEX_ANCHORED),
                                (GRegexMatchFlags)0,
                                error);
    if (!regex)
    {
        g_rw_lock_reader_unlock(&wm->rwlock);
        return NULL;
    }

    GPtrArray *result = g_ptr_array_new_with_free_func(NULL);

    for (GList *l = wm->windows; l != NULL; l = l->next)
    {
        SdlAppWindow *window = (SdlAppWindow *)l->data;
        if (window && window->name &&
            g_regex_match(regex, window->name, (GRegexMatchFlags)0, NULL))
        {
            g_ptr_array_add(result, window);
        }
    }

    g_regex_unref(regex);
    g_rw_lock_reader_unlock(&wm->rwlock);

    *count = result->len;

    g_ptr_array_add(result, NULL);

    return (SdlAppWindow **)g_ptr_array_free(result, FALSE);
}

/**
 * @brief Rozešle SDL událost všem registrovaným oknům.
 *
 * Projde okna od konce seznamu a zavolá sdlapp_window_process_event
 * na každé z nich. Přístup je chráněn reader zámkem.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 * @param event Ukazatel na SDL událost k rozeslání.
 */
void sdlapp_winmanager_process_SDL_event_for_all(SdlAppWindowManager *wm, SDL_Event *event)
{
    if (!wm || !event)
        return;

    g_rw_lock_reader_lock(&wm->rwlock);
    GList *iter = g_list_last(wm->windows);
    while (iter)
    {
        SdlAppWindow *win = (SdlAppWindow *)iter->data;
        sdlapp_window_process_event(win, event);
        iter = iter->prev;
    }
    g_rw_lock_reader_unlock(&wm->rwlock);
}

/**
 * @brief Zavolá renderovací callback pro všechna aktivní okna.
 *
 * Projde okna od konce seznamu a zavolá sdlapp_window_process_render
 * na každé z nich. Přístup je chráněn reader zámkem.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 */
void sdlapp_winmanager_process_render_for_all(SdlAppWindowManager *wm)
{
    if (!wm)
        return;

    // projdeme všechna okna a zavoláme render u tech, ktera jsou aktivni a mají nastavený callback
    g_rw_lock_reader_lock(&wm->rwlock);
    GList *iter = g_list_last(wm->windows);
    while (iter)
    {
        SdlAppWindow *win = (SdlAppWindow *)iter->data;
        sdlapp_window_process_render(win);
        iter = iter->prev;
    }
    g_rw_lock_reader_unlock(&wm->rwlock);
}

/**
 * @brief Najde nejlepší místo pro nové okno tak, aby se minimalizovalo překrývání s existujícími okny.
 *
 * Prohledá rastr 50x50 px na aktuálním monitoru a vrátí první pozici,
 * kde nové okno nekoliduje s žádným existujícím viditelným oknem.
 * Pokud volné místo neexistuje, vrátí SDL_WINDOWPOS_CENTERED.
 *
 * @param wm Správce oken (SdlAppWindowManager).
 * @param new_window Nové okno, pro které hledáme nejlepší pozici.
 * @param x Ukazatel na proměnnou, kam se uloží doporučená X souřadnice.
 * @param y Ukazatel na proměnnou, kam se uloží doporučená Y souřadnice.
 */
void sdlapp_winmanager_good_place_for(SdlAppWindowManager *wm, SdlAppWindow *new_window, int *x, int *y)
{
    g_return_if_fail(wm != NULL);
    g_return_if_fail(new_window != NULL);
    g_return_if_fail(new_window->sdl_window != NULL);
    g_return_if_fail(x != NULL);
    g_return_if_fail(y != NULL);

    // pri chybe umistime okno na neznamou pozici - at uz si s tim SDL udelá co chce
    *x = SDL_WINDOWPOS_UNDEFINED;
    *y = SDL_WINDOWPOS_UNDEFINED;

    int win_width = 0, win_height = 0;
    SDL_GetWindowSize(new_window->sdl_window, &win_width, &win_height);
    int top = 0, left = 0, bottom = 0, right = 0;
    SDL_GetWindowBordersSize(new_window->sdl_window, &top, &left, &bottom, &right);
    win_width += left + right;
    win_height += top + bottom;

    int win_x = 0, win_y = 0;
    SDL_GetWindowPosition(new_window->sdl_window, &win_x, &win_y);

    SDL_DisplayID win_displayID = SDL_GetDisplayForWindow(new_window->sdl_window);

    // g_print("Searching new position for: %s, x: %d, y: %d size: %dx%d\n", new_window->name, win_x, win_y, win_width, win_height);

    int display_count = 0;
    SDL_DisplayID *displays = SDL_GetDisplays(&display_count);
    if (!displays)
    {
        WARN("SDL_GetDisplays failed: %s", SDL_GetError());
        return;
    }

#if 0
    for (int i = 0; i < display_count; i++)
    {
        const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(displays[i]);
        if (!mode)
        {
            WARN("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
            continue;
        }

        g_print("Display %d(%u): %dx%d\n", i, displays[i], mode->w, mode->h);
    }
    g_free(displays);
#endif

    SDL_Rect screen_rect;
    if (!SDL_GetDisplayBounds(win_displayID, &screen_rect))
    {
        WARN("SDL_GetDisplayBounds failed: %s", SDL_GetError());
        return;
    }

    // g_print("Screen 1 size: %d x %d\n", screen_rect.w, screen_rect.h);

    GList *iter;
    GList *windows = g_list_first(wm->windows);

    GList *occupied_areas = NULL;
    int windows_count = 0;
    for (iter = windows; iter != NULL; iter = iter->next)
    {
        SdlAppWindow *win = (SdlAppWindow *)iter->data;
        if (win == new_window || win->sdl_window == NULL)
            continue;

        Uint32 flags = SDL_GetWindowFlags(win->sdl_window);
        if (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED))
        {
            // g_print("- Competitor window %s is hidden or minimized\n", win->name);
            continue;
        }

        SDL_DisplayID displayID = SDL_GetDisplayForWindow(win->sdl_window);
        if (displayID != win_displayID)
        {
            // g_print("- Competitor window %s is on different display\n", win->name);
            continue;
        }

        SDL_Rect *occupied = g_new(SDL_Rect, 1);
        SDL_GetWindowPosition(win->sdl_window, &occupied->x, &occupied->y);
        SDL_GetWindowSize(win->sdl_window, &occupied->w, &occupied->h);
        int top = 0, left = 0, bottom = 0, right = 0;
        SDL_GetWindowBordersSize(win->sdl_window, &top, &left, &bottom, &right);
        occupied->w += left + right;
        occupied->h += top + bottom;

        // g_print("- Competitor window %d: %s, x: %d, y: %d size: %dx%d\n", windows_count, win->name, occupied->x, occupied->y, occupied->w, occupied->h);
        occupied_areas = g_list_append(occupied_areas, occupied);
        windows_count++;
    }

    int best_x = 0, best_y = 50;
    gboolean found = FALSE;

    if (windows_count != 0)
    {
        for (int ty = 50; ty + win_height <= screen_rect.h; ty += 50)
        {
            for (int tx = 50; tx + win_width <= screen_rect.w; tx += 50)
            {
                SDL_Rect candidate = {tx, ty, win_width, win_height};
                gboolean overlaps = FALSE;

                for (GList *s = occupied_areas; s != NULL; s = s->next)
                {
                    SDL_Rect *occupied = (SDL_Rect *)s->data;
                    if (SDL_HasRectIntersection(&candidate, occupied))
                    {
                        overlaps = TRUE;
                        break;
                    }
                }

                if (!overlaps)
                {
                    best_x = tx;
                    best_y = ty;
                    found = TRUE;
                    break;
                }
            }
            if (found)
                break;
        }
    };

    if (!found)
    {
        // Žádné jiné okno (nebo se nenašlo volné místo) -> okno na střed.
        // Centrujeme do "usable" oblasti displeje (bez taskbaru/panelů) a levý
        // horní roh clampneme na origin, aby titulkový pruh velkého okna
        // (např. scale Big na 1080p) nevyjel nad horní/levý okraj a šel chytit.
        SDL_Rect usable;
        if (SDL_GetDisplayUsableBounds(win_displayID, &usable))
        {
            best_x = usable.x + (usable.w - win_width) / 2;
            best_y = usable.y + (usable.h - win_height) / 2;
            if (best_x < usable.x)
                best_x = usable.x;
            if (best_y < usable.y)
                best_y = usable.y;
        }
        else
        {
            WARN("SDL_GetDisplayUsableBounds failed: %s", SDL_GetError());
            best_x = SDL_WINDOWPOS_CENTERED;
            best_y = SDL_WINDOWPOS_CENTERED;
        };
    };

    g_list_free_full(occupied_areas, g_free);

    *x = best_x;
    *y = best_y;

    // g_print("Best position: %d x %d\n\n", *x, *y);
}

/**
 * @brief Vrátí počet registrovaných oken.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 * @return Počet oken, nebo 0 pokud je wm NULL.
 */
guint sdlapp_winmanager_get_window_count(SdlAppWindowManager *wm)
{
    if (!wm)
        return 0;

    g_rw_lock_reader_lock(&wm->rwlock);
    guint count = g_list_length(wm->windows);
    g_rw_lock_reader_unlock(&wm->rwlock);
    return count;
}

/**
 * @brief Zjistí, zda existuje alespoň jedno okno s OpenGL rendererem.
 *
 * @param wm Ukazatel na SdlAppWindowManager.
 * @return TRUE pokud existuje OpenGL okno, jinak FALSE.
 */
gboolean sdlapp_winmanager_has_opengl_window(SdlAppWindowManager *wm)
{
    if (!wm)
        return FALSE;

    g_rw_lock_reader_lock(&wm->rwlock);
    gboolean found = FALSE;
    for (GList *l = wm->windows; l != NULL; l = l->next)
    {
        SdlAppWindow *win = (SdlAppWindow *)l->data;
        if (win && win->renderer_type == SDLAPP_RENDERER_OPENGL)
        {
            found = TRUE;
            break;
        }
    }
    g_rw_lock_reader_unlock(&wm->rwlock);
    return found;
}
