/*
 * File:   jsonl_io.h
 *
 * Veřejné API JSONL transportu pro MCP backend (V0.A.2 - core).
 *
 * JSONL = line-delimited JSON. Každá zpráva je jeden JSON objekt
 * zapsaný na jednom řádku, oddělovač `\n`. Žádné multi-line JSON,
 * žádné pretty-print. Sdílený parser/encoder pro pipe i (později)
 * TCP transport.
 *
 * Tato vrstva poskytuje:
 *  - parser řádku do opaque `st_JSONL_MESSAGE` struktury
 *  - encoder builders pro 6 typů zpráv (REQUEST, RESPONSE, EVENT,
 *    TRAP, TRAP_RESPONSE, HELLO)
 *  - stream I/O (`FILE *`) helpery pro line-buffered čtení/zápis
 *
 * Co tato vrstva NEdělá (= scope V0.A.3+):
 *  - žádné mapování `cmd` -> DBGAPI příkaz (= dispatch table V0.A.3)
 *  - žádné napojení na emu state / dbgapi
 *  - žádný TCP socket kód (= V0.A.5)
 *  - žádný state machine pro request/response korelaci
 *
 * Thread-safety:
 *  - Parser i encoder jsou re-entrantní (žádný globální stav).
 *  - Stream I/O funkce `jsonl_read_line` / `jsonl_write_line` operují
 *    nad `FILE *` který si caller spravuje sám. Sdílí-li se stejný
 *    `FILE *` mezi více vlákny pro zápis, caller MUSÍ poskytnout
 *    vlastní synchronizaci (= mutex). V0.A.5 doplní wrapper s mutexem
 *    pro TCP transport.
 *
 * Reference: `mcp-server/plans/INSTRUKCE-faze-V0.A.2-jsonl-parser.md`,
 * `debugger/rozbory/budoucnost/mcp-server/README.md` sekce 6.3.
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

#ifndef MZ800EMU_MCP_JSONL_IO_H
#define MZ800EMU_MCP_JSONL_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>


    /**
     * @brief Typ JSONL zprávy.
     *
     * Identifikuje sémantiku JSON objektu na drátě. Parser typ
     * určuje podle přítomnosti polí `type` (přednostně) a `req_id`
     * / `cmd` / `trap_id`. Konkrétní pravidla viz `jsonl_parse_line`.
     */
    typedef enum en_JSONL_MSG_TYPE {
        /** @brief Žádost klienta o vykonání příkazu (req_id + cmd + data?). */
        JSONL_MSG_REQUEST = 0,
        /** @brief Odpověď serveru na REQUEST (req_id + success + data? / error?). */
        JSONL_MSG_RESPONSE,
        /** @brief Asynchronní notifikace ze serveru (type:event + topic + data?). */
        JSONL_MSG_EVENT,
        /** @brief Blokující dotaz ze serveru ke klientovi (trap_id + topic + data + timeout_ms). */
        JSONL_MSG_TRAP,
        /** @brief Odpověď klienta na TRAP (trap_id + action). */
        JSONL_MSG_TRAP_RESPONSE,
        /** @brief Úvodní discovery zpráva serveru (type:hello + version + capabilities + commands). */
        JSONL_MSG_HELLO,
    } en_JSONL_MSG_TYPE;


    /**
     * @brief Návratový kód operací nad JSONL.
     *
     * Hodnoty jsou stabilní (export do dispatch tabulky V0.A.3+). Nové
     * kódy se přidávají na konec.
     */
    typedef enum en_JSONL_RESULT {
        /** @brief Úspěch. */
        JSONL_OK = 0,
        /** @brief Stream dosáhl EOF před načtením kompletního řádku. */
        JSONL_ERR_EOF,
        /** @brief Chyba I/O při čtení/zápisu streamu. */
        JSONL_ERR_IO,
        /** @brief Vstupní řádek neobsahuje validní JSON. */
        JSONL_ERR_PARSE,
        /** @brief JSON je platný, ale neodpovídá schématu JSONL zprávy. */
        JSONL_ERR_SCHEMA,
        /** @brief Selhání alokace paměti. */
        JSONL_ERR_ALLOC,
    } en_JSONL_RESULT;


    /**
     * @brief Opaque handle parsované JSONL zprávy.
     *
     * Drží referenci na vnitřní JSON strom (json-glib `JsonNode`).
     * Caller přistupuje k polím přes getter funkce (`jsonl_msg_get_*`).
     * Uvolnění výhradně přes `jsonl_msg_free()`.
     *
     * Lifetime: vlastní svůj vnitřní JSON strom (parse vytvoří kopii).
     * Getter funkce vrací `const` view do tohoto stromu - vrácené
     * stringy / sub-uzly jsou platné jen do `jsonl_msg_free()`.
     */
    typedef struct st_JSONL_MESSAGE st_JSONL_MESSAGE;


    /* ------------------------------------------------------------------ */
    /* Parser API                                                          */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Parsuje jeden JSONL řádek do opaque struktury.
     *
     * Vstupní `line` musí být null-terminated JSON objekt bez trailing
     * `\n`. Funkce alokuje novou `st_JSONL_MESSAGE` a uloží do
     * `*out_msg`. Při chybě `*out_msg` zůstane `NULL`.
     *
     * Klasifikace typu:
     *  - Pole `type` rozhoduje primárně: "hello" -> HELLO, "event" ->
     *    EVENT, "trap" -> TRAP, "trap_response" -> TRAP_RESPONSE.
     *  - Bez pole `type`: přítomnost `req_id` + `cmd` -> REQUEST,
     *    přítomnost `req_id` + `success` -> RESPONSE.
     *  - Jinak `JSONL_ERR_SCHEMA`.
     *
     * @param[in]  line     null-terminated JSON string (musí být objekt)
     * @param[out] out_msg  výstupní handle (caller uvolní `jsonl_msg_free`)
     *
     * @return JSONL_OK / JSONL_ERR_PARSE / JSONL_ERR_SCHEMA / JSONL_ERR_ALLOC
     *
     * @pre  line != NULL, out_msg != NULL
     * @post při JSONL_OK je `*out_msg` validní handle, jinak NULL
     */
    en_JSONL_RESULT jsonl_parse_line(const char *line,
                                     st_JSONL_MESSAGE **out_msg);

    /**
     * @brief Uvolní zprávu vytvořenou `jsonl_parse_line`.
     *
     * Bezpečné volat s `NULL` (no-op). Po návratu jsou všechny pointery
     * vrácené z `jsonl_msg_get_*` neplatné.
     *
     * @param[in] msg  handle k uvolnění nebo NULL
     */
    void jsonl_msg_free(st_JSONL_MESSAGE *msg);


    /* ------------------------------------------------------------------ */
    /* Getters - read-only view do parsovaného JSON stromu                 */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Vrátí typ zprávy.
     *
     * @param[in] msg  validní handle
     * @return typ z `en_JSONL_MSG_TYPE`
     * @pre msg != NULL
     */
    en_JSONL_MSG_TYPE jsonl_msg_get_type(const st_JSONL_MESSAGE *msg);

    /**
     * @brief Vrátí `req_id` (REQUEST / RESPONSE).
     *
     * Pro zprávy které `req_id` nemají vrací -1.
     *
     * @param[in] msg  validní handle
     * @return req_id nebo -1
     * @pre msg != NULL
     */
    int64_t jsonl_msg_get_req_id(const st_JSONL_MESSAGE *msg);

    /**
     * @brief Vrátí `cmd` (REQUEST).
     *
     * Vrácený pointer je platný do `jsonl_msg_free`. Vrací NULL pokud
     * zpráva pole `cmd` nemá.
     *
     * @param[in] msg  validní handle
     * @return cmd string nebo NULL
     * @pre msg != NULL
     */
    const char *jsonl_msg_get_cmd(const st_JSONL_MESSAGE *msg);

    /**
     * @brief Vrátí `topic` (EVENT / TRAP).
     *
     * Vrácený pointer je platný do `jsonl_msg_free`. Vrací NULL pokud
     * zpráva pole `topic` nemá.
     *
     * @param[in] msg  validní handle
     * @return topic string nebo NULL
     * @pre msg != NULL
     */
    const char *jsonl_msg_get_topic(const st_JSONL_MESSAGE *msg);

    /**
     * @brief Vrátí `trap_id` (TRAP / TRAP_RESPONSE).
     *
     * Pro zprávy bez `trap_id` vrací -1.
     *
     * @param[in] msg  validní handle
     * @return trap_id nebo -1
     * @pre msg != NULL
     */
    int64_t jsonl_msg_get_trap_id(const st_JSONL_MESSAGE *msg);

    /**
     * @brief Vrátí `action` (TRAP_RESPONSE).
     *
     * Vrácený pointer je platný do `jsonl_msg_free`. Vrací NULL pokud
     * zpráva pole `action` nemá.
     *
     * @param[in] msg  validní handle
     * @return action string nebo NULL
     * @pre msg != NULL
     */
    const char *jsonl_msg_get_action(const st_JSONL_MESSAGE *msg);

    /**
     * @brief Vrátí `success` flag (RESPONSE).
     *
     * Pro zprávy bez `success` vrací false. Caller by měl typ ověřit
     * přes `jsonl_msg_get_type` než se na hodnotu spolehne.
     *
     * @param[in] msg  validní handle
     * @return success flag
     * @pre msg != NULL
     */
    bool jsonl_msg_get_success(const st_JSONL_MESSAGE *msg);

    /**
     * @brief Vrátí chybovou zprávu (RESPONSE s success=false).
     *
     * Vrácený pointer je platný do `jsonl_msg_free`. NULL pokud pole
     * `error` chybí.
     *
     * @param[in] msg  validní handle
     * @return error message nebo NULL
     * @pre msg != NULL
     */
    const char *jsonl_msg_get_error(const st_JSONL_MESSAGE *msg);

    /**
     * @brief Vrátí `timeout_ms` (TRAP).
     *
     * Hodnota -1 pokud pole chybí.
     *
     * @param[in] msg  validní handle
     * @return timeout v ms nebo -1
     * @pre msg != NULL
     */
    int64_t jsonl_msg_get_timeout_ms(const st_JSONL_MESSAGE *msg);

    /**
     * @brief Vrátí opaque ukazatel na JSON sub-strom `data`.
     *
     * Typ skutečného objektu je `JsonNode *` (json-glib), exponuje se
     * jako `void *` aby header neměl povinný include `<json-glib/...>`.
     * Caller který chce data interpretovat musí includovat json-glib
     * sám a castovat.
     *
     * Vrácený pointer je platný do `jsonl_msg_free`. NULL pokud pole
     * `data` chybí.
     *
     * @param[in] msg  validní handle
     * @return JsonNode * cast na void * nebo NULL
     * @pre msg != NULL
     */
    void *jsonl_msg_get_data_node(const st_JSONL_MESSAGE *msg);


    /* ------------------------------------------------------------------ */
    /* Encoder API - vrací nově alokované JSONL řádky (bez trailing '\n') */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Sestaví JSONL řádek typu REQUEST.
     *
     * Vrácený string je single-line JSON object bez trailing `\n` (ten
     * doplní `jsonl_write_line`). Volající uvolní přes `free()` (alokuje
     * se `g_strdup` ekvivalentem - viz implementace).
     *
     * @param[in] req_id     monotonní ID requestu (>= 0)
     * @param[in] cmd        jméno příkazu (musí být non-NULL, non-empty)
     * @param[in] data_node  volitelný JSON sub-strom s parametry
     *                       (`JsonNode *` cast na void *), předává se
     *                       ownership? NE - encoder dělá deep-copy
     *                       a caller `data_node` může uvolnit kdykoliv
     * @return alokovaný string (caller `free`) nebo NULL při chybě
     *
     * @pre cmd != NULL && cmd[0] != '\0'
     */
    char *jsonl_build_request(int64_t req_id,
                              const char *cmd,
                              void *data_node);

    /**
     * @brief Sestaví JSONL řádek typu RESPONSE.
     *
     * Při `success == false` se připojí pole `error` (musí být
     * non-NULL). Při `success == true` se připojí pole `data` pokud
     * `data_node != NULL`.
     *
     * @param[in] req_id     ID requestu na který odpovídáme
     * @param[in] success    true = data, false = error
     * @param[in] data_node  volitelný JSON sub-strom (success=true);
     *                       deep-copy, caller si vlastní pointer
     * @param[in] error_msg  povinný při success=false, jinak ignorován
     *                       (texty: anglicky, viz CLAUDE.md i18n)
     * @return alokovaný string (caller `free`) nebo NULL při chybě
     *
     * @pre success || error_msg != NULL
     */
    char *jsonl_build_response(int64_t req_id,
                               bool success,
                               void *data_node,
                               const char *error_msg);

    /**
     * @brief Sestaví JSONL řádek typu EVENT.
     *
     * @param[in] topic      povinné, např. "emu.paused", "memory.write"
     * @param[in] data_node  volitelný JSON sub-strom (deep-copy)
     * @return alokovaný string (caller `free`) nebo NULL
     *
     * @pre topic != NULL && topic[0] != '\0'
     */
    char *jsonl_build_event(const char *topic, void *data_node);

    /**
     * @brief Sestaví JSONL řádek typu TRAP.
     *
     * TRAP = blokující dotaz serveru ke klientovi. Klient odpovídá
     * `TRAP_RESPONSE` se stejným `trap_id` do `timeout_ms` ms, jinak
     * server interpretuje jako default action.
     *
     * @param[in] trap_id     monotonní ID trapu (>= 0)
     * @param[in] topic       povinné, např. "breakpoint.hit"
     * @param[in] data_node   volitelný JSON sub-strom (deep-copy)
     * @param[in] timeout_ms  timeout v ms (-1 = bez timeoutu, neaktivní)
     * @return alokovaný string (caller `free`) nebo NULL
     *
     * @pre topic != NULL && topic[0] != '\0'
     */
    char *jsonl_build_trap(int64_t trap_id,
                           const char *topic,
                           void *data_node,
                           int timeout_ms);

    /**
     * @brief Sestaví JSONL řádek typu TRAP_RESPONSE.
     *
     * @param[in] trap_id  ID trapu na který odpovídáme
     * @param[in] action   řetězec popisující reakci klienta (např.
     *                     "continue", "step", "abort"); povinný
     * @return alokovaný string (caller `free`) nebo NULL
     *
     * @pre action != NULL && action[0] != '\0'
     */
    char *jsonl_build_trap_response(int64_t trap_id, const char *action);

    /**
     * @brief Sestaví JSONL řádek typu HELLO (discovery).
     *
     * HELLO posílá server hned po navázání spojení. Obsahuje verzi
     * protokolu, volitelné capabilities a seznam podporovaných příkazů.
     *
     * @param[in] version            string verze (např. "1.0"), povinný
     * @param[in] capabilities_node  volitelný JSON sub-strom (deep-copy)
     * @param[in] commands           NULL-terminated pole jmen příkazů
     *                               nebo NULL pro prázdný seznam
     * @return alokovaný string (caller `free`) nebo NULL
     *
     * @pre version != NULL && version[0] != '\0'
     */
    char *jsonl_build_hello(const char *version,
                            void *capabilities_node,
                            const char *const *commands);


    /* ------------------------------------------------------------------ */
    /* Stream I/O - line-buffered čtení/zápis nad FILE *                  */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Přečte jeden JSONL řádek ze streamu.
     *
     * Čte do prvního `\n` (nebo EOF). Trailing `\n` se ze vstupu
     * odstraní (i `\r\n` -> bez obojího). Prázdný řádek se vrátí jako
     * prázdný string (caller může vyfiltrovat sám).
     *
     * Output `*out_line` musí volající uvolnit `free()`. Při návratu
     * jiném než `JSONL_OK` je `*out_line` nastaven na NULL.
     *
     * @param[in]  fp        otevřený stream pro čtení
     * @param[out] out_line  výstup (caller `free`) nebo NULL při chybě
     *
     * @return JSONL_OK / JSONL_ERR_EOF / JSONL_ERR_IO / JSONL_ERR_ALLOC
     *
     * @pre fp != NULL, out_line != NULL
     *
     * @note Thread-safety: caller zajistí synchronizaci pokud více
     *       vláken čte ze stejného `fp`.
     */
    en_JSONL_RESULT jsonl_read_line(FILE *fp, char **out_line);

    /**
     * @brief Zapíše JSONL řádek do streamu (s trailing `\n` + flush).
     *
     * Funkce přidá `\n` automaticky - `line` musí být JSON objekt
     * bez trailing newline.
     *
     * @param[in] fp    otevřený stream pro zápis
     * @param[in] line  null-terminated JSON object
     *
     * @return JSONL_OK / JSONL_ERR_IO
     *
     * @pre fp != NULL, line != NULL
     *
     * @note Thread-safety: caller MUSÍ zajistit mutex pokud více vláken
     *       zapisuje do stejného `fp`. V0.A.5 doplní wrapper s mutexem.
     */
    en_JSONL_RESULT jsonl_write_line(FILE *fp, const char *line);


#ifdef __cplusplus
}
#endif

#endif /* MZ800EMU_MCP_JSONL_IO_H */
