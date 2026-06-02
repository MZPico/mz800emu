/*
 * File:   trap_manager.h
 *
 * TRAP manager pro MCP backend (V1.A.4).
 *
 * Lehký nadstavbový mechanismus nad EVENT busem pro "blokující" události,
 * které vyžadují klientovu odpověď. V V1.A.4 je TRAP **non-blocking pro
 * EMU vlákno** - emu už se zastavuje skrz `emulator_pause(true)` v BP
 * hooku (= reuse existující pause smyčky), trap_manager jen:
 *
 *   1) Při BP hit (pause action = empty), EMU vlákno volá
 *      `trap_manager_emit_breakpoint_hit()` - dostane unikátní trap_id,
 *      event bus emituje topic "breakpoint_hit" s trap_id v payloadu.
 *      EMU vlákno pokračuje svou normální cestou (= emulator_pause +
 *      MSG_BREAKPOINT_HIT) a vstoupí do pause loopu.
 *
 *   2) AI klient přes `emu_event_poll` dostane event a vidí trap_id.
 *      Pošle `emu_trap_respond(trap_id, action)`.
 *
 *   3) Handler `_handle_trap_respond` v dispatch.c mapuje action na CMD:
 *        - "continue"   -> DBGAPI_CMD_RUN
 *        - "step_into"  -> DBGAPI_CMD_STEP_INTO
 *        - "step_over"  -> DBGAPI_CMD_STEP_OVER
 *        - "abort"      -> DBGAPI_CMD_RUN (= V1.A.4 minimum;
 *                           graceful abort = V1.A.5+)
 *      a submit-uje příslušný CMDRQ. Trap_id se z map odstraní.
 *
 * Toto NENÍ skutečné GCond-blocking forwarding - emu nečeká na klientovu
 * odpověď v synchronní cestě. Pause je řešena existujícím pause flagem
 * v emulator core; klientovi je dán strukturovaný způsob jak se zeptat
 * "kterým BP jsem trefil, co s tím" a jak na to reagovat. Reálné blocking
 * forwarding (= emu vlákno blokované v GCond) je odloženo do V1.A.5+
 * kvůli komplexitě interakce s pause loopem a více-vláknovou shutdown
 * cestou.
 *
 * Trap_id space:
 *   - Monotónně rostoucí int64 counter (sdílený napříč všemi connections).
 *   - 0 je rezervováno (= "no trap").
 *
 * Žádný hard TTL - trapy expirují tichým způsobem:
 *   - Pokud klient pošle trap_respond na trap_id, který už byl vyřízen
 *     (= run/step_into byl už mezitím přijat jinou cestou), respond
 *     vrátí ok=false s důvodem "unknown trap_id".
 *
 * ----------------------------- License -------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MZ800EMU_MCP_TRAP_MANAGER_H
#define MZ800EMU_MCP_TRAP_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>


/**
 * @brief Akce pro emu_trap_respond.
 *
 * Mapování na dbgapi CMD viz Doxygen u trap_manager_dispatch_response.
 */
typedef enum en_TRAP_ACTION {
    TRAP_ACTION_INVALID    = 0, /**< Neplatná hodnota (chybný string). */
    TRAP_ACTION_CONTINUE   = 1, /**< Pokračovat (= RUN). */
    TRAP_ACTION_STEP_INTO  = 2, /**< Krok dovnitř. */
    TRAP_ACTION_STEP_OVER  = 3, /**< Krok přes CALL. */
    TRAP_ACTION_ABORT      = 4, /**< Abort (= v V1.A.4 mapuje na RUN). */
} en_TRAP_ACTION;


/**
 * @brief Inicializuje trap manager.
 *
 * Alokuje counter a pending traps map. Idempotentní.
 */
void trap_manager_init(void);


/**
 * @brief Uvolní trap manager + pending traps mapu.
 *
 * Volá se při shutdown MCP backendu.
 */
void trap_manager_shutdown(void);


/**
 * @brief Zaregistruje nový TRAP a vrátí jeho ID.
 *
 * Volá se z EMU vlákna v BP hooku těsně před `emulator_pause`. Vrácený
 * trap_id má klient použít v `emu_trap_respond`.
 *
 * @return Unikátní trap_id > 0.
 */
int64_t trap_manager_register(void);


/**
 * @brief Parse string action ("continue" / "step_into" / "step_over" / "abort")
 *        na enum.
 *
 * @param str  Action string nebo NULL.
 * @return Odpovídající enum, nebo TRAP_ACTION_INVALID.
 */
en_TRAP_ACTION trap_manager_parse_action(const char *str);


/**
 * @brief Zruší registraci TRAPu a vrátí success flag.
 *
 * Volá se z dispatch handleru `emu_trap_respond` před tím, než se
 * skutečně submitne RUN/STEP CMD. Pokud trap_id není v mapě (= už
 * vyřízen / neexistoval), vrací false a caller pošle error response.
 *
 * @param trap_id  ID z `trap_manager_register`.
 * @return true pokud trap_id byl pending a byl odstraněn, false jinak.
 */
bool trap_manager_consume(int64_t trap_id);


/**
 * @brief Diagnostika - počet pending traps.
 *
 * @return Aktuální velikost pending traps mapy.
 */
int trap_manager_pending_count(void);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MZ800EMU_MCP_TRAP_MANAGER_H */
