/**
 * @file   sdlapp.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Hlavní hlavičkový soubor knihovny SdlApp — SDL3 aplikační framework.
 *
 * Definuje hlavní strukturu SdlApp, režimy hlavní smyčky, typ callbacku
 * pro timery a veřejné API pro správu životního cyklu aplikace, řízení
 * hlavní smyčky, FPS limitu, custom eventů a timerů.
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
#ifndef SDLAPP_H
#define SDLAPP_H

#include <glib.h>
#include <SDL3/SDL.h>
#include "sdlapp_winmanager.h"
#include "sdlapp_window.h"
#include "sdlapp_paths.h"
#include "sdl3_backend/sdl3_backend.h"

/** @brief Makro pro výpis chybové hlášky s názvem funkce a číslem řádku. */
#define SDLAPP_ERROR(fmt, ...) \
    g_printerr("[ERROR] %s():%d: " fmt "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)

/** @brief Makro pro výpis varovné hlášky s názvem funkce a číslem řádku. */
#define WARN(fmt, ...) \
    g_warning("[WARNING] %s():%d: " fmt "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)

/** @brief Makro pro assert kontrolu podmínky. */
#define ASSERT(cond) g_assert(cond)

typedef struct SdlApp SdlApp;

/**
 * @brief Režim hlavní smyčky aplikace.
 *
 * Určuje způsob, jakým se v hlavní smyčce čeká na SDL eventy.
 */
typedef enum SdlAppLoopMode
{
    SDLAPP_LOOP_POLL,         /**< SDL_PollEvent — aktivní polling, výchozí režim */
    SDLAPP_LOOP_WAIT,         /**< SDL_WaitEvent — blokuje do příchodu eventu */
    SDLAPP_LOOP_WAIT_TIMEOUT  /**< SDL_WaitEventTimeout — čeká s timeoutem */
} SdlAppLoopMode;

/**
 * @brief Typ callbacku pro timer.
 *
 * Callback se volá v hlavním vlákně při vypršení intervalu timeru.
 *
 * @param timer_id Identifikátor timeru.
 * @param user_data Uživatelská data předaná při registraci timeru.
 * @return TRUE pro pokračování timeru, FALSE pro zastavení.
 */
typedef gboolean (*SdlAppTimerCb)(guint timer_id, gpointer user_data);

/**
 * @brief Hlavní struktura aplikace SdlApp.
 *
 * Obsahuje stav hlavní smyčky, window manager, nastavení FPS limitu,
 * registrované custom eventy a aktivní timery.
 */
typedef struct SdlApp
{
    gboolean running;                    /**< Příznak běhu hlavní smyčky */
    gboolean quit_requested;             /**< Příznak vyžádaného ukončení (nezávislý na running) */
    SdlAppWindowManager *manager;        /**< Správce aplikačních oken */
    SdlAppLoopMode loop_mode;            /**< Režim hlavní smyčky */
    gint32 wait_timeout_ms;              /**< Timeout pro SDLAPP_LOOP_WAIT_TIMEOUT [ms] */
    guint fps_limit;                     /**< Maximální počet snímků za sekundu (0 = bez limitu) */
    Uint64 last_frame_ticks;             /**< Časová značka posledního snímku [ns] */
    gboolean quit_on_last_window_close;  /**< Automatické ukončení při zavření posledního okna */
    GHashTable *custom_events;           /**< Registrované custom eventy (jméno → Uint32 event type) */
    GList *timers;                       /**< Seznam aktivních timerů (SdlAppTimer) */
    guint next_timer_id;                 /**< Další volné ID pro nový timer */
    Uint32 timer_event_type;             /**< SDL event type pro doručení timer callbacků do hlavního vlákna */
    SdlAppPaths *paths;                  /**< Aplikační adresáře (HOME/CFG/WORK) — viz sdlapp_paths.h */
} SdlApp;

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Vytvoří novou SDL aplikaci s SDL rendererem.
     *
     * Inicializuje SDL video subsystém, vytvoří window manager a nastaví
     * výchozí hodnoty všech parametrů.
     *
     * @return Ukazatel na novou instanci SdlApp, nebo NULL při selhání.
     */
    SdlApp *sdlapp_new(void);

    /**
     * @brief Vytvoří novou SDL aplikaci s OpenGL backendem.
     *
     * Inicializuje SDL video subsystém s OpenGL atributy (core profile 3.0,
     * double buffer, depth 24, stencil 8).
     *
     * @return Ukazatel na novou instanci SdlApp, nebo NULL při selhání.
     */
    SdlApp *sdlapp_opengl_new(void);

    /**
     * @brief Zničí aplikaci a uvolní všechny zdroje.
     *
     * Zastaví všechny timery, uvolní custom eventy, zničí všechna okna
     * přes window manager a ukončí SDL video subsystém. Bezpečné volat s NULL.
     *
     * @param app Ukazatel na aplikaci (může být NULL).
     */
    void sdlapp_destroy(SdlApp *app);

    /**
     * @brief Provede jednu iteraci hlavní smyčky.
     *
     * Zpracuje SDL eventy, provede rendering všech aktivních oken
     * a aplikuje FPS limiter. Užitečné pro manuální řízení smyčky.
     *
     * @param app Ukazatel na aplikaci.
     */
    void sdlapp_iteration(SdlApp *app);

    /**
     * @brief Spustí hlavní smyčku aplikace.
     *
     * Nastaví running=TRUE a opakovaně volá sdlapp_iteration() dokud
     * running zůstává TRUE.
     *
     * @param app Ukazatel na aplikaci.
     */
    void sdlapp_run(SdlApp *app);

    /**
     * @brief Požádá o ukončení hlavní smyčky.
     *
     * Nastaví quit_requested=TRUE a running=FALSE — hlavní smyčka se ukončí
     * po aktuální iteraci. Příznak quit_requested je nezávislý na running,
     * takže ukončení lze rozlišit i v okamžiku, kdy smyčka ještě nebyla
     * spuštěna (running==FALSE před prvním sdlapp_run). Bezpečné volat s NULL.
     *
     * @param app Ukazatel na aplikaci (může být NULL).
     */
    void sdlapp_quit(SdlApp *app);

    /**
     * @brief Zjistí, zda hlavní smyčka aplikace běží.
     *
     * @param app Ukazatel na aplikaci (může být NULL).
     * @return TRUE pokud aplikace běží, FALSE pokud ne nebo pokud app je NULL.
     */
    gboolean sdlapp_is_running(SdlApp *app);

    /**
     * @brief Zjistí, zda bylo vyžádáno ukončení aplikace.
     *
     * Na rozdíl od sdlapp_is_running rozlišuje stav "ukončení vyžádáno"
     * od stavu "smyčka ještě nebyla spuštěna" — oba mají running==FALSE.
     * Použij ve startup synchronizaci vláken, která čekají na rozběh
     * aplikace, aby korektně vyskočila i při ukončení před spuštěním smyčky.
     *
     * @param app Ukazatel na aplikaci (může být NULL).
     * @return TRUE pokud bylo vyžádáno ukončení, jinak FALSE (i pro NULL).
     */
    gboolean sdlapp_is_quit_requested(SdlApp *app);

    /**
     * @brief Nastaví režim hlavní smyčky.
     *
     * @param app Ukazatel na aplikaci.
     * @param mode Nový režim smyčky (POLL/WAIT/WAIT_TIMEOUT).
     * @param timeout_ms Timeout v milisekundách (použije se pouze pro WAIT_TIMEOUT).
     */
    void sdlapp_set_loop_mode(SdlApp *app, SdlAppLoopMode mode, gint32 timeout_ms);

    /**
     * @brief Zjistí aktuální režim hlavní smyčky.
     *
     * @param app Ukazatel na aplikaci.
     * @return Aktuální režim smyčky, SDLAPP_LOOP_POLL při NULL.
     */
    SdlAppLoopMode sdlapp_get_loop_mode(SdlApp *app);

    /**
     * @brief Nastaví FPS limit.
     *
     * @param app Ukazatel na aplikaci.
     * @param fps Maximální počet snímků za sekundu (0 = bez limitu).
     */
    void sdlapp_set_fps_limit(SdlApp *app, guint fps);

    /**
     * @brief Zjistí aktuální FPS limit.
     *
     * @param app Ukazatel na aplikaci.
     * @return Aktuální FPS limit, 0 při NULL.
     */
    guint sdlapp_get_fps_limit(SdlApp *app);

    /**
     * @brief Nastaví automatické ukončení při zavření posledního okna.
     *
     * @param app Ukazatel na aplikaci.
     * @param quit TRUE pro automatické ukončení, FALSE pro zachování běhu.
     */
    void sdlapp_set_quit_on_last_window_close(SdlApp *app, gboolean quit);

    /**
     * @brief Zjistí nastavení automatického ukončení při zavření posledního okna.
     *
     * @param app Ukazatel na aplikaci.
     * @return TRUE pokud je automatické ukončení zapnuto, TRUE při NULL.
     */
    gboolean sdlapp_get_quit_on_last_window_close(SdlApp *app);

    /**
     * @brief Odešle SDL user event cílený na konkrétní okno podle SDL WindowID.
     *
     * Používá SDL_PushEvent() — bezpečné volat z libovolného vlákna.
     *
     * @param win_id SDL identifikátor cílového okna.
     * @param type Typ SDL eventu.
     * @param code Uživatelský kód eventu.
     * @param data1 První uživatelský ukazatel.
     * @param data2 Druhý uživatelský ukazatel.
     * @return TRUE při úspěšném odeslání, FALSE při selhání.
     */
    gboolean sdlapp_send_message_for_SDL_windowID(SDL_WindowID win_id, Uint32 type, Sint32 code, gpointer data1, gpointer data2);

    /**
     * @brief Vrátí ukazatel na window manager aplikace.
     *
     * @param app Ukazatel na aplikaci.
     * @return Ukazatel na SdlAppWindowManager, NULL při NULL vstupu.
     */
    SdlAppWindowManager *sdlapp_get_winmanager(SdlApp *app);

    /**
     * @brief Zaregistruje nový pojmenovaný SDL custom event typ.
     *
     * Pokud jméno již existuje, vrátí existující typ a vypíše varování.
     *
     * @param app Ukazatel na aplikaci.
     * @param name Unikátní název event typu.
     * @return SDL event type (Uint32), nebo 0 při selhání.
     */
    Uint32 sdlapp_register_event_type(SdlApp *app, const gchar *name);

    /**
     * @brief Vyhledá dříve zaregistrovaný event typ podle jména.
     *
     * @param app Ukazatel na aplikaci.
     * @param name Název event typu.
     * @return SDL event type, nebo 0 pokud neexistuje.
     */
    Uint32 sdlapp_get_event_type(SdlApp *app, const gchar *name);

    /**
     * @brief Přidá periodický timer.
     *
     * Timer běží v SDL timer vlákně, ale uživatelský callback se volá
     * v hlavním vlákně (přes custom event).
     *
     * @param app Ukazatel na aplikaci.
     * @param interval_ms Interval v milisekundách (nesmí být 0).
     * @param cb Callback volaný v hlavním vlákně (TRUE=pokračovat, FALSE=zastavit).
     * @param user_data Uživatelská data předaná callbacku.
     * @return ID timeru (> 0), nebo 0 při selhání.
     */
    guint sdlapp_add_timer(SdlApp *app, guint interval_ms, SdlAppTimerCb cb, gpointer user_data);

    /**
     * @brief Zastaví a odstraní timer podle ID.
     *
     * Bezpečné volat s neexistujícím ID — v tom případě nedělá nic.
     *
     * @param app Ukazatel na aplikaci.
     * @param timer_id ID timeru k odstranění.
     */
    void sdlapp_remove_timer(SdlApp *app, guint timer_id);

#ifdef __cplusplus
}
#endif

#endif /* SDLAPP_H */
