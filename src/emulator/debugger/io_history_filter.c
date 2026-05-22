/*
 * io_history_filter.c - AST-based parser a evaluátor filter syntaxe.
 *
 * Viz io_history_filter.h pro kontrakty a syntax. Fáze 3 boolean-expr
 * refactor (V1.5.F3): parser doplněn o závorky "(" ")" jako primární
 * atom branch a o unární "!" operátor nad libovolným atomem (= leaf
 * token nebo závorkovaný podvýraz). NOT nad jediným leaf tokenem se
 * collapse-uje do LEAF s negate=true (= bit-exact F1/F2 chování pro
 * "!port:CE" apod.). NOT nad závorkovaným podvýrazem postaví NOT
 * uzel, který v matcher invertuje boolean výsledek subtree.
 *
 * Gramatika v F3:
 *   expr     = or_expr
 *   or_expr  = and_expr ( ( "|" | "OR" ) and_expr )*
 *   and_expr = unary    ( ( "&" | "AND" | implicit-WS ) unary )*
 *   unary    = "!" unary | atom
 *   atom     = "(" expr ")" | leaf_token
 *
 * Datový model je AST s LEAF / NOT / AND / OR uzly v aréně, matcher
 * je rekurzivní eval se short-circuit AND/OR. Arena má pevný limit
 * IOFILT_MAX_NODES (64) k omezení patologických vstupů.
 *
 * Licence: GPLv3
 */

#include "io_history_filter.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


/* ========================================================================= */
/*  AST uzly - interní typy                                                  */
/* ========================================================================= */


/**
 * @brief Druh AST uzlu.
 *
 * - LEAF: atomární podmínka (jeden leaf token z dnešní syntaxe).
 * - NOT:  unární negace nad jedním podstromem.
 * - AND:  binární AND, short-circuit (false → false).
 * - OR:   binární OR, short-circuit (true → true). Připraveno pro F2.
 */
typedef enum
{
    IOFILT_NODE_LEAF = 0,
    IOFILT_NODE_NOT  = 1,
    IOFILT_NODE_AND  = 2,
    IOFILT_NODE_OR   = 3
} en_IOFILT_NODE_KIND;


/**
 * @brief Druh leaf payloadu (= konkrétní typ atomické podmínky).
 *
 * Hodnoty odpovídají dnešním filter prefixům (port, port16, pc, frame,
 * value, cycle, addr, type=in/out, memtype=mr/mw, name=plain text).
 */
typedef enum
{
    IOFILT_LEAF_PORT    = 0,    /**< port: nebo port16: (rozlišeno přes port_is_8bit). */
    IOFILT_LEAF_PC      = 1,    /**< pc: */
    IOFILT_LEAF_VALUE   = 2,    /**< value: */
    IOFILT_LEAF_FRAME   = 3,    /**< frame: */
    IOFILT_LEAF_CYCLE   = 4,    /**< cycle: */
    IOFILT_LEAF_ADDR    = 5,    /**< addr: (MMIO only) */
    IOFILT_LEAF_TYPE    = 6,    /**< in / out keyword */
    IOFILT_LEAF_MEMTYPE = 7,    /**< mr / mw keyword */
    IOFILT_LEAF_NAME    = 8     /**< plain text substring match */
} en_IOFILT_LEAF_KIND;


/**
 * @brief Maximální délka name substring tokenu uvnitř leaf payloadu.
 *
 * Shodné s předchozí flat implementací (= 64 B včetně null).
 */
#define IOFILT_NAME_MAX 64


/**
 * @brief Leaf payload - tagged union dle @ref en_IOFILT_LEAF_KIND.
 *
 * Pro range / single-value leaf jsou min/max v unified poli (single = min==max).
 *
 * @field kind       Druh leaf payloadu.
 * @field negate     true pokud byl token uvozen '!' (negace na úrovni tokenu).
 * @field port_is_8bit Platí jen pro IOFILT_LEAF_PORT: true = "port:" (low byte),
 *                   false = "port16:" (full 16-bit). U ostatních leaf typů ignorováno.
 * @field match_in   Platí jen pro IOFILT_LEAF_TYPE: true pokud token byl "in".
 * @field match_out  Platí jen pro IOFILT_LEAF_TYPE: true pokud token byl "out".
 * @field match_mr   Platí jen pro IOFILT_LEAF_MEMTYPE: true pokud "mr".
 * @field match_mw   Platí jen pro IOFILT_LEAF_MEMTYPE: true pokud "mw".
 * @field rmin/rmax  Range hodnota - 32-bit pojme všechny varianty
 *                   (port/pc/addr 16-bit, value 8-bit, frame/cycle 32-bit).
 *                   Při single-value match rmin == rmax. UINT32_MAX značí
 *                   "neomezeno nahoře" (pro frame:>N / cycle:>N).
 * @field name       Buffer pro NAME leaf substring (case-insensitive match).
 */
typedef struct st_IOFILT_LEAF
{
    uint8_t   kind;            /**< en_IOFILT_LEAF_KIND */
    bool      negate;
    bool      port_is_8bit;
    bool      match_in;
    bool      match_out;
    bool      match_mr;
    bool      match_mw;
    uint32_t  rmin;
    uint32_t  rmax;
    char      name[ IOFILT_NAME_MAX ];
} st_IOFILT_LEAF;


/**
 * @brief Jeden AST uzel.
 *
 * Vlastní jen pointery do stejné arény, žádné externí heap alokace.
 *
 * @field kind   Druh uzlu (LEAF / NOT / AND / OR).
 * @field u      Union dle kind:
 *               - leaf: payload pro LEAF
 *               - child: pointer na potomka pro NOT
 *               - bin.l/r: pointery na potomky pro AND/OR
 */
typedef struct st_IOFILT_NODE
{
    uint8_t kind;             /**< en_IOFILT_NODE_KIND */
    union {
        st_IOFILT_LEAF leaf;
        struct st_IOFILT_NODE *child;             /* NOT */
        struct { struct st_IOFILT_NODE *l, *r; } bin;  /* AND / OR */
    } u;
} st_IOFILT_NODE;


/**
 * @brief Bump-allocator arena pro AST uzly.
 *
 * Jeden malloc bloku, bump alokace nodů, free jediným free(buf) v
 * io_history_filter_free(). Reset() jen vynuluje used a node_count
 * (žádný free).
 *
 * @field buf         Surový buffer (cap bytů).
 * @field used        Počet již alokovaných bytů (= bump offset).
 * @field cap         Celková kapacita.
 * @field node_count  Počet už alokovaných AST uzlů (LEAF/NOT/AND/OR).
 *                    Slouží k hard-cap kontrole proti patologickým
 *                    vstupům (viz IOFILT_MAX_NODES).
 *
 * Invariant: used <= cap, node_count <= IOFILT_MAX_NODES. Při
 * překročení limitu parser ohlásí chybu "Expression too complex".
 */
typedef struct st_IOFILT_ARENA
{
    uint8_t *buf;
    size_t   used;
    size_t   cap;
    size_t   node_count;
} st_IOFILT_ARENA;


/**
 * @brief Veřejná opaque struktura - drží AST root + arenu.
 *
 * @field root   Pointer na kořen AST (= ukazuje dovnitř arény).
 *               NULL = "match all" stav (po new() / reset() / parse() error).
 * @field arena  Bump arena pro uzly.
 */
struct st_IO_HISTORY_FILTER
{
    st_IOFILT_NODE  *root;
    st_IOFILT_ARENA  arena;
};


/**
 * @brief Defaultní velikost arény při io_history_filter_new().
 *
 * 4 KB pojme cca 70 leaf nodů (sizeof leaf ~80 B) nebo víc binárních uzlů
 * (sizeof bin uzel ~24 B). 128 B filter buffer omezuje reálný počet
 * tokenů na ~30, takže 4 KB je s rezervou.
 */
#define IOFILT_ARENA_DEFAULT_CAP 4096u


/**
 * @brief Maximální počet AST uzlů na jeden filter.
 *
 * Pevný limit proti patologickým vstupům (= hluboce vnořené závorky,
 * dlouhé řetězy AND/OR). Při překročení parser vrátí error
 * "Expression too complex (max 64 nodes)" a filter zůstane v reset
 * stavu. 64 je s velkou rezervou - typický real-world filter má
 * pod 10 uzlů, 128 B buffer fyzicky neumožní víc než ~30 leaf tokenů.
 */
#define IOFILT_MAX_NODES 64u


/* ========================================================================= */
/*  Arena allocator                                                          */
/* ========================================================================= */


/**
 * @brief Alokuje sz bytů z arény (8-byte aligned). Vrací NULL při OOM.
 *
 * Nemění node_count - inkrementaci nodů provádějí node konstruktory
 * (make_leaf / make_and / make_or / make_not), které kromě OOM
 * hlídají i IOFILT_MAX_NODES.
 */
static void *arena_alloc ( st_IOFILT_ARENA *a, size_t sz )
{
    if ( !a || !a->buf ) return NULL;
    /* Zarovnání na 8 B (= bezpečné pro všechny pole v st_IOFILT_NODE). */
    size_t aligned = ( a->used + 7u ) & ~(size_t) 7u;
    if ( aligned + sz > a->cap ) return NULL;
    void *p = a->buf + aligned;
    a->used = aligned + sz;
    return p;
}


/**
 * @brief Reset arény (bump offset zpět na 0, paměť zůstane alokovaná).
 *
 * Vynuluje i node_count - po reset jde znovu plně využít IOFILT_MAX_NODES.
 */
static void arena_reset ( st_IOFILT_ARENA *a )
{
    if ( !a ) return;
    a->used = 0;
    a->node_count = 0;
}


/* ========================================================================= */
/*  AST konstruktory                                                         */
/* ========================================================================= */


/**
 * @brief Vytvoří LEAF uzel v aréně. Volající dotvoří leaf payload.
 *
 * Hlídá IOFILT_MAX_NODES limit i OOM arény. Při překročení vrátí NULL
 * - caller rozliší zda hlásit "too complex" vs "arena OOM".
 *
 * @return Pointer na nový uzel s kind=LEAF a vynulovaným leaf payloadem;
 *         NULL při dosažení max-nodes limitu nebo OOM arény.
 */
static st_IOFILT_NODE *make_leaf ( st_IOFILT_ARENA *a )
{
    if ( !a || a->node_count >= IOFILT_MAX_NODES ) return NULL;
    st_IOFILT_NODE *n = (st_IOFILT_NODE *) arena_alloc ( a, sizeof ( *n ) );
    if ( !n ) return NULL;
    memset ( n, 0, sizeof ( *n ) );
    n->kind = IOFILT_NODE_LEAF;
    a->node_count++;
    return n;
}


/**
 * @brief Vytvoří binární AND uzel s daty levým a pravým podstromem.
 *
 * @return Nový uzel; NULL při OOM, max-nodes limitu nebo NULL operandu.
 */
static st_IOFILT_NODE *make_and ( st_IOFILT_ARENA *a,
                                   st_IOFILT_NODE *l, st_IOFILT_NODE *r )
{
    if ( !l || !r ) return NULL;
    if ( !a || a->node_count >= IOFILT_MAX_NODES ) return NULL;
    st_IOFILT_NODE *n = (st_IOFILT_NODE *) arena_alloc ( a, sizeof ( *n ) );
    if ( !n ) return NULL;
    memset ( n, 0, sizeof ( *n ) );
    n->kind = IOFILT_NODE_AND;
    n->u.bin.l = l;
    n->u.bin.r = r;
    a->node_count++;
    return n;
}


/**
 * @brief Vytvoří binární OR uzel s daným levým a pravým podstromem.
 *
 * Symetrický k make_and(); rozdíl je v matcher short-circuit logice
 * (OR vrací true při prvním true potomku).
 *
 * @return Nový uzel; NULL při OOM, max-nodes limitu nebo NULL operandu.
 */
static st_IOFILT_NODE *make_or ( st_IOFILT_ARENA *a,
                                  st_IOFILT_NODE *l, st_IOFILT_NODE *r )
{
    if ( !l || !r ) return NULL;
    if ( !a || a->node_count >= IOFILT_MAX_NODES ) return NULL;
    st_IOFILT_NODE *n = (st_IOFILT_NODE *) arena_alloc ( a, sizeof ( *n ) );
    if ( !n ) return NULL;
    memset ( n, 0, sizeof ( *n ) );
    n->kind = IOFILT_NODE_OR;
    n->u.bin.l = l;
    n->u.bin.r = r;
    a->node_count++;
    return n;
}


/**
 * @brief Vytvoří unární NOT uzel nad daným podstromem.
 *
 * Volá se z parse_unary() pro "!" před závorkovaným atomem nebo pro
 * dvojitou negaci nad NOT-paren stromem. NOT nad jednolistovým atomem
 * (= "!port:CE") se v parseru collapse-uje do LEAF s negate=true a
 * make_not() se v té cestě nevolá - viz parse_unary().
 *
 * @return Nový uzel; NULL při OOM, max-nodes limitu nebo NULL child.
 */
static st_IOFILT_NODE *make_not ( st_IOFILT_ARENA *a, st_IOFILT_NODE *child )
{
    if ( !child ) return NULL;
    if ( !a || a->node_count >= IOFILT_MAX_NODES ) return NULL;
    st_IOFILT_NODE *n = (st_IOFILT_NODE *) arena_alloc ( a, sizeof ( *n ) );
    if ( !n ) return NULL;
    memset ( n, 0, sizeof ( *n ) );
    n->kind = IOFILT_NODE_NOT;
    n->u.child = child;
    a->node_count++;
    return n;
}


/* ========================================================================= */
/*  Pomocné statické helpery (parser primitiva)                              */
/* ========================================================================= */


/**
 * @brief Case-insensitive substring search.
 */
static bool io_filter_str_contains_ci ( const char *haystack, const char *needle )
{
    if ( !needle || !*needle ) return true;
    if ( !haystack ) return false;
    size_t hl = strlen ( haystack );
    size_t nl = strlen ( needle );
    if ( nl > hl ) return false;
    for ( size_t i = 0; i + nl <= hl; i++ ) {
        size_t j = 0;
        for ( ; j < nl; j++ ) {
            char a = haystack[ i + j ];
            char b = needle[ j ];
            if ( a >= 'A' && a <= 'Z' ) a = (char) ( a - 'A' + 'a' );
            if ( b >= 'A' && b <= 'Z' ) b = (char) ( b - 'A' + 'a' );
            if ( a != b ) break;
        }
        if ( j == nl ) return true;
    }
    return false;
}


/**
 * @brief Parse hex literal (1-8 chars, case-insensitive). Vrací true při OK.
 */
static bool io_filter_parse_hex ( const char *s, size_t len, uint32_t *out )
{
    if ( len == 0 || len > 8 ) return false;
    uint32_t val = 0;
    for ( size_t i = 0; i < len; i++ ) {
        char c = s[ i ];
        uint32_t d;
        if ( c >= '0' && c <= '9' ) d = (uint32_t)( c - '0' );
        else if ( c >= 'a' && c <= 'f' ) d = (uint32_t)( c - 'a' + 10 );
        else if ( c >= 'A' && c <= 'F' ) d = (uint32_t)( c - 'A' + 10 );
        else return false;
        val = ( val << 4 ) | d;
    }
    *out = val;
    return true;
}


/**
 * @brief Parse decimal literal. Vrací true při OK.
 */
static bool io_filter_parse_dec ( const char *s, size_t len, uint32_t *out )
{
    if ( len == 0 ) return false;
    uint32_t val = 0;
    for ( size_t i = 0; i < len; i++ ) {
        char c = s[ i ];
        if ( c < '0' || c > '9' ) return false;
        val = val * 10u + (uint32_t)( c - '0' );
    }
    *out = val;
    return true;
}


/**
 * @brief Set err message (no-op pokud err je NULL nebo errsz==0).
 */
static void io_filter_set_err ( char *err, size_t errsz, const char *msg )
{
    if ( !err || errsz == 0 ) return;
    snprintf ( err, errsz, "%s", msg );
}


/* ========================================================================= */
/*  Per-prefix leaf builders                                                 */
/* ========================================================================= */


/**
 * @brief Parse "value:" hodnotu do LEAF payloadu (1-2 hex chars nebo range).
 *
 * Range "value:00-7F" povolen; hodnoty omezeny na 8-bit (0x00-0xFF).
 */
static bool leaf_fill_value ( const char *val, size_t vlen,
                                st_IOFILT_LEAF *leaf,
                                char *err, size_t errsz )
{
    const char *dash = NULL;
    for ( size_t i = 0; i < vlen; i++ ) {
        if ( val[ i ] == '-' ) { dash = &val[ i ]; break; }
    }
    leaf->kind = IOFILT_LEAF_VALUE;

    if ( dash ) {
        size_t lo_len = (size_t)( dash - val );
        size_t hi_len = vlen - lo_len - 1;
        uint32_t lo, hi;
        if ( !io_filter_parse_hex ( val, lo_len, &lo )
             || !io_filter_parse_hex ( dash + 1, hi_len, &hi )
             || lo > 0xFF || hi > 0xFF || lo > hi ) {
            io_filter_set_err ( err, errsz, "Invalid value range" );
            return false;
        }
        leaf->rmin = lo;
        leaf->rmax = hi;
        return true;
    }

    uint32_t v;
    if ( !io_filter_parse_hex ( val, vlen, &v ) || v > 0xFF ) {
        io_filter_set_err ( err, errsz, "Invalid value hex" );
        return false;
    }
    leaf->rmin = v;
    leaf->rmax = v;
    return true;
}


/**
 * @brief Parse "port:" / "port16:" hodnotu do LEAF payloadu.
 *
 * `is_8bit` rozlišuje semantiku 8-bit (low byte) vs 16-bit (full bus).
 */
static bool leaf_fill_port ( const char *val, size_t vlen,
                              st_IOFILT_LEAF *leaf, bool is_8bit,
                              char *err, size_t errsz )
{
    const uint32_t max_val = is_8bit ? 0xFFu : 0xFFFFu;
    const char *err_range  = is_8bit ? "Invalid port range (8-bit)"
                                      : "Invalid port16 range (16-bit)";
    const char *err_value  = is_8bit ? "Invalid port hex value (8-bit)"
                                      : "Invalid port16 hex value (16-bit)";

    const char *dash = NULL;
    for ( size_t i = 0; i < vlen; i++ ) {
        if ( val[ i ] == '-' ) { dash = &val[ i ]; break; }
    }
    leaf->kind = IOFILT_LEAF_PORT;
    leaf->port_is_8bit = is_8bit;

    if ( dash ) {
        size_t lo_len = (size_t)( dash - val );
        size_t hi_len = vlen - lo_len - 1;
        uint32_t lo, hi;
        if ( !io_filter_parse_hex ( val, lo_len, &lo )
             || !io_filter_parse_hex ( dash + 1, hi_len, &hi )
             || lo > max_val || hi > max_val || lo > hi ) {
            io_filter_set_err ( err, errsz, err_range );
            return false;
        }
        leaf->rmin = lo;
        leaf->rmax = hi;
        return true;
    }

    uint32_t v;
    if ( !io_filter_parse_hex ( val, vlen, &v ) || v > max_val ) {
        io_filter_set_err ( err, errsz, err_value );
        return false;
    }
    leaf->rmin = v;
    leaf->rmax = v;
    return true;
}


/**
 * @brief Parse "addr:" hodnotu do LEAF payloadu (16-bit MMIO addr).
 */
static bool leaf_fill_addr ( const char *val, size_t vlen,
                              st_IOFILT_LEAF *leaf,
                              char *err, size_t errsz )
{
    const char *dash = NULL;
    for ( size_t i = 0; i < vlen; i++ ) {
        if ( val[ i ] == '-' ) { dash = &val[ i ]; break; }
    }
    leaf->kind = IOFILT_LEAF_ADDR;

    if ( dash ) {
        size_t lo_len = (size_t)( dash - val );
        size_t hi_len = vlen - lo_len - 1;
        uint32_t lo, hi;
        if ( !io_filter_parse_hex ( val, lo_len, &lo )
             || !io_filter_parse_hex ( dash + 1, hi_len, &hi )
             || lo > 0xFFFF || hi > 0xFFFF || lo > hi ) {
            io_filter_set_err ( err, errsz, "Invalid addr range" );
            return false;
        }
        leaf->rmin = lo;
        leaf->rmax = hi;
        return true;
    }

    uint32_t v;
    if ( !io_filter_parse_hex ( val, vlen, &v ) || v > 0xFFFF ) {
        io_filter_set_err ( err, errsz, "Invalid addr hex value" );
        return false;
    }
    leaf->rmin = v;
    leaf->rmax = v;
    return true;
}


/**
 * @brief Parse "pc:" hodnotu do LEAF payloadu (16-bit PC).
 */
static bool leaf_fill_pc ( const char *val, size_t vlen,
                            st_IOFILT_LEAF *leaf,
                            char *err, size_t errsz )
{
    const char *dash = NULL;
    for ( size_t i = 0; i < vlen; i++ ) {
        if ( val[ i ] == '-' ) { dash = &val[ i ]; break; }
    }
    leaf->kind = IOFILT_LEAF_PC;

    if ( dash ) {
        size_t lo_len = (size_t)( dash - val );
        size_t hi_len = vlen - lo_len - 1;
        uint32_t lo, hi;
        if ( !io_filter_parse_hex ( val, lo_len, &lo )
             || !io_filter_parse_hex ( dash + 1, hi_len, &hi )
             || lo > 0xFFFF || hi > 0xFFFF || lo > hi ) {
            io_filter_set_err ( err, errsz, "Invalid pc range" );
            return false;
        }
        leaf->rmin = lo;
        leaf->rmax = hi;
        return true;
    }

    uint32_t v;
    if ( !io_filter_parse_hex ( val, vlen, &v ) || v > 0xFFFF ) {
        io_filter_set_err ( err, errsz, "Invalid pc hex value" );
        return false;
    }
    leaf->rmin = v;
    leaf->rmax = v;
    return true;
}


/**
 * @brief Parse decimal value s podporou >, <, range do LEAF payloadu.
 *
 * Sdílený helper pro "frame:" a "cycle:". Při X:>N nastaví rmin=N+1,
 * rmax=UINT32_MAX. Při X:<N nastaví rmin=0, rmax=N-1. Při range A-B
 * rmin=A, rmax=B. Při single hodnotě N rmin=rmax=N.
 */
static bool leaf_fill_decrange ( const char *val, size_t vlen,
                                  st_IOFILT_LEAF *leaf,
                                  char *err, size_t errsz,
                                  const char *what )
{
    char ebuf[ 64 ];
    if ( vlen == 0 ) {
        snprintf ( ebuf, sizeof ( ebuf ), "Empty %s value", what );
        io_filter_set_err ( err, errsz, ebuf );
        return false;
    }

    /* X:>N */
    if ( val[ 0 ] == '>' ) {
        uint32_t n;
        if ( !io_filter_parse_dec ( val + 1, vlen - 1, &n ) ) {
            snprintf ( ebuf, sizeof ( ebuf ), "Invalid %s:>N value", what );
            io_filter_set_err ( err, errsz, ebuf );
            return false;
        }
        leaf->rmin = ( n < 0xFFFFFFFFu ) ? n + 1u : 0xFFFFFFFFu;
        leaf->rmax = 0xFFFFFFFFu;
        return true;
    }

    /* X:<N */
    if ( val[ 0 ] == '<' ) {
        uint32_t n;
        if ( !io_filter_parse_dec ( val + 1, vlen - 1, &n ) ) {
            snprintf ( ebuf, sizeof ( ebuf ), "Invalid %s:<N value", what );
            io_filter_set_err ( err, errsz, ebuf );
            return false;
        }
        leaf->rmin = 0;
        leaf->rmax = ( n > 0 ) ? n - 1u : 0;
        return true;
    }

    /* range X:A-B */
    const char *dash = NULL;
    for ( size_t i = 0; i < vlen; i++ ) {
        if ( val[ i ] == '-' ) { dash = &val[ i ]; break; }
    }
    if ( dash ) {
        size_t lo_len = (size_t)( dash - val );
        size_t hi_len = vlen - lo_len - 1;
        uint32_t lo, hi;
        if ( !io_filter_parse_dec ( val, lo_len, &lo )
             || !io_filter_parse_dec ( dash + 1, hi_len, &hi )
             || lo > hi ) {
            snprintf ( ebuf, sizeof ( ebuf ), "Invalid %s range", what );
            io_filter_set_err ( err, errsz, ebuf );
            return false;
        }
        leaf->rmin = lo;
        leaf->rmax = hi;
        return true;
    }

    /* X:N (= equality) */
    uint32_t n;
    if ( !io_filter_parse_dec ( val, vlen, &n ) ) {
        snprintf ( ebuf, sizeof ( ebuf ), "Invalid %s value", what );
        io_filter_set_err ( err, errsz, ebuf );
        return false;
    }
    leaf->rmin = n;
    leaf->rmax = n;
    return true;
}


/**
 * @brief Parse "frame:" do LEAF (kind=FRAME).
 */
static bool leaf_fill_frame ( const char *val, size_t vlen,
                                st_IOFILT_LEAF *leaf,
                                char *err, size_t errsz )
{
    leaf->kind = IOFILT_LEAF_FRAME;
    return leaf_fill_decrange ( val, vlen, leaf, err, errsz, "frame" );
}


/**
 * @brief Parse "cycle:" do LEAF (kind=CYCLE).
 */
static bool leaf_fill_cycle ( const char *val, size_t vlen,
                                st_IOFILT_LEAF *leaf,
                                char *err, size_t errsz )
{
    leaf->kind = IOFILT_LEAF_CYCLE;
    return leaf_fill_decrange ( val, vlen, leaf, err, errsz, "cycle" );
}


/* ========================================================================= */
/*  Eval (matcher) - rekurzivní walk přes AST                                */
/* ========================================================================= */


/**
 * @brief Evaluace LEAF uzlu proti eventu.
 *
 * 9-cestný switch dle leaf->kind. Per-leaf negate flag invertuje raw match.
 */
static bool eval_leaf ( const st_IOFILT_LEAF *leaf,
                         const st_IO_HISTORY_EVENT *e,
                         const char *port_name )
{
    bool raw = false;

    switch ( (en_IOFILT_LEAF_KIND) leaf->kind ) {
        case IOFILT_LEAF_PORT: {
            uint16_t value = leaf->port_is_8bit
                             ? (uint16_t)( e->port & 0xFFu )
                             : e->port;
            raw = ( value >= leaf->rmin && value <= leaf->rmax );
            break;
        }
        case IOFILT_LEAF_PC:
            raw = ( e->pc >= leaf->rmin && e->pc <= leaf->rmax );
            break;
        case IOFILT_LEAF_VALUE:
            raw = ( e->value >= leaf->rmin && e->value <= leaf->rmax );
            break;
        case IOFILT_LEAF_FRAME:
            raw = ( e->frame >= leaf->rmin && e->frame <= leaf->rmax );
            break;
        case IOFILT_LEAF_CYCLE:
            raw = ( e->cpu_cycle >= leaf->rmin && e->cpu_cycle <= leaf->rmax );
            break;
        case IOFILT_LEAF_ADDR: {
            /* addr: matchuje JEN MMIO eventy v rozsahu. */
            bool ev_mem = ( ( e->flags & IO_HISTORY_FLAG_MEMORY ) != 0 );
            if ( ev_mem ) {
                raw = ( e->port >= leaf->rmin && e->port <= leaf->rmax );
            }
            break;
        }
        case IOFILT_LEAF_TYPE: {
            /* "in" = IN || MR, "out" = OUT || MW (V1.5.E sjednoceno). */
            bool ev_in = ( ( e->flags & IO_HISTORY_FLAG_READ ) != 0 );
            raw = ev_in ? leaf->match_in : leaf->match_out;
            break;
        }
        case IOFILT_LEAF_MEMTYPE: {
            /* mr/mw - matchuje JEN memory eventy. */
            bool ev_mem  = ( ( e->flags & IO_HISTORY_FLAG_MEMORY ) != 0 );
            bool ev_read = ( ( e->flags & IO_HISTORY_FLAG_READ ) != 0 );
            if ( ev_mem ) {
                raw = ev_read ? leaf->match_mr : leaf->match_mw;
            }
            break;
        }
        case IOFILT_LEAF_NAME: {
            /* case-insensitive substring proti port_name. NULL = no match. */
            if ( !port_name ) {
                raw = false;
            } else {
                raw = io_filter_str_contains_ci ( port_name, leaf->name );
            }
            break;
        }
    }

    return leaf->negate ? !raw : raw;
}


/**
 * @brief Rekurzivní eval AST se short-circuit AND/OR.
 *
 * @param n          Kořen podstromu (NULL = "match all" = true).
 * @param e          Event.
 * @param port_name  Port name pro NAME leaf (může být NULL).
 * @return true pokud event projde podstromem.
 */
static bool eval_node ( const st_IOFILT_NODE *n,
                         const st_IO_HISTORY_EVENT *e,
                         const char *port_name )
{
    if ( !n ) return true;
    switch ( (en_IOFILT_NODE_KIND) n->kind ) {
        case IOFILT_NODE_LEAF:
            return eval_leaf ( &n->u.leaf, e, port_name );
        case IOFILT_NODE_NOT:
            return !eval_node ( n->u.child, e, port_name );
        case IOFILT_NODE_AND: {
            if ( !eval_node ( n->u.bin.l, e, port_name ) ) return false;
            return eval_node ( n->u.bin.r, e, port_name );
        }
        case IOFILT_NODE_OR: {
            if ( eval_node ( n->u.bin.l, e, port_name ) ) return true;
            return eval_node ( n->u.bin.r, e, port_name );
        }
    }
    return false;
}


/* ========================================================================= */
/*  Parser - token-level + or_expr / and_expr (Fáze 2: + OR bez závorek)     */
/* ========================================================================= */


/**
 * @brief Pomocná detekce zda znak ukončuje leaf token.
 *
 * Leaf token (= contiguous run vstupního streamu) končí na whitespace,
 * operátorech "|" / "&" / "!", závorkách "(" / ")" nebo EOF (F3).
 *
 * Pozn.: "!" je v F3 unární operátor (= "!port:CE" tokenizuje jako
 * "!" + "port:CE"). parse_token_to_leaf si pro zpětnou kompatibilitu
 * udržuje větev pro leading "!" v tokenu - tato cesta je nyní mrtvá
 * (= scan_leaf_token "!" nikdy nezahrne), ale ponechána pro bezpečnost.
 */
static bool is_token_stop ( char c )
{
    if ( c == '\0' ) return true;
    if ( c == '|' ) return true;
    if ( c == '&' ) return true;
    if ( c == '!' ) return true;
    if ( c == '(' ) return true;
    if ( c == ')' ) return true;
    if ( isspace ( (unsigned char) c ) ) return true;
    return false;
}


/**
 * @brief Posune kurzor přes whitespace.
 */
static void skip_ws ( const char **pp )
{
    const char *p = *pp;
    while ( *p && isspace ( (unsigned char) *p ) ) p++;
    *pp = p;
}


/**
 * @brief Extrahuje další contiguous leaf token z kurzoru.
 *
 * Předpokládá že caller už přeskočil WS. Token sahá od *pp po první
 * is_token_stop znak. Kurzor *pp se posune za poslední znak tokenu.
 *
 * @param pp     Pointer na kurzor (in/out).
 * @param tok    Out: pointer na začátek tokenu (= **pp v okamžiku volání).
 * @param tlen   Out: délka tokenu (může být 0 pokud kurzor stojí na stop chr).
 */
static void scan_leaf_token ( const char **pp, const char **tok, size_t *tlen )
{
    const char *p = *pp;
    const char *start = p;
    while ( *p && !is_token_stop ( *p ) ) p++;
    *tok  = start;
    *tlen = (size_t)( p - start );
    *pp   = p;
}


/**
 * @brief Porovnání tokenu s reserved keywordem (case-sensitive).
 *
 * V F2 jsou keywords "OR" a "AND" rezervované jen v uppercase tvaru -
 * lowercase "or"/"and"/"Or"/"And" zůstávají platné plain-text leaf
 * tokeny (= name match). Tato volba zachová zpětnou kompatibilitu pro
 * uživatele kteří mají v port name třeba "OR-gate" zapsané malými.
 */
static bool token_eq ( const char *tok, size_t tlen, const char *kw )
{
    size_t kl = strlen ( kw );
    if ( tlen != kl ) return false;
    for ( size_t i = 0; i < kl; i++ ) {
        if ( tok[ i ] != kw[ i ] ) return false;
    }
    return true;
}


/**
 * @brief Postaví LEAF uzel z jednoho atomického tokenu.
 *
 * Token: optionally prefixovaný '!' (per-token negate), pak buď:
 *   - "prefix:value" (port, port16, pc, frame, value, cycle, addr)
 *   - bare keyword (in, out, mr, mw)
 *   - plain text (name substring)
 *
 * Při chybě nastaví err a vrátí NULL. Při OOM arény také NULL + err.
 *
 * @param a          Arena pro alokaci nodu.
 * @param tok        Pointer na začátek tokenu.
 * @param tlen       Délka tokenu.
 * @param err/errsz  Error buffer.
 * @return Nový LEAF node nebo NULL.
 */
static st_IOFILT_NODE *parse_token_to_leaf ( st_IOFILT_ARENA *a,
                                              const char *tok, size_t tlen,
                                              char *err, size_t errsz )
{
    /* V F3 je '!' samostatný stop-char, scan_leaf_token ho do tlen
     * nevrátí. Větev níže je proto v praxi mrtvá, ale zachována pro
     * bezpečnost (kdyby caller obešel scan_leaf_token). */
    bool negate_this = false;
    if ( tlen > 0 && tok[ 0 ] == '!' ) {
        negate_this = true;
        tok++;
        tlen--;
        if ( tlen == 0 ) {
            io_filter_set_err ( err, errsz, "Empty token after '!'" );
            return NULL;
        }
    }

    st_IOFILT_NODE *node = make_leaf ( a );
    if ( !node ) {
        io_filter_set_err ( err, errsz,
                             "Expression too complex (max 64 nodes)" );
        return NULL;
    }
    st_IOFILT_LEAF *leaf = &node->u.leaf;
    leaf->negate = negate_this;

    /* Detect prefix:value format. */
    const char *colon = NULL;
    for ( size_t i = 0; i < tlen; i++ ) {
        if ( tok[ i ] == ':' ) { colon = &tok[ i ]; break; }
    }

    if ( colon ) {
        size_t plen = (size_t)( colon - tok );
        const char *val = colon + 1;
        size_t vlen = tlen - plen - 1;

        if ( vlen == 0 ) {
            io_filter_set_err ( err, errsz, "Empty value after prefix" );
            return NULL;
        }

        if ( plen == 4 && strncmp ( tok, "port", 4 ) == 0 ) {
            if ( !leaf_fill_port ( val, vlen, leaf, true, err, errsz ) ) return NULL;
            return node;
        }
        if ( plen == 6 && strncmp ( tok, "port16", 6 ) == 0 ) {
            if ( !leaf_fill_port ( val, vlen, leaf, false, err, errsz ) ) return NULL;
            return node;
        }
        if ( plen == 2 && strncmp ( tok, "pc", 2 ) == 0 ) {
            if ( !leaf_fill_pc ( val, vlen, leaf, err, errsz ) ) return NULL;
            return node;
        }
        if ( plen == 5 && strncmp ( tok, "frame", 5 ) == 0 ) {
            if ( !leaf_fill_frame ( val, vlen, leaf, err, errsz ) ) return NULL;
            return node;
        }
        if ( plen == 5 && strncmp ( tok, "value", 5 ) == 0 ) {
            if ( !leaf_fill_value ( val, vlen, leaf, err, errsz ) ) return NULL;
            return node;
        }
        if ( plen == 5 && strncmp ( tok, "cycle", 5 ) == 0 ) {
            if ( !leaf_fill_cycle ( val, vlen, leaf, err, errsz ) ) return NULL;
            return node;
        }
        if ( plen == 4 && strncmp ( tok, "addr", 4 ) == 0 ) {
            if ( !leaf_fill_addr ( val, vlen, leaf, err, errsz ) ) return NULL;
            return node;
        }

        io_filter_set_err ( err, errsz, "Unknown filter prefix" );
        return NULL;
    }

    /* Bare keyword "in" / "out" / "mr" / "mw" (case-insensitive). */
    if ( tlen == 2 && ( tok[ 0 ] == 'i' || tok[ 0 ] == 'I' )
         && ( tok[ 1 ] == 'n' || tok[ 1 ] == 'N' ) ) {
        leaf->kind = IOFILT_LEAF_TYPE;
        leaf->match_in = true;
        return node;
    }
    if ( tlen == 3 && ( tok[ 0 ] == 'o' || tok[ 0 ] == 'O' )
         && ( tok[ 1 ] == 'u' || tok[ 1 ] == 'U' )
         && ( tok[ 2 ] == 't' || tok[ 2 ] == 'T' ) ) {
        leaf->kind = IOFILT_LEAF_TYPE;
        leaf->match_out = true;
        return node;
    }
    if ( tlen == 2 && ( tok[ 0 ] == 'm' || tok[ 0 ] == 'M' )
         && ( tok[ 1 ] == 'r' || tok[ 1 ] == 'R' ) ) {
        leaf->kind = IOFILT_LEAF_MEMTYPE;
        leaf->match_mr = true;
        return node;
    }
    if ( tlen == 2 && ( tok[ 0 ] == 'm' || tok[ 0 ] == 'M' )
         && ( tok[ 1 ] == 'w' || tok[ 1 ] == 'W' ) ) {
        leaf->kind = IOFILT_LEAF_MEMTYPE;
        leaf->match_mw = true;
        return node;
    }

    /* Plain text → NAME leaf. */
    leaf->kind = IOFILT_LEAF_NAME;
    if ( tlen >= IOFILT_NAME_MAX ) {
        io_filter_set_err ( err, errsz, "Name token too long" );
        return NULL;
    }
    memcpy ( leaf->name, tok, tlen );
    leaf->name[ tlen ] = '\0';
    return node;
}


/* ========================================================================= */
/*  Parser - recursive-descent or_expr / and_expr (F2)                       */
/* ========================================================================= */


/**
 * @brief Pomocný typ pro lookahead - co následuje za WS na kurzoru.
 *
 * Volá se uvnitř parse_and_expr a parse_or_expr k rozhodnutí, jestli
 * pokračovat v sekvenci nebo vrátit hotový podstrom.
 *
 * - PEEK_END: kurzor je na konci řetězce.
 * - PEEK_PIPE: kurzor je na "|" (= konec and_expr, OR vlevo).
 * - PEEK_OR_KW: kurzor je na bare tokenu "OR" (uppercase).
 * - PEEK_AMP: kurzor je na "&" (= explicit AND uvnitř and_expr).
 * - PEEK_AND_KW: kurzor je na bare tokenu "AND" (uppercase).
 * - PEEK_LPAREN: kurzor je na "(" (F3, primary atom branch).
 * - PEEK_RPAREN: kurzor je na ")" (F3, ukončuje vnořený expr).
 * - PEEK_BANG: kurzor je na "!" (F3, unární operátor).
 * - PEEK_LEAF: kurzor je na čemkoliv ostatním (= leaf token start).
 */
typedef enum
{
    PEEK_END     = 0,
    PEEK_PIPE    = 1,
    PEEK_OR_KW   = 2,
    PEEK_AMP     = 3,
    PEEK_AND_KW  = 4,
    PEEK_LPAREN  = 5,
    PEEK_RPAREN  = 6,
    PEEK_BANG    = 7,
    PEEK_LEAF    = 8
} en_PEEK;


/**
 * @brief Lookahead přes WS - vrátí druh nadcházejícího tokenu / operátoru.
 *
 * Kurzor se NEpohne (= caller si přeskočí WS sám pomocí skip_ws před
 * konzumací). Bare-token keywordy "OR" / "AND" se rozpoznají jen v
 * uppercase tvaru a jen jako contiguous run mezi WS / operátory /
 * EOF (= "ORx" se peeknu jako PEEK_LEAF).
 */
static en_PEEK peek_next ( const char *p )
{
    while ( *p && isspace ( (unsigned char) *p ) ) p++;
    if ( *p == '\0' ) return PEEK_END;
    if ( *p == '|' )  return PEEK_PIPE;
    if ( *p == '&' )  return PEEK_AMP;
    if ( *p == '(' )  return PEEK_LPAREN;
    if ( *p == ')' )  return PEEK_RPAREN;
    if ( *p == '!' )  return PEEK_BANG;

    /* Test na "OR" nebo "AND" bare token (uppercase, oddělený WS / |
     * & / EOF). Test je ostře case-sensitive - lowercase "or" / "and"
     * propadne na PEEK_LEAF a stane se z něj name match. */
    const char *t = p;
    while ( *t && !is_token_stop ( *t ) ) t++;
    size_t tlen = (size_t)( t - p );
    if ( tlen == 2 && p[ 0 ] == 'O' && p[ 1 ] == 'R' )  return PEEK_OR_KW;
    if ( tlen == 3 && p[ 0 ] == 'A' && p[ 1 ] == 'N'
         && p[ 2 ] == 'D' ) return PEEK_AND_KW;

    return PEEK_LEAF;
}


/**
 * @brief Posune kurzor přes WS a následující "OR" / "AND" keyword bare token.
 *
 * Caller už zavolal peek_next a dostal PEEK_OR_KW / PEEK_AND_KW. Tato
 * funkce konzumuje WS a samotný keyword (2 nebo 3 znaky).
 */
static void consume_kw ( const char **pp, size_t kwlen )
{
    skip_ws ( pp );
    *pp += kwlen;
}


/* Forward declarations kvůli vzájemnému volání recursive-descent vrstev. */
static st_IOFILT_NODE *parse_or_expr ( st_IOFILT_ARENA *a, const char **pp,
                                        char *err, size_t errsz );
static st_IOFILT_NODE *parse_unary ( st_IOFILT_ARENA *a, const char **pp,
                                      char *err, size_t errsz );


/**
 * @brief Pomocný překlad chybového kódu PEEK na human-readable jméno.
 *
 * Používá se pro generování konzistentních error messages typu
 * "Empty expression before 'X'".
 */
static const char *peek_name ( en_PEEK k )
{
    switch ( k ) {
        case PEEK_PIPE:   return "'|'";
        case PEEK_AMP:    return "'&'";
        case PEEK_OR_KW:  return "'OR'";
        case PEEK_AND_KW: return "'AND'";
        case PEEK_LPAREN: return "'('";
        case PEEK_RPAREN: return "')'";
        case PEEK_BANG:   return "'!'";
        default:          return "operator";
    }
}


/**
 * @brief Parse jednoho leaf tokenu z kurzoru (= scan + parse_token_to_leaf).
 *
 * Předpokládá že peek_next() vrátil PEEK_LEAF. Při empty extrahování
 * (= caller nezavolal peek) vrátí chybu.
 */
static st_IOFILT_NODE *parse_leaf ( st_IOFILT_ARENA *a, const char **pp,
                                     char *err, size_t errsz )
{
    skip_ws ( pp );
    const char *tok;
    size_t tlen;
    scan_leaf_token ( pp, &tok, &tlen );
    if ( tlen == 0 ) {
        io_filter_set_err ( err, errsz, "Expected expression, got operator" );
        return NULL;
    }
    st_IOFILT_NODE *n = parse_token_to_leaf ( a, tok, tlen, err, errsz );
    if ( !n ) {
        /* Pokud parse_token_to_leaf vrátil NULL bez nastavení err, byl
         * to max-nodes / arena OOM v make_leaf. Doplníme generický error
         * pokud err buffer není nastaven. */
        if ( err && errsz > 0 && err[ 0 ] == '\0' ) {
            io_filter_set_err ( err, errsz,
                                 "Expression too complex (max 64 nodes)" );
        }
    }
    return n;
}


/**
 * @brief Parse atom = "(" expr ")" | leaf_token.
 *
 * Primární jednotka výrazu: závorkovaný podvýraz (= passthrough,
 * žádný extra AST uzel) nebo jeden leaf token. Empty "()" je parse
 * error. Mismatched "(" bez ")" je parse error.
 *
 * @return Kořen subtree nebo NULL při chybě.
 */
static st_IOFILT_NODE *parse_atom ( st_IOFILT_ARENA *a, const char **pp,
                                     char *err, size_t errsz )
{
    en_PEEK k = peek_next ( *pp );

    if ( k == PEEK_LPAREN ) {
        skip_ws ( pp );
        (*pp)++;                    /* '(' */

        /* Prázdné závorky jsou explicit chyba. */
        en_PEEK inner = peek_next ( *pp );
        if ( inner == PEEK_RPAREN ) {
            io_filter_set_err ( err, errsz, "Empty expression in parens" );
            return NULL;
        }
        if ( inner == PEEK_END ) {
            io_filter_set_err ( err, errsz, "Expected ')'" );
            return NULL;
        }

        st_IOFILT_NODE *inside = parse_or_expr ( a, pp, err, errsz );
        if ( !inside ) return NULL;

        en_PEEK after = peek_next ( *pp );
        if ( after != PEEK_RPAREN ) {
            io_filter_set_err ( err, errsz, "Expected ')'" );
            return NULL;
        }
        skip_ws ( pp );
        (*pp)++;                    /* ')' */
        return inside;              /* passthrough */
    }

    if ( k == PEEK_LEAF ) {
        return parse_leaf ( a, pp, err, errsz );
    }

    /* Vše ostatní (operator, RPAREN, END) je syntax chyba. */
    if ( k == PEEK_END ) {
        io_filter_set_err ( err, errsz, "Empty expression" );
    } else if ( k == PEEK_RPAREN ) {
        io_filter_set_err ( err, errsz, "Unexpected ')'" );
    } else {
        char buf[ 64 ];
        snprintf ( buf, sizeof ( buf ), "Empty expression before %s",
                    peek_name ( k ) );
        io_filter_set_err ( err, errsz, buf );
    }
    return NULL;
}


/**
 * @brief Parse unary = "!" unary | atom.
 *
 * Unární NOT před atomem. Pokud child je LEAF, NOT se collapse-uje
 * do LEAF s flipnutým negate flagem - to udržuje bit-exact zpětnou
 * kompatibilitu s F1/F2 per-token "!" prefixem. Pokud child je
 * NOT/AND/OR uzel (= "!" před závorkovaným výrazem nebo přes další
 * NOT), parser postaví NOT uzel přes child.
 *
 * Dvojitá negace (= "!!") se collapse-uje sériově: vnitřní "!" buď
 * collapsne do LEAF s negate=true (pokud child je LEAF), nebo vznikne
 * NOT(child). Vnější "!" pak buď flipne LEAF negate zpět na false,
 * nebo postaví NOT(NOT(...)). Sémanticky obě varianty == identity,
 * ale druhá použije 2 NOT uzly navíc.
 *
 * @return Kořen subtree nebo NULL při chybě.
 */
static st_IOFILT_NODE *parse_unary ( st_IOFILT_ARENA *a, const char **pp,
                                      char *err, size_t errsz )
{
    en_PEEK k = peek_next ( *pp );
    if ( k == PEEK_BANG ) {
        skip_ws ( pp );
        (*pp)++;                    /* '!' */

        en_PEEK after = peek_next ( *pp );
        if ( after == PEEK_END ) {
            io_filter_set_err ( err, errsz, "Empty token after '!'" );
            return NULL;
        }
        if ( after != PEEK_LEAF && after != PEEK_LPAREN
             && after != PEEK_BANG ) {
            io_filter_set_err ( err, errsz, "Empty token after '!'" );
            return NULL;
        }

        st_IOFILT_NODE *child = parse_unary ( a, pp, err, errsz );
        if ( !child ) return NULL;

        /* Collapse NOT-LEAF do LEAF s flipnutým negate (F1/F2 compat). */
        if ( child->kind == IOFILT_NODE_LEAF ) {
            child->u.leaf.negate = !child->u.leaf.negate;
            return child;
        }

        /* Jinak postavit explicit NOT uzel. */
        st_IOFILT_NODE *n = make_not ( a, child );
        if ( !n ) {
            io_filter_set_err ( err, errsz,
                                 "Expression too complex (max 64 nodes)" );
            return NULL;
        }
        return n;
    }

    return parse_atom ( a, pp, err, errsz );
}


/**
 * @brief Parse and_expr = unary ( ( "&" | "AND" | implicit-WS ) unary )*.
 *
 * Levo-asociativní řetězení atomů přes AND uzly. Operátor "|" / "OR"
 * znamená "konec and_expr" - vrátíme caller. ")" a EOF znamená totéž.
 *
 * Implicit AND (= WS separator) se rozpozná tak, že po prvním unary
 * peek_next() vrátí PEEK_LEAF / PEEK_LPAREN / PEEK_BANG. Explicit AND
 * (= "&" / "AND") se konzumuje samostatně.
 *
 * Při syntax chybě vrátí NULL a nastaví err.
 */
static st_IOFILT_NODE *parse_and_expr ( st_IOFILT_ARENA *a, const char **pp,
                                         char *err, size_t errsz )
{
    /* První operand. */
    en_PEEK k = peek_next ( *pp );
    if ( k != PEEK_LEAF && k != PEEK_LPAREN && k != PEEK_BANG ) {
        /* Empty and_expr - např. začátek vstupu byl operátor. */
        if ( k == PEEK_PIPE ) {
            io_filter_set_err ( err, errsz, "Empty expression before '|'" );
        } else if ( k == PEEK_AMP ) {
            io_filter_set_err ( err, errsz, "Empty expression before '&'" );
        } else if ( k == PEEK_OR_KW ) {
            io_filter_set_err ( err, errsz, "Empty expression before 'OR'" );
        } else if ( k == PEEK_AND_KW ) {
            io_filter_set_err ( err, errsz, "Empty expression before 'AND'" );
        } else if ( k == PEEK_RPAREN ) {
            io_filter_set_err ( err, errsz, "Unexpected ')'" );
        } else {
            io_filter_set_err ( err, errsz, "Empty expression" );
        }
        return NULL;
    }

    st_IOFILT_NODE *lhs = parse_unary ( a, pp, err, errsz );
    if ( !lhs ) return NULL;

    for ( ;; ) {
        en_PEEK nxt = peek_next ( *pp );

        /* Konec and_expr: EOF, OR-class, RPAREN. */
        if ( nxt == PEEK_END || nxt == PEEK_PIPE || nxt == PEEK_OR_KW
             || nxt == PEEK_RPAREN ) {
            return lhs;
        }

        /* Explicit AND keyword/symbol - konzumuj separator. */
        if ( nxt == PEEK_AMP ) {
            skip_ws ( pp );
            (*pp)++;                /* přeskočit '&' */
        } else if ( nxt == PEEK_AND_KW ) {
            consume_kw ( pp, 3 );   /* "AND" */
        }
        /* PEEK_LEAF / PEEK_LPAREN / PEEK_BANG = implicit AND, nic
         * nekonzumujeme - rovnou pokračujeme dalším unary. */

        en_PEEK after = peek_next ( *pp );
        if ( after != PEEK_LEAF && after != PEEK_LPAREN
             && after != PEEK_BANG ) {
            if ( after == PEEK_PIPE || after == PEEK_OR_KW ) {
                io_filter_set_err ( err, errsz,
                                     "Empty expression between AND and 'OR'/'|'" );
            } else if ( after == PEEK_END ) {
                io_filter_set_err ( err, errsz, "Trailing AND operator" );
            } else if ( after == PEEK_RPAREN ) {
                io_filter_set_err ( err, errsz, "Empty expression before ')'" );
            } else {
                io_filter_set_err ( err, errsz,
                                     "Unexpected operator after AND" );
            }
            return NULL;
        }

        st_IOFILT_NODE *rhs = parse_unary ( a, pp, err, errsz );
        if ( !rhs ) return NULL;

        st_IOFILT_NODE *combined = make_and ( a, lhs, rhs );
        if ( !combined ) {
            io_filter_set_err ( err, errsz,
                                 "Expression too complex (max 64 nodes)" );
            return NULL;
        }
        lhs = combined;
    }
}


/**
 * @brief Parse or_expr = and_expr ( ( "|" | "OR" ) and_expr )*.
 *
 * Levo-asociativní řetězení and_expr přes OR uzly.
 *
 * Při syntax chybě vrátí NULL a nastaví err.
 */
static st_IOFILT_NODE *parse_or_expr ( st_IOFILT_ARENA *a, const char **pp,
                                        char *err, size_t errsz )
{
    st_IOFILT_NODE *lhs = parse_and_expr ( a, pp, err, errsz );
    if ( !lhs ) return NULL;

    for ( ;; ) {
        en_PEEK nxt = peek_next ( *pp );

        if ( nxt == PEEK_END || nxt == PEEK_RPAREN ) return lhs;

        if ( nxt == PEEK_PIPE ) {
            skip_ws ( pp );
            (*pp)++;                /* '|' */
        } else if ( nxt == PEEK_OR_KW ) {
            consume_kw ( pp, 2 );   /* "OR" */
        } else {
            /* Cokoli jiného by mělo skonzumovat parse_and_expr; pokud
             * zde stojíme, je něco nekonzistentního - syntax error
             * pojistka. */
            io_filter_set_err ( err, errsz, "Unexpected token" );
            return NULL;
        }

        /* Po '|' / OR musí následovat další and_expr. */
        en_PEEK after = peek_next ( *pp );
        if ( after == PEEK_END ) {
            io_filter_set_err ( err, errsz, "Empty expression after '|'" );
            return NULL;
        }
        if ( after == PEEK_PIPE || after == PEEK_OR_KW ) {
            io_filter_set_err ( err, errsz, "Empty expression between two 'OR'" );
            return NULL;
        }
        if ( after == PEEK_RPAREN ) {
            io_filter_set_err ( err, errsz, "Empty expression before ')'" );
            return NULL;
        }

        st_IOFILT_NODE *rhs = parse_and_expr ( a, pp, err, errsz );
        if ( !rhs ) return NULL;

        st_IOFILT_NODE *combined = make_or ( a, lhs, rhs );
        if ( !combined ) {
            io_filter_set_err ( err, errsz,
                                 "Expression too complex (max 64 nodes)" );
            return NULL;
        }
        lhs = combined;
    }
}


/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */


st_IO_HISTORY_FILTER *io_history_filter_new ( void )
{
    st_IO_HISTORY_FILTER *f = (st_IO_HISTORY_FILTER *)
                              malloc ( sizeof ( *f ) );
    if ( !f ) return NULL;
    f->root = NULL;
    f->arena.buf = (uint8_t *) malloc ( IOFILT_ARENA_DEFAULT_CAP );
    if ( !f->arena.buf ) {
        free ( f );
        return NULL;
    }
    f->arena.used = 0;
    f->arena.cap  = IOFILT_ARENA_DEFAULT_CAP;
    f->arena.node_count = 0;
    return f;
}


void io_history_filter_free ( st_IO_HISTORY_FILTER *f )
{
    if ( !f ) return;
    free ( f->arena.buf );
    free ( f );
}


void io_history_filter_reset ( st_IO_HISTORY_FILTER *f )
{
    if ( !f ) return;
    f->root = NULL;
    arena_reset ( &f->arena );
}


bool io_history_filter_parse ( const char *src,
                                st_IO_HISTORY_FILTER *out,
                                char *err, size_t errsz )
{
    if ( !out ) return false;
    io_history_filter_reset ( out );
    if ( err && errsz > 0 ) err[ 0 ] = '\0';

    if ( !src || !src[ 0 ] ) return true;  /* empty = match all */

    /* Empty / whitespace-only vstup = "match all" (= ponechat root NULL). */
    const char *p = src;
    skip_ws ( &p );
    if ( *p == '\0' ) return true;

    /* Recursive-descent: or_expr → and_expr → leaf. */
    st_IOFILT_NODE *root = parse_or_expr ( &out->arena, &p, err, errsz );
    if ( !root ) {
        /* Při chybě uvedeme filter do reset stavu (= match all). */
        io_history_filter_reset ( out );
        return false;
    }

    /* Po hotovém or_expr nesmí zbýt nic kromě WS. F3 dovolí "(" ")"
     * v gramatice, takže ")" na top-level znamená nepárovou závorku. */
    skip_ws ( &p );
    if ( *p != '\0' ) {
        if ( *p == ')' ) {
            io_filter_set_err ( err, errsz, "Unexpected ')'" );
        } else {
            io_filter_set_err ( err, errsz, "Unexpected trailing token" );
        }
        io_history_filter_reset ( out );
        return false;
    }

    out->root = root;
    return true;
}


bool io_history_filter_match ( const st_IO_HISTORY_FILTER *f,
                                const st_IO_HISTORY_EVENT *e,
                                const char *port_name )
{
    if ( !f || !e ) return false;
    return eval_node ( f->root, e, port_name );
}


/**
 * @brief Pomocný rekurzivní walk - hledá NAME leaf v AST.
 */
static bool node_has_name_leaf ( const st_IOFILT_NODE *n )
{
    if ( !n ) return false;
    switch ( (en_IOFILT_NODE_KIND) n->kind ) {
        case IOFILT_NODE_LEAF:
            return ( (en_IOFILT_LEAF_KIND) n->u.leaf.kind ) == IOFILT_LEAF_NAME;
        case IOFILT_NODE_NOT:
            return node_has_name_leaf ( n->u.child );
        case IOFILT_NODE_AND:
        case IOFILT_NODE_OR:
            return node_has_name_leaf ( n->u.bin.l )
                || node_has_name_leaf ( n->u.bin.r );
    }
    return false;
}


bool io_history_filter_uses_name ( const st_IO_HISTORY_FILTER *f )
{
    if ( !f ) return false;
    return node_has_name_leaf ( f->root );
}


/* ========================================================================= */
/*  String quick-action helpery (V1.7+ položka 5.1 - row context menu)       */
/* ========================================================================= */


bool io_history_filter_string_group_wrap ( char *buf, size_t bufsz,
                                            en_IOFILT_GROUP_OP op,
                                            const char *token )
{
    if ( !buf || bufsz == 0 ) return false;
    if ( !token || token[ 0 ] == '\0' ) return false;
    if ( op != IOFILT_GROUP_AND && op != IOFILT_GROUP_OR ) return false;

    /* Pojistka: buf musí být null-terminated v rámci bufsz. */
    size_t cur_len = 0;
    while ( cur_len < bufsz && buf[ cur_len ] != '\0' ) cur_len++;
    if ( cur_len >= bufsz ) return false;     /* malformed input */

    /* Empty filter: jen bare token (= sémantika "match all AND X" == X). */
    if ( cur_len == 0 ) {
        size_t tlen = strlen ( token );
        if ( tlen + 1 > bufsz ) return false;  /* overflow */
        memcpy ( buf, token, tlen );
        buf[ tlen ] = '\0';
        return true;
    }

    const char *sep = ( op == IOFILT_GROUP_AND ) ? " & " : " | ";

    /* Spočítej délku výsledku BEZ skutečného zapsání:
     *   "(" + buf + ")" + sep + token + '\0'
     * = 1 + cur_len + 1 + strlen(sep) + strlen(token) + 1 */
    size_t tlen   = strlen ( token );
    size_t seplen = strlen ( sep );
    size_t need   = 1u + cur_len + 1u + seplen + tlen + 1u;
    if ( need > bufsz ) return false;          /* overflow, buf neměníme */

    /* In-place sestavení s posunem starého obsahu doprava o 1 byte
     * (kvůli leading '('). Pracujeme zezadu dopředu, ale jednodušší a
     * bezpečnější je memmove + přepsání headeru. */
    memmove ( buf + 1, buf, cur_len );         /* "X" -> " X" (posun) */
    buf[ 0 ] = '(';
    buf[ 1 + cur_len ] = ')';
    /* Za ")" patří separátor a token. */
    memcpy ( buf + 1 + cur_len + 1, sep, seplen );
    memcpy ( buf + 1 + cur_len + 1 + seplen, token, tlen );
    buf[ 1 + cur_len + 1 + seplen + tlen ] = '\0';
    return true;
}
