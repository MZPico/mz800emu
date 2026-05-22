/*
 * File:   bp_action.h
 *
 * Action mini-DSL pro smart breakpointy (V1, fáze D.1.3).
 *
 * Po splnění condition (= bp_expr eval -> non-zero) se vykoná action
 * skript. Skript je line-based, řádky odděluje '\n' nebo ';'. Komentáře
 * začínají '#' a pokračují do konce řádku. Argumenty příkazů jsou
 * vyhodnocovány přes bp_expr.
 *
 * Implicit continue:
 *   action == NULL nebo prázdný  → BP zastaví emulátor (= klasický stop)
 *   action s libovolným non-empty obsahem → BP automaticky pokračuje
 *
 * V1 omezení: žádný explicit "stop" příkaz. User musí buď ponechat
 * action prázdnou, nebo si stop vynutit pomocí "if cond then ..." +
 * absence příkazu.
 *
 * Příkazy (11):
 *   log "fmt" [args...]      - printf-style do stdout (TODO V1.5: TTY/Trace)
 *   continue                 - jen explicit marker, default už je continue
 *   disable_self             - tento BP se po hitu vypne
 *   enable <name>            - aktivuj jiný BP podle jména
 *   disable <name>           - deaktivuj jiný BP podle jména
 *   poke <addr> <value>      - zápis byte do RAM
 *   set <reg> <value>        - nastav Z80 registr
 *   mark "name"              - marker do trace-suite (V1.5: skutečný hook)
 *   $name = <expr>           - přiřazení do user var
 *   $name <op>= <expr>       - += -= *= /= <<= >>= &= |= ^=
 *   clear_vars               - reset všech existujících $vars na 0
 *   if <expr> then <stmt>    - jednořádkový conditional, žádný else
 *
 * Format string `log` podporuje:
 *   %X / %x  - hex (upper / lower)
 *   %d       - dec (signed)
 *   %u       - dec (unsigned 32b)
 *   %b       - binary
 *   %c       - char (LSB ASCII)
 *   %s       - string literal (jen jako parametr-volné, NEpodporuje
 *              expr argument v V1)
 *   %%       - literál %
 *
 * Lifecycle:
 *   bp_action_t *act = bp_action_parse(src, errbuf, sizeof errbuf);
 *   if (!act) ... ;
 *   bool cont = bp_action_execute(act, &ctx);  // true = pokračuj
 *   bp_action_free(act);
 *
 * ----------------------------- License -------------------------------------
 * GPL-3.0-or-later, viz licence header v breakpoints.h.
 * ---------------------------------------------------------------------------
 */

#ifndef BP_ACTION_H
#define BP_ACTION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


    /**
     * @brief Opaque handle reprezentující parsovaný action skript.
     *
     * Vlastněný caller-em, freed přes bp_action_free(). Threading: AST
     * je read-only po parse. Executor však provádí mutace (vars, BP
     * enable/disable, memory poke, register set), takže concurrent
     * execute mezi vlákny není povolený - typicky volá jen EMU vlákno.
     */
    typedef struct st_BP_ACTION bp_action_t;


    /* Forward decl - plný typ je v bp_expr.h. Caller používá ten samý
     * kontext jako pro bp_expr_eval(). */
    struct bp_expr_ctx_s;


    /**
     * @brief Parse zdrojový text action skriptu.
     *
     * @param source       Zdrojový text (UTF-8, NULL-terminated).
     *                     NULL nebo prázdný řetězec → vrátí NULL bez chyby
     *                     (caller interpretuje jako "no action = stop").
     * @param errbuf       Buffer pro chybovou hlášku (může být NULL).
     * @param errbuf_size  Velikost errbuf.
     * @return Vlastněný handle, nebo NULL při syntax chybě (errbuf
     *         se naplní). Caller musí volat bp_action_free().
     *
     * Side effects: žádné. Funkce je čistá.
     *
     * Pozn.: parser je striktní - neznámý příkaz, špatná arita, neznámý
     *        operátor = chyba (= caller dostane NULL). Komentáře (#) a
     *        prázdné řádky jsou tiše ignorovány.
     */
    extern bp_action_t* bp_action_parse ( const char *source,
                                           char *errbuf, size_t errbuf_size );


    /**
     * @brief Vykoná parsovaný action skript.
     *
     * @param action  Parsovaný skript (musí být non-NULL).
     * @param ctx     Kontext pro vyhodnocení argumentů příkazů
     *                (musí být non-NULL, předává se identicky jako
     *                pro bp_expr_eval).
     * @return true   = emulátor má pokračovat (= continue / implicit
     *                  continue); BP nezastavuje
     *         false  = emulátor má zastavit (= action je prázdná nebo
     *                  triggeroval příkaz vyžadující stop - V1 nemá)
     *
     * Side effects:
     *   - Mutuje user $vars storage (assignment, clear_vars).
     *   - Mutuje state breakpointů (disable_self, enable/disable).
     *   - Volá memory_write_byte (poke) a CPU register zápis (set).
     *   - Píše do stdout (log) a trace markeru (mark, V1.5+).
     *
     * Threading: musí být voláno jen z EMU vlákna (mutace CPU/memory
     * stavu).
     */
    extern bool bp_action_execute ( const bp_action_t *action,
                                     const struct bp_expr_ctx_s *ctx );


    /**
     * @brief Uvolní paměť asociovanou s parsovaným skriptem.
     *
     * @param action AST k uvolnění (NULL = no-op).
     *
     * Postcondition: pointer je po návratu invalidní.
     */
    extern void bp_action_free ( bp_action_t *action );


    /**
     * @brief Klasifikace dominantního "primárního" účinku action skriptu.
     *
     * Slouží pro vrstvu logování / UI (= Event Viewer @c BP_FIRE subtype,
     * statistiky), kde chceme jedním enum číslem charakterizovat, "co BP
     * dělá při fire" bez procházení detailního AST. Klasifikace je
     * heuristická - skutečný behavior určuje až @ref bp_action_execute().
     *
     * Pravidla (vyhodnocují se v pořadí, vrací první match):
     *
     *  1. @c action == NULL    -> @c BP_ACTION_SUB_HALT
     *  2. obsahuje @c STMT_MARK -> @c BP_ACTION_SUB_MARK
     *  3. obsahuje @c STMT_DISABLE nebo @c STMT_DISABLE_SELF
     *                          -> @c BP_ACTION_SUB_DISABLE
     *  4. obsahuje @c STMT_ENABLE -> @c BP_ACTION_SUB_ENABLE
     *  5. jinak                -> @c BP_ACTION_SUB_CONTINUE
     *
     * Klasifikace prochází i tělo @c STMT_IF (then + else větve), aby
     * `if cond then mark "x"` byla detekována jako @c MARK.
     *
     * Hodnoty enumu jsou stabilní (= persistované v Event Viewer cfg
     * filtrech a v eventlog ringu). Sjednoceno s
     * @c en_EVENTLOG_BP_FIRE_SUB (viz @c eventlog.h) - 1:1 mapping,
     * caller nemusí překládat.
     */
    typedef enum en_BP_ACTION_SUB {
        BP_ACTION_SUB_HALT     = 0, /**< action prázdná - emu se pozastaví. */
        BP_ACTION_SUB_MARK     = 1, /**< @c STMT_MARK je přítomen v skriptu. */
        BP_ACTION_SUB_CONTINUE = 2, /**< log / poke / set / var assign / continue ... */
        BP_ACTION_SUB_IGNORE   = 3, /**< Rezervováno (DSL aktuálně nemá "ignore" stmt). */
        BP_ACTION_SUB_ENABLE   = 4, /**< @c STMT_ENABLE přítomen. */
        BP_ACTION_SUB_DISABLE  = 5, /**< @c STMT_DISABLE / @c STMT_DISABLE_SELF přítomen. */
    } en_BP_ACTION_SUB;


    /**
     * @brief Klasifikuje dominantní primární účinek skriptu.
     *
     * @param action  Parsovaný AST (může být @c NULL = mapuje na
     *                @c BP_ACTION_SUB_HALT).
     * @return  Hodnota @ref en_BP_ACTION_SUB dle pravidel popsaných u
     *          enumu.
     *
     * Side effects: žádné. Čistá funkce, čte AST.
     * Threading: re-entrant, AST je read-only.
     */
    extern en_BP_ACTION_SUB bp_action_classify_subtype ( const bp_action_t *action );


#ifdef __cplusplus
}
#endif

#endif /* BP_ACTION_H */
