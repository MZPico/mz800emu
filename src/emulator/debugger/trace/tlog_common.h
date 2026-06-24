/**
 * @file   tlog_common.h
 * @brief  Společný framework pro 4 trace-suite subsystémy.
 *
 * Sdílená infrastruktura pro `cputrack`, `iorqlog`, `intlog` a `hwlog`:
 *  - chunk writer (RAM buffer + sync flush na disk)
 *  - meta.json builder (per-recording, aktualizovaný po každém flush)
 *  - per-subsystém limity (chunk-mb, max-total-mb)
 *  - sjednocený přístup k pxCLK / cpuclk / screens (přes GDG counter)
 *  - konzolové notifikace (chunk swap, max reached)
 *
 * Architektura: každý subsystém vlastní jednu instanci @ref tlog_writer_t
 * (RAM buffer + soubor handle + meta state). Subsystém volá
 * @ref tlog_writer_append() po každé události. Při naplnění RAM bufferu se
 * provede synchronní blocking flush a vypíše se notifikace na konzoli.
 *
 * @note Sync writer = žádné async thready. Emulace se může zakuckávat
 *       během flush - akceptovatelné, je to ladící mód.
 *
 * @author Michal Hucik <hucik@ordoz.com>
 */

#ifndef TLOG_COMMON_H
#define TLOG_COMMON_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <glib.h>

/**
 * @brief Režim aktivace trace-suite subsystému.
 *
 * Paralelní k @c en_DEBUGGER_MHMAP_MODE (CDL pattern). Všechny 4 subsystémy
 * trace-suite (cputrack/iorqlog/intlog/hwlog) sdílí tento enum přes alias.
 */
typedef enum en_TLOG_MODE
{
    TLOG_MODE_OFF = 0,           /**< default - subsystém vypnutý, žádná režie */
    TLOG_MODE_WITH_WINDOW,       /**< aktivní jen pokud je otevřené debug okno */
    TLOG_MODE_ALWAYS,            /**< aktivní trvale */
} en_TLOG_MODE;

/**
 * @brief Default chunk velikost v MB (per subsystém).
 *
 * Po dosažení této velikosti se RAM buffer flushne na disk jako další
 * `<name>.NNN.bin` soubor. Vyšší hodnota = méně častý disk I/O ale
 * vyšší peak RAM usage.
 */
#define TLOG_DEFAULT_CHUNK_MB     64u

/**
 * @brief Default max-total-mb (0 = unlimited).
 *
 * Hard limit na celkovou velikost recordingu (sum všech chunků). Po
 * dosažení se subsystém zastaví, do meta.json se zapíše
 * `"truncated": true, "reason": "max_total_mb"`.
 */
#define TLOG_DEFAULT_MAX_TOTAL_MB 0u

/**
 * @brief Stav writeru jednoho trace-suite subsystému.
 *
 * Nezbytné invariants:
 *  - `buffer != NULL` po @ref tlog_writer_open(), `NULL` po @ref tlog_writer_close()
 *  - `buffer_used <= buffer_size`
 *  - `total_bytes_written` = sum všech doposud flushnutých bytů +
 *    `buffer_used` (= "live" total)
 *  - `chunk_index` = počet již zapsaných chunků (= příští flush bude
 *    `<dir>/<name>.<chunk_index:03d>.bin`)
 *  - `truncated == 1` => writer odmítá další @ref tlog_writer_append()
 *
 * @ownership Buffer alokuje @ref tlog_writer_open(), uvolňuje
 *            @ref tlog_writer_close().
 * @ownership `dir`, `name`, `subsys_name` - pointers vlastněné callerem,
 *            writer je jen referencuje (lifetime musí přesahovat writer).
 */
typedef struct st_TLOG_WRITER
{
    /* Konfigurace (neměnné po open) */
    const char *subsys_name;     /**< "cputrack" / "iorqlog" / ... (pro msg/log) */
    char *dir;                   /**< Cílový adresář (g_strdup, vlastní writer) */
    char *name;                  /**< Basename recordingu (g_strdup, vlastní writer) */
    size_t chunk_size_bytes;     /**< chunk-mb * 1024 * 1024 */
    size_t max_total_bytes;      /**< 0 = unlimited */

    /* Runtime stav */
    uint8_t *buffer;             /**< RAM buffer velikosti chunk_size_bytes */
    size_t buffer_size;          /**< == chunk_size_bytes (cached) */
    size_t buffer_used;          /**< Počet platných bajtů v buffer[0..buffer_used-1] */
    unsigned chunk_index;        /**< Index příštího chunku (= počet hotových chunků) */
    uint64_t total_bytes_written;/**< Sum všech doposud flushnutých bajtů + buffer_used */
    int truncated;               /**< 1 = max_total_bytes dosaženo, append blokován */

    /* Per-chunk anchors pro meta.json (timestamp na startu chunku) */
    uint64_t current_chunk_start_pxclk;   /**< pxCLK total na začátku aktuálního bufferu */
    uint64_t current_chunk_start_cpuclk;  /**< CPU clk total na začátku aktuálního bufferu */
    uint32_t current_chunk_start_screens; /**< screens total na začátku aktuálního bufferu */

    /* Meta.json - akumulace záznamů o chunkách (rebuild celé meta.json
     * po každém flush). Implementováno jako pole st_TLOG_CHUNK_META. */
    GArray *chunks_meta;         /**< Pole st_TLOG_CHUNK_META */

    /* Subsys-specific header fragment (JSON), předaný při start.
     * Drží se pro automatické re-zápisy meta.json při flush_chunk a close,
     * aby header neztratil obsah po prvním auto flush. NULL = žádný header.
     * Vlastní writer (g_strdup), uvolní se v close. */
    char *subsys_header_cache;
} st_TLOG_WRITER;

/**
 * @brief Per-chunk metadata zapisovaná do meta.json.
 *
 * Pro každý flushnutý chunk si pamatujeme jeho start timestamp (= seek
 * anchor) a velikost. To umožňuje O(1) seek na chunk + lineární scan
 * uvnitř (rekonstrukce timestampu sumací insn_T_states + wait_clk).
 */
typedef struct st_TLOG_CHUNK_META
{
    unsigned index;              /**< 0-based chunk index */
    size_t bytes;                /**< Velikost souboru v bytech */
    uint64_t start_pxclk;        /**< pxCLK total na začátku chunku */
    uint64_t start_cpuclk;       /**< CPU clk total na začátku chunku */
    uint32_t start_screens;      /**< screens total na začátku chunku */
} st_TLOG_CHUNK_META;

/**
 * @brief Otevřít writer pro daný subsystém.
 *
 * Zalokuje RAM buffer (`chunk_mb` MB), inicializuje counter, vytvoří
 * (resp. zajistí existenci) cílový adresář. Pokud adresář nelze
 * vytvořit, vrací -1.
 *
 * @param w               Writer struktura (caller-allocated, vyplní se).
 * @param subsys_name     Identifikátor subsystému (např. "cputrack");
 *                        používá se v console notifikacích a v meta.json.
 *                        Lifetime musí přesahovat writer (typ. string literal).
 * @param dir             Cílový adresář (zkopíruje se přes g_strdup).
 * @param name            Basename (zkopíruje se přes g_strdup).
 * @param chunk_mb        Velikost chunku v MB (0 = použít default).
 * @param max_total_mb    Max total v MB (0 = unlimited).
 * @param init_pxclk      pxCLK total na startu recordingu.
 * @param init_cpuclk     CPU clk total na startu recordingu.
 * @param init_screens    screens total na startu recordingu.
 *
 * @return 0 OK, -1 error (alloc fail, mkdir fail).
 *
 * @post Při úspěchu w->buffer != NULL, w->buffer_used == 0.
 */
int tlog_writer_open ( st_TLOG_WRITER *w,
                       const char *subsys_name,
                       const char *dir, const char *name,
                       unsigned chunk_mb, unsigned max_total_mb,
                       uint64_t init_pxclk, uint64_t init_cpuclk,
                       uint32_t init_screens );

/**
 * @brief Připojit `n` bajtů event payloadu do bufferu.
 *
 * Pokud by append způsobil přetečení buffer_size, nejdřív se volá
 * @ref tlog_writer_flush_chunk() (synchronní disk write). Pokud je
 * writer truncated (max_total dosaženo), volání se ignoruje (tichý
 * no-op - pollerem se zjistí přes @ref tlog_writer_is_truncated()).
 *
 * @note Funkce je volaná z hot path (každý CPU instr u cputrack), proto
 *       je inline-friendly a bez bounds checks na user side.
 *
 * @param w        Writer (musí být open).
 * @param data     Payload buffer (n bytes).
 * @param n        Velikost payloadu v bytech (>= 1).
 *
 * @return 0 OK, -1 disk write error nebo truncated stop.
 */
int tlog_writer_append ( st_TLOG_WRITER *w, const void *data, size_t n );

/**
 * @brief Vynutit flush aktuálního bufferu na disk (i nezaplněného).
 *
 * Použito při zavírání recordingu, nebo při explicitním "Export now"
 * z UI. Pokud je buffer_used == 0, no-op. Po flush se buffer_used
 * resetuje na 0 a `chunk_index++`.
 *
 * Po flush se update meta.json (sériový rewrite celé meta).
 *
 * @param w           Writer.
 * @param now_pxclk   Aktuální pxCLK total (pro aktualizaci anchoru
 *                    příštího chunku).
 * @param now_cpuclk  Aktuální CPU clk total.
 * @param now_screens Aktuální screens total.
 *
 * @return 0 OK, -1 disk write error.
 */
int tlog_writer_flush_chunk ( st_TLOG_WRITER *w,
                              uint64_t now_pxclk, uint64_t now_cpuclk,
                              uint32_t now_screens );

/**
 * @brief Aktualizovat / přepsat meta.json soubor.
 *
 * Používá GLib JSON serializaci. Soubor: `<dir>/<name>.json`.
 * Volá se po každém @ref tlog_writer_flush_chunk(). Lze volat i ručně.
 *
 * @param w               Writer.
 * @param header_extra    Volitelný JSON fragment (string) přidaný do
 *                        objektu pod klíč "subsys_header" (per-subsystém
 *                        specifická pole). NULL = bez extra.
 *
 * @return 0 OK, -1 write error.
 */
int tlog_writer_update_meta ( st_TLOG_WRITER *w, const char *header_extra );

/**
 * @brief Zavřít writer, flushnout zbytek bufferu, finalizovat meta.json.
 *
 * Po close je writer nepoužitelný (buffer dealokován). Idempotentní -
 * druhé volání = no-op.
 *
 * @param w           Writer.
 * @param now_pxclk   Aktuální pxCLK total.
 * @param now_cpuclk  Aktuální CPU clk total.
 * @param now_screens Aktuální screens total.
 */
void tlog_writer_close ( st_TLOG_WRITER *w,
                         uint64_t now_pxclk, uint64_t now_cpuclk,
                         uint32_t now_screens );

/**
 * @brief Test, zda byl writer zastaven kvůli max_total limitu.
 *
 * @return 1 = truncated, 0 = stále recording.
 */
static inline int tlog_writer_is_truncated ( const st_TLOG_WRITER *w )
{
    return w->truncated;
}

/**
 * @brief Pomocná - vytvoří cílový adresář (s parents) pokud neexistuje.
 *
 * @param dir     Cílový adresář.
 * @return 0 OK, -1 error.
 */
int tlog_common_ensure_dir ( const char *dir );

/**
 * @brief Přičte @p n do globálního kumulativního čítače disk-bajtů trace-suite.
 *
 * Volá se z každého reálného zápisu na disk v rámci trace-suite (chunk soubor,
 * meta.json, per-subsystém initial-state dumpy), aby existoval jeden centrální
 * součet CELÉHO disk footprintu, který trace-suite zapsala. Slouží 0019
 * vrstvě 3 (byte backstop): trace_save BP akce countuje deltu tohoto čítače,
 * takže do auto-pauzy se započítá i objem chunk segmentů a initial-state
 * dumpů (ne jen hlavní index soubor - to byl undercount vektor, viz V19).
 *
 * @param n  Počet bajtů úspěšně zapsaných na disk (0 = no-op).
 *
 * Side effects: inkrement process-globálního čítače (monotónní).
 * Threading: čítač modifikován z emu vlákna (trace zápisy běží na něm); prostý
 * uint64 read/modify/write bez locku - případný race jen posune okamžik
 * accountingu, neohrozí korektnost.
 */
void tlog_common_disk_bytes_add ( uint64_t n );

/**
 * @brief Vrátí kumulativní počet disk-bajtů zapsaných trace-suite od startu
 *        procesu (resp. od posledního @ref tlog_common_disk_bytes_reset).
 *
 * Monotónně neklesající (kromě explicitního resetu). Konzument (0019 byte
 * backstop v bp_action.c) si pamatuje předchozí hodnotu a accountuje deltu.
 *
 * @return Kumulativní součet disk-bajtů trace-suite.
 */
uint64_t tlog_common_disk_bytes_total ( void );

/**
 * @brief Vynuluje kumulativní čítač disk-bajtů trace-suite.
 *
 * Určeno pro reset emulátoru a pro deterministickou izolaci testů. V produkci
 * běžně volat netřeba (konzument pracuje s deltou, ne s absolutní hodnotou).
 */
void tlog_common_disk_bytes_reset ( void );

/**
 * @brief Typ hooku volaného po každém inkrementu disk-byte čítače (0019 v3 flush-side).
 *
 * Notifikační callback, který trace vrstva (tlog_common) volá pokaždé, když
 * inkrementuje kumulativní disk-byte čítač přes @ref tlog_common_disk_bytes_add
 * (chunk swap, meta.json, initial-state dump). Slouží flush-side byte backstop
 * guardu: trace flooduje disk INKREMENTÁLNĚ po chunkách i mezi dvěma trace_save
 * BP fire, takže per-fire accounting (bp_action) by tu rostoucí stopu chytil až
 * při dalším fire. Hook umožní vyhodnotit backstop průběžně, na flush cestě.
 *
 * LAYERING: tlog_common je čistá trace vrstva a NESMÍ znát breakpoints ani
 * emulator (závislostní inverze). Proto jen vystaví tento hook; konkrétní
 * vyhodnocení prahu + auto-pauzu provádí vrstva, která breakpoints/emulator
 * už zná (bp_action.c), a hook si registruje. tlog_common hooku jen předá
 * právě přičtený počet bajtů.
 *
 * @param added  Počet bajtů, o který právě narostl kumulativní čítač.
 *
 * Threading: volán z emu vlákna (trace zápisy běží na něm), synchronně uvnitř
 * @ref tlog_common_disk_bytes_add. Hook nesmí re-entrantně volat
 * tlog_common_disk_bytes_add.
 */
typedef void ( *tlog_disk_flush_hook_fn ) ( uint64_t added );

/**
 * @brief Zaregistruje hook volaný po každém inkrementu disk-byte čítače (0019 v3).
 *
 * @param fn  Hook funkce, nebo @c NULL pro odregistraci (default = žádný hook,
 *            chování trace-suite beze změny).
 *
 * Side effects: mutuje process-globální ukazatel na hook.
 * Threading: typicky voláno při inicializaci (breakpoints_init) nebo z testu;
 * není synchronizováno - nevolat souběžně s běžící emulací používající čítač.
 */
void tlog_common_set_disk_flush_hook ( tlog_disk_flush_hook_fn fn );

/**
 * @brief Získat aktuální pxCLK total (= přes všechny screens).
 *
 * @return uint64 počet pxCLK od startu emulace.
 */
uint64_t tlog_common_get_pxclk_total ( void );

/**
 * @brief Získat aktuální screens count.
 */
uint32_t tlog_common_get_screens_total ( void );

/**
 * @brief Získat aktuální pxCLK uvnitř aktuálního screenu.
 */
uint32_t tlog_common_get_pxclk_in_screen ( void );

/**
 * @brief Získat aktuální CPU clk total.
 *
 * Odvozeno jako pxCLK total / cpu_divider (5 pro MZ-800/700/1500).
 */
uint64_t tlog_common_get_cpuclk_total ( void );

/**
 * @brief Získat platform string ("MZ-800", "MZ-700", "MZ-1500").
 *
 * Vrací konstantní string podle MZARCH compile-time konstanty.
 */
const char *tlog_common_get_platform_name ( void );

/**
 * @brief Získat pxCLK frekvenci (Hz) - typ. 17734475 pro standard config.
 */
uint32_t tlog_common_get_pxclk_freq ( void );

/**
 * @brief Získat CPU divider (= pxCLK / divider = CPU CLK).
 *
 * Pro MZ-800/700/1500 = 5 (3.546895 MHz).
 */
uint32_t tlog_common_get_cpu_divider ( void );

/**
 * @brief Získat počet pxCLK per screen (typ. 97344 pro MZ-800).
 */
uint32_t tlog_common_get_pxclk_per_screen ( void );

#ifdef __cplusplus
}
#endif

#endif /* TLOG_COMMON_H */
