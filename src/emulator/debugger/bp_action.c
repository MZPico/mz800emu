/*
 * File:   bp_action.c
 *
 * Implementace action mini-DSL pro smart breakpointy (V1, fáze D.1.3).
 *
 * Pipeline:
 *   1. Source split do "statementů" - oddělovače '\n' / ';', komentáře
 *      '#' až do konce řádku.
 *   2. Per-statement parser - rozpozná příkaz podle keyword nebo
 *      úvodního '$' (= var assignment) a sestrojí AST uzel.
 *   3. Argumenty výrazového typu (= většina) parsuje přes bp_expr_parse.
 *   4. Executor iteruje seznam příkazů, vyhodnocuje argumenty proti
 *      ctx, provádí side-effecty (log, poke, set, var assign, atd.).
 *
 * Action AST je seznam (pole) `bp_action_stmt_t`. Statement obsahuje
 * tagged union dat per kind. Memory ownership: heap stringy a sub-AST
 * (bp_expr_t*) vlastněné stmt, uvolňováno cez bp_action_free.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later, viz licence header v breakpoints.h.
 *
 * ---------------------------------------------------------------------------
 */

#include "mzarch/mzarch_config.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <stdarg.h>
#include <strings.h>
#include <glib.h>

#include "bp_action.h"
#include "bp_expr.h"
#include "bp_vars.h"
#include "breakpoints.h"
#include "symbols/sym_db.h"
#include "libs/cpu-z80/z80.h"
#include "hw-generic/memory/memory.h"
#include "trace/marklog.h"


/* ========================================================================= */
/*  Statement kinds                                                          */
/* ========================================================================= */

/**
 * @brief Druhy příkazů action mini-DSL.
 */
typedef enum {
    STMT_LOG,            /**< log "fmt" args... */
    STMT_CONTINUE,       /**< continue (= explicit no-op v V1) */
    STMT_DISABLE_SELF,   /**< disable_self */
    STMT_ENABLE,         /**< enable <name> */
    STMT_DISABLE,        /**< disable <name> */
    STMT_POKE,           /**< poke <addr> <value> */
    STMT_SET_REG,        /**< set <reg> <value> */
    STMT_MARK,           /**< mark "name" */
    STMT_VAR_ASSIGN,     /**< $name op= expr (op je kód níže) */
    STMT_CLEAR_VARS,     /**< clear_vars */
    STMT_IF              /**< if expr then <stmt> */
} stmt_kind_t;


/**
 * @brief Druhy assignment operátorů pro $vars.
 *
 * VA_SET = prosté '='. Ostatní = compound (+=, -=, ...).
 */
typedef enum {
    VA_SET,
    VA_ADD,    /* += */
    VA_SUB,    /* -= */
    VA_MUL,    /* *= */
    VA_DIV,    /* /= */
    VA_SHL,    /* <<= */
    VA_SHR,    /* >>= */
    VA_AND,    /* &= */
    VA_OR,     /* |= */
    VA_XOR     /* ^= */
} var_assign_op_t;


#define BP_ACTION_VAR_NAME_MAX  31     /**< konzistentní s bp_expr ident */
#define BP_ACTION_LOG_MAX_ARGS  8      /**< max formátovacích argumentů */
#define BP_ACTION_BPNAME_MAX    63     /**< limit pro enable/disable target */


/* Forward decl. */
typedef struct bp_action_stmt_s bp_action_stmt_t;


/**
 * @brief Jeden parsovaný příkaz akce.
 *
 * Tagged union dle kind. Vlastnictví:
 *   - Heap stringy (fmt, target_name, mark_name) freed přes free().
 *   - bp_expr_t* freed přes bp_expr_free().
 *   - Nested if-stmt: child je heap-allocated bp_action_stmt_t,
 *     freed rekurzivně.
 */
struct bp_action_stmt_s {
    stmt_kind_t kind;
    union {
        /* STMT_LOG */
        struct {
            char       *fmt;                                 /**< formát po extrakci, vlastněný */
            bp_expr_t  *args[BP_ACTION_LOG_MAX_ARGS];        /**< parsované expr args */
            int         n_args;
        } log;
        /* STMT_ENABLE / STMT_DISABLE / STMT_MARK */
        struct {
            char *name;
            /* Pro STMT_MARK: pre-registered marker_id z marklog_register()
             * získané při parse. Hot path fire pak nevolá string operace.
             * Pro STMT_ENABLE / STMT_DISABLE nepoužito (= MARKLOG_INVALID_ID). */
            uint16_t marker_id;
        } target;
        /* STMT_POKE: poke addr value */
        struct {
            bp_expr_t *addr;
            bp_expr_t *value;
        } poke;
        /* STMT_SET_REG: set <reg-name> <value-expr> */
        struct {
            char       reg[BP_ACTION_VAR_NAME_MAX + 1];
            bp_expr_t *value;
        } setreg;
        /* STMT_VAR_ASSIGN: $name op= expr */
        struct {
            char            name[BP_ACTION_VAR_NAME_MAX + 1];
            var_assign_op_t op;
            bp_expr_t      *value;
        } varas;
        /* STMT_IF: cond + then-child statement + optional else-child */
        struct {
            bp_expr_t        *cond;
            bp_action_stmt_t *child;       /**< then-branch (vždy non-NULL při validním parse) */
            bp_action_stmt_t *else_child;  /**< else-branch nebo NULL pokud žádný */
        } iff;
    };
};


/**
 * @brief Top-level handle pro parsovaný action skript.
 *
 * Drží pole statementů + jejich počet. Pole je heap, growable přes
 * malloc/realloc.
 */
struct st_BP_ACTION {
    bp_action_stmt_t *stmts;
    int               n_stmts;
    int               cap_stmts;
};


/* ========================================================================= */
/*  Util                                                                     */
/* ========================================================================= */

/**
 * @brief Zapíše chybu do errbuf (pokud jsou k dispozici).
 *
 * Vrací false jako shortcut pro `return ac_err(...);`.
 */
static bool ac_err ( char *errbuf, size_t errbuf_size,
                      const char *fmt, ... )
                      __attribute__((format(printf,3,4)));
static bool ac_err ( char *errbuf, size_t errbuf_size,
                      const char *fmt, ... ) {
    if ( errbuf && errbuf_size > 0 ) {
        va_list ap;
        va_start ( ap, fmt );
        vsnprintf ( errbuf, errbuf_size, fmt, ap );
        va_end ( ap );
    };
    return false;
}


/**
 * @brief Uvolní vnitřek statementu, statement samotný NEuvolňuje.
 */
static void bp_action_stmt_free_inner ( bp_action_stmt_t *s ) {
    if ( !s ) return;
    switch ( s->kind ) {
        case STMT_LOG:
            free ( s->log.fmt );
            for ( int i = 0; i < s->log.n_args; i++ ) {
                bp_expr_free ( s->log.args[i] );
            };
            break;
        case STMT_ENABLE:
        case STMT_DISABLE:
        case STMT_MARK:
            free ( s->target.name );
            break;
        case STMT_POKE:
            bp_expr_free ( s->poke.addr );
            bp_expr_free ( s->poke.value );
            break;
        case STMT_SET_REG:
            bp_expr_free ( s->setreg.value );
            break;
        case STMT_VAR_ASSIGN:
            bp_expr_free ( s->varas.value );
            break;
        case STMT_IF:
            bp_expr_free ( s->iff.cond );
            if ( s->iff.child ) {
                bp_action_stmt_free_inner ( s->iff.child );
                free ( s->iff.child );
            };
            if ( s->iff.else_child ) {
                bp_action_stmt_free_inner ( s->iff.else_child );
                free ( s->iff.else_child );
            };
            break;
        case STMT_CONTINUE:
        case STMT_DISABLE_SELF:
        case STMT_CLEAR_VARS:
            break;
    };
}


/* ========================================================================= */
/*  Source slicing                                                           */
/* ========================================================================= */

/**
 * @brief Skipne whitespace na začátku, vrátí pointer na první neWS znak.
 */
static const char* skip_ws ( const char *s ) {
    while ( *s && isspace ( (unsigned char) *s ) ) s++;
    return s;
}


/**
 * @brief Trimne trailing whitespace v duplikované kopii.
 *
 * Modifikuje str in-place (nahradí trailing WS '\0').
 */
static void rtrim ( char *str ) {
    if ( !str ) return;
    size_t n = strlen ( str );
    while ( n > 0 && isspace ( (unsigned char) str[n - 1] ) ) {
        str[n - 1] = '\0';
        n--;
    };
}


/**
 * @brief Extrahuje další "logický řádek" (= statement source).
 *
 * Oddělovače: '\n' a ';'.
 * Komentář: '#' až do konce fyzického řádku (= '\n' nebo EOF), uvnitř
 * uvozovek se '#' nepovažuje za komentář.
 *
 * @param[in,out] cursor  Pointer na pointer ve zdrojáku - posune se za
 *                        oddělovač.
 * @param[out]    out     Heap buffer (g_strdup) s textem statementu,
 *                        nebo NULL pokud řádek je prázdný (caller
 *                        může pokračovat v iteraci).
 * @return true pokud zbývá víc dat (= ne EOF), false pokud cursor
 *         dorazil na konec.
 */
static bool slice_next_stmt ( const char **cursor, char **out ) {
    *out = NULL;
    const char *p = *cursor;

    /* Vynech leading separátory a whitespace. */
    while ( *p ) {
        if ( *p == '\n' || *p == ';' ) { p++; continue; };
        if ( isspace ( (unsigned char) *p ) ) { p++; continue; };
        if ( *p == '#' ) {
            /* Komentář mimo statement - skipneme do '\n'. */
            while ( *p && *p != '\n' ) p++;
            continue;
        };
        break;
    };

    if ( !*p ) {
        *cursor = p;
        return false;
    };

    /* Statement od p do dalšího ';' nebo '\n' (pozor na řetězce). */
    const char *start = p;
    bool in_str = false;
    char str_q = 0;
    while ( *p ) {
        char c = *p;
        if ( in_str ) {
            if ( c == '\\' && p[1] ) {
                p += 2; continue;
            };
            if ( c == str_q ) {
                in_str = false;
                p++; continue;
            };
            p++; continue;
        };
        if ( c == '"' || c == '\'' ) {
            in_str = true; str_q = c; p++; continue;
        };
        if ( c == '#' ) {
            /* Komentář - statement končí těsně před ním. */
            break;
        };
        if ( c == ';' || c == '\n' ) {
            break;
        };
        p++;
    };

    size_t n = (size_t) ( p - start );
    char *buf = (char *) malloc ( n + 1 );
    if ( !buf ) {
        *cursor = p;
        return *p ? true : false;
    };
    memcpy ( buf, start, n );
    buf[n] = '\0';
    rtrim ( buf );

    /* Posun cursor přes oddělovač / komentář. */
    if ( *p == '#' ) {
        while ( *p && *p != '\n' ) p++;
    };
    if ( *p == ';' || *p == '\n' ) p++;

    *cursor = p;

    if ( buf[0] == '\0' ) {
        free ( buf );
        return *p ? true : true;  /* prázdný stmt - další iter */
    };

    *out = buf;
    return true;
}


/* ========================================================================= */
/*  Token helpers (uvnitř jednoho statementu)                                */
/* ========================================================================= */

/**
 * @brief Načte identifier od cursoru (alphanumeric + '_').
 *
 * @return Počet zkonzumovaných znaků (0 = nic), zapíše do out (NUL
 *         terminator). Posune *cur o n znaků.
 */
static int read_ident ( const char **cur, char *out, size_t out_max ) {
    const char *p = *cur;
    size_t i = 0;
    if ( !( isalpha ( (unsigned char) *p ) || *p == '_' ) ) return 0;
    while ( *p && ( isalnum ( (unsigned char) *p ) || *p == '_' ) ) {
        if ( i + 1 < out_max ) out[i++] = *p;
        p++;
    };
    out[i] = '\0';
    int n = (int) ( p - *cur );
    *cur = p;
    return n;
}


/**
 * @brief Strict prefix match keyword (case-sensitive).
 *
 * Akceptuje jen pokud po keyword následuje whitespace, EOL nebo
 * end-of-string (= keyword není prefix delšího identifikátoru).
 */
static bool match_keyword ( const char *p, const char *kw ) {
    size_t n = strlen ( kw );
    if ( strncmp ( p, kw, n ) != 0 ) return false;
    char c = p[n];
    if ( c == '\0' || isspace ( (unsigned char) c ) || c == '#' ) return true;
    return false;
}


/**
 * @brief Načte string literal v "..." od cursoru (po skip_ws).
 *
 * Podpora escape: \\ \" \n \t (jiné = literál).
 *
 * @return heap kopie obsahu (bez okolních uvozovek), NULL při chybě.
 *         *cur se posune za závěrečnou ".
 */
static char* read_string_literal ( const char **cur,
                                    char *errbuf, size_t errbuf_size ) {
    const char *p = skip_ws ( *cur );
    if ( *p != '"' ) {
        ac_err ( errbuf, errbuf_size, "expected '\"' at start of string literal" );
        return NULL;
    };
    p++;

    GString *gs = g_string_new ( NULL );
    while ( *p && *p != '"' ) {
        if ( *p == '\\' && p[1] ) {
            char e = p[1];
            switch ( e ) {
                case 'n':  g_string_append_c ( gs, '\n' ); break;
                case 't':  g_string_append_c ( gs, '\t' ); break;
                case '"':  g_string_append_c ( gs, '"' );  break;
                case '\\': g_string_append_c ( gs, '\\' ); break;
                default:
                    g_string_append_c ( gs, e );
                    break;
            };
            p += 2;
            continue;
        };
        g_string_append_c ( gs, *p );
        p++;
    };
    if ( *p != '"' ) {
        ac_err ( errbuf, errbuf_size, "unterminated string literal" );
        g_string_free ( gs, TRUE );
        return NULL;
    };
    p++;
    *cur = p;
    return g_string_free ( gs, FALSE );  /* vrátí heap char* */
}


/* ========================================================================= */
/*  Parser - per-statement                                                   */
/* ========================================================================= */

/* Forward decl. */
static bool parse_stmt ( const char *src,
                          bp_action_stmt_t *out,
                          char *errbuf, size_t errbuf_size );


/**
 * @brief Parsuje výraz z C-stringu přes bp_expr_parse.
 *
 * Convenience wrapper - ošetří NULL/empty.
 */
static bp_expr_t* parse_expr_str ( const char *src,
                                    char *errbuf, size_t errbuf_size ) {
    if ( !src || !*src ) {
        ac_err ( errbuf, errbuf_size, "missing expression argument" );
        return NULL;
    };
    return bp_expr_parse ( src, errbuf, errbuf_size );
}


/**
 * @brief Parsuje LOG statement: log "fmt" [, expr]*.
 *
 * Po keyword "log" očekává string literal, pak nula nebo více
 * čárkou-oddělených výrazů jako argumenty pro %X / %d / ...
 */
static bool parse_log_stmt ( const char *args_str,
                              bp_action_stmt_t *out,
                              char *errbuf, size_t errbuf_size ) {
    const char *cur = args_str;
    char *fmt = read_string_literal ( &cur, errbuf, errbuf_size );
    if ( !fmt ) return false;

    out->kind = STMT_LOG;
    out->log.fmt = fmt;
    out->log.n_args = 0;

    cur = skip_ws ( cur );
    while ( *cur ) {
        if ( *cur != ',' ) {
            ac_err ( errbuf, errbuf_size, "expected ',' between log arguments" );
            return false;
        };
        cur++;
        /* Najdeme následující čárku nebo konec - pro bp_expr_parse. */
        const char *arg_start = skip_ws ( cur );
        const char *p = arg_start;
        int depth_paren = 0;
        int depth_brack = 0;
        int depth_brace = 0;
        bool in_str = false;
        char sq = 0;
        while ( *p ) {
            char c = *p;
            if ( in_str ) {
                if ( c == '\\' && p[1] ) { p += 2; continue; };
                if ( c == sq ) { in_str = false; p++; continue; };
                p++; continue;
            };
            if ( c == '"' || c == '\'' ) { in_str = true; sq = c; p++; continue; };
            if ( c == '(' ) depth_paren++;
            else if ( c == ')' ) depth_paren--;
            else if ( c == '[' ) depth_brack++;
            else if ( c == ']' ) depth_brack--;
            else if ( c == '{' ) depth_brace++;
            else if ( c == '}' ) depth_brace--;
            else if ( c == ',' && depth_paren == 0 && depth_brack == 0 && depth_brace == 0 ) break;
            p++;
        };
        size_t n = (size_t) ( p - arg_start );
        char *expr_buf = (char *) malloc ( n + 1 );
        if ( !expr_buf ) return ac_err ( errbuf, errbuf_size, "out of memory" );
        memcpy ( expr_buf, arg_start, n );
        expr_buf[n] = '\0';
        rtrim ( expr_buf );

        if ( out->log.n_args >= BP_ACTION_LOG_MAX_ARGS ) {
            free ( expr_buf );
            return ac_err ( errbuf, errbuf_size, "too many log arguments (max %d)",
                            BP_ACTION_LOG_MAX_ARGS );
        };
        bp_expr_t *e = parse_expr_str ( expr_buf, errbuf, errbuf_size );
        free ( expr_buf );
        if ( !e ) return false;
        out->log.args[ out->log.n_args++ ] = e;
        cur = p;
        cur = skip_ws ( cur );
    };
    return true;
}


/**
 * @brief Parsuje VAR_ASSIGN statement: $name [op]= expr.
 *
 * Formát: $name op_or_eq expr. Operátory: = += -= *= /= <<= >>= &= |= ^=.
 */
static bool parse_var_assign ( const char *src,
                                bp_action_stmt_t *out,
                                char *errbuf, size_t errbuf_size ) {
    const char *p = skip_ws ( src );
    if ( *p != '$' ) {
        return ac_err ( errbuf, errbuf_size, "expected '$' for var assignment" );
    };
    p++;
    char name[BP_ACTION_VAR_NAME_MAX + 1] = {0};
    if ( read_ident ( &p, name, sizeof ( name ) ) == 0 ) {
        return ac_err ( errbuf, errbuf_size, "expected identifier after '$'" );
    };
    /* V1.5.B: validace identifikátoru proti regex (= konzistence
     * se storage layer). read_ident už lexikálně omezuje znaky, ale
     * tato kontrola pokryje i krajní případy (max length, prázdné
     * jméno) a zaručuje single source of truth pro pravidla. */
    if ( !bp_var_name_is_valid ( name ) ) {
        return ac_err ( errbuf, errbuf_size,
                        "invalid variable name '$%s': must match "
                        "[a-zA-Z_][a-zA-Z0-9_]* (max %d chars)",
                        name, BP_VAR_NAME_MAX );
    };
    p = skip_ws ( p );

    var_assign_op_t op = VA_SET;
    if ( strncmp ( p, "<<=", 3 ) == 0 ) { op = VA_SHL; p += 3; }
    else if ( strncmp ( p, ">>=", 3 ) == 0 ) { op = VA_SHR; p += 3; }
    else if ( strncmp ( p, "+=", 2 ) == 0 ) { op = VA_ADD; p += 2; }
    else if ( strncmp ( p, "-=", 2 ) == 0 ) { op = VA_SUB; p += 2; }
    else if ( strncmp ( p, "*=", 2 ) == 0 ) { op = VA_MUL; p += 2; }
    else if ( strncmp ( p, "/=", 2 ) == 0 ) { op = VA_DIV; p += 2; }
    else if ( strncmp ( p, "&=", 2 ) == 0 ) { op = VA_AND; p += 2; }
    else if ( strncmp ( p, "|=", 2 ) == 0 ) { op = VA_OR;  p += 2; }
    else if ( strncmp ( p, "^=", 2 ) == 0 ) { op = VA_XOR; p += 2; }
    else if ( *p == '=' && p[1] != '=' ) { op = VA_SET; p++; }
    else {
        return ac_err ( errbuf, errbuf_size, "expected '=' or compound assign op after $%s",
                        name );
    };

    p = skip_ws ( p );
    bp_expr_t *e = parse_expr_str ( p, errbuf, errbuf_size );
    if ( !e ) return false;

    out->kind = STMT_VAR_ASSIGN;
    strncpy ( out->varas.name, name, BP_ACTION_VAR_NAME_MAX );
    out->varas.op = op;
    out->varas.value = e;
    return true;
}


/**
 * @brief Parsuje POKE statement: poke <addr> <value>.
 *
 * Argumenty oddělené whitespace na top-level (= mimo závorky/string).
 * Použijeme bp_expr_parse na addr (tail je value od první mezery na
 * top-level depth 0). Pro split najdeme index první whitespace mimo
 * závorky.
 *
 * @return true při úspěchu.
 */
static int find_top_level_split ( const char *s ) {
    int depth_paren = 0, depth_brack = 0, depth_brace = 0;
    bool in_str = false;
    char sq = 0;
    int i = 0;
    while ( s[i] ) {
        char c = s[i];
        if ( in_str ) {
            if ( c == '\\' && s[i+1] ) { i += 2; continue; };
            if ( c == sq ) { in_str = false; i++; continue; };
            i++; continue;
        };
        if ( c == '"' || c == '\'' ) { in_str = true; sq = c; i++; continue; };
        if ( c == '(' ) depth_paren++;
        else if ( c == ')' ) depth_paren--;
        else if ( c == '[' ) depth_brack++;
        else if ( c == ']' ) depth_brack--;
        else if ( c == '{' ) depth_brace++;
        else if ( c == '}' ) depth_brace--;
        if ( isspace ( (unsigned char) c ) &&
             depth_paren == 0 && depth_brack == 0 && depth_brace == 0 ) {
            return i;
        };
        i++;
    };
    return -1;
}


static bool parse_two_expr_stmt ( const char *args_str,
                                   bp_expr_t **out_a,
                                   bp_expr_t **out_b,
                                   char *errbuf, size_t errbuf_size,
                                   const char *what ) {
    const char *p = skip_ws ( args_str );
    if ( !*p ) return ac_err ( errbuf, errbuf_size, "%s: missing arguments", what );
    int split = find_top_level_split ( p );
    if ( split < 0 ) {
        return ac_err ( errbuf, errbuf_size, "%s: expected two arguments", what );
    };
    char *first = (char *) malloc ( (size_t) split + 1 );
    if ( !first ) return ac_err ( errbuf, errbuf_size, "out of memory" );
    memcpy ( first, p, (size_t) split );
    first[split] = '\0';
    rtrim ( first );

    const char *rest = skip_ws ( p + split );
    if ( !*rest ) {
        free ( first );
        return ac_err ( errbuf, errbuf_size, "%s: missing second argument", what );
    };

    bp_expr_t *a = parse_expr_str ( first, errbuf, errbuf_size );
    free ( first );
    if ( !a ) return false;

    bp_expr_t *b = parse_expr_str ( rest, errbuf, errbuf_size );
    if ( !b ) {
        bp_expr_free ( a );
        return false;
    };
    *out_a = a;
    *out_b = b;
    return true;
}


/**
 * @brief Parsuje SET_REG statement: set <reg-name> <value-expr>.
 *
 * Reg-name akceptuje běžný identifier + volitelný apostrof na konci
 * (= shadow registry: `AF'`, `BC'`, `DE'`, `HL'`). Apostrof se zachová
 * v jméně a porovnává v write_cpu_reg case-insensitive na lowercase.
 */
static bool parse_set_reg_stmt ( const char *args_str,
                                  bp_action_stmt_t *out,
                                  char *errbuf, size_t errbuf_size ) {
    const char *p = skip_ws ( args_str );
    char reg[BP_ACTION_VAR_NAME_MAX + 1] = {0};
    int n = read_ident ( &p, reg, sizeof ( reg ) );
    if ( n == 0 ) {
        return ac_err ( errbuf, errbuf_size, "set: expected register name" );
    };
    /* Volitelný apostrof pro shadow registry (AF', BC', DE', HL'). */
    if ( *p == '\'' ) {
        size_t cur_len = strlen ( reg );
        if ( cur_len + 1 < sizeof ( reg ) ) {
            reg[cur_len]     = '\'';
            reg[cur_len + 1] = '\0';
        };
        p++;
    };
    p = skip_ws ( p );
    if ( !*p ) return ac_err ( errbuf, errbuf_size, "set: missing value expression" );
    bp_expr_t *e = parse_expr_str ( p, errbuf, errbuf_size );
    if ( !e ) return false;

    out->kind = STMT_SET_REG;
    strncpy ( out->setreg.reg, reg, BP_ACTION_VAR_NAME_MAX );
    out->setreg.value = e;
    return true;
}


/**
 * @brief Najde top-level keyword (jako celé slovo) v textu.
 *
 * Top-level = mimo závorky/brackets/braces a mimo string literály.
 * Keyword se musí jevit jako samostatný token (= obklopen whitespace
 * nebo začátkem/koncem řetězce).
 *
 * @param src       Vstupní text.
 * @param keyword   Hledané klíčové slovo.
 * @return Pointer do src na začátek keyword, nebo NULL pokud nenalezen.
 */
static const char* find_top_level_keyword ( const char *src, const char *keyword ) {
    if ( !src || !keyword || !*keyword ) return NULL;
    size_t kw_n = strlen ( keyword );

    const char *p = src;
    int depth_paren = 0, depth_brack = 0, depth_brace = 0;
    bool in_str = false;
    char sq = 0;
    while ( *p ) {
        char c = *p;
        if ( in_str ) {
            if ( c == '\\' && p[1] ) { p += 2; continue; };
            if ( c == sq ) { in_str = false; p++; continue; };
            p++; continue;
        };
        if ( c == '"' || c == '\'' ) { in_str = true; sq = c; p++; continue; };
        if ( c == '(' ) depth_paren++;
        else if ( c == ')' ) depth_paren--;
        else if ( c == '[' ) depth_brack++;
        else if ( c == ']' ) depth_brack--;
        else if ( c == '{' ) depth_brace++;
        else if ( c == '}' ) depth_brace--;

        if ( depth_paren == 0 && depth_brack == 0 && depth_brace == 0 ) {
            if ( ( p == src || isspace ( (unsigned char) p[-1] ) ) &&
                 strncmp ( p, keyword, kw_n ) == 0 ) {
                char after = p[kw_n];
                if ( after == '\0' || isspace ( (unsigned char) after ) ) {
                    return p;
                };
            };
        };
        p++;
    };
    return NULL;
}


/**
 * @brief Parsuje IF statement: if <expr> then <stmt> [else <stmt>].
 *
 * `else` je volitelný a parsuje se jako jeden statement (= jednořádkový
 * conditional, žádný blok).
 */
static bool parse_if_stmt ( const char *args_str,
                             bp_action_stmt_t *out,
                             char *errbuf, size_t errbuf_size ) {
    /* Najdi pozici keyword "then" - top-level. */
    const char *then_at = find_top_level_keyword ( args_str, "then" );

    if ( !then_at ) {
        return ac_err ( errbuf, errbuf_size, "if: missing 'then' keyword" );
    };

    /* Cond text = od start do then_at (trim). */
    size_t cond_n = (size_t) ( then_at - args_str );
    char *cond_buf = (char *) malloc ( cond_n + 1 );
    if ( !cond_buf ) return ac_err ( errbuf, errbuf_size, "out of memory" );
    memcpy ( cond_buf, args_str, cond_n );
    cond_buf[cond_n] = '\0';
    rtrim ( cond_buf );
    bp_expr_t *cond = parse_expr_str ( cond_buf, errbuf, errbuf_size );
    free ( cond_buf );
    if ( !cond ) return false;

    /* Then-body = za "then" + skip_ws. */
    const char *body_src = skip_ws ( then_at + 4 );
    if ( !*body_src ) {
        bp_expr_free ( cond );
        return ac_err ( errbuf, errbuf_size, "if: missing body statement after 'then'" );
    };

    /* Hledáme volitelný "else" v body_src. Pokud najdeme, then-body je
     * text před ním a else-body je text za ním. */
    const char *else_at = find_top_level_keyword ( body_src, "else" );

    /* Then-body text */
    char *then_buf = NULL;
    if ( else_at ) {
        size_t then_n = (size_t) ( else_at - body_src );
        then_buf = (char *) malloc ( then_n + 1 );
        if ( !then_buf ) {
            bp_expr_free ( cond );
            return ac_err ( errbuf, errbuf_size, "out of memory" );
        };
        memcpy ( then_buf, body_src, then_n );
        then_buf[then_n] = '\0';
        rtrim ( then_buf );
    } else {
        then_buf = g_strdup ( body_src );
        if ( !then_buf ) {
            bp_expr_free ( cond );
            return ac_err ( errbuf, errbuf_size, "out of memory" );
        };
        rtrim ( then_buf );
    };

    if ( then_buf[0] == '\0' ) {
        free ( then_buf );
        bp_expr_free ( cond );
        return ac_err ( errbuf, errbuf_size, "if: empty 'then' body" );
    };

    bp_action_stmt_t *child = (bp_action_stmt_t *) calloc ( 1, sizeof ( bp_action_stmt_t ) );
    if ( !child ) {
        free ( then_buf );
        bp_expr_free ( cond );
        return ac_err ( errbuf, errbuf_size, "out of memory" );
    };
    if ( !parse_stmt ( then_buf, child, errbuf, errbuf_size ) ) {
        free ( then_buf );
        bp_expr_free ( cond );
        free ( child );
        return false;
    };
    free ( then_buf );

    /* Else-body (volitelné) */
    bp_action_stmt_t *else_child = NULL;
    if ( else_at ) {
        const char *else_src = skip_ws ( else_at + 4 );
        if ( !*else_src ) {
            bp_action_stmt_free_inner ( child );
            free ( child );
            bp_expr_free ( cond );
            return ac_err ( errbuf, errbuf_size, "if: missing body statement after 'else'" );
        };
        else_child = (bp_action_stmt_t *) calloc ( 1, sizeof ( bp_action_stmt_t ) );
        if ( !else_child ) {
            bp_action_stmt_free_inner ( child );
            free ( child );
            bp_expr_free ( cond );
            return ac_err ( errbuf, errbuf_size, "out of memory" );
        };
        if ( !parse_stmt ( else_src, else_child, errbuf, errbuf_size ) ) {
            free ( else_child );
            bp_action_stmt_free_inner ( child );
            free ( child );
            bp_expr_free ( cond );
            return false;
        };
    };

    /* IF body NEsmí být další IF (= no nested for V1) - vědomě povolíme,
     * je to jednoduché. */

    out->kind = STMT_IF;
    out->iff.cond = cond;
    out->iff.child = child;
    out->iff.else_child = else_child;
    return true;
}


/**
 * @brief Parsuje libovolný statement do `out`. Out musí být zero-init.
 */
static bool parse_stmt ( const char *src,
                          bp_action_stmt_t *out,
                          char *errbuf, size_t errbuf_size ) {
    const char *p = skip_ws ( src );
    if ( !*p ) {
        return ac_err ( errbuf, errbuf_size, "empty statement" );
    };

    /* Var assignment začíná '$'. */
    if ( *p == '$' ) {
        return parse_var_assign ( p, out, errbuf, errbuf_size );
    };

    /* Bezargumentové keyword příkazy. */
    if ( match_keyword ( p, "continue" ) ) {
        out->kind = STMT_CONTINUE;
        return true;
    };
    if ( match_keyword ( p, "disable_self" ) ) {
        out->kind = STMT_DISABLE_SELF;
        return true;
    };
    if ( match_keyword ( p, "clear_vars" ) ) {
        out->kind = STMT_CLEAR_VARS;
        return true;
    };

    /* Keyword + argumenty. */
    if ( match_keyword ( p, "log" ) ) {
        return parse_log_stmt ( skip_ws ( p + 3 ), out, errbuf, errbuf_size );
    };
    if ( match_keyword ( p, "enable" ) ) {
        const char *arg = skip_ws ( p + 6 );
        if ( !*arg ) return ac_err ( errbuf, errbuf_size, "enable: missing target name" );
        out->kind = STMT_ENABLE;
        out->target.name = g_strdup ( arg );
        rtrim ( out->target.name );
        return true;
    };
    if ( match_keyword ( p, "disable" ) ) {
        const char *arg = skip_ws ( p + 7 );
        if ( !*arg ) return ac_err ( errbuf, errbuf_size, "disable: missing target name" );
        out->kind = STMT_DISABLE;
        out->target.name = g_strdup ( arg );
        rtrim ( out->target.name );
        return true;
    };
    if ( match_keyword ( p, "poke" ) ) {
        bp_expr_t *a = NULL, *b = NULL;
        if ( !parse_two_expr_stmt ( skip_ws ( p + 4 ), &a, &b, errbuf, errbuf_size, "poke" ) ) {
            return false;
        };
        out->kind = STMT_POKE;
        out->poke.addr = a;
        out->poke.value = b;
        return true;
    };
    if ( match_keyword ( p, "set" ) ) {
        return parse_set_reg_stmt ( skip_ws ( p + 3 ), out, errbuf, errbuf_size );
    };
    if ( match_keyword ( p, "mark" ) ) {
        const char *arg = skip_ws ( p + 4 );
        char *name = read_string_literal ( &arg, errbuf, errbuf_size );
        if ( !name ) return false;
        out->kind = STMT_MARK;
        out->target.name = name;
        /* Parse-time registrace markeru do trace-suite registru. Selhání
         * (overflow / truncate) je signalizováno přes MARKLOG_INVALID_ID,
         * fire path pak no-op pro marklog (stdout funguje nezávisle). */
        out->target.marker_id = marklog_register ( name );
        return true;
    };
    if ( match_keyword ( p, "if" ) ) {
        return parse_if_stmt ( skip_ws ( p + 2 ), out, errbuf, errbuf_size );
    };

    return ac_err ( errbuf, errbuf_size, "unknown action statement: '%s'", p );
}


/**
 * @brief Append parsovaný statement do action seznamu (s realokem).
 */
static bool append_stmt ( bp_action_t *act, const bp_action_stmt_t *stmt ) {
    if ( act->n_stmts >= act->cap_stmts ) {
        int new_cap = act->cap_stmts ? act->cap_stmts * 2 : 4;
        bp_action_stmt_t *neu = (bp_action_stmt_t *)
            realloc ( act->stmts, (size_t) new_cap * sizeof ( bp_action_stmt_t ) );
        if ( !neu ) return false;
        act->stmts = neu;
        act->cap_stmts = new_cap;
    };
    act->stmts[ act->n_stmts++ ] = *stmt;
    return true;
}


/* ========================================================================= */
/*  Public API - parse                                                       */
/* ========================================================================= */

bp_action_t* bp_action_parse ( const char *source,
                                char *errbuf, size_t errbuf_size ) {
    if ( errbuf && errbuf_size > 0 ) errbuf[0] = '\0';
    if ( !source ) return NULL;
    /* Prázdný / whitespace-only source = NULL bez chyby. */
    const char *check = skip_ws ( source );
    if ( !*check ) return NULL;

    bp_action_t *act = (bp_action_t *) calloc ( 1, sizeof ( bp_action_t ) );
    if ( !act ) {
        ac_err ( errbuf, errbuf_size, "out of memory" );
        return NULL;
    };

    const char *cursor = source;
    while ( 1 ) {
        char *stmt_src = NULL;
        bool more = slice_next_stmt ( &cursor, &stmt_src );
        if ( !stmt_src ) {
            if ( !more ) break;
            continue;
        };
        bp_action_stmt_t s;
        memset ( &s, 0, sizeof ( s ) );
        if ( !parse_stmt ( stmt_src, &s, errbuf, errbuf_size ) ) {
            free ( stmt_src );
            bp_action_free ( act );
            return NULL;
        };
        free ( stmt_src );
        if ( !append_stmt ( act, &s ) ) {
            bp_action_stmt_free_inner ( &s );
            ac_err ( errbuf, errbuf_size, "out of memory" );
            bp_action_free ( act );
            return NULL;
        };
        if ( !more ) break;
    };

    /* Pokud po slicování nezbyl žádný stmt = chyba (= mělo by být zachyceno
     * dříve "empty source"). */
    if ( act->n_stmts == 0 ) {
        bp_action_free ( act );
        return NULL;
    };

    return act;
}


void bp_action_free ( bp_action_t *action ) {
    if ( !action ) return;
    for ( int i = 0; i < action->n_stmts; i++ ) {
        bp_action_stmt_free_inner ( &action->stmts[i] );
    };
    free ( action->stmts );
    free ( action );
}


/* ========================================================================= */
/*  Classifier (Event Viewer BP_FIRE subtype - public API)                   */
/* ========================================================================= */

/**
 * @brief Klasifikační priorita pro výběr dominantního subtype.
 *
 * Vyšší číslo = "důležitější" - pokud skript obsahuje víc různých typů
 * statementů, vyhraje ten s nejvyšší prioritou. Pořadí odpovídá komentáři
 * u @ref bp_action_classify_subtype() v bp_action.h.
 *
 * Použití: per-stmt @c classify_single() vrátí kandidátní subtype +
 * prioritu, vnější iterace si drží max.
 */
static int classify_priority ( en_BP_ACTION_SUB sub ) {
    switch ( sub ) {
        case BP_ACTION_SUB_HALT:     return 0;
        case BP_ACTION_SUB_CONTINUE: return 1;
        case BP_ACTION_SUB_IGNORE:   return 2;
        case BP_ACTION_SUB_ENABLE:   return 3;
        case BP_ACTION_SUB_DISABLE:  return 4;
        case BP_ACTION_SUB_MARK:     return 5;
    };
    return 0;
}


/**
 * @brief Mapování interního @c stmt_kind_t na veřejný subtype.
 *
 * Pro @c STMT_IF prochází rekurzivně then/else větve a vrací max prioritu
 * z obou. Pro CONTINUE statement (= explicit `continue` v DSL) vrací
 * @c CONTINUE - stejné jako "běžné" stmt (log/poke/set/var), protože
 * uživatelská intence je "BP fíruje a nezastavuje".
 */
static en_BP_ACTION_SUB classify_stmt ( const bp_action_stmt_t *s ) {
    if ( !s ) return BP_ACTION_SUB_CONTINUE;
    switch ( s->kind ) {
        case STMT_MARK:
            return BP_ACTION_SUB_MARK;
        case STMT_DISABLE:
        case STMT_DISABLE_SELF:
            return BP_ACTION_SUB_DISABLE;
        case STMT_ENABLE:
            return BP_ACTION_SUB_ENABLE;
        case STMT_IF: {
            /* Rekurzivně zvol nejvyšší prioritu z then/else větví.
             * Pokud žádná větev neexistuje (nemělo by se stát po validním
             * parse), fallback na CONTINUE. */
            en_BP_ACTION_SUB best = BP_ACTION_SUB_CONTINUE;
            if ( s->iff.child ) {
                en_BP_ACTION_SUB sub = classify_stmt ( s->iff.child );
                if ( classify_priority ( sub ) > classify_priority ( best ) ) {
                    best = sub;
                };
            };
            if ( s->iff.else_child ) {
                en_BP_ACTION_SUB sub = classify_stmt ( s->iff.else_child );
                if ( classify_priority ( sub ) > classify_priority ( best ) ) {
                    best = sub;
                };
            };
            return best;
        };
        case STMT_LOG:
        case STMT_CONTINUE:
        case STMT_POKE:
        case STMT_SET_REG:
        case STMT_VAR_ASSIGN:
        case STMT_CLEAR_VARS:
            return BP_ACTION_SUB_CONTINUE;
    };
    return BP_ACTION_SUB_CONTINUE;
}


en_BP_ACTION_SUB bp_action_classify_subtype ( const bp_action_t *action ) {
    /* NULL nebo prázdné AST = "stop" sémantika = HALT. */
    if ( !action ) return BP_ACTION_SUB_HALT;
    if ( action->n_stmts <= 0 ) return BP_ACTION_SUB_HALT;

    /* Procházíme všechny top-level stmts a držíme si max prioritu. */
    en_BP_ACTION_SUB best = BP_ACTION_SUB_CONTINUE;
    int best_prio = classify_priority ( best );
    for ( int i = 0; i < action->n_stmts; i++ ) {
        en_BP_ACTION_SUB sub = classify_stmt ( &action->stmts[i] );
        int p = classify_priority ( sub );
        if ( p > best_prio ) {
            best = sub;
            best_prio = p;
        };
    };
    return best;
}


/* ========================================================================= */
/*  Executor                                                                 */
/* ========================================================================= */

/**
 * @brief Vyhodnotí výraz proti ctx (NULL-safe).
 */
static int32_t eval_arg ( bp_expr_t *e, const struct bp_expr_ctx_s *ctx ) {
    if ( !e || !ctx ) return 0;
    return bp_expr_eval ( e, ctx );
}


/**
 * @brief Append binární reprezentaci value (nepravolinová) do GString.
 *
 * Trim leading nuly (= "0" pro 0, jinak nejvyšší 1-bit).
 */
static void append_binary ( GString *gs, uint32_t value ) {
    if ( value == 0 ) {
        g_string_append_c ( gs, '0' );
        return;
    };
    /* Najít první '1'. */
    int i = 31;
    while ( i > 0 && ! ( value & ( 1u << i ) ) ) i--;
    for ( ; i >= 0; i-- ) {
        g_string_append_c ( gs, ( value & ( 1u << i ) ) ? '1' : '0' );
    };
}


/**
 * @brief Provede log statement - format string + N expr args.
 *
 * Specifikátory: %X %x %d %u %b %c %s %%. %s podporuje jen literál
 * (= žádný expr argument) a konzumuje arg jako int (= ukládáme jako
 * char[]?). V1 omezení: %s se chová jako %d (= varování, V1.5
 * doplníme). Pokud user dává %s, má rozšířit log fmt sám.
 */
static void execute_log ( const bp_action_stmt_t *s,
                           const struct bp_expr_ctx_s *ctx ) {
    GString *out = g_string_new ( NULL );
    int next_arg = 0;
    const char *p = s->log.fmt;

    while ( *p ) {
        if ( *p == '%' && p[1] ) {
            char spec = p[1];
            if ( spec == '%' ) {
                g_string_append_c ( out, '%' );
                p += 2;
                continue;
            };
            int32_t v = 0;
            if ( next_arg < s->log.n_args ) {
                v = eval_arg ( s->log.args[next_arg++], ctx );
            };
            switch ( spec ) {
                case 'X':
                    g_string_append_printf ( out, "%X", (unsigned) v );
                    break;
                case 'x':
                    g_string_append_printf ( out, "%x", (unsigned) v );
                    break;
                case 'd':
                    g_string_append_printf ( out, "%d", (int) v );
                    break;
                case 'u':
                    g_string_append_printf ( out, "%u", (unsigned) v );
                    break;
                case 'b':
                    append_binary ( out, (uint32_t) v );
                    break;
                case 'c':
                    g_string_append_c ( out, (char) ( v & 0xFF ) );
                    break;
                case 's': {
                    /* V1.6+ 4.1: %s arg = expression vyhodnoceny jako adresa,
                     * lookup v sym_db. Pokud najdeme symbol pro adresu,
                     * vypiseme jeho name; jinak placeholder s konkretni
                     * neznamou adresou.
                     *
                     * Pouzivame bank_id 0 (= "CPU view" globalni symbols).
                     * Specificke bank scopes nejsou v action DSL exposed -
                     * future expansion (V1.7+ TBD). */
                    uint32_t addr = (uint32_t) ( v & 0xFFFFu );
                    const st_SYMBOL *sym = sym_db_lookup_by_addr ( addr, 0 );
                    if ( sym && sym->name ) {
                        g_string_append ( out, sym->name );
                    }
                    else {
                        g_string_append_printf ( out,
                            "<no-symbol-at-0x%04X>", (unsigned) addr );
                    };
                    break;
                };
                default:
                    g_string_append_c ( out, '%' );
                    g_string_append_c ( out, spec );
                    break;
            };
            p += 2;
            continue;
        };
        g_string_append_c ( out, *p );
        p++;
    };

    /* V1: log do stdout. V1.5+: trace-suite + UI debug konzole. */
    fprintf ( stdout, "[BP-LOG] %s\n", out->str );
    fflush ( stdout );
    g_string_free ( out, TRUE );
}


/**
 * @brief Najde BP podle name (případ-citlivé shoda).
 *
 * @return ID nebo -1 pokud nenalezen.
 */
static int find_bp_id_by_name ( const char *name ) {
    if ( !name || !*name || !g_breakpoints.breakpoints ) return -1;
    unsigned i;
    for ( i = 0; i < g_breakpoints.breakpoints->len; i++ ) {
        st_BPT *bpt = &g_array_index ( g_breakpoints.breakpoints, st_BPT, i );
        if ( bpt->name && strcmp ( bpt->name, name ) == 0 ) {
            return bpt->id;
        };
    };
    return -1;
}


/**
 * @brief Aplikuje var_assign_op na current+rhs, vrátí novou hodnotu.
 *
 * Dělení nulou → 0 (= konzistentní s bp_expr eval).
 */
static int32_t apply_var_op ( var_assign_op_t op, int32_t cur, int32_t rhs ) {
    switch ( op ) {
        case VA_SET: return rhs;
        case VA_ADD: return cur + rhs;
        case VA_SUB: return cur - rhs;
        case VA_MUL: return cur * rhs;
        case VA_DIV: return ( rhs == 0 ) ? 0 : cur / rhs;
        case VA_SHL: return (int32_t) ( (uint32_t) cur << ( rhs & 31 ) );
        case VA_SHR: return (int32_t) ( (uint32_t) cur >> ( rhs & 31 ) );
        case VA_AND: return cur & rhs;
        case VA_OR:  return cur | rhs;
        case VA_XOR: return cur ^ rhs;
    };
    return cur;
}


/**
 * @brief Zápis Z80 registru podle case-insensitive jména.
 *
 * Podporuje stejnou množinu jmen jako bp_expr resolve_ident pro 8/16-bit
 * registry + I/R/IM/IFF1/IFF2 + shadow páry AF'/BC'/DE'/HL' (apostrof
 * zachován v lowercase jméně, konzistentní se čtecí stranou).
 *
 * @return true při úspěchu, false pokud reg name neznámý.
 */
static bool write_cpu_reg ( const char *name_raw, z80_t *cpu, int32_t value ) {
    if ( !cpu || !name_raw ) return false;
    char n[BP_ACTION_VAR_NAME_MAX + 1];
    size_t i;
    for ( i = 0; name_raw[i] && i < BP_ACTION_VAR_NAME_MAX; i++ ) {
        n[i] = (char) tolower ( (unsigned char) name_raw[i] );
    };
    n[i] = '\0';

    uint16_t v16 = (uint16_t) value;
    uint8_t  v8  = (uint8_t)  value;

    if ( strcmp ( n, "a" ) == 0 )  { cpu->af.h = v8;  return true; }
    if ( strcmp ( n, "f" ) == 0 )  { cpu->af.l = v8;  return true; }
    if ( strcmp ( n, "b" ) == 0 )  { cpu->bc.h = v8;  return true; }
    if ( strcmp ( n, "c" ) == 0 )  { cpu->bc.l = v8;  return true; }
    if ( strcmp ( n, "d" ) == 0 )  { cpu->de.h = v8;  return true; }
    if ( strcmp ( n, "e" ) == 0 )  { cpu->de.l = v8;  return true; }
    if ( strcmp ( n, "h" ) == 0 )  { cpu->hl.h = v8;  return true; }
    if ( strcmp ( n, "l" ) == 0 )  { cpu->hl.l = v8;  return true; }
    if ( strcmp ( n, "af" ) == 0 ) { cpu->af.w = v16; return true; }
    if ( strcmp ( n, "bc" ) == 0 ) { cpu->bc.w = v16; return true; }
    if ( strcmp ( n, "de" ) == 0 ) { cpu->de.w = v16; return true; }
    if ( strcmp ( n, "hl" ) == 0 ) { cpu->hl.w = v16; return true; }
    if ( strcmp ( n, "ix" ) == 0 ) { cpu->ix.w = v16; return true; }
    if ( strcmp ( n, "iy" ) == 0 ) { cpu->iy.w = v16; return true; }
    if ( strcmp ( n, "ixh" ) == 0 ) { cpu->ix.h = v8; return true; }
    if ( strcmp ( n, "ixl" ) == 0 ) { cpu->ix.l = v8; return true; }
    if ( strcmp ( n, "iyh" ) == 0 ) { cpu->iy.h = v8; return true; }
    if ( strcmp ( n, "iyl" ) == 0 ) { cpu->iy.l = v8; return true; }
    if ( strcmp ( n, "sp" ) == 0 ) { cpu->sp   = v16; return true; }
    if ( strcmp ( n, "pc" ) == 0 ) { cpu->pc   = v16; return true; }
    if ( strcmp ( n, "i" ) == 0 )  { cpu->i    = v8;  return true; }
    if ( strcmp ( n, "r" ) == 0 )  { cpu->r    = v8;  return true; }
    if ( strcmp ( n, "im" ) == 0 ) { cpu->im   = v8 & 3; return true; }
    if ( strcmp ( n, "iff1" ) == 0 ) { cpu->iff1 = v8 & 1; return true; }
    if ( strcmp ( n, "iff2" ) == 0 ) { cpu->iff2 = v8 & 1; return true; }

    /* Shadow registry - apostrof zachovaný v lowercase. */
    if ( strcmp ( n, "af'" ) == 0 ) { cpu->af2.w = v16; return true; }
    if ( strcmp ( n, "bc'" ) == 0 ) { cpu->bc2.w = v16; return true; }
    if ( strcmp ( n, "de'" ) == 0 ) { cpu->de2.w = v16; return true; }
    if ( strcmp ( n, "hl'" ) == 0 ) { cpu->hl2.w = v16; return true; }

    return false;
}


/**
 * @brief Forward decl pro rekurzivní eval (= IF body).
 */
static void execute_stmt ( const bp_action_stmt_t *s,
                            const struct bp_expr_ctx_s *ctx );


static void execute_stmt ( const bp_action_stmt_t *s,
                            const struct bp_expr_ctx_s *ctx ) {
    switch ( s->kind ) {
        case STMT_LOG:
            execute_log ( s, ctx );
            break;
        case STMT_CONTINUE:
            /* Default už je continue - V1 jen marker. */
            break;
        case STMT_DISABLE_SELF:
            /* D.2: ctx->self_id naplní BP enforcement vrstva. Pokud je
             * akce volaná mimo enforcement (= test / manual eval),
             * self_id zůstane -1 a stmt se chová jako no-op + warning. */
            if ( ctx && ctx->self_id >= 0 ) {
                breakpoints_set_enabled ( ctx->self_id, false );
            } else {
                fprintf ( stderr, "[BP-ACTION] disable_self: no enforcement context (self_id=-1) - skipped\n" );
            };
            break;
        case STMT_ENABLE: {
            int id = find_bp_id_by_name ( s->target.name );
            if ( id < 0 ) {
                fprintf ( stderr, "[BP-ACTION] enable: BP '%s' not found\n",
                          s->target.name );
                break;
            };
            breakpoints_set_enabled ( id, true );
            break;
        };
        case STMT_DISABLE: {
            int id = find_bp_id_by_name ( s->target.name );
            if ( id < 0 ) {
                fprintf ( stderr, "[BP-ACTION] disable: BP '%s' not found\n",
                          s->target.name );
                break;
            };
            breakpoints_set_enabled ( id, false );
            break;
        };
        case STMT_POKE: {
            int32_t addr = eval_arg ( s->poke.addr, ctx );
            int32_t val  = eval_arg ( s->poke.value, ctx );
            memory_write_byte ( (uint16_t) addr, (uint8_t) val );
            break;
        };
        case STMT_SET_REG: {
            int32_t v = eval_arg ( s->setreg.value, ctx );
            if ( !ctx || !ctx->cpu ) {
                fprintf ( stderr, "[BP-ACTION] set: no CPU context\n" );
                break;
            };
            if ( !write_cpu_reg ( s->setreg.reg, ctx->cpu, v ) ) {
                fprintf ( stderr, "[BP-ACTION] set: unknown register '%s'\n",
                          s->setreg.reg );
            };
            break;
        };
        case STMT_MARK:
            /* Trace-suite marklog binární záznam (5. log subsystem).
             * marker_id je pre-resolved při parse přes marklog_register().
             * No-op pokud writer ne-aktivní nebo id == MARKLOG_INVALID_ID
             * (overflow registry při parse). */
            if ( s->target.marker_id != MARKLOG_INVALID_ID ) {
                marklog_record ( s->target.marker_id );
            }
            /* Stdout část - zachována pro back-compat, nezávisle
             * konfigurovatelná přes [TRACE_MARKLOG].stdout_enabled. */
            if ( g_marklog_config.stdout_enabled ) {
                fprintf ( stdout, "[BP-MARK] %s\n", s->target.name );
                fflush ( stdout );
            }
            break;
        case STMT_VAR_ASSIGN: {
            int32_t cur = bp_var_get ( s->varas.name );
            int32_t rhs = eval_arg ( s->varas.value, ctx );
            int32_t neu = apply_var_op ( s->varas.op, cur, rhs );
            bp_var_set ( s->varas.name, neu );
            break;
        };
        case STMT_CLEAR_VARS:
            bp_vars_clear_all ( );
            break;
        case STMT_IF: {
            int32_t cond = eval_arg ( s->iff.cond, ctx );
            if ( cond ) {
                if ( s->iff.child ) execute_stmt ( s->iff.child, ctx );
            } else {
                if ( s->iff.else_child ) execute_stmt ( s->iff.else_child, ctx );
            };
            break;
        };
    };
}


/**
 * @brief Maximální hloubka rekurze při exekuci action skriptů.
 *
 * STMT_POKE volá memory_write_byte, což může reentrovat enforce vrstvu
 * (= MEM_W BP nad stejnou adresou s další action) a tím rekurzivně
 * vyvolat bp_action_execute. Bez ochrany hrozí stack overflow.
 *
 * Limit 4 = dostatečné pro legitimní 1-2 úrovně nested action volaných
 * přes poke; nad tím se jedná o run-away rekurzi.
 */
#define BP_ACTION_MAX_DEPTH  4


/**
 * @brief Aktuální hloubka exekuce action skriptů.
 *
 * Inkrementuje se na začátku bp_action_execute, dekrementuje na konci.
 * Globální (per-thread by bylo lepší, ale BP enforce je single-thread
 * v emulator vlákně).
 */
static int g_bp_action_depth = 0;


bool bp_action_execute ( const bp_action_t *action,
                          const struct bp_expr_ctx_s *ctx ) {
    /* NULL nebo prázdná action → stop. */
    if ( !action || action->n_stmts == 0 ) return false;
    if ( !ctx ) return false;

    /* Re-entry guard: chrání proti nekonečné rekurzi pri self-poke
     * (= MEM_W BP s action 'poke <same-addr>'). Po překročení limitu
     * vypisujeme warning a action skipujeme - emulator pokračuje
     * nezraněn. */
    if ( g_bp_action_depth >= BP_ACTION_MAX_DEPTH ) {
        fprintf ( stderr,
                  "[BP-ACTION] re-entry depth limit (%d) exceeded, skipping action\n",
                  BP_ACTION_MAX_DEPTH );
        return true;
    };

    g_bp_action_depth++;

    int i;
    for ( i = 0; i < action->n_stmts; i++ ) {
        execute_stmt ( &action->stmts[i], ctx );
    };

    g_bp_action_depth--;

    /* V1 implicit continue: jakákoliv non-empty action způsobí continue.
     * Žádný explicit `stop` v V1 neexistuje. */
    return true;
}

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
