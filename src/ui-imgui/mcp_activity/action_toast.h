/*
 * File:   action_toast.h
 *
 * V1.C.3 - selektivní toast popup pro destruktivní MCP akce.
 *
 * Účel: dát uživateli rychlou vizuální zpětnou vazbu kdykoliv AI klient
 * provede potenciálně destruktivní operaci (= reset emulátoru, load
 * snapshotu, platform set, start/stop emulace). Bez tohoto toast okna
 * by Michal měl jediný feedback v MCP Activity log okně, které ale
 * běžně není otevřené.
 *
 * Filtrování:
 *   - Toast se aktivuje POUZE pro origin == DBGAPI_CMD_ORIGIN_MCP.
 *     USER akce trigger nemají - Michal si je sám vyvolal.
 *   - Z MCP akcí jen "destruktivní" subset (viz action_toast_is_destructive
 *     v .cpp). Read-only operace (mem_read, bp_list, ...) trigger nemají.
 *
 * Render:
 *   - Top-right roh aktuálního viewport, vertical stack pro až N souběžných
 *     toastů (= rychlé série akcí).
 *   - Auto-fade po N sekundách (default 4.0s).
 *   - Lze zavřít klikem (= close button v rohu toast).
 *
 * Threading:
 *   - action_toast_show() je thread-safe (volá se z dbgapi dispatcher
 *     hooku, který běží v UI vlákně po thread-switchi z EMU). Defenzivně
 *     chráněno GMutex.
 *   - action_toast_render() volá main render loop v UI vlákně.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later, viz licence header v action_toast.cpp.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef ACTION_TOAST_H
#define ACTION_TOAST_H

#include <stdbool.h>
#include "emulator/debugger/dbgapi_cmdrq.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximální počet souběžných toastů ve stacku.
 *
 * Při dosažení limitu se nejstarší toast preemptivně odstraní (drop-tail-old).
 */
#define ACTION_TOAST_MAX 3

/**
 * @brief Default délka zobrazení jednoho toastu (sekundy).
 */
#define ACTION_TOAST_DURATION_SEC 4.0f


/**
 * @brief Jednorázová inicializace toast subsystému.
 *
 * Idempotentní - druhé volání je no-op.
 *
 * @pre Žádné.
 * @post Toast queue je prázdná, GMutex inicializován,
 *       action_toast_show() je možné volat.
 */
void action_toast_init(void);


/**
 * @brief Uvolnění interních struktur.
 *
 * Po volání už action_toast_show() ani action_toast_render() nesmí
 * být voláno (= toast queue + mutex zničeny).
 */
void action_toast_shutdown(void);


/**
 * @brief Vrátí true pokud daný (origin, cmd) je destruktivní a má
 *        trigger toast.
 *
 * Toast se zobrazí jen pokud:
 *   - origin == DBGAPI_CMD_ORIGIN_MCP
 *   - cmd patří do whitelisted destruktivní množiny:
 *     EMU_RESET, EMU_START, EMU_STOP, SNAPSHOT_LOAD*, PLATFORM_SET,
 *     PERIPH_ATTACH/DETACH.
 *
 * Pro ostatní kombinace vrací false.
 *
 * @param origin Hodnota en_DBGAPI_CMD_ORIGIN.
 * @param cmd    Identifikátor CMDRQ příkazu (en_DBGAPI_CMD).
 * @return true pokud má být vyvolán toast, false jinak.
 */
bool action_toast_is_destructive(en_DBGAPI_CMD_ORIGIN origin,
                                 en_DBGAPI_CMD cmd);


/**
 * @brief Přidá toast do queue, pokud (origin, cmd) projde destruktivním
 *        filtrem.
 *
 * Thread-safe. Pokud (origin, cmd) není destruktivní, je no-op (= bezpečné
 * volat z dispatcheru pro každou akci, filter řeší interně).
 *
 * Pokud je queue plná (ACTION_TOAST_MAX), nejstarší toast se odstraní.
 *
 * @param origin       Zdroj akce (typicky DBGAPI_CMD_ORIGIN_MCP).
 * @param cmd          Identifikátor CMDRQ příkazu.
 * @param description  Human-readable popis (NULL = jen cmd jméno).
 */
void action_toast_show(en_DBGAPI_CMD_ORIGIN origin,
                       en_DBGAPI_CMD cmd,
                       const char *description);


/**
 * @brief Vykreslí všechny aktivní toasty v aktuálním ImGui frame.
 *
 * Volat per-frame z main render loop. Fade-out + auto-remove řeší interně
 * podle elapsed real-time.
 *
 * @pre Aktivní ImGui kontext, volání v UI vlákně.
 */
void action_toast_render(void);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ACTION_TOAST_H */
