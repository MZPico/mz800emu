/*
 * File:   bookmarks.h
 *
 * Globální storage pro pojmenované adresové záložky (Bookmarks) v
 * debuggeru. Záložka je dvojice (user_input, comment), kde user_input
 * je buď hex literál ($1234, 0x1234, #1234, 1234h), nebo jméno symbolu
 * z sym_db. Adresa se resolve **dynamicky** při každém renderu UI - tj.
 * pokud se symbol v sym_db přejmenuje / load mapy / nový import, záložka
 * se automaticky napojí na novou adresu (nebo přestane být platná).
 *
 * Životnost storage je per-emulator-session: alokace v
 * `bookmarks_init()`, uvolnění v `bookmarks_cleanup()`. Persistence
 * je do per-arch `.bookmarks` JSON souboru v cfg dir (MZARCH-závislá
 * cesta).
 *
 * Threading: storage je čten i zapisován jen z UI vlákna (ImGui).
 * Žádný mutex - UI vlákno je single-threaded vůči vlastnímu storage.
 *
 * Vzor: `bp_vars.{c,h}` (V1.5.B) - identický pattern (GArray, JSON
 * persist přes json-glib, cfg sekce s auto_save/auto_load).
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later, viz licence header v bookmarks.c.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef BOOKMARKS_H
#define BOOKMARKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


    /**
     * @brief Maximální délka user_input (= bez terminátoru).
     *
     * Dostatečné pro hex literál i většinu symbol jmen. Limit je
     * defenzivní - storage truncate-uje přes g_strdup s žádnou validací.
     */
#define BOOKMARK_INPUT_MAX 63


    /**
     * @brief Maximální délka comment řetězce (bez terminátoru).
     */
#define BOOKMARK_COMMENT_MAX 256


    /**
     * @brief Záznam jedné záložky.
     *
     * Vlastnictví: `user_input` a `comment` jsou heap-allocated (g_strdup),
     * uvolňuje per-záznam handler v `bookmarks_clear()` /
     * `bookmarks_remove()` / `bookmarks_cleanup()`.
     *
     * @invariant `id` je nenulový a unique napříč session storage.
     *            Counter je monotonic - po remove se ID nerecykluje.
     * @invariant `user_input` je non-NULL, neprázdný řetězec.
     *            Storage si dělá vlastní g_strdup kopii.
     * @invariant `comment` je NULL nebo heap-allocated řetězec
     *            (může být prázdný = NULL po normalizaci v setteru).
     *
     * Pozn.: `addr` se nedrží v záznamu - je odvozená dynamicky z
     * `user_input` přes `bookmarks_resolve_addr()`. To umožňuje,
     * aby symbol (= jméno) přežil změnu adresy v sym_db.
     */
    typedef struct bookmark_s {
        uint32_t  id;            /**< Monotonic counter (>= 1) */
        char     *user_input;    /**< Hex literál nebo symbol jméno */
        char     *comment;       /**< Komentář, NULL = no comment */
    } bookmark_t;


    /**
     * @brief Inicializuje storage. Idempotentní.
     *
     * Volat z UI / debugger startup. Po volání je storage prázdný.
     *
     * Pokud je auto_load v cfg true a default `.bookmarks` soubor
     * existuje, provede se auto-load (= storage se naplní obsahem
     * souboru).
     */
    extern void bookmarks_init ( void );


    /**
     * @brief Uvolní storage. Po volání je nutno znovu volat init().
     *
     * Pokud je auto_save v cfg true, provede se auto-save do default
     * `.bookmarks` souboru před wipe storage.
     */
    extern void bookmarks_cleanup ( void );


    /**
     * @brief Vrátí monotonic verzi storage.
     *
     * Verze se inkrementuje při každé změně storage (add / remove /
     * set_user_input / set_comment / clear / load / merge). UI může
     * cachovat odvozený stav (= filtered list) a re-build jen když
     * se verze změní.
     *
     * @return Aktuální verze (>= 1 po init, nebo 0 pokud storage
     *         není init).
     */
    extern unsigned bookmarks_get_version ( void );


    /* ===================================================================== */
    /*  CRUD                                                                 */
    /* ===================================================================== */


    /**
     * @brief Přidá novou záložku.
     *
     * @param user_input  Hex literál nebo symbol jméno (NULL/prázdné
     *                    = no-op, vrací 0).
     * @param comment     Komentář (NULL nebo prázdné = clear).
     * @return Nové ID (>= 1) při úspěchu, 0 při chybě (NULL input).
     */
    extern uint32_t bookmarks_add ( const char *user_input, const char *comment );


    /**
     * @brief Smaže záložku podle ID.
     *
     * @param id  ID záznamu.
     * @return true pokud byl záznam nalezen a odstraněn.
     */
    extern bool bookmarks_remove ( uint32_t id );


    /**
     * @brief Změní user_input existující záložky.
     *
     * @param id          ID záznamu.
     * @param new_input   Nový vstup (NULL/prázdné = no-op, vrací false).
     * @return true pokud záznam existoval a byl aktualizován.
     */
    extern bool bookmarks_set_user_input ( uint32_t id, const char *new_input );


    /**
     * @brief Změní comment existující záložky.
     *
     * @param id           ID záznamu.
     * @param new_comment  Nový komentář (NULL nebo prázdné = clear).
     * @return true pokud záznam existoval a byl aktualizován.
     */
    extern bool bookmarks_set_comment ( uint32_t id, const char *new_comment );


    /**
     * @brief Smaže všechny záložky.
     */
    extern void bookmarks_clear ( void );


    /* ===================================================================== */
    /*  Iterace / lookup                                                     */
    /* ===================================================================== */


    /**
     * @brief Počet aktuálně registrovaných záložek.
     */
    extern size_t bookmarks_count ( void );


    /**
     * @brief Vrátí read-only ukazatel na záznam podle indexu.
     *
     * @param idx  Index 0..count()-1.
     * @return Pointer na záznam, nebo NULL pokud `idx` je out-of-range.
     *
     * Lifetime: do nejbližšího add/remove/clear/cleanup/load/merge.
     */
    extern const bookmark_t *bookmarks_get_by_index ( size_t idx );


    /**
     * @brief Vrátí read-only ukazatel na záznam podle ID.
     *
     * @return Pointer nebo NULL pokud `id` neexistuje.
     */
    extern const bookmark_t *bookmarks_get_by_id ( uint32_t id );


    /* ===================================================================== */
    /*  Resolve helper                                                       */
    /* ===================================================================== */


    /**
     * @brief Pokus o převod user_input na 16-bit CPU adresu.
     *
     * Postup:
     *   1. Trim leading/trailing whitespace.
     *   2. Zkus hex literál v jednom z formátů (case-insensitive):
     *      - `0x1234`, `0X1234`
     *      - `#1234`
     *      - `$1234`
     *      - `1234h`, `01234h` (suffix)
     *      - holé `1234` (jen pokud všechny znaky hex digit, max 4 znaky)
     *   3. Pokud hex parse selže, zkus `sym_db_lookup_by_name(trimmed)`.
     *      Pokud symbol existuje, vrať jeho `addr & 0xFFFF`.
     *   4. Jinak false.
     *
     * @param user_input  Vstup (NULL/prázdné = false).
     * @param addr_out    Výstupní 16-bit adresa (může být NULL při test-only).
     * @return true při úspěchu.
     */
    extern bool bookmarks_resolve_addr ( const char *user_input, uint16_t *addr_out );


    /* ===================================================================== */
    /*  Persistence                                                          */
    /* ===================================================================== */


    /**
     * @brief Uloží storage do JSON souboru.
     *
     * Schéma:
     * @code
     * {
     *   "version": 1,
     *   "bookmarks": [
     *     {"id": 1, "input": "MAIN_LOOP", "comment": "hlavni smycka"},
     *     ...
     *   ]
     * }
     * @endcode
     *
     * @param filepath  Cílová cesta (NULL = default per-arch).
     * @return true při úspěchu, false při I/O / serialize chybě.
     */
    extern bool bookmarks_save ( const char *filepath );


    /**
     * @brief Načte storage ze souboru (REPLACE mode).
     *
     * Existující obsah storage se před loadem wipne. Pokud soubor
     * neexistuje, storage se wipne a vrací true (= empty state).
     *
     * @return true při úspěchu, false při parse chybě.
     */
    extern bool bookmarks_load ( const char *filepath );


    /**
     * @brief Sloučí soubor s existujícím storage (MERGE mode).
     *
     * Pravidla:
     *   - Skip-duplicate: pokud ID již existuje a obsah je identický
     *     (= shodný user_input + comment), záznam ze souboru se ignoruje.
     *   - Konflikt ID s odlišným obsahem: záznam ze souboru dostane
     *     **nové** ID (= renumber, aby se zachovala oba).
     *   - Nová ID: pokud ID v souboru je <= aktuální counter, dostane
     *     nové ID.
     *
     * @return true při úspěchu.
     */
    extern bool bookmarks_merge ( const char *filepath );


    /**
     * @brief Vrátí default `.bookmarks` filepath per-arch.
     *
     * Per-arch filename:
     *   - MZ800 build: `mz800.bookmarks`
     *   - MZ1500 build: `mz1500.bookmarks`
     *   - MZ700 build: `mz700.bookmarks`
     *
     * Cesta je resolved proti cfg dir přes sdlapp_paths_resolve_cfg().
     *
     * @return Read-only pointer do internal cache. Lifetime: do
     *         dalšího volání nebo do bookmarks_cleanup(). Caller
     *         **nesmí** g_free.
     */
    extern const char *bookmarks_default_filepath ( void );


    /* ===================================================================== */
    /*  Cfg sekce                                                            */
    /* ===================================================================== */


    /**
     * @brief Inicializuje cfg sekci [DEBUGGER] klíče pro bookmarks.
     *
     * Volat z `bookmarks_init()` (= idempotentní, ale interní helper).
     * Public API pro případ že modul je init mimo `bookmarks_init()`.
     *
     * Cfg klíče v sekci "DEBUGGER":
     *   - `bookmarks_file` (string, default `mz<N>.bookmarks`)
     *   - `bookmarks_auto_save` (bool, default 1)
     *   - `bookmarks_auto_load` (bool, default 1)
     */
    extern void bookmarks_cfg_init ( void );


#ifdef __cplusplus
}
#endif

#endif /* BOOKMARKS_H */
