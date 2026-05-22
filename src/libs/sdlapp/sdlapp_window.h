/**
 * @file   sdlapp_window.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Veřejné API pro aplikační okna SdlApp.
 *
 * Definuje strukturu SdlAppWindow, stavový automat životního cyklu okna,
 * typy renderovacích backendů, callback typy a kompletní API pro vytvoření,
 * zničení, správu stavu, callbacků a vlastností aplikačních oken.
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
#ifndef SDLAPP_WINDOW_H
#define SDLAPP_WINDOW_H

#include "sdlapp.h"

/** @brief Počáteční hodnota pro interní sdlapp event typy oken. */
#define SDLAPP_WINDOW_FIRST_EVENT_TYPE (SDL_EVENT_USER + 1)

typedef struct SdlAppWindow SdlAppWindow;
typedef struct SdlAppWindowManager SdlAppWindowManager;

/**
 * @brief Callback pro inter-thread požadavek na zavolání funkce v hlavním vlákně.
 *
 * Volá se při příjmu eventu SDLAPP_WINDOW_EVENT_WINDOW_CALLBACK_REQUEST
 * v hlavním vlákně pro dané okno.
 *
 * @param win Ukazatel na cílové okno.
 * @param event SDL event nesoucí data požadavku.
 */
typedef void (*SdlAppWindowCallMeRqCb)(SdlAppWindow *, SDL_Event *);

/**
 * @brief Interní SDL event typy pro komunikaci s okny.
 */
typedef enum SdlAppWindowSDLEventType
{
    SDLAPP_WINDOW_EVENT_WINDOW_CLOSE_REQUESTED = SDLAPP_WINDOW_FIRST_EVENT_TYPE, /**< Požadavek na zavření okna */
    SDLAPP_WINDOW_EVENT_WINDOW_CALLBACK_REQUEST,                                  /**< Inter-thread callback request */
    SDLAPP_WINDOW_LAST_EVENT_TYPE                                                  /**< Koncová značka (nepoužívat) */
} SdlAppWindowSDLEventType;

/**
 * @brief Typ renderovacího backendu okna.
 */
typedef enum
{
    SDLAPP_RENDERER_SDL,    /**< SDL renderer — podpora více oken */
    SDLAPP_RENDERER_OPENGL, /**< OpenGL kontext — maximálně jedno SDL okno! */
} SdlAppRendererType;

/**
 * @brief Stavový automat životního cyklu okna.
 *
 * Definuje povolené stavy okna od vytvoření po zničení.
 * Přechody mezi stavy jsou validovány interně.
 */
typedef enum SdlAppWindowState
{
    SDLAPP_WINDOW_STATE_CREATED,     /**< Okno vytvořeno, renderer inicializován */
    SDLAPP_WINDOW_STATE_INITIALIZED, /**< init_callback úspěšně dokončen */
    SDLAPP_WINDOW_STATE_ACTIVE,      /**< Okno aktivní — zpracovává eventy a renderuje */
    SDLAPP_WINDOW_STATE_PAUSED,      /**< Okno pozastaveno (is_active=FALSE po předchozí aktivaci) */
    SDLAPP_WINDOW_STATE_DESTROYING,  /**< Probíhá destrukce okna */
    SDLAPP_WINDOW_STATE_DESTROYED    /**< Okno zničeno */
} SdlAppWindowState;

/**
 * @brief Callback pro inicializaci okna.
 * @param win Ukazatel na inicializované okno.
 * @param init_user_data Uživatelská data.
 * @return TRUE při úspěchu, FALSE při selhání (okno se zničí).
 */
typedef gboolean (*SdlAppWindowInitCb)(SdlAppWindow *, gpointer init_user_data);

/**
 * @brief Callback pro zpracování SDL eventu.
 * @param win Ukazatel na okno.
 * @param event SDL event ke zpracování.
 * @param event_user_data Uživatelská data.
 */
typedef void (*SdlAppWindowEventCb)(SdlAppWindow *, SDL_Event *, gpointer event_user_data);

/**
 * @brief Callback pro vykreslení snímku.
 * @param win Ukazatel na okno.
 * @param render_user_data Uživatelská data.
 */
typedef void (*SdlAppWindowRenderCb)(SdlAppWindow *, gpointer render_user_data);

/**
 * @brief Callback pro úklid před zničením okna.
 * @param win Ukazatel na ničené okno.
 * @param destroy_user_data Uživatelská data.
 */
typedef void (*SdlAppWindowDestroyCb)(SdlAppWindow *, gpointer destroy_user_data);

/**
 * @brief Callback pro požadavek na zavření okna.
 * @param win Ukazatel na okno, které se má zavřít.
 * @param user_data Uživatelská data.
 * @return TRUE pro povolení zavření, FALSE pro zrušení.
 */
typedef gboolean (*SdlAppWindowCloseRequestedCb)(SdlAppWindow *, gpointer user_data);

/**
 * @brief Callback pro drag & drop souboru.
 * @param win Ukazatel na cílové okno.
 * @param file_path Cesta k přetaženému souboru.
 * @param user_data Uživatelská data.
 */
typedef void (*SdlAppWindowDropFileCb)(SdlAppWindow *, const char *file_path, gpointer user_data);

/**
 * @brief Kontejner callbacků okna.
 *
 * Sdružuje všechny callback funkce a jejich uživatelská data
 * pro jedno aplikační okno.
 */
typedef struct SdlAppWindowCallbacks
{
    SdlAppWindowInitCb init;                     /**< Callback inicializace */
    gpointer init_data;                          /**< Data pro init callback */
    SdlAppWindowEventCb event;                   /**< Callback zpracování eventů */
    gpointer event_data;                         /**< Data pro event callback */
    SdlAppWindowRenderCb render;                 /**< Callback vykreslení snímku */
    gpointer render_data;                        /**< Data pro render callback */
    SdlAppWindowDestroyCb destroy;               /**< Callback destrukce */
    gpointer destroy_data;                       /**< Data pro destroy callback */
    SdlAppWindowCloseRequestedCb close_requested; /**< Callback požadavku na zavření */
    gpointer close_requested_data;               /**< Data pro close_requested callback */
    SdlAppWindowDropFileCb drop_file;            /**< Callback drag & drop souboru */
    gpointer drop_file_data;                     /**< Data pro drop_file callback */
} SdlAppWindowCallbacks;

/**
 * @brief Struktura aplikačního okna.
 *
 * Reprezentuje jedno SDL okno spravované knihovnou sdlapp. Obsahuje
 * SDL handle, renderer/GL kontext (union), stavový automat, mutex
 * pro thread-safe přístup a sadu callbacků.
 */
typedef struct SdlAppWindow
{
    char *name;                      /**< Unikátní název okna (pro vyhledávání) */
    SdlAppWindowManager *manager;    /**< Zpětný ukazatel na window manager */
    SdlAppWindow *parent;            /**< Rodičovské okno (pro popup, může být NULL) */
    gboolean is_active;              /**< Příznak aktivity (eventy + rendering) */
    SdlAppWindowState state;         /**< Aktuální stav ve stavovém automatu */
    gpointer gui_data;               /**< Uživatelská GUI data */
    SDL_Window *sdl_window;          /**< SDL okno */
    GMutex mutex;                    /**< Mutex pro thread-safe přístup */
    SdlAppRendererType renderer_type; /**< Typ renderovacího backendu */
    union
    {
        SDL_Renderer *sdl_renderer;  /**< SDL renderer (pro SDLAPP_RENDERER_SDL) */
        SDL_GLContext gl_context;    /**< OpenGL kontext (pro SDLAPP_RENDERER_OPENGL) */
    };
    SdlAppWindowCallbacks callbacks; /**< Sada callbacků okna */
} SdlAppWindow;

#ifdef __cplusplus
extern "C"
{
#endif

    /*****************************
     * Funkce pro vytvoření a zničení AppWindow
     */

    /**
     * @brief Vytvoří nové aplikační okno.
     *
     * Vytvoří SDL okno a renderer, zaregistruje v window manageru,
     * zavolá init_callback a nastaví pozici (hledá místo bez překrytí).
     *
     * @param wm Window manager.
     * @param name Unikátní název okna.
     * @param title Titulek okna (NULL = použije name).
     * @param width Šířka okna v pixelech.
     * @param height Výška okna v pixelech.
     * @param flags SDL_CreateWindow flagy.
     * @param type Typ renderovacího backendu.
     * @param init_callback Callback pro inicializaci (může být NULL).
     * @param init_user_data Data pro init_callback.
     * @return Ukazatel na nové okno, nebo NULL při selhání.
     */
    SdlAppWindow *sdlapp_window_create(SdlAppWindowManager *wm, const char *name, const char *title, int width, int height, Uint32 flags, SdlAppRendererType type, SdlAppWindowInitCb init_callback, gpointer init_user_data);

    /**
     * @brief Vytvoří popup okno relativně k rodičovskému oknu.
     *
     * @param wm Window manager.
     * @param name Unikátní název okna.
     * @param parent Rodičovské okno (nesmí být NULL).
     * @param offset_x Relativní X pozice vůči parentu.
     * @param offset_y Relativní Y pozice vůči parentu.
     * @param width Šířka okna v pixelech.
     * @param height Výška okna v pixelech.
     * @param flags SDL popup window flagy.
     * @param type Typ renderovacího backendu.
     * @param init_callback Callback pro inicializaci (může být NULL).
     * @param init_user_data Data pro init_callback.
     * @return Ukazatel na nové popup okno, nebo NULL při selhání.
     */
    SdlAppWindow *sdlapp_window_create_popup(SdlAppWindowManager *wm, const char *name, SdlAppWindow *parent, int offset_x, int offset_y, int width, int height, Uint32 flags, SdlAppRendererType type, SdlAppWindowInitCb init_callback, gpointer init_user_data);

    /**
     * @brief Vytvoří okno přes SDL_CreateWindowWithProperties.
     *
     * Umožňuje plnou kontrolu nad SDL vlastnostmi okna.
     *
     * @param wm Window manager.
     * @param name Unikátní název okna.
     * @param parent Rodičovské okno (může být NULL).
     * @param props SDL properties ID.
     * @param type Typ renderovacího backendu.
     * @param init_callback Callback pro inicializaci (může být NULL).
     * @param init_user_data Data pro init_callback.
     * @return Ukazatel na nové okno, nebo NULL při selhání.
     */
    SdlAppWindow *sdlapp_window_create_with_properties(SdlAppWindowManager *wm, const char *name, SdlAppWindow *parent, SDL_PropertiesID props, SdlAppRendererType type, SdlAppWindowInitCb init_callback, gpointer init_user_data);

    /**
     * @brief Zničí aplikační okno.
     *
     * Nastaví stav DESTROYING, zavolá destroy callback, odregistruje
     * z window manageru, zničí renderer/kontext, SDL okno a uvolní paměť.
     *
     * @param wm Window manager.
     * @param window Okno ke zničení.
     */
    void sdlapp_window_destroy(SdlAppWindowManager *wm, SdlAppWindow *window);

    /*****************************
     * Zpracování SDL_Event a renderingu
     */

    /**
     * @brief Zpracuje SDL event pro konkrétní okno.
     *
     * Obsluhuje inter-thread callback requesty, drag & drop souboru
     * a předání eventu uživatelskému event callbacku (pouze pro aktivní okna).
     *
     * @param win Ukazatel na okno.
     * @param event SDL event ke zpracování.
     */
    void sdlapp_window_process_event(SdlAppWindow *win, SDL_Event *event);

    /**
     * @brief Provede rendering pro okno.
     *
     * Zavolá render callback, nebo provede výchozí render (černá obrazovka
     * pro SDL renderer, swap pro OpenGL). Pouze pro aktivní okna.
     *
     * @param win Ukazatel na okno.
     */
    void sdlapp_window_process_render(SdlAppWindow *win);

    /*****************************
     * Odeslání zprávy oknu
     */

    /**
     * @brief Odešle SDL user event cílený na toto okno.
     *
     * Wrapper nad sdlapp_send_message_for_SDL_windowID(). Bezpečné
     * volat z libovolného vlákna.
     *
     * @param win Ukazatel na cílové okno.
     * @param type Typ SDL eventu.
     * @param code Uživatelský kód.
     * @param data1 První uživatelský ukazatel.
     * @param data2 Druhý uživatelský ukazatel.
     * @return TRUE při úspěšném odeslání, FALSE při selhání.
     */
    gboolean sdlapp_window_send_message(SdlAppWindow *win, Uint32 type, Sint32 code, gpointer data1, gpointer data2);

    /*****************************
     * Jméno okna
     */

    /**
     * @brief Vrátí interní ukazatel na jméno okna.
     *
     * @warning Ukazatel může být invalidován z jiného vlákna
     * (po přejmenování/zničení okna). Pro thread-safe přístup
     * použijte sdlapp_window_get_name_dup().
     *
     * @param win Ukazatel na okno.
     * @return Interní ukazatel na řetězec jména.
     */
    const char *sdlapp_window_get_name(SdlAppWindow *win);

    /**
     * @brief Vrátí thread-safe kopii jména okna.
     *
     * Volající musí uvolnit vrácený řetězec přes g_free().
     *
     * @param win Ukazatel na okno.
     * @return Kopie jména (volající uvolní přes g_free), nebo NULL.
     */
    gchar *sdlapp_window_get_name_dup(SdlAppWindow *win);

    /**
     * @brief Přejmenuje okno.
     *
     * Selže pokud jméno je prázdné nebo duplicitní v rámci window manageru.
     *
     * @param win Ukazatel na okno.
     * @param name Nové jméno.
     * @return TRUE při úspěchu, FALSE při selhání.
     */
    gboolean sdlapp_window_set_name(SdlAppWindow *win, const char *name);

    /*****************************
     * Aktivita okna
     */

    /**
     * @brief Zjistí, zda je okno aktivní. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @return TRUE pokud je okno aktivní, FALSE jinak.
     */
    gboolean sdlapp_window_get_active(SdlAppWindow *win);

    /**
     * @brief Aktivuje okno (přechod INITIALIZED/PAUSED → ACTIVE). Thread-safe.
     *
     * @param win Ukazatel na okno.
     */
    void sdlapp_window_activate(SdlAppWindow *win);

    /**
     * @brief Nastaví/odebere aktivitu okna. Thread-safe.
     *
     * Při active=TRUE: přechod INITIALIZED/PAUSED → ACTIVE.
     * Při active=FALSE: přechod ACTIVE → PAUSED.
     *
     * @param win Ukazatel na okno.
     * @param active Nový stav aktivity.
     */
    void sdlapp_window_set_active(SdlAppWindow *win, gboolean active);

    /*****************************
     * Stav okna (lifecycle state machine)
     */

    /**
     * @brief Vrátí aktuální stav okna ve stavovém automatu. Thread-safe.
     *
     * @param win Ukazatel na okno (při NULL vrací DESTROYED).
     * @return Aktuální stav okna.
     */
    SdlAppWindowState sdlapp_window_get_state(SdlAppWindow *win);

    /*****************************
     * GUI data
     */

    /**
     * @brief Vrátí uživatelská GUI data asociovaná s oknem. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @return Ukazatel na GUI data, nebo NULL.
     */
    gpointer sdlapp_window_get_gui_data(SdlAppWindow *win);

    /**
     * @brief Nastaví GUI data a vrátí předchozí hodnotu. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param gui_data Nová GUI data.
     * @return Předchozí hodnota GUI dat, nebo NULL.
     */
    gpointer sdlapp_window_set_gui_data(SdlAppWindow *win, gpointer gui_data);

    /*****************************
     * Gettery pro SDL handles (alternativa k přímému přístupu do struktury)
     */

    /**
     * @brief Vrátí SDL_Window handle okna.
     *
     * @param win Ukazatel na okno.
     * @return SDL_Window ukazatel, nebo NULL.
     */
    SDL_Window *sdlapp_window_get_sdl_window(SdlAppWindow *win);

    /**
     * @brief Vrátí SDL renderer okna.
     *
     * @param win Ukazatel na okno.
     * @return SDL_Renderer ukazatel, nebo NULL pokud okno používá OpenGL.
     */
    SDL_Renderer *sdlapp_window_get_sdl_renderer(SdlAppWindow *win);

    /**
     * @brief Vrátí OpenGL kontext okna.
     *
     * @param win Ukazatel na okno.
     * @return SDL_GLContext, nebo NULL pokud okno používá SDL renderer.
     */
    SDL_GLContext sdlapp_window_get_gl_context(SdlAppWindow *win);

    /**
     * @brief Vrátí typ renderovacího backendu okna.
     *
     * @param win Ukazatel na okno.
     * @return Typ rendereru (výchozí SDLAPP_RENDERER_SDL při NULL).
     */
    SdlAppRendererType sdlapp_window_get_renderer_type(SdlAppWindow *win);

    /**
     * @brief Vrátí window manager, ke kterému okno patří.
     *
     * @param win Ukazatel na okno.
     * @return Ukazatel na SdlAppWindowManager, nebo NULL.
     */
    SdlAppWindowManager *sdlapp_window_get_manager(SdlAppWindow *win);

    /*****************************
     * Window state wrappery
     */

    /**
     * @brief Zjistí velikost okna.
     *
     * @param win Ukazatel na okno.
     * @param w Výstupní parametr — šířka.
     * @param h Výstupní parametr — výška.
     */
    void sdlapp_window_get_size(SdlAppWindow *win, int *w, int *h);

    /**
     * @brief Nastaví velikost okna.
     *
     * @param win Ukazatel na okno.
     * @param w Nová šířka.
     * @param h Nová výška.
     */
    void sdlapp_window_set_size(SdlAppWindow *win, int w, int h);

    /**
     * @brief Zjistí pozici okna.
     *
     * @param win Ukazatel na okno.
     * @param x Výstupní parametr — X souřadnice.
     * @param y Výstupní parametr — Y souřadnice.
     */
    void sdlapp_window_get_position(SdlAppWindow *win, int *x, int *y);

    /**
     * @brief Nastaví pozici okna.
     *
     * @param win Ukazatel na okno.
     * @param x Nová X souřadnice.
     * @param y Nová Y souřadnice.
     */
    void sdlapp_window_set_position(SdlAppWindow *win, int x, int y);

    /**
     * @brief Nastaví režim celé obrazovky.
     *
     * @param win Ukazatel na okno.
     * @param fullscreen TRUE pro fullscreen, FALSE pro windowed.
     */
    void sdlapp_window_set_fullscreen(SdlAppWindow *win, gboolean fullscreen);

    /**
     * @brief Zjistí, zda je okno v režimu celé obrazovky.
     *
     * @param win Ukazatel na okno.
     * @return TRUE pokud je okno ve fullscreenu, FALSE jinak.
     */
    gboolean sdlapp_window_get_fullscreen(SdlAppWindow *win);

    /**
     * @brief Nastaví titulek okna.
     *
     * @param win Ukazatel na okno.
     * @param title Nový titulek.
     */
    void sdlapp_window_set_title(SdlAppWindow *win, const char *title);

    /**
     * @brief Vrátí titulek okna.
     *
     * @param win Ukazatel na okno.
     * @return Řetězec titulku, nebo NULL.
     */
    const char *sdlapp_window_get_title(SdlAppWindow *win);

    /**
     * @brief Minimalizuje okno.
     *
     * @param win Ukazatel na okno.
     */
    void sdlapp_window_minimize(SdlAppWindow *win);

    /**
     * @brief Maximalizuje okno.
     *
     * @param win Ukazatel na okno.
     */
    void sdlapp_window_maximize(SdlAppWindow *win);

    /**
     * @brief Obnoví okno z minimalizace/maximalizace.
     *
     * @param win Ukazatel na okno.
     */
    void sdlapp_window_restore(SdlAppWindow *win);

    /*****************************
     * Callbacky pro AppWindow
     */

    /**
     * @brief Vrátí init callback a jeho data. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param init_data Výstupní parametr pro data callbacku (může být NULL).
     * @return Ukazatel na init callback, nebo NULL.
     */
    SdlAppWindowInitCb sdlapp_window_get_init_cb(SdlAppWindow *win, gpointer *init_data);

    /**
     * @brief Nastaví event callback. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param event_cb Nový event callback (může být NULL).
     * @param event_data Uživatelská data pro callback.
     */
    void sdlapp_window_set_event_cb(SdlAppWindow *win, SdlAppWindowEventCb event_cb, gpointer event_data);

    /**
     * @brief Vrátí event callback a jeho data. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param event_data Výstupní parametr pro data callbacku (může být NULL).
     * @return Ukazatel na event callback, nebo NULL.
     */
    SdlAppWindowEventCb sdlapp_window_get_event_cb(SdlAppWindow *win, gpointer *event_data);

    /**
     * @brief Nastaví render callback. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param render_cb Nový render callback (může být NULL).
     * @param render_data Uživatelská data pro callback.
     */
    void sdlapp_window_set_render_cb(SdlAppWindow *win, SdlAppWindowRenderCb render_cb, gpointer render_data);

    /**
     * @brief Vrátí render callback a jeho data. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param render_data Výstupní parametr pro data callbacku (může být NULL).
     * @return Ukazatel na render callback, nebo NULL.
     */
    SdlAppWindowRenderCb sdlapp_window_get_render_cb(SdlAppWindow *win, gpointer *render_data);

    /**
     * @brief Nastaví destroy callback. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param destroy_cb Nový destroy callback (může být NULL).
     * @param destroy_data Uživatelská data pro callback.
     */
    void sdlapp_window_set_destroy_cb(SdlAppWindow *win, SdlAppWindowDestroyCb destroy_cb, gpointer destroy_data);

    /**
     * @brief Vrátí destroy callback a jeho data. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param destroy_data Výstupní parametr pro data callbacku (může být NULL).
     * @return Ukazatel na destroy callback, nebo NULL.
     */
    SdlAppWindowDestroyCb sdlapp_window_get_destroy_cb(SdlAppWindow *win, gpointer *destroy_data);

    /**
     * @brief Nastaví close_requested callback. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param cb Nový close_requested callback (může být NULL).
     * @param user_data Uživatelská data pro callback.
     */
    void sdlapp_window_set_close_requested_cb(SdlAppWindow *win, SdlAppWindowCloseRequestedCb cb, gpointer user_data);

    /**
     * @brief Vrátí close_requested callback a jeho data. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param user_data Výstupní parametr pro data callbacku (může být NULL).
     * @return Ukazatel na close_requested callback, nebo NULL.
     */
    SdlAppWindowCloseRequestedCb sdlapp_window_get_close_requested_cb(SdlAppWindow *win, gpointer *user_data);

    /**
     * @brief Nastaví drag & drop callback. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param cb Nový drop_file callback (může být NULL).
     * @param user_data Uživatelská data pro callback.
     */
    void sdlapp_window_set_drop_file_cb(SdlAppWindow *win, SdlAppWindowDropFileCb cb, gpointer user_data);

    /**
     * @brief Vrátí drag & drop callback a jeho data. Thread-safe.
     *
     * @param win Ukazatel na okno.
     * @param user_data Výstupní parametr pro data callbacku (může být NULL).
     * @return Ukazatel na drop_file callback, nebo NULL.
     */
    SdlAppWindowDropFileCb sdlapp_window_get_drop_file_cb(SdlAppWindow *win, gpointer *user_data);

#ifdef __cplusplus
}
#endif

#endif /* SDLAPP_WINDOW_H */
