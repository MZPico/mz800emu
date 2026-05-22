/*
 * File:   bp_vars.h
 *
 * Globální storage pro user-defined `$name` proměnné smart breakpointů
 * (V1, fáze D.1.3). Proměnné mají skalární doménu signed 32-bit int,
 * default 0 pro neexistující jméno (= konzistentní s evaluatorem
 * bp_expr).
 *
 * Životnost storage je per-emulator-session: alokace v
 * breakpoints_init(), uvolnění v breakpoints_exit(). Persistence do
 * .bpt souboru je řešena spolu s BP definicemi v breakpoints.c
 * (sekce "vars" v JSON).
 *
 * Threading:
 *   - Zápisy probíhají z EMU vlákna v action executoru při BP hitu.
 *   - Čtení (UI panel) zatím není - V1 nemá vars panel; pokud bude
 *     v V2 přidán, je třeba zavést mutex / atomic snapshot. V1 nemá
 *     race protection.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later, viz licence header v breakpoints.h.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef BP_VARS_H
#define BP_VARS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


    /**
     * @brief Maximální délka platného identifikátoru (= bez `$` prefixu).
     *
     * Konzistentní s `BP_EXPR_IDENT_MAX` (lexer bp_expr.c). Limit kontroluje
     * `bp_var_name_is_valid()`.
     */
#define BP_VAR_NAME_MAX 31


    /**
     * @brief Maximální délka comment řetězce (bez terminátoru).
     *
     * UI Add Var input + inline edit Comment validuje proti tomuto limitu
     * (truncate při překročení). Persistence ukládá comment beze změny
     * (= zodpovědnost je na UI / settru, storage drží co dostane).
     */
#define BP_VAR_COMMENT_MAX 256


    /**
     * @brief Záznam jedné user var.
     *
     * Vlastnictví: `name` a `comment` jsou heap-allocated (g_strdup),
     * uvolňuje `bp_vars_destroy()` resp. `bp_vars_clear_storage()`
     * resp. `bp_var_unset()` per-záznam.
     *
     * @invariant `name` je non-NULL, neprázdný řetězec po vytvoření
     *            přes `bp_var_set()`. Identifikátor odpovídá lexeru
     *            bp_expr.c (= identifier po `$` prefixu, max
     *            `BP_VAR_NAME_MAX` znaků).
     * @invariant `comment` je NULL nebo heap-allocated řetězec
     *            (může být prázdný). NULL = "no comment".
     * @invariant `persist_value`: pokud `true`, hodnota `value` se ukládá
     *            do .bpt / .vars souboru a obnoví se při loadu. Pokud
     *            `false`, persistuje jen `name` + `comment` + flag, value
     *            se při loadu reset na 0 (= use case "runtime counter
     *            reset každý start").
     */
    typedef struct bp_var_s {
        char    *name;          /**< Identifier bez `$` prefixu */
        int32_t  value;         /**< Hodnota (signed 32-bit) */
        char    *comment;       /**< V1.5.B: heap, g_strdup, NULL = no comment */
        bool     persist_value; /**< V1.5.B: true = value v persist; false = reset on load */
    } bp_var_t;


    /**
     * @brief Inicializuje storage. Idempotentní.
     *
     * Volat z breakpoints_init() před prvním load souboru. Po volání
     * je storage prázdný a připravený pro `bp_var_set()`.
     */
    extern void bp_vars_init ( void );


    /**
     * @brief Uvolní storage. Po volání je storage prázdný a invalidní
     *        (= nutno volat bp_vars_init() znovu před použitím).
     *
     * Volat z breakpoints_exit() po posledním save souboru.
     */
    extern void bp_vars_destroy ( void );


    /**
     * @brief Vrátí hodnotu proměnné podle jména.
     *
     * @param name Identifier bez `$` prefixu (case-sensitive,
     *             konzistentní s lexerem bp_expr).
     * @return Aktuální hodnota, nebo 0 pokud `name` neexistuje
     *         nebo storage není inicializovaný.
     *
     * Side effects: žádné (pure lookup).
     */
    extern int32_t bp_var_get ( const char *name );


    /**
     * @brief Nastaví hodnotu proměnné. Pokud `name` neexistuje, vytvoří
     *        nový záznam s `comment = NULL` a `persist_value = true`
     *        (= default pro běžné callers, value se persistuje).
     *
     * Pokud `name` existuje, **mění jen value** - `comment` a
     * `persist_value` zůstanou beze změny (= action `set $name = expr`
     * nemá ambici resetovat metadata, jen mění hodnotu).
     *
     * Defenzivní validace: pokud `bp_var_name_is_valid(name)` vrátí
     * false, funkce loguje warning na stderr a **neudělá nic**
     * (= storage neuloží invalid jméno).
     *
     * @param name  Identifier bez `$` (NULL nebo prázdný = no-op).
     * @param value Nová hodnota.
     *
     * Side effects: může alokovat nový záznam (heap g_strdup pro `name`).
     * Storage je dynamic array; pointer na předchozí záznamy se může
     * realokací invalidovat - nedržet `bp_var_t *` přes set/destroy/clear.
     */
    extern void bp_var_set ( const char *name, int32_t value );


    /**
     * @brief Nastaví comment pro existující variable.
     *
     * Pokud variable neexistuje, funkce nedělá nic a vrací false
     * (= comment lze nastavit jen pro již vytvořenou variable).
     *
     * @param name     Identifier bez `$` prefixu.
     * @param comment  Nový comment. NULL nebo prázdný řetězec
     *                 = clear comment (storage drží NULL).
     *                 Storage si dělá vlastní g_strdup kopii.
     * @return true pokud variable existovala a comment byl nastaven.
     *
     * Side effects: g_free pro stávající comment, g_strdup pro nový.
     */
    extern bool bp_var_set_comment ( const char *name, const char *comment );


    /**
     * @brief Vrátí comment pro variable (read-only pointer do storage).
     *
     * @param name  Identifier bez `$` prefixu.
     * @return Pointer do interního storage (lifetime do nejbližšího
     *         set/set_comment/destroy/clear) nebo NULL pokud variable
     *         neexistuje nebo nemá comment.
     */
    extern const char* bp_var_get_comment ( const char *name );


    /**
     * @brief Nastaví persist_value flag pro existující variable.
     *
     * @param name     Identifier bez `$` prefixu.
     * @param persist  true = ukládat value do persist souborů,
     *                 false = persist jen metadata (name + comment + flag),
     *                 value se při loadu reset na 0.
     * @return true pokud variable existovala a flag byl nastaven.
     */
    extern bool bp_var_set_persist ( const char *name, bool persist );


    /**
     * @brief Vrátí persist_value flag pro variable.
     *
     * @param name  Identifier bez `$` prefixu.
     * @return Hodnota flagu (true / false), nebo true pokud variable
     *         neexistuje (= default sémantika "ulož value").
     */
    extern bool bp_var_get_persist ( const char *name );


    /**
     * @brief Validuje variable name (= identifier po `$` prefixu).
     *
     * Akceptuje regex `^[a-zA-Z_][a-zA-Z0-9_]*$` a délku
     * <= `BP_VAR_NAME_MAX`. Žádné reserved keywords (= `$` prefix
     * v action / expression jazyce je sám o sobě distinktivní, takže
     * `$if`, `$set`, `$log` jsou platná uživatelská jména).
     *
     * @param name  Identifier bez `$` prefixu (NULL = false).
     * @return true pokud name je platný identifier.
     */
    extern bool bp_var_name_is_valid ( const char *name );


    /**
     * @brief Internal helper - vytvoří / aktualizuje záznam s explicit
     *        nastavením comment + persist_value flagu.
     *
     * Použití: persistence loader, který musí dohrát všechny tři pole
     * najednou (existing `bp_var_set` zachovává metadata existing var).
     *
     * Pokud `name` neexistuje, vytvoří nový záznam s danými hodnotami.
     * Pokud `name` existuje, přepíše value, comment i persist_value.
     *
     * @param name           Identifier bez `$` (NULL/prázdné = no-op).
     * @param value          Nová hodnota (= 0 pokud persist_value=false
     *                       v load scénáři).
     * @param comment        Comment (NULL = no comment), storage si
     *                       dělá kopii.
     * @param persist_value  Flag pro persist value.
     */
    extern void bp_var_set_full ( const char *name,
                                  int32_t value,
                                  const char *comment,
                                  bool persist_value );


    /**
     * @brief Smaže záznam podle jména. Ostatní záznamy se posunou v
     *        underlying GArray (= invaliduje `bp_var_t *` získaný přes
     *        `bp_vars_get_by_index` po této operaci).
     *
     * @param name  Identifier bez `$` prefixu (NULL nebo prázdný = no-op).
     * @return true pokud byl záznam nalezen a odstraněn,
     *         false pokud `name` neexistoval nebo storage není init.
     *
     * Side effects: g_free pro `name` field záznamu, posun ostatních
     * záznamů v dynamic array.
     *
     * Použití: UI panel "delete var" tlačítko (D.6).
     */
    extern bool bp_var_unset ( const char *name );


    /**
     * @brief Vynuluje hodnoty všech existujících proměnných (záznamy
     *        zůstávají v storage = lze stále enumerovat přes `_count`
     *        a `_get_by_index`).
     *
     * Sémantika action příkazu `clear_vars`: state-machine reset.
     * Záznamy nemizí, jen value se nastaví na 0 - tj. UI panel
     * vidí stejné jméno proměnných, ale zero hodnoty.
     */
    extern void bp_vars_clear_all ( void );


    /**
     * @brief Smaže všechny záznamy (= storage je po volání prázdný).
     *
     * Použití: load .bpt souboru = nejprve clear, pak append load
     *          z JSON `"vars"` pole. Není totožné s `bp_vars_clear_all()`,
     *          které jen nuluje hodnoty.
     */
    extern void bp_vars_clear_storage ( void );


    /**
     * @brief Počet aktuálně registrovaných záznamů.
     *
     * @return Počet (může být 0 pokud storage není init nebo prázdný).
     */
    extern size_t bp_vars_count ( void );


    /**
     * @brief Vrátí read-only ukazatel na záznam podle indexu.
     *
     * @param idx Index 0..bp_vars_count()-1.
     * @return Pointer na záznam, nebo NULL pokud `idx` je out-of-range.
     *
     * Lifetime: pointer platný do nejbližšího `bp_var_set()` /
     * `bp_vars_clear_storage()` / `bp_vars_destroy()` (může nastat
     * realokace dynamic array). Pro persistentní použití caller musí
     * zkopírovat data.
     */
    extern const bp_var_t* bp_vars_get_by_index ( size_t idx );


    /* ===================================================================== */
    /*  V1.5.B per-arch .vars file API                                       */
    /* ===================================================================== */


    /**
     * @brief Mód pro `bp_vars_load_from_filepath()`.
     *
     * Určuje, jak se zachází s konflikty mezi existujícím storage a
     * vars načítanými ze souboru.
     */
    typedef enum en_BP_VARS_LOAD_MODE {
        BP_VARS_LOAD_REPLACE = 0,         /**< Wipe storage před loadem (= file = nový stav) */
        BP_VARS_LOAD_MERGE_OVERWRITE,     /**< Merge: existing + file, konflikt = file vyhrává */
        BP_VARS_LOAD_MERGE_SKIP,          /**< Merge: existing + file, konflikt = ignore from file */
    } en_BP_VARS_LOAD_MODE;


    /**
     * @brief Uloží vars do standalone `.vars` souboru.
     *
     * Schéma: top-level objekt s klíčem `"vars"` (= subset `.bpt`).
     * Per-záznam pole: `name` (povinné), `value` (jen pokud
     * `persist_value=true`), `comment` (jen pokud non-NULL),
     * `persist_value` (vždy).
     *
     * @param path          Cílová cesta. NULL = default per-arch
     *                      filename (`mz800.vars` / `mz1500.vars` /
     *                      `mz700.vars`) v cfg dir.
     * @param errbuf        Error buffer (může být NULL).
     * @param errbuf_size   Velikost error bufferu (0 pokud NULL).
     * @return true při úspěchu, false při I/O / serialize chybě
     *         (popis v `errbuf`).
     *
     * Side effects: zápis na disk.
     */
    extern bool bp_vars_save_to_filepath ( const char *path,
                                            char *errbuf, size_t errbuf_size );


    /**
     * @brief Načte vars ze standalone `.vars` souboru per zvolený mód.
     *
     * Pravidla:
     * - REPLACE: storage se wipne, pak se naplní obsahem souboru.
     * - MERGE_OVERWRITE: existující vars zůstanou, file vars přepíší
     *   konflikt jména.
     * - MERGE_SKIP: existující vars zůstanou, file vars se přidají
     *   jen pokud jméno ještě neexistuje.
     *
     * BC: missing `comment` -> NULL, missing `persist_value` -> true.
     * persist_value=false v souboru -> value reset 0 i pokud "value"
     * v souboru je (= warning na stderr).
     *
     * @param path          Zdrojová cesta. NULL = default per-arch.
     * @param mode          REPLACE / MERGE_OVERWRITE / MERGE_SKIP.
     * @param errbuf        Error buffer (může být NULL).
     * @param errbuf_size   Velikost error bufferu (0 pokud NULL).
     * @return true při úspěchu (i pokud soubor neexistuje a mode!=REPLACE
     *         - tj. nic se nestane), false při I/O / parse chybě.
     */
    extern bool bp_vars_load_from_filepath ( const char *path,
                                              en_BP_VARS_LOAD_MODE mode,
                                              char *errbuf, size_t errbuf_size );


    /**
     * @brief Vrátí default `.vars` filepath per-arch (resolved proti cfg dir).
     *
     * Cesta odpovídá filename:
     * - MZ800 build: `mz800.vars`
     * - MZ1500 build: `mz1500.vars`
     * - MZ700 build: `mz700.vars`
     *
     * @return Heap-allocated string, caller je zodpovědný za `g_free`.
     *         NULL pokud cfg dir není dostupný.
     */
    extern char* bp_vars_default_filepath ( void );


    /**
     * @brief Inicializuje cfg sekci [BP_VARS] (volat z breakpoints_init()).
     *
     * Cfg klíče:
     * - `vars_file` = path (default `mz<N>.vars`)
     * - `auto_save` = bool (default 1)
     * - `auto_load` = bool (default 1)
     *
     * Po registraci modulu provede také auto_load pokud je nastaven.
     */
    extern void bp_vars_cfg_init ( void );


    /**
     * @brief Auto-save vars do default souboru pokud `auto_save=1` v cfg.
     *
     * Volat z breakpoints_exit() před `bp_vars_destroy()`.
     */
    extern void bp_vars_cfg_auto_save ( void );


#ifdef __cplusplus
}
#endif

#endif /* BP_VARS_H */
