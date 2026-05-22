/*
 * io_history.h - Ring buffer pro IORQ event history (UI History tab).
 *
 * V1.5 Sprint 1 vrstva pro budouci History tab v I/O Ports panelu
 * (Sprint 2). Ring buffer drzi posledni N IORQ eventu (= IN/OUT) s
 * metadaty. V1.5.E rozsireni: ring nyni take pojme memory-mapped
 * (MMIO) eventy z 0xE000-0xE008 (MZ-700 mode mirror PIO/CTC/GDG).
 *
 *   - frame_screen   - g_gdg.total_elapsed.screens (= snimek)
 *   - port           - dvoji semantika podle flags bit 1:
 *                      * IORQ event   = 16-bit BC (high byte = B reg)
 *                      * memory event = MMIO addr 0xE000-0xE008
 *   - pc             - CPU PC instrukce, ktera event vyvolala
 *   - value          - byte hodnota (read result / write data)
 *   - flags          - bit 0 = is_read (1 = IN/MR, 0 = OUT/MW)
 *                      bit 1 = is_memory (0 = IORQ, 1 = MMIO 0xE000-0xE008)
 *
 * Default capacity 10000 events (~16 B per event = ~160 KB). UI Sprint 2
 * Settings dialog umozni resize 1000..50000 pres io_history_set_capacity.
 *
 * Hot-path hook v port_*_with_logging_cb (= stejny gating flag jako
 * io_activity: g_io_window_tracking_active). Default OFF = zero overhead.
 *
 * Pozn.: io_history je samostatny ring v paměti, ne soubor (zarozdílu od
 * trace-suite/iorqlog). Slouzi pro real-time UI display, ne pro post-mortem
 * analyzu velkych dat.
 *
 * Threading: hot-path zapisuje v emu vlakne, UI cte v UI vlakne. Bez locku
 * - krátký data race pri cteni jednoho event je akceptovatelny (UI muze
 * zobrazit "ulomek" eventu, ale ne korumpovat ring strukturu - vsechny
 * pole jsou aligned 16/32 bit, atomicke pri x86 single-write).
 *
 * Licence: GPLv3
 */

#ifndef IO_HISTORY_H
#define IO_HISTORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Default capacity ringu - 10000 eventu (~160 KB pri 16 B / event).
 */
#define IO_HISTORY_DEFAULT_CAPACITY  10000u

/**
 * @brief Minimum capacity (Settings dialog slider).
 */
#define IO_HISTORY_MIN_CAPACITY       1000u

/**
 * @brief Maximum capacity (Settings dialog slider).
 */
#define IO_HISTORY_MAX_CAPACITY      50000u


/**
 * @brief Velikost per-port record_enabled mapy (= 256 = 8-bit I/O space).
 *
 * V1.7+ 2.6 Selective per-port history capture. Filter v
 * @ref io_history_record indexovaný low byte port adresy. MMIO eventy
 * (@ref io_history_record_mem) NEJSOU filtrovany - sdileji 0xE000-0xE008
 * adresni prostor, ne 8-bit I/O.
 */
#define IO_HISTORY_RECORD_MAP_SIZE   256u


/**
 * @brief Per-port record_enabled flag.
 *
 * Indexovany low byte 16-bit IORQ port adresy (= addr & 0xFF). Hodnota:
 *   - 1  = port je nahravan do ringu (= default = puvodni chovani).
 *   - 0  = port je preskocen v @ref io_history_record hot-path filtru.
 *
 * Threading: byte writes/reads jsou na x86 atomicke. UI vlakno toggle je
 * okamzite videt v emu vlakne (cache coherency). Pripadny "torn read" v
 * cca 1 nanosekundovem okenku znamena nejvyse 1-2 eventu navic zaznamenanych
 * (= acceptable, jako global tracking_active flag pattern).
 *
 * Default po io_history_init = vsechny 1 (zachovani back-compat chovani).
 *
 * Pozn.: 256 bytes je marginal misto, ale dovoluje O(1) lookup v hot-path
 * bez bit shift/mask costu (= optimalizace na cache line: vejde se do 4 cache
 * lines, ale typicky aktivni "horká" sada portů zabira 1-2 lines).
 */
extern uint8_t g_io_history_record_enabled[ IO_HISTORY_RECORD_MAP_SIZE ];


/**
 * @brief Nastavi vsechny porty na record_enabled = 1 (= default state).
 *
 * Volat z @ref io_history_init pro reset behem reinicializace. Bezpecne
 * volat i z UI vlakna (= "Reset Record mask" tlacitko).
 */
void io_history_record_enable_all ( void );


/**
 * @brief Jeden IORQ event v history ringu.
 *
 * Velikost: 20 B (V1.5.D fix #3 pridal cpu_cycle uint32 - drive 16 B).
 *
 * @field frame    Aktualni screen number z g_gdg.total_elapsed.screens.
 * @field cpu_cycle Kumulativni T-state counter z cpu->total_cycles v
 *                 okamziku zaznamu (V1.5.D fix #3). Slouzi pro presnou
 *                 casovou linii eventu (= mnohem jemnejsi nez frame, vidi
 *                 vsechny instrukce v ramci frame).
 * @field port     16-bit IORQ adresa (= obsah BC pri IN/OUT instrukci).
 * @field pc       CPU PC instrukce (= source addr v IORQ).
 * @field value    Prenesena hodnota (IN read, OUT write).
 * @field flags    Bitmaska semantiky eventu (V1.5.E memory-mapped support):
 *                   bit 0 (`IO_HISTORY_FLAG_READ`)   - 1 = IN/MR (read), 0 = OUT/MW (write)
 *                   bit 1 (`IO_HISTORY_FLAG_MEMORY`) - 1 = memory-mapped event
 *                                                     (port = MMIO addr 0xE000-0xE008),
 *                                                     0 = IORQ event (port = full BC).
 *                 Pozn.: ulozeno jako uint8 misto bool kvuli explicit layout.
 *                 Backward-compat shortcut `e->is_in` lze nahradit `(e->flags & IO_HISTORY_FLAG_READ)`.
 * @field scanline Aktualni raster row (= g_gdg.beam_row, 0..311 pro PAL
 *                 GDG MZ-800). Slouzi pro UI zobrazeni "kdy v ramci frame"
 *                 udalost nastala (= V1.5 fix #5 nahrada za chybejici cycle).
 *                 Range: 0..1023 (16-bit overflow safe pro vsechny GDG variants).
 * @field px       Pixel column v ramci scanline (V1.5 fix #6).
 *                 Hodnota = VIDEO_GET_SCREEN_COL(g_gdg.total_elapsed.ticks)
 *                 = ticks % VIDEO_SCREEN_WIDTH. UI History tab zobrazuje
 *                 vedle scanline pro presnejsi raster timing IORQ eventu.
 * @field _pad     Padding pro zarovnani struktury na 4-byte boundary.
 */

/**
 * @brief Bit 0 v `flags` - 1 = IN/MR (CPU read), 0 = OUT/MW (CPU write).
 *
 * Sjednocena semantika pro IORQ i memory eventy.
 */
#define IO_HISTORY_FLAG_READ    0x01u

/**
 * @brief Bit 1 v `flags` - 1 = memory-mapped (MMIO 0xE000-0xE008), 0 = IORQ.
 *
 * V1.5.E rozsireni: memory-mapped MZ-700 mode IO (PIO/CTC/GDG zrcadlené v
 * adrese 0xE000-0xE008 v ROM space) sdili stejny ring jako IORQ eventy,
 * rozliseni pres tento bit.
 */
#define IO_HISTORY_FLAG_MEMORY  0x02u

typedef struct st_IO_HISTORY_EVENT
{
    uint32_t frame;
    uint32_t cpu_cycle;
    uint16_t port;
    uint16_t pc;
    uint8_t  value;
    uint8_t  flags;
    uint16_t scanline;
    uint16_t px;
    uint16_t _pad;
} st_IO_HISTORY_EVENT;


/**
 * @brief Ring buffer state.
 *
 * @field events     Pole eventu (heap-alokovane, capacity * sizeof(event)).
 * @field capacity   Maximalni pocet eventu v ringu.
 * @field head       Index dalsiho zapisu (= 0..capacity-1, wrap-around).
 * @field count      Pocet validnich eventu (capped at capacity).
 * @field overflow   true pokud uz doslo k wrap-around (= count == capacity
 *                   a head pretocil).
 */
typedef struct st_IO_HISTORY_RING
{
    st_IO_HISTORY_EVENT *events;
    size_t   capacity;
    size_t   head;
    size_t   count;
    bool     overflow;
} st_IO_HISTORY_RING;


/**
 * @brief Globalni instance ringu.
 */
extern st_IO_HISTORY_RING g_io_history;


/**
 * @brief Inicializace s default capacity (10000 eventu).
 *
 * Idempotentni - pokud je uz inicializovany, jen vynuluje stav.
 * Volat z debugger_init().
 */
void io_history_init ( size_t capacity );


/**
 * @brief Uvolneni heap pameti.
 */
void io_history_destroy ( void );


/**
 * @brief Zmena capacity za behu (Settings dialog slider).
 *
 * Realokuje events[] na novou velikost a ZAHODI predchozi data.
 * UI by mela zobrazit potvrzovaci dialog ("History bude vymazana").
 *
 * @param new_capacity  Nova velikost (musi byt v [MIN, MAX]).
 */
void io_history_set_capacity ( size_t new_capacity );


/**
 * @brief Hot-path hook - zaznamenat IORQ event.
 *
 * Volat z port_*_with_logging_cb gated pres g_io_window_tracking_active.
 *
 * @param is_in     true pro IN (CPU read), false pro OUT (CPU write).
 * @param port      16-bit IORQ adresa.
 * @param value     Prenesena hodnota.
 * @param pc        CPU PC instrukce.
 * @param frame     Aktualni screen number z g_gdg.total_elapsed.screens.
 * @param scanline  Aktualni raster row (= g_gdg.beam_row), pro UI zobrazeni.
 * @param px        Pixel column v ramci scanline (V1.5 fix #6).
 *                  = VIDEO_GET_SCREEN_COL(g_gdg.total_elapsed.ticks).
 * @param cpu_cycle Kumulativni T-state counter (cpu->total_cycles)
 *                  v okamziku zaznamu (V1.5.D fix #3).
 */
void io_history_record ( bool is_in, uint16_t port, uint8_t value,
                          uint16_t pc, uint32_t frame,
                          uint16_t scanline, uint16_t px,
                          uint32_t cpu_cycle );


/**
 * @brief Hot-path hook pro memory-mapped I/O event (V1.5.E).
 *
 * Sjednocena varianta @ref io_history_record pro MMIO 0xE000-0xE008
 * (MZ-700 mode mirror PIO/CTC/GDG v ROM space). Event je ulozen do
 * stejneho ringu, ale s `flags` = (is_read?READ:0) | MEMORY (= bit 1 set).
 *
 * Volat z `memory_read_with_logging_cb` / `memory_write_with_logging_cb`
 * filtrovane na rozsah `addr >= 0xE000 && addr <= 0xE008`. Hot-path je
 * gated stejnym `g_io_window_tracking_active` flagem jako IORQ.
 *
 * @param is_read   true = MR (memory read, CPU LD A,(addr)),
 *                  false = MW (memory write, CPU LD (addr),A).
 * @param addr      Plna 16-bit MMIO adresa (0xE000-0xE008).
 *                  Ulozeno do `port` field, sloupec UI render rozliseny
 *                  pres `flags & IO_HISTORY_FLAG_MEMORY`.
 * @param value     Prenesena hodnota.
 * @param pc        CPU PC instrukce.
 * @param frame     Aktualni screen number.
 * @param scanline  Aktualni raster row.
 * @param px        Pixel column.
 * @param cpu_cycle Kumulativni T-state counter.
 */
void io_history_record_mem ( bool is_read, uint16_t addr, uint8_t value,
                              uint16_t pc, uint32_t frame,
                              uint16_t scanline, uint16_t px,
                              uint32_t cpu_cycle );


/**
 * @brief Vyprazdneni ringu (= count=0, head=0, overflow=false).
 */
void io_history_clear ( void );


/**
 * @brief Vrati event pro dany "logical" index (0 = oldest).
 *
 * Indexy [0, count-1]:
 *   - Pri non-overflow: idx = pozice od oldest.
 *   - Pri overflow: idx 0 odpovida (head) wrapped event.
 *
 * @param idx  Logical index (0 = oldest, count-1 = newest).
 * @return Pointer na event nebo NULL pokud idx >= count.
 */
const st_IO_HISTORY_EVENT* io_history_get ( size_t idx );


#ifdef __cplusplus
}
#endif

#endif /* IO_HISTORY_H */
