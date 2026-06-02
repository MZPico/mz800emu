/*
 * File:   event_bus.h
 *
 * EVENT bus pro MCP backend (V1.A.4).
 *
 * Asynchronní per-connection EVENT fronta - emulátor (EMU vlákno) emituje
 * notifikace o událostech (BP hit, step done, paused, I/O write); MCP
 * klient si EVENTy vyzvedne přes Tool `emu_event_poll`.
 *
 * Životní cyklus:
 *   - `event_bus_init` jednou při startu MCP backendu.
 *   - Pro každou MCP connection: `event_bus_attach(conn_id)`.
 *   - Klient přes Tool: `event_bus_subscribe(conn_id, topics)`,
 *     `event_bus_unsubscribe(conn_id, topics)`.
 *   - EMU vlákno: `event_bus_emit(topic, payload_obj)` - emit do všech
 *     subscriberů daného topicu.
 *   - Klient přes Tool: `event_bus_poll(conn_id, timeout_ms, max_events)`
 *     - vyzvedne pending eventy.
 *   - Při uzavření connection: `event_bus_detach(conn_id)`.
 *   - `event_bus_shutdown` na konci.
 *
 * 4 podporované topics v V1.A.4:
 *   - `breakpoint_hit`  - BP fíruje (data: id, addr, type, hits)
 *   - `step_done`       - step_into/step_over/step_n dokončen
 *   - `paused`          - emu pozastaven (manual, fatal, BP hit)
 *   - `io_write`        - Z80 OUT na port (data: port, value, cycles)
 *
 * Thread safety:
 *   - Všechny funkce jsou thread-safe (interní GMutex).
 *   - `event_bus_emit` může být voláno z EMU vlákna; ostatní funkce
 *     typicky z dispatch handleru.
 *
 * Backpressure:
 *   - Per-connection queue má limit 100 eventů. Při překročení se
 *     nejstarší event drop-uje (FIFO drop-tail-old). Drop se loguje
 *     do stderr (= jednorázové varování + counter).
 *
 * Per-connection vs single-conn:
 *   - V1.A.4 transport (pipe) podporuje conn_id = 0 (single connection).
 *   - API je per-conn pro budoucí TCP multi-connection rozšíření.
 *
 * Scope V1.A.4:
 *   - Žádný server-pushed MCP `notifications/message` (= polling jen).
 *   - Žádné Resources subscribe.
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
 * ---------------------------------------------------------------------------
 */

#ifndef MZ800EMU_MCP_EVENT_BUS_H
#define MZ800EMU_MCP_EVENT_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <glib.h>
#include <json-glib/json-glib.h>


/** @brief Maximální počet eventů v per-connection queue (backpressure). */
#define EVENT_BUS_MAX_QUEUE_SIZE 100

/** @brief Default conn_id pro pipe transport (single connection). */
#define EVENT_BUS_CONN_PIPE 0


/**
 * @brief Inicializuje event bus subsystém.
 *
 * Alokuje globální struktury (subscribers list, mutex). Musí být voláno
 * jednou před prvním `event_bus_attach`. Idempotentní (= druhé volání
 * je no-op).
 */
void event_bus_init(void);


/**
 * @brief Uvolní všechny globální struktury event busu.
 *
 * Volá se při shutdown MCP backendu. Po návratu žádné další volání
 * event_bus_* funkcí nesmí proběhnout. Idempotentní.
 */
void event_bus_shutdown(void);


/**
 * @brief Zaregistruje novou connection v event busu.
 *
 * Vytvoří per-conn slot (= prázdná subscription set + prázdná queue).
 * Po `attach` může klient subscribovat na topics a polling vyzvedne
 * eventy z queue.
 *
 * @param conn_id  Identifikátor connection (pipe = 0, TCP per-conn N).
 *
 * @pre  event_bus_init() byl zavolán
 * @post conn_id je platný pro subscribe/poll/unsubscribe
 */
void event_bus_attach(int conn_id);


/**
 * @brief Odregistruje connection a uvolní její queue.
 *
 * Po `detach` další emit do tohoto conn_id je no-op. Subscribed topics
 * se zahodí. Pending eventy v queue se zahodí.
 *
 * @param conn_id  Identifikátor connection.
 *
 * Idempotentní (= detach neexistující conn_id je no-op).
 */
void event_bus_detach(int conn_id);


/**
 * @brief Subscribe connection na seznam topics.
 *
 * Topics se přidávají k existujícímu setu (= union). Neznámé topics se
 * tolerují (= klient může poslat budoucí topic, no-op pro V1.A.4).
 *
 * @param conn_id  Identifikátor connection (musí být attached).
 * @param topics   NULL-terminated pole topic stringů.
 * @return true při úspěchu, false pokud conn_id neexistuje.
 */
bool event_bus_subscribe(int conn_id, const char *const *topics);


/**
 * @brief Unsubscribe topics z connection.
 *
 * Pokud `topics` je NULL nebo empty array, odebere se VŠECHNY topics
 * (= unsubscribe all). Pending eventy v queue se NEzahodí (klient si je
 * stále může vyzvednout přes poll).
 *
 * @param conn_id  Identifikátor connection.
 * @param topics   NULL-terminated pole topic stringů, nebo NULL = all.
 * @return true při úspěchu, false pokud conn_id neexistuje.
 */
bool event_bus_unsubscribe(int conn_id, const char *const *topics);


/**
 * @brief Vyzvedne pending eventy z queue connection.
 *
 * Pokud queue je prázdná a `timeout_ms > 0`, blokuje (= GCond wait)
 * max `timeout_ms` ms na příchod prvního eventu. Při `timeout_ms == 0`
 * vrací okamžitě (možná prázdný array).
 *
 * Vrácený JsonArray vlastní caller (= musí volat g_object_unref nebo
 * json_node_free pokud zabalený do JsonNode).
 *
 * @param conn_id    Identifikátor connection.
 * @param timeout_ms Max čekání (0 = pure poll, max 60000).
 * @param max_events Max počet eventů ve výsledku (1..100).
 * @return Nový JsonArray s eventy, nebo NULL pokud conn_id neexistuje.
 *
 * Format jednotlivého eventu:
 *   { "topic": "...", "ts_us": <int>, "data": { ... topic-specific ... } }
 */
JsonArray *event_bus_poll(int conn_id, int timeout_ms, int max_events);


/**
 * @brief Emit eventu - distribuuje do všech subscriberů daného topicu.
 *
 * Volá se z EMU vlákna (BP hook, step done hook, paused signal, I/O
 * filter). Thread-safe (per-bus mutex).
 *
 * Vlastnictví `payload` přechází na funkci - po návratu už nesmí
 * caller `payload` použít (= funkce ho zkopíruje do per-conn queues
 * a uvolní původní).
 *
 * Pokud daný topic nemá žádné subscribery, payload se tiše uvolní
 * (= no-op).
 *
 * @param topic    Název topicu (statický string, neporušený obsah).
 * @param payload  JsonObject s topic-specific daty; vlastnictví přejímá funkce.
 */
void event_bus_emit(const char *topic, JsonObject *payload);


/**
 * @brief Rychlý test - má topic alespoň jednoho subscribera?
 *
 * Mutant mcp-server V1.A.5: hot-path guard pro emit místa, kde
 * sestavení JsonObject payload je netriviálně drahé (= Z80 OUT
 * loop, step done). Caller smí zkonstruovat payload jen pokud tato
 * funkce vrátí true, ušetří se alokace a deep copy.
 *
 * @param topic  Název topicu (např. "io_write").
 * @return true pokud existuje aspoň jeden subscriber tohoto topicu.
 *
 * Thread-safe (per-bus mutex). O(N) přes počet připojených connections,
 * ale za realistických podmínek (1-2 connections) zanedbatelné.
 */
bool event_bus_has_subscriber(const char *topic);


/**
 * @brief Diagnostika - počet pending eventů ve queue connection.
 *
 * @param conn_id  Identifikátor connection.
 * @return Počet eventů ve queue, nebo -1 pokud conn_id neexistuje.
 */
int event_bus_get_pending_count(int conn_id);


/**
 * @brief Diagnostika - počet drop-ovaných eventů kvůli backpressure.
 *
 * Counter monotónně roste od `attach`. Reset jen `detach + attach`.
 *
 * @param conn_id  Identifikátor connection.
 * @return Počet drop-ovaných eventů, nebo -1 pokud conn_id neexistuje.
 */
int event_bus_get_dropped_count(int conn_id);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MZ800EMU_MCP_EVENT_BUS_H */
