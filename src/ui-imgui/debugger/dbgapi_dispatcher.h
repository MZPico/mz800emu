/*
 * dbgapi_dispatcher.h - UI dispatcher pro thread switch EMU -> UI
 *
 * Implementuje thread switch z EMU vlákna do UI vlákna pro doručení
 * MSG notifikací z dbgapi. Dbgapi (= EMU strana) volá zaregistrovaný
 * dispatcher z dbgapi_emu_send_msg() v EMU vlákně, dispatcher posílá
 * SDL custom event do hlavního okna a SDL handler pak v UI vlákně volá
 * dbgapi_ui_invoke_msg_callback() pro doručení listeneru.
 *
 * Tímto rozdělením EMU strana dbgapi (= dbgapi.c) nezná SDL ani sdlapp -
 * thread switch je čistě v UI vrstvě.
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
#ifndef UI_DBGAPI_DISPATCHER_H
#define UI_DBGAPI_DISPATCHER_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Inicializace dbgapi UI dispatcheru.
 *
 * Zaregistruje SDL-based dispatcher do dbgapi přes
 * dbgapi_emu_register_msg_dispatcher(). Po této registraci EMU vlákno
 * doručuje MSG notifikace přes thread switch do UI vlákna, kde se
 * volá zaregistrovaný listener (přes dbgapi_ui_register_msg_callback).
 *
 * Volat z UI vlákna při startup, idempotentní (lze volat opakovaně,
 * vždy přepíše předchozí registraci).
 *
 * @note Pokud žádný dispatcher zaregistrován, MSGy se v dbgapi tiše
 *       zahodí.
 */
void dbgapi_dispatcher_init(void);

/**
 * @brief Odregistrace dbgapi UI dispatcheru.
 *
 * Volat při shutdown UI vlákna. Po této funkci se MSGy z EMU
 * zahodí.
 */
void dbgapi_dispatcher_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DBGAPI_DISPATCHER_H */
