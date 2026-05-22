/*
 * io_history_filter.h - Parser a evaluátor filter syntaxe pro History tab.
 *
 * Filter syntax (V1.5 boolean-expression refactor, fáze F1-F5):
 * Datový model je AST (Abstract Syntax Tree) z LEAF/NOT/AND/OR uzlů
 * alokovaných v aréně (= jeden malloc bloku, bump alokace). Veřejně
 * je struktura @ref st_IO_HISTORY_FILTER **opaque** - callsity musí
 * používat io_history_filter_new() / _free().
 *
 * Gramatika (BNF):
 *
 *   expr     = or_expr
 *   or_expr  = and_expr ( ( "|" | "OR" ) and_expr )*
 *   and_expr = unary    ( ( "&" | "AND" | WS ) unary )*
 *   unary    = "!" unary | atom
 *   atom     = "(" expr ")" | leaf_token
 *
 * Operátory v sestupné precedenci:
 *   1. "(" ")"          grouping (nejvyšší)
 *   2. "!"              unární NOT (nad leaf nebo nad závorkou)
 *   3. WS / "&" / "AND" implicit nebo explicit AND
 *   4. "|" / "OR"       OR (nejnižší)
 *
 * Case-sensitivita keywordů:
 *   - "AND" a "OR" jsou rezervovaná slova **jen** ve velkých písmenech.
 *     Lowercase "and" / "or" (i mixed-case "And", "oR") se interpretuje
 *     jako plain-text name match - backward compat pro porty s "OR"
 *     ve jméně.
 *   - Symbolové varianty "&" a "|" jsou samostatné znaky bez
 *     case-sensitivity.
 *
 * Zpětná kompatibilita (F1-F5):
 *   Veškerá existing syntax z verzí před F1 zůstává validní:
 *   - Mezera = implicit AND ("port:CE pc:4042" == "port:CE & pc:4042"
 *     == "port:CE AND pc:4042").
 *   - Per-token "!" prefix ("!port:CE") se interně collapse-uje do
 *     LEAF uzlu s negate=true flagem (bit-exact shoda s F1/F2 chováním).
 *
 * NOT sémantika:
 *   NOT invertuje boolean výsledek celého podstromu, ne per-leaf
 *   relevance. Pro non-MMIO event a NOT nad ADDR/MR/MW LEAF to znamená,
 *   že raw match je false → NOT flipne na true (= event "není v MMIO
 *   range", tj. projde). Pokud chce uživatel filter "jen MMIO mimo
 *   0xE008", musí explicitně zúžit: "!addr:E008 & (mr | mw)".
 *
 * Příklady:
 *   "port:CE | port:CF"                = port==0xCE OR port==0xCF
 *   "port:CE pc:42 | port:CF"          = (port==0xCE AND pc==0x42) OR port==0xCF
 *   "(port:CE | port:CF) pc:42"        = (CE OR CF) AND pc==0x42
 *   "!(port:CE pc:42)"                 = NOT (port==0xCE AND pc==0x42)
 *   "!(in | out)"                      = ani IN ani OUT eventy
 *   "(port:CE pc:4000-40FF) | port16:CF06"  = dvě nezávislé skupiny OR
 *   "!!port:CE"                        = port==0xCE (dvojitá negace)
 *
 * Limity:
 *   - Filter string buffer (volající): 128 bytů.
 *   - Max AST uzly: 64 - parser ohlásí "Expression too complex
 *     (max 64 nodes)". Defenzivně chytá i příliš hluboké vnoření.
 *   - Plain-text name leaf payload: 63 znaků (+ null terminator).
 *
 * Leaf token syntax (zachovaná z V1.5.E):
 *   port:CE        low byte match (= (event.port & 0xFF) == 0xCE).
 *                  Vhodné pro 8-bit IORQ instrukce (OUT (n),A / IN A,(n)),
 *                  které mají na bus high byte z A registru, resp. random.
 *                  Filter ignoruje high byte - matchuje všechny IORQ s daným
 *                  low byte.
 *   port:C0-CF     low byte range (= (event.port & 0xFF) v rozsahu).
 *   port16:00CE    full 16-bit match (= event.port == 0x00CE přesně).
 *   port16:CF06    full 16-bit (= 0xCF family - sub-registry GDG/I/O).
 *   port16:CF00-CF0F  full 16-bit range.
 *   pc:4042        jen události s PC == 0x4042
 *   pc:4000-40FF   PC range 0x4000..0x40FF (inclusive)
 *   value:42       jen události s value == 0x42
 *   value:00-7F    value range 0x00..0x7F
 *   frame:>100     frame > 100
 *   frame:<100     frame < 100
 *   frame:50-150   frame range 50..150 (inclusive)
 *   cycle:>1000000 cumulative T-state cycle > 1M
 *   cycle:<500000  cycle < 500000
 *   cycle:1000-2000 cycle range
 *   in             jen IN (CPU read) eventy + MR (memory read 0xE000-0xE008)
 *   out            jen OUT (CPU write) eventy + MW (memory write 0xE000-0xE008)
 *   mr             jen MR (memory read) eventy
 *   mw             jen MW (memory write) eventy
 *   addr:E008      jen MMIO eventy s addr 0xE008
 *   addr:E000-E008 MMIO addr range
 *   <text>         bez prefixu = case-insensitive substring match na port name
 *
 * Per-token negace pomocí '!' prefixu:
 *   !port:CE       exclude low byte 0xCE
 *   !port16:00CE   exclude full 16-bit 0x00CE
 *   !pc:4042       exclude PC 0x4042
 *   !frame:>100    invertuje (= match frames <= 100)
 *   !value:42      exclude value 0x42
 *   !in / !out     exclude IN / exclude OUT
 *   !text          exclude port jejichz name obsahuje text
 *
 * Multiple tokens (space-separated) = implicit AND. Příklad:
 *   "port:CE pc:4042"  = events kde port==0xCE AND pc==0x4042
 *   "port:CE !pc:4042" = events kde port==0xCE AND pc != 0x4042
 *   "port:CE | pc:4042" = port==0xCE OR pc==0x4042 (F2)
 *
 * Ownership:
 * Filtr (= AST + arena) vlastní volající. Lifetime obvykle per-okno;
 * v UI se používá single static instance (lazy init při prvním render
 * History tab, free se neprovádí - pamět drží OS až do exit).
 *
 * Parser je idempotentní - opakované volání parse() resetuje strom
 * (vynuluje arenu) a postaví nový. Při chybě vrací false, zapíše
 * err message do bufferu a strom je v reset stavu (= match all).
 *
 * Licence: GPLv3
 */

#ifndef IO_HISTORY_FILTER_H
#define IO_HISTORY_FILTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "io_history.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Opaque handle drží AST root + arenu uzlů.
 *
 * Vnitřní layout je v io_history_filter.c. Externě se s ní pracuje
 * jen přes new / free / reset / parse / match API.
 *
 * Invariant: po new() je strom v reset stavu (= AST root NULL → match
 * all). Po parse() success drží strom; po parse() error je opět v
 * reset stavu. free() musí být voláno právě jednou na výsledek z new().
 */
typedef struct st_IO_HISTORY_FILTER st_IO_HISTORY_FILTER;


/**
 * @brief Alokuje a inicializuje nový filter.
 *
 * Inicializuje arenu (= jeden interní malloc bloku ~4 KB) a nastaví
 * AST root na NULL (= match all default).
 *
 * @return Pointer na nově alokovaný filter; NULL při OOM.
 *
 * Ownership: caller přebírá vlastnictví, je povinen volat
 * io_history_filter_free() právě jednou.
 */
st_IO_HISTORY_FILTER *io_history_filter_new ( void );


/**
 * @brief Uvolní filter (AST + arena + handle).
 *
 * No-op pokud f je NULL.
 *
 * @param f  Filter získaný z io_history_filter_new() (nebo NULL).
 */
void io_history_filter_free ( st_IO_HISTORY_FILTER *f );


/**
 * @brief Resetuje filter na "match all" (= AST root NULL).
 *
 * Recykluje internu arenu (= žádný free + malloc, jen reset offsetu).
 * Po volání filter projde každý event.
 *
 * No-op pokud f je NULL.
 *
 * @param f  Filter získaný z io_history_filter_new().
 */
void io_history_filter_reset ( st_IO_HISTORY_FILTER *f );


/**
 * @brief Parse filter syntax string do AST uvnitř existujícího filteru.
 *
 * Před parsováním provede reset() (= vynuluje předchozí strom).
 * Při chybě vrátí false, do err zapíše error message (max errsz-1 znaků
 * + null) a filter zůstane v reset stavu.
 *
 * Možné error stringy (citace anglických hlášek z io_history_filter.c):
 *
 *   Hodnota / range leaf tokenu:
 *     - "Invalid port hex value (8-bit)"
 *     - "Invalid port range (8-bit)"
 *     - "Invalid port16 hex value (16-bit)"
 *     - "Invalid port16 range (16-bit)"
 *     - "Invalid pc hex value"
 *     - "Invalid pc range"
 *     - "Invalid addr hex value"
 *     - "Invalid addr range"
 *     - "Invalid <name> value"       (frame / value / cycle)
 *     - "Invalid <name> range"
 *     - "Invalid <name>:>N value"    compare literál
 *     - "Invalid <name>:<N value"
 *     - "Empty <name> value"         prefix bez hodnoty
 *     - "Empty value after prefix"   obecná varianta
 *     - "Unknown filter prefix"      neznámý "xxx:" prefix
 *     - "Name token too long"        plain-text token > 63 znaků
 *
 *   Boolean expression (F1-F5):
 *     - "Empty token after '!'"
 *     - "Empty expression"
 *     - "Empty expression before '|'"   /   "Empty expression after '|'"
 *     - "Empty expression before '&'"
 *     - "Empty expression before 'OR'"  /   "Empty expression before 'AND'"
 *     - "Empty expression before ')'"
 *     - "Empty expression between two 'OR'"
 *     - "Empty expression between AND and 'OR'/'|'"
 *     - "Empty expression in parens"
 *     - "Trailing AND operator"
 *     - "Unexpected operator after AND"
 *     - "Expected ')'"                  nezavřená závorka
 *     - "Unexpected ')'"                nepárová zavírací závorka
 *     - "Unexpected token"
 *     - "Unexpected trailing token"
 *     - "Expected expression, got operator"
 *     - "Expression too complex (max 64 nodes)"
 *
 * Při empty / NULL src vrátí true a filter je v "match all" stavu.
 *
 * @param src    Filter string (případně NULL nebo prázdný).
 * @param out    Filter získaný z io_history_filter_new() (povinné non-NULL).
 * @param err    Buffer pro error message (může být NULL).
 * @param errsz  Velikost err bufferu.
 * @return true při úspěchu (i pro empty src), false při syntax error.
 */
bool io_history_filter_parse ( const char *src,
                                st_IO_HISTORY_FILTER *out,
                                char *err, size_t errsz );


/**
 * @brief Test zda event vyhovuje filtru.
 *
 * Provádí rekurzivní eval AST se short-circuit AND/OR. Při root == NULL
 * (= match all stav) vrací true.
 *
 * NOT sémantika:
 *   - NOT uzel invertuje boolean výsledek **celého podstromu**, ne
 *     per-leaf relevance.
 *   - Token-level "!port:CE" je v AST collapse-nuto do LEAF s
 *     negate=true (bit-exact backward compat).
 *   - Pro non-MMIO event (= IORQ) a NOT nad ADDR / MR / MW leaf je
 *     raw match podstromu false → NOT flipne na true. Tj. "!addr:E008"
 *     projde i pro čistý IORQ event. Pokud uživatel chce filter
 *     "jen MMIO mimo 0xE008", musí explicitně kombinovat:
 *     "!addr:E008 & (mr | mw)".
 *
 * Pro plain-text name match (= leaf typu NAME) potřebujeme port_name -
 * lookup z g_io_ports volá caller. Pokud filter nemá NAME leaf, port_name
 * může být NULL (viz io_history_filter_uses_name()).
 *
 * @param f          Parsovaný filter (povinné non-NULL).
 * @param e          Event z ringu (povinné non-NULL).
 * @param port_name  Lidsky čitelný název portu (nebo NULL).
 * @return true pokud event projde filtrem (= zobrazit v UI), false jinak.
 */
bool io_history_filter_match ( const st_IO_HISTORY_FILTER *f,
                                const st_IO_HISTORY_EVENT *e,
                                const char *port_name );


/**
 * @brief Operátor pro group-wrap quick-action.
 *
 * Používá @ref io_history_filter_string_group_wrap pro výběr spojky
 * mezi obaleným původním filtrem a novým tokenem.
 */
typedef enum
{
    IOFILT_GROUP_AND = 0,   /**< Vloží " & " mezi (current) a token. */
    IOFILT_GROUP_OR  = 1    /**< Vloží " | " mezi (current) a token. */
} en_IOFILT_GROUP_OP;


/**
 * @brief Obalí current filter string do závorek a připojí token přes AND/OR.
 *
 * Použití (V1.7+ položka 5.1 v CHECKLIST.md): row-context-menu quick-action
 * "Add AND group:" / "Add OR group:" v io_window History tab. Smysl je
 * dovolit uživateli jedním kliknutím přidat operand nad CELÝ dosavadní
 * filter, ne jen jako další člen do existujícího AND/OR řetězu
 * (= operator precedence "&" > "|" jinak udělá špatné seskupení).
 *
 * Semantika dle stavu bufferu:
 *
 *  - `buf[0] == '\0'` (prázdný filter): zapíše jen `token` bez obalení.
 *    Důvod: `() & X` nedává smysl, prázdný filter == "match all", takže
 *    "match all AND X" je sémanticky == X.
 *  - `buf[0] != '\0'`: přepíše buffer na `"(<old>) <sep> <token>"`,
 *    kde `<sep>` je `"&"` (AND) nebo `"|"` (OR).
 *
 * Truncate-safe: pokud by výsledek nepřežil v `bufsz`, funkce nic nezmění
 * (= buffer zůstane v původním stavu) a vrátí false. Volající (UI) může
 * vizuálně ignorovat (= uživatel uvidí, že filter se nezměnil).
 *
 * Buffer overflow detekce je striktní: helper si interně postaví celý
 * výsledek do dočasného bufferu (= 2× velikost vstupního bufferu na
 * stacku) a teprve při úspěšné kontrole délky ho zkopíruje zpět.
 *
 * Funkce nemodifikuje žádný globální stav, je čistá vzhledem k bufferu.
 *
 * @param buf    In/out buffer s filter stringem (povinné non-NULL, null-terminated).
 * @param bufsz  Velikost bufferu (povinné > 0; doporučeno >= 32).
 * @param op     IOFILT_GROUP_AND / IOFILT_GROUP_OR (= výběr spojky).
 * @param token  Token k připojení (povinné non-NULL, ne-prázdný).
 *               Helper nekontroluje syntaktickou validitu tokenu - to
 *               řeší následný io_history_filter_parse() ve volajícím
 *               (typicky callback @ImGui input box parse).
 * @return true při úspěchu (i pro prázdný buf), false při buffer overflow
 *         nebo invalid argumentech.
 *
 * Příklady:
 *
 *  - `buf=""`,                op=AND, token="pc:42" -> `"pc:42"`
 *  - `buf="port:CE"`,         op=AND, token="pc:42" -> `"(port:CE) & pc:42"`
 *  - `buf="port:CE | port:CF"`, op=AND, token="pc:42"
 *      -> `"(port:CE | port:CF) & pc:42"`
 *  - `buf="(a | b)"`,         op=OR,  token="c"
 *      -> `"((a | b)) | c"` (dvojité závorky parser tolerantně sní)
 */
bool io_history_filter_string_group_wrap ( char *buf, size_t bufsz,
                                            en_IOFILT_GROUP_OP op,
                                            const char *token );


/**
 * @brief Zjistí, zda AST filteru obsahuje aspoň jeden NAME (plain-text) leaf.
 *
 * UI optimalizace: pokud false, není třeba volat io_history_lookup_port_name
 * před každým match() (= lookup je drahý). Při root == NULL nebo bez NAME
 * leaf vrátí false.
 *
 * @param f  Filter (může být NULL → false).
 * @return true pokud match() bude číst port_name parametr.
 */
bool io_history_filter_uses_name ( const st_IO_HISTORY_FILTER *f );


#ifdef __cplusplus
}
#endif

#endif /* IO_HISTORY_FILTER_H */
