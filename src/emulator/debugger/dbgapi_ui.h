/*
 * dbgapi_ui.h — API pro stranu UI (klient)
 *
 * Funkce v tomto headeru volá UI vlákno (hlavní vlákno, SDL event loop):
 * - Odesílání CMDRQ příkazů do emulátoru a čekání na odpovědi
 * - Příjem MSG notifikací z emulátoru (callback v SDL event loop)
 *
 * Includovat pouze v UI části kódu (src/ui-imgui/, src/ui/).
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
#ifndef DBGAPI_UI_H
#define DBGAPI_UI_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include <glib.h>
#include "app/app_thread.h"
#include "dbgapi_cmdrq.h"
#include "dbgapi_msg.h"

/* Globální fronta CMDRQ — definice v dbgapi.c */
extern st_DBGAPI_CMDRQ_QUEUE g_dbgapi_cmdrq_queue;


#ifdef __cplusplus
extern "C"
{
#endif

/* ============================================================================
 * ODESÍLÁNÍ PŘÍKAZŮ (CMDRQ) — UI → EMU
 *
 * UI vlákno vkládá příkazy do kruhového bufferu a čeká na odpovědi.
 * Všechny funkce jsou blokující — UI vlákno čeká na zpracování emulátorem.
 *
 * Vlastnictví paměti:
 * - data_ptr a result_ptr alokuje a vlastní volající
 * - data_ptr: vstupní data pro emulátor (může být NULL)
 * - result_ptr: buffer kam emulátor zapíše odpověď (může být NULL)
 * - Po návratu z submit_cmd_sync() klient může číst result_ptr a success
 * ============================================================================ */

/*
 * Synchronní odeslání příkazu do emulátoru.
 *
 * Vloží příkaz do CMDRQ fronty, probudí emulátorové vlákno (queue_cond)
 * a čeká na odpověď (slot->cond) s timeoutem.
 *
 * Parametry:
 *   queue:       ukazatel na CMDRQ frontu
 *   cmd:         příkaz (en_DBGAPI_CMD), volitelně OR s DBGAPI_CMDFLAG_BLOCKING
 *   data_ptr:    vstupní data pro emulátor (NULL pokud příkaz nepotřebuje data)
 *   result_ptr:  buffer pro odpověď (NULL pokud příkaz nevrací data)
 *   timeout_ms:  maximální čekání na odpověď v milisekundách (0 = neomezený)
 *
 * Vrací:
 *   true  = příkaz byl úspěšně zpracován (rq->success == true)
 *   false = chyba (timeout, fronta plná, emulátor se ukončuje, rq->success == false)
 *
 * Příklad:
 *   bool is_running;
 *   bool ok = dbgapi_ui_submit_cmd_sync(&g_dbgapi_cmdrq_queue,
 *                                       DBGAPI_CMD_IS_RUNNING,
 *                                       NULL, &is_running, 100);
 */
bool dbgapi_ui_submit_cmd_sync(st_DBGAPI_CMDRQ_QUEUE *queue,
                                en_DBGAPI_CMD cmd,
                                void *data_ptr,
                                void *result_ptr,
                                int timeout_ms);

/*
 * Zjistí, zda je CMDRQ fronta plná.
 * Vrací true pokud není místo pro další příkaz.
 * Thread-safe (zamyká queue_mutex).
 */
bool dbgapi_ui_queue_is_full(st_DBGAPI_CMDRQ_QUEUE *queue);

/*
 * Zjistí, zda se emulátor chystá ukončit.
 * Pokud vrátí true, UI by nemělo posílat další příkazy.
 * Thread-safe (zamyká queue_mutex).
 */
bool dbgapi_ui_is_ending(st_DBGAPI_CMDRQ_QUEUE *queue);


/* ============================================================================
 * PŘÍJEM MSG NOTIFIKACÍ — EMU → UI
 *
 * MSG přicházejí přes SDL event loop jako custom eventy.
 * Callback dbgapi_msg_handler_cb() se registruje jako handler
 * pro SDLAPP_WINDOW_EVENT_WINDOW_CALLBACK_REQUEST eventy.
 *
 * UI zaregistruje vlastní callback, který se zavolá při příjmu MSG.
 * Callback běží v kontextu UI vlákna (SDL event loop).
 * ============================================================================ */

/* Typ callbacku pro příjem MSG v UI */
typedef void (*dbgapi_msg_callback_t)(en_DBGAPI_MSG msg, st_DBGAPI_MSG_DATA *data, void *user_data);

/*
 * Registrace callbacku pro příjem MSG z emulátoru.
 * callback: funkce, která se zavolá v UI vlákně při příjmu MSG
 * user_data: libovolná data předávaná callbacku
 *
 * Může být zaregistrován pouze jeden callback. Opakované volání
 * přepíše předchozí registraci.
 *
 * Callback je zodpovědný za uvolnění msg_data (pokud není NULL):
 *   void my_msg_handler(en_DBGAPI_MSG msg, st_DBGAPI_MSG_DATA *data, void *ud)
 *   {
 *       switch (msg) { ... }
 *       if (data) g_free(data);
 *   }
 */
void dbgapi_ui_register_msg_callback(dbgapi_msg_callback_t callback, void *user_data);

/*
 * Odregistrace MSG callbacku.
 * Po zavolání nebudou MSG doručovány.
 */
void dbgapi_ui_unregister_msg_callback(void);


/**
 * @brief Doručit MSG do registrovaného UI callbacku.
 *
 * Volá registrovaný callback (přes dbgapi_ui_register_msg_callback)
 * synchronně v aktuálním vlákně. Volat z UI vlákna - typicky z SDL
 * handleru po thread switchi z EMU.
 *
 * Pokud žádný callback není zaregistrován, data se uvolní přes
 * g_free() a nic se neudělá. Jinak je zodpovědnost za uvolnění dat
 * na callbacku (per kontrakt dbgapi_msg_callback_t).
 *
 * @param msg   Typ zprávy.
 * @param data  Volitelná data - vlastnictví předáno callbacku, nebo
 *              uvolněno pokud žádný callback.
 *
 * @note Tato funkce je veřejné API mezi UI dispatcherem a registrovaným
 *       MSG callbackem. dbgapi.c neví o SDL ani sdlapp - thread switch
 *       řeší výhradně UI vrstva (= dbgapi_dispatcher v src/ui-imgui/).
 */
void dbgapi_ui_invoke_msg_callback(en_DBGAPI_MSG msg, st_DBGAPI_MSG_DATA *data);


#ifdef __cplusplus
}
#endif

#endif /* DBGAPI_UI_H */
