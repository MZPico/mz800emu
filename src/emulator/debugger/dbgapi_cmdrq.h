/*
 * dbgapi_cmdrq.h — CMDRQ kanál: příkazy z UI do Emulátoru
 *
 * Synchronní request/response komunikace přes kruhový buffer (frontu).
 * UI vlákno vkládá příkazy, emulátorové vlákno je zpracovává a vrací odpovědi.
 *
 * Vlastnictví paměti:
 * - data_ptr a result_ptr alokuje a vlastní volající (UI strana)
 * - emulátor pouze čte z data_ptr a zapisuje do result_ptr
 * - po návratu z submit_cmd_sync() je klient zodpovědný za uvolnění
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 */
#ifndef DBGAPI_CMDRQ_H
#define DBGAPI_CMDRQ_H

#include <stdint.h>
#include <stdbool.h>
#include "app/app_thread.h"

/* ============================================================================
 * PŘÍKAZY (CMDRQ)
 *
 * Plochý enum — bez hierarchie. Nové příkazy se přidávají na konec.
 * Horní bit (31) je rezervovaný pro BLOCKING flag.
 * ============================================================================ */

typedef enum en_DBGAPI_CMD
{
    /* --- Řízení emulace --- */
    DBGAPI_CMD_NONE = 0,            /* Bez efektu — ping */
    DBGAPI_CMD_IS_DEBUGGER_ACTIVE,  /* Dotaz na stav debuggeru — result_ptr: bool* */
    DBGAPI_CMD_DEBUGGER_ACTIVATE,   /* Aktivovat debugger (začne se zaznamenávat historie) */
    DBGAPI_CMD_DEBUGGER_DEACTIVATE, /* Deaktivovat debugger */
    DBGAPI_CMD_PAUSE,               /* Pozastavit emulaci */
    DBGAPI_CMD_FORCE_PAUSE,         /* Vynuceně pozastavit (nepřeskočitelné) */
    DBGAPI_CMD_RUN,                 /* Spustit emulaci */
    DBGAPI_CMD_IS_RUNNING,          /* Dotaz na stav emulace — result_ptr: bool* */
    DBGAPI_CMD_STEP_INTO,           /* Jeden krok (Step Into) */
    DBGAPI_CMD_STEP_OVER,           /* Step Over (přes CALL, blokové instrukce) */
    DBGAPI_CMD_RUN_TO,              /* Běh do adresy — data_ptr: uint16_t* (cílová adresa) */
    DBGAPI_CMD_RESET,               /* Reset CPU */

    /* --- CPU registry --- */
    DBGAPI_CMD_GET_REG,      /* Čtení registru — data_ptr: uint8_t* (reg_id), result_ptr: uint16_t* */
    DBGAPI_CMD_SET_REG,      /* Zápis registru — data_ptr: st_DBGAPI_REG_PARAM* */
    DBGAPI_CMD_GET_ALL_REGS, /* Čtení všech registrů — result_ptr: uint16_t[DBGAPI_REG_COUNT] */

    /* --- Paměť --- */
    DBGAPI_CMD_MEM_READ,  /* Čtení bloku paměti — data_ptr: st_DBGAPI_MEM_PARAM*, result_ptr: uint8_t* */
    DBGAPI_CMD_MEM_WRITE, /* Zápis bloku paměti — data_ptr: st_DBGAPI_MEM_PARAM* (včetně dat) */

    /* --- Breakpointy --- */
    DBGAPI_CMD_BP_ADD,    /* Přidání breakpointu — data_ptr: st_DBGAPI_BP_PARAM* */
    DBGAPI_CMD_BP_REMOVE, /* Odebrání breakpointu — data_ptr: st_DBGAPI_BP_PARAM* */
    DBGAPI_CMD_BP_LIST,   /* Seznam breakpointů — result_ptr: st_DBGAPI_BP_LIST_RESULT* */
    /* --- Breakpointy CRUD (V1.7+ migrace UI -> dbgapi) --- */
    DBGAPI_CMD_BP_UPDATE,            /* Selektivní update polí existujícího BP — data_ptr: st_DBGAPI_BP_UPDATE_PARAM* (update_mask řídí co přepsat) */
    DBGAPI_CMD_BP_SET_ENABLED,       /* Quick toggle enabled/disabled — data_ptr: st_DBGAPI_BP_SET_ENABLED_PARAM* */
    DBGAPI_CMD_BP_SET_PARENT,        /* Quick reparent (drag-drop) — data_ptr: st_DBGAPI_BP_SET_PARENT_PARAM* */
    DBGAPI_CMD_BP_CREATE_WITH_INIT,  /* Atomický create + init polí (= add_auto + UPDATE) — data_ptr: st_DBGAPI_BP_UPDATE_PARAM* (id=-1 vstup, naplní handler) */

    /* --- Breakpoint groups CRUD (V1.7+ migrace UI -> dbgapi) --- */
    DBGAPI_CMD_BPGRP_ADD,            /* Přidání nové skupiny — data_ptr: st_DBGAPI_BPGRP_ADD_PARAM* (name + parent, handler naplní id) */
    DBGAPI_CMD_BPGRP_REMOVE,         /* Odebrání skupiny podle ID — data_ptr: st_DBGAPI_BPGRP_REMOVE_PARAM* */
    DBGAPI_CMD_BPGRP_UPDATE,         /* Selektivní update polí skupiny — data_ptr: st_DBGAPI_BPGRP_UPDATE_PARAM* (update_mask) */

    /* --- Disassembly --- */
    DBGAPI_CMD_DASM,        /* Disassembly na adrese — data_ptr: st_DBGAPI_DASM_PARAM*, result_ptr: st_DBGAPI_DASM_RESULT* */
    DBGAPI_CMD_HISTORY_GET, /* Čtení historie instrukcí — result_ptr: buffer pro historii */

    /* --- CPU rozšířený stav (CPU window) --- */
    DBGAPI_CMD_GET_CPU_FLAGS,   /* Čtení doplňkového CPU stavu — result_ptr: st_DBGAPI_CPU_FLAGS* */
    DBGAPI_CMD_SET_CPU_FLAGS,   /* Selektivní zápis CPU stavu (IFF1/IFF2/IM/I/R) — data_ptr: st_DBGAPI_CPU_FLAGS* */
    DBGAPI_CMD_GET_IM2_VECTOR,  /* Čtení IM2 ISR vektoru (PIO-Z80) — result_ptr: st_DBGAPI_IM2_VECTOR* */
    DBGAPI_CMD_GET_RASTER_POS,  /* Čtení pozice rastru a frame counteru — result_ptr: st_DBGAPI_RASTER_POS* */
    DBGAPI_CMD_GET_LAST_INSTR,  /* Poslední dokončená instrukce z history ringu — result_ptr: st_DBGAPI_LAST_INSTR* */
    DBGAPI_CMD_GET_CPU_PANEL_BATCH, /* Agregovaný batch pro CPU panel — data_ptr: uint32_t* (which-mask), result_ptr: st_DBGAPI_CPU_PANEL_BATCH* */
    DBGAPI_CMD_SET_USER_CYCLE_ORIGIN, /* Nastavení počátku User cycle counteru — data_ptr: uint32_t* (= absolutní total_cycles snapshot) */
    DBGAPI_CMD_SET_PIOZ80_INTERRUPT_VECTOR, /* Zápis PIO-Z80 interrupt_vector — data_ptr: st_DBGAPI_PIOZ80_VEC_PARAM* */
    DBGAPI_CMD_MEM_WRITE_CHECKED,   /* Zápis paměti s region check — data_ptr: st_DBGAPI_MEM_WRITE_CHECKED_PARAM* */

    /* --- Stack monitor --- */
    DBGAPI_CMD_STACK_DUMP,          /* Hex dump paměti zásobníku kolem zadané adresy — data_ptr: st_DBGAPI_STACK_DUMP_PARAM* */
    DBGAPI_CMD_STACK_REGIONS_LIST,            /* Snapshot definovaných stack regionů — data_ptr: st_DBGAPI_STACK_REGIONS_LIST_PARAM* */
    DBGAPI_CMD_STACK_REGIONS_ADD,             /* Přidat region — data_ptr: st_DBGAPI_STACK_REGIONS_ADD_PARAM* */
    DBGAPI_CMD_STACK_REGIONS_REMOVE,          /* Odebrat region — data_ptr: st_DBGAPI_STACK_REGIONS_REMOVE_PARAM* */
    DBGAPI_CMD_STACK_REGIONS_RESET_WATERMARK, /* Reset watermark + counters — data_ptr: st_DBGAPI_STACK_REGIONS_REMOVE_PARAM* (sdílí pole index) */
    DBGAPI_CMD_STACK_REGIONS_EDIT,            /* V7: edit existujícího regionu (name/base/limit) — data_ptr: st_DBGAPI_STACK_REGIONS_EDIT_PARAM* */
    DBGAPI_CMD_STACK_HISTORY_ENABLE, /* Zapnout/vypnout SP history recording — data_ptr: st_DBGAPI_STACK_HISTORY_ENABLE_PARAM* */
    DBGAPI_CMD_STACK_HISTORY_GET,    /* Bulk snapshot SP history ring bufferu — data_ptr: st_DBGAPI_STACK_HISTORY_GET_PARAM* */
    DBGAPI_CMD_STACK_HISTORY_RESET,  /* Vyprázdnit ring buffer (recording flag zachován) — bez paramu */

    /* --- Event Viewer (mutant event-viewer, Vlna 1) --- */
    DBGAPI_CMD_EVENTLOG_START,       /* Spustit recording — bez paramu, result: rq->success */
    DBGAPI_CMD_EVENTLOG_STOP,        /* Zastavit recording — bez paramu */
    DBGAPI_CMD_EVENTLOG_CLEAR,       /* Vyprázdnit ring — bez paramu */
    DBGAPI_CMD_EVENTLOG_SET_CAPACITY,/* Změnit capacity ringu — data_ptr: st_DBGAPI_EVENTLOG_CAPACITY_PARAM* */
    DBGAPI_CMD_EVENTLOG_SET_MASK,    /* Změnit categories bitmask — data_ptr: st_DBGAPI_EVENTLOG_MASK_PARAM* */
    DBGAPI_CMD_EVENTLOG_GET_EVENT,   /* Načíst event[idx] z ringu — data_ptr: st_DBGAPI_EVENTLOG_GET_EVENT_PARAM* */

    /* --- Callstack (mutant callstack, Fáze 2B) --- */
    DBGAPI_CMD_GET_CALLSTACK,        /* Snapshot shadow stacku + stats — data_ptr: st_DBGAPI_CALLSTACK_GET_PARAM* */

    /* --- Profiler (mutant profiler V1, fáze F2) --- */
    DBGAPI_CMD_GET_PROFILER,         /* Snapshot agregátoru + stats - data_ptr: st_DBGAPI_PROFILER_GET_PARAM* */
    DBGAPI_CMD_PROFILER_SET_ACTIVE,  /* Set active flag - data_ptr: st_DBGAPI_PROFILER_SET_ACTIVE_PARAM* */
    DBGAPI_CMD_PROFILER_RESET,       /* Reset agregátoru - data_ptr: NULL (no payload) */

} en_DBGAPI_CMD;

/* ============================================================================
 * BLOCKING FLAG
 *
 * Pokud je nastaven v horním bitu příkazu, emulátor po zpracování tohoto
 * příkazu nepřechází zpět do normálního režimu, ale okamžitě kontroluje
 * frontu na další příkaz. Umožňuje transakční dávky:
 *
 *   CMD_GET_REG | BLOCKING  →  CMD_GET_REG | BLOCKING  →  CMD_GET_REG
 *
 * Poslední příkaz v dávce nemá BLOCKING flag — emulátor se vrátí do normálu.
 * ============================================================================ */

#define DBGAPI_CMDFLAG_BLOCKING (1 << 31)

/* Maska pro extrakci příkazu bez flagů */
#define DBGAPI_CMD_MASK 0x0000FFFF

/* ============================================================================
 * STAVY ZPRACOVÁNÍ PŘÍKAZU
 * ============================================================================ */

typedef enum en_DBGAPI_CMDSTATE
{
    DBGAPI_CMDSTATE_NONE = 0,  /* Slot je volný */
    DBGAPI_CMDSTATE_PENDING,   /* Příkaz čeká na zpracování emulátorem */
    DBGAPI_CMDSTATE_PROCESSED, /* Příkaz byl zpracován — odpověď je připravena */
} en_DBGAPI_CMDSTATE;

/* ============================================================================
 * STAV ODPOVĚDI — ochranný příznak
 *
 * Emulátor nastaví ENDING, když se chystá ukončit.
 * UI vlákno pak ví, že nemá smysl posílat další příkazy.
 * ============================================================================ */

typedef enum en_DBGAPI_CMDREPLY_STATE
{
    DBGAPI_CMDREPLY_STATE_NONE = 0,
    DBGAPI_CMDREPLY_STATE_ENDING, /* Emulátor se ukončuje — nekomunikovat */
} en_DBGAPI_CMDREPLY_STATE;

/* ============================================================================
 * SLOT CMDRQ — jeden příkaz ve frontě
 *
 * Každý slot má vlastní mutex a condition variable pro synchronizaci
 * mezi UI vláknem (čekající na odpověď) a emulačním vláknem (zpracovávající
 * příkaz).
 *
 * Životní cyklus:
 * 1. UI zamkne slot->mutex, nastaví cmd/data_ptr/result_ptr, cmd_state=PENDING
 * 2. UI čeká na slot->cond (blokuje se)
 * 3. EMU zpracuje příkaz, zapíše result_ptr/success, cmd_state=PROCESSED
 * 4. EMU signalizuje slot->cond → UI se probudí
 * 5. UI přečte výsledek, nastaví cmd_state=NONE → slot volný
 * ============================================================================ */

typedef struct st_DBGAPI_CMDRQ
{
    en_DBGAPI_CMDSTATE cmd_state; /* Stav zpracování příkazu */
    en_DBGAPI_CMD cmd;            /* Příkaz + případný BLOCKING flag v horním bitu */
    void *data_ptr;               /* Vstupní data od klienta (vlastní klient) */
    void *result_ptr;             /* Buffer pro odpověď (vlastní klient) */
    bool success;                 /* Výsledek: true = úspěch, false = chyba */
    app_mutex_t *mutex;           /* Per-slot mutex */
    app_cond_t *cond;             /* Per-slot condition variable */
} st_DBGAPI_CMDRQ;

/* ============================================================================
 * FRONTA CMDRQ — kruhový buffer
 *
 * Implementace: ring buffer s pevnou velikostí.
 * Přístup k head/tail je chráněn queue_mutex.
 * Čekající emulátor (v pause) se probudí přes queue_cond.
 *
 * head = pozice dalšího příkazu ke zpracování (EMU čte)
 * tail = pozice pro vložení nového příkazu (UI zapisuje)
 * Fronta je prázdná pokud head == tail.
 * Fronta je plná pokud (tail + 1) % SIZE == head.
 * ============================================================================ */

#define DBGAPI_CMDRQ_QUEUE_SIZE 16

typedef struct st_DBGAPI_CMDRQ_QUEUE
{
    st_DBGAPI_CMDRQ cmdrq[DBGAPI_CMDRQ_QUEUE_SIZE]; /* Pole slotů */
    int head;                                       /* Index čtení (EMU) */
    int tail;                                       /* Index zápisu (UI) */
    app_mutex_t *queue_mutex;                       /* Mutex pro přístup k head/tail */
    app_cond_t *queue_cond;                         /* Signalizace: nový příkaz ve frontě */
    en_DBGAPI_CMDREPLY_STATE reply_state;           /* Ochranný příznak pro ukončení */
} st_DBGAPI_CMDRQ_QUEUE;

/* ============================================================================
 * PARAMETRICKÉ STRUKTURY PRO PŘÍKAZY
 *
 * Struktury, které klient alokuje a předává přes data_ptr/result_ptr.
 * Definovány zde, protože je potřebují obě strany (UI i EMU).
 * ============================================================================ */

/**
 * @brief Počet Z80 registrů v poli pro CMD_GET_ALL_REGS.
 *
 * Odpovídá počtu hodnot enum z80_reg_t (Z80_REG_AF..Z80_REG_IR).
 * Pořadí v poli je dle z80_reg_t (= 0=AF, 1=BC, 2=DE, 3=HL,
 * 4=AF', 5=BC', 6=DE', 7=HL', 8=IX, 9=IY, 10=SP, 11=PC, 12=WZ,
 * 13=IR). Caller musí alokovat uint16_t[DBGAPI_REG_COUNT].
 */
#define DBGAPI_REG_COUNT 14

/* Parametr pro CMD_SET_REG */
typedef struct st_DBGAPI_REG_PARAM
{
    uint8_t reg_id; /* Identifikátor registru */
    uint16_t value; /* Nová hodnota */
} st_DBGAPI_REG_PARAM;

/* Parametr pro CMD_MEM_READ / CMD_MEM_WRITE */
typedef struct st_DBGAPI_MEM_PARAM
{
    uint16_t addr; /* Počáteční adresa */
    uint16_t len;  /* Délka bloku v bajtech */
    uint8_t *buf;  /* Buffer pro data (pro WRITE: vstupní data, pro READ: výstup) */
} st_DBGAPI_MEM_PARAM;

/* Parametr pro CMD_BP_ADD / CMD_BP_REMOVE */
typedef struct st_DBGAPI_BP_PARAM
{
    uint16_t addr; /* Adresa breakpointu */
    int id;        /* ID breakpointu (kladné číslo) */
} st_DBGAPI_BP_PARAM;

/* Parametr pro CMD_DASM */
typedef struct st_DBGAPI_DASM_PARAM
{
    uint16_t addr; /* Počáteční adresa disassembly */
    int count;     /* Počet instrukcí k disassemblování */
} st_DBGAPI_DASM_PARAM;

/* Výsledek CMD_DASM — jedna instrukce */
typedef struct st_DBGAPI_DASM_RESULT
{
    uint16_t addr;     /* Adresa instrukce */
    uint8_t bytes[4];  /* Bajty instrukce */
    int num_bytes;     /* Délka instrukce (1–4) */
    char mnemonic[64]; /* Textová mnemonika */
} st_DBGAPI_DASM_RESULT;

/**
 * @brief Doplňkový stav CPU pro CPU window (over GET_ALL_REGS).
 *
 * Drží interrupt flip-flops, IM mód, HALT stav, čekající přerušení,
 * EI delay flag, interní Q registr a cycle countery. Pole nejsou
 * pokryta v Z80_REG_* enumu - GET_ALL_REGS je nedoručuje.
 *
 * Update_mask je rezervováno pro budoucí SET_CPU_FLAGS (selektivní zápis
 * vybraných fieldů). V V0 (read-only refresh) se neevaluuje.
 *
 * @invariant im je vždy 0, 1 nebo 2 po dispatch.
 * @invariant iff1, iff2, halted, int_pending, nmi_pending, ei_delay
 *            jsou 0 nebo 1.
 */
typedef struct st_DBGAPI_CPU_FLAGS
{
    uint8_t iff1;          /**< Master interrupt enable (0/1) */
    uint8_t iff2;          /**< Shadow IFF1 - kopie pro NMI/RETN (0/1) */
    uint8_t im;            /**< Interrupt mode (0/1/2) */
    uint8_t halted;        /**< CPU v HALT instrukci (0/1) */
    uint8_t int_pending;   /**< Čekající INT (0/1) */
    uint8_t nmi_pending;   /**< Čekající NMI (0/1) */
    uint8_t ei_delay;      /**< EI delay flag (po EI 1 instrukce odklad) */
    uint8_t q;             /**< Interní Q registr (F z poslední ALU operace) */
    uint32_t total_cycles; /**< Celkové T-stavy od resetu */
    uint32_t cycles;       /**< T-stavy v aktuálním frame */
    int32_t op_tstate;     /**< T-stavy od začátku aktuální instrukce */
    uint16_t update_mask;  /**< Bity DBGAPI_CPU_FLAGS_UM_* - selektivní SET */
    uint8_t i_reg;         /**< Interrupt Vector register I (8-bit) */
    uint8_t r_reg;         /**< Memory Refresh register R (8-bit) */
} st_DBGAPI_CPU_FLAGS;

/**
 * @brief Bity update_mask pro DBGAPI_CMD_SET_CPU_FLAGS.
 *
 * Caller v UI naplní jen ty fieldy struct, které chce zapsat, a v
 * update_mask nastaví odpovídající bity. Handler v dbgapi.c projde mask
 * a aplikuje jen vyžádané změny. Ostatní fieldy struktury jsou ignorovány.
 *
 * Bezpečnost: zápis IFF/IM/I/R během běžící emulace je v principu race
 * (modifikuje CPU stav mezi instrukcemi). Caller (= UI) ručí za pause
 * stav (typicky přes dbg_autopause_silent před commit). Handler sám
 * neguarduje - běží v emu vlákně v safepointu mezi instrukcemi, takže
 * atomicita zápisu je zajištěna implicitně.
 */
#define DBGAPI_CPU_FLAGS_UM_IFF1 (1u << 0)  /**< Zapsat iff1 (0/1) */
#define DBGAPI_CPU_FLAGS_UM_IFF2 (1u << 1)  /**< Zapsat iff2 (0/1) */
#define DBGAPI_CPU_FLAGS_UM_IM   (1u << 2)  /**< Zapsat im (0/1/2 - jine hodnoty rejected) */
#define DBGAPI_CPU_FLAGS_UM_I    (1u << 3)  /**< Zapsat i_reg (8-bit) */
#define DBGAPI_CPU_FLAGS_UM_R    (1u << 4)  /**< Zapsat r_reg (8-bit) */

/**
 * @brief IM2 ISR vektor pro CPU window (jen platformy s PIO-Z80).
 *
 * Drží stav potřebný k dekódování, kam by Z80 skočil v případě IM 2
 * interruptu obslouženého PIO-Z80 (MZ-800, MZ-1500). Platforma bez
 * PIO-Z80 (MZ-700) vrátí `available = 0` a ostatní fieldy nejsou
 * definované.
 *
 * Composition v IM 2:
 *   - I  ............ horní byte adresy
 *   - vector_byte ... dolní byte (PIO-Z80 dá na bus přes IORQ/INT)
 *   - isr_table_addr = (I << 8) | vector_byte
 *   - isr_target_addr = MEM[isr_table_addr] | (MEM[isr_table_addr+1] << 8)
 *
 * Vector_byte je definován jen pokud `pio_irq_pending == 1`. Bez
 * čekajícího interruptu PIO-Z80 sice vrátí 0x00 (viz pioz80.c
 * `pioz80_interrupt_ack_im2_cb`), ale je to implementačně specifické -
 * UI to musí signalizovat ("no IRQ pending").
 *
 * PIO-Z80 IRQ chain priorita: port A nad port B (= pioz80.c iteruje
 * `for port_id = PORT_A; port_id < PORT_COUNT`). pio_source signalizuje,
 * který port by vektor poskytl.
 *
 * @invariant Pokud available == 0, ostatní pole se nezkoumají.
 * @invariant im in {0, 1, 2}.
 * @invariant pio_source in {0=PIO-A, 1=PIO-B}; význam jen při
 *            pio_irq_pending == 1.
 */
typedef struct st_DBGAPI_IM2_VECTOR
{
    uint8_t  available;        /**< 0 = arch nemá PIO-Z80 (MZ-700) */
    uint8_t  im;               /**< Aktuální IM mód (0/1/2) */
    uint8_t  i_register;       /**< Horní byte vektoru = registr I */
    uint8_t  vector_byte;      /**< Dolní byte = co by PIO-Z80 dal na bus */
    uint16_t isr_table_addr;   /**< (I << 8) | vector_byte */
    uint16_t isr_target_addr;  /**< Dereferenced = MEM[isr_table_addr] little-endian */
    uint8_t  pio_irq_pending;  /**< 0/1 - PIO-Z80 IRQ pending? */
    uint8_t  pio_source;       /**< 0=PIO-A, 1=PIO-B (jen pokud pending) */
} st_DBGAPI_IM2_VECTOR;


/**
 * @brief Pozice rastru a frame counter pro CPU window "Cycles & raster".
 *
 * Drží aktuální stav GDG rasteru a Z80 cycle counterů, aby UI mohlo
 * vykreslit kde právě paprsek stojí a kolik T-states uplynulo.
 *
 * @field frame_number  Pořadové číslo aktuálního snímku
 *                       (= g_gdg.total_elapsed.screens).
 * @field scanline      Aktuální raster row = g_gdg.beam_row
 *                       (0..VIDEO_SCREEN_HEIGHT-1, pro MZ-800 PAL 312).
 * @field column_pixel  Pixel sloupec v aktuálním scanline
 *                       (= VIDEO_GET_SCREEN_COL z `g_gdg.total_elapsed.ticks`).
 * @field total_cycles  Kumulativní T-stavy Z80 od resetu
 *                       (= cpu->total_cycles).
 * @field frame_cycles  T-stavy v rámci aktuálního snímku
 *                       (= cpu->cycles).
 */
typedef struct st_DBGAPI_RASTER_POS
{
    uint32_t frame_number;
    uint16_t scanline;
    uint16_t column_pixel;
    uint32_t total_cycles;
    uint32_t frame_cycles;
} st_DBGAPI_RASTER_POS;


/**
 * @brief Poslední dokončená Z80 instrukce z debugger history ringu.
 *
 * Vrací nejnovější záznam g_debugger_history.row[position & POSMASK] +
 * délku instrukce dopočítanou disassemblerem. Pokud history není
 * aktivní (= TEST_DEBUGGER_CPUHIST_ACTIVE = 0) nebo zatím nezaznamenala
 * žádnou instrukci, vrací valid=0.
 *
 * @field valid        1 = záznam je platný, 0 = history prázdná/neaktivní
 * @field addr         Adresa instrukce (PC v okamžiku M1 startu)
 * @field bytes        Bajty instrukce (až 4) - obsah row.byte[]
 * @field length       Délka instrukce (1..4), dopočítaná z disassembleru
 */
typedef struct st_DBGAPI_LAST_INSTR
{
    uint8_t  valid;
    uint8_t  length;
    uint16_t addr;
    uint8_t  bytes[4];
} st_DBGAPI_LAST_INSTR;


/**
 * @brief Bitová pole pro CMD_GET_CPU_PANEL_BATCH which-mask.
 *
 * Caller alokuje uint32_t mask, OR'd flagy podle toho které sekce panelu
 * potřebují aktuální data, a předá pres data_ptr. Emu handler vrací jen
 * vyžádané fieldy do st_DBGAPI_CPU_PANEL_BATCH (per-section valid flagy
 * indikují co bylo skutečně naplněno).
 *
 * Účel: per-section gating - pokud je v UI sekce (IM2/raster/last_instr)
 * collapsed, nemusí jít do baseline batch payloadu, čímž se zredukuje
 * práce na emu straně.
 *
 * Registry + flags se ptají vždy (= core panel, levný read), proto pro
 * ně samostatný flag neexistuje - jsou součástí každého batch volání.
 */
#define DBGAPI_CPU_PANEL_WANT_IM2          (1u << 0) /**< IM2 ISR vector */
#define DBGAPI_CPU_PANEL_WANT_RASTER       (1u << 1) /**< Cycles & raster pos */
#define DBGAPI_CPU_PANEL_WANT_LAST_INSTR   (1u << 2) /**< Last instruction */


/**
 * @brief Agregovaný snapshot dat pro CPU panel v jediném round-tripu.
 *
 * Eliminuje 5 separátních sync requestů (= 5 čekání na emu safepoint
 * při běžící emulaci) za 1 batch call. UI vlákno tak ztratí jen 1× čas
 * na safepoint místo 5×, což odstraňuje viditelný lag CPU panelu při
 * běžícím emulátoru a zrychluje paint i v pauze (= drain zpoždění
 * proti UI tickeru se kumuluje pomaleji).
 *
 * Caller:
 *  - Alokuje strukturu (typicky stack lokál)
 *  - Předá pres result_ptr
 *  - Předá uint32_t mask přes data_ptr (DBGAPI_CPU_PANEL_WANT_*)
 *  - Po návratu čte regs + flags vždy, ostatní jen pokud
 *    odpovídající *_valid je 1
 *
 * Per-section valid flagy reflektují obě podmínky:
 *  1. Sekce byla vyžádána ve which-mask
 *  2. Handler ji úspěšně naplnil (analogie current per-cmd success
 *     samostatných GET_* příkazů)
 *
 * @invariant regs[] je vždy naplněn, regs_valid = 1.
 * @invariant flags je vždy naplněn, flags_valid = 1.
 * @invariant Pokud byl bit DBGAPI_CPU_PANEL_WANT_X = 0 ve which masce,
 *            x_valid bude 0 a obsah x je nedefinovaný (zachová caller
 *            cache).
 */
typedef struct st_DBGAPI_CPU_PANEL_BATCH
{
    uint32_t which;                 /**< Kopie which-masky (echo pro debug) */

    /* Vždy naplněné (core panel) */
    uint16_t regs[14];              /**< Z80 registry, indexace dle z80_reg_t. Velikost = DBGAPI_REG_COUNT */
    st_DBGAPI_CPU_FLAGS flags;      /**< IFF/IM/HALT/.../cycles */
    uint8_t  regs_valid;            /**< 1 = regs naplněny */
    uint8_t  flags_valid;           /**< 1 = flags naplněny */

    /* V3.1 core fieldy (vždy naplněné, levné čtení). Umístěné mimo
     * st_DBGAPI_RASTER_POS aby UI mohlo počítat Frame cyc / User cyc
     * i pokud "Cycles & raster" sekce není expandovaná (bez ní by
     * raster substruct zůstal valid=0 a frame_number nedostupný). */
    uint32_t frame_number;          /**< Pořadové číslo aktuálního snímku (g_gdg.total_elapsed.screens) */
    uint32_t user_cycle_origin;     /**< Snapshot total_cycles z g_debugger.user_cycle_origin pro UI Frame/User cyc */

    /* Volitelné (per-section gating) */
    st_DBGAPI_IM2_VECTOR im2;       /**< Naplněno jen pokud WANT_IM2 a im2_valid=1 */
    uint8_t              im2_valid;
    st_DBGAPI_RASTER_POS raster;    /**< Naplněno jen pokud WANT_RASTER a raster_valid=1 */
    uint8_t              raster_valid;
    st_DBGAPI_LAST_INSTR last_instr;/**< Naplněno jen pokud WANT_LAST_INSTR a last_instr_valid=1 */
    uint8_t              last_instr_valid;

    /* === V3.3 core fieldy: PIO-Z80 interrupt vector + ISR target ===
     *
     * Vždy plněné při HAVE_PIOZ80 (= MZ-800, MZ-1500). Pro MZ-700
     * (HAVE_PIOZ80 == 0) zůstává has_pioz80 = 0 a ostatní fieldy = 0.
     *
     * Účel: CPU panel zobrazuje pod IFF2 dva řádky VECA/ISRA a VECB/ISRB.
     * UI sekce existuje jen pro architektury s PIO-Z80 - has_pioz80
     * řídí viditelnost.
     *
     * Composition (pro každý port):
     *   - VEC*  = (cpu->i << 8) | (port[*].interrupt_vector & 0xFE)
     *   - ISR*  = MEM[VEC*] | (MEM[VEC*+1] << 8)
     *
     * pio_int_vec_a/b drží surovou hodnotu interrupt_vector registru
     * jednotlivých portů (bez & 0xFE maskování) - UI ji nepotřebuje k
     * zobrazení (VEC* už je hotová), ale slouží jako baseline při edit
     * commitu k otestování že se hodnota nezměnila mezi refreshem a
     * submitem (zatím nepoužité, rezerva). */
    uint8_t  has_pioz80;            /**< 1 = arch má PIO-Z80, plní VEC/ISR */
    uint8_t  pio_int_vec_a;         /**< Raw g_pioz80.port[A].interrupt_vector */
    uint8_t  pio_int_vec_b;         /**< Raw g_pioz80.port[B].interrupt_vector */
    uint16_t veca;                  /**< (i_reg<<8) | (pio_int_vec_a & 0xFE) */
    uint16_t vecb;                  /**< (i_reg<<8) | (pio_int_vec_b & 0xFE) */
    uint16_t isra;                  /**< MEM[veca] little-endian */
    uint16_t isrb;                  /**< MEM[vecb] little-endian */
} st_DBGAPI_CPU_PANEL_BATCH;


/**
 * @brief Parametr pro CMD_SET_PIOZ80_INTERRUPT_VECTOR.
 *
 * Zápis hodnoty interrupt_vector registru pro vybraný port PIO-Z80
 * (A nebo B). Handler v dbgapi.c maskuje bit 0 ven (= IVW spec: bit 0
 * vždy 0). Pokud port_id mimo {0, 1}, handler vrátí success = false.
 *
 * Na MZ-700 (HAVE_PIOZ80 == 0) handler vrátí success = false (= PIO-Z80
 * v této architektuře neexistuje).
 *
 * @field port_id      0 = PIOZ80_PORT_A, 1 = PIOZ80_PORT_B
 * @field vector_byte  Nová hodnota (bit 0 se vynuluje handlerem)
 */
typedef struct st_DBGAPI_PIOZ80_VEC_PARAM
{
    uint8_t port_id;
    uint8_t vector_byte;
} st_DBGAPI_PIOZ80_VEC_PARAM;

/**
 * @brief Parametr pro CMD_MEM_WRITE_CHECKED.
 *
 * Zápis bloku do paměti s ověřením, že každá dotčená adresa je v
 * zapisovatelném regionu. Pokud kterákoliv adresa padne do ROM
 * (ROM_LOW/HIGH), CG-ROM, prohibited nebo unmapped (případně MZ-800
 * native VRAM_I/II - viz handler), žádný bajt se nezapíše a handler
 * vrátí success = 0 + first_failed_addr.
 *
 * Použití: editor ISR target v CPU panelu chce zapsat 2 bajty na adresu
 * VEC*. Pokud VEC* ukazuje do ROM (typické tabulky vektorů!), zápis by
 * tiše propadl - banking memory_write_byte ROM regiony ignoruje. Místo
 * toho UI dostane explicit failure a může uživateli ukázat warning.
 *
 * @field addr               Počáteční adresa zápisu
 * @field length             Počet bajtů
 * @field data               Vstupní data (caller vlastní; handler jen čte)
 * @field success            (OUT) 1 = celý blok zapsán, 0 = žádný bajt zapsán
 * @field first_failed_addr  (OUT) Adresa, na které check selhal (pouze při success=0)
 * @field first_failed_kind  (OUT) en_MEMMAP_REGION_KIND padlé adresy
 *                                 (uint8_t, pouze při success=0)
 */
typedef struct st_DBGAPI_MEM_WRITE_CHECKED_PARAM
{
    uint16_t addr;
    uint16_t length;
    const uint8_t *data;
    uint8_t  success;
    uint16_t first_failed_addr;
    uint8_t  first_failed_kind;
} st_DBGAPI_MEM_WRITE_CHECKED_PARAM;

/**
 * @brief Typ disasm-back dekódování pro jeden slot stacku (V3).
 *
 * Určuje, jakou instrukci handler předpokládá těsně před danou
 * 16-bit hodnotou, tedy zda by hodnota mohla být návratovou adresou
 * po CALL/RST. Heuristika - bez záruky, že před adresou opravdu CALL
 * leží (= mohou to být náhodná data).
 */
typedef enum en_DBGAPI_STACK_DECODE_TYPE
{
    DBGAPI_STACK_DECODE_NONE    = 0, /* Žádný match (= prázdná decode buňka) */
    DBGAPI_STACK_DECODE_CALL    = 1, /* CALL nn (opcode 0xCD) na W-3 */
    DBGAPI_STACK_DECODE_CALL_CC = 2, /* CALL cc,nn (opcody C4/CC/D4/DC/E4/EC/F4/FC) na W-3 */
    DBGAPI_STACK_DECODE_RST     = 3, /* RST n (opcody C7/CF/D7/DF/E7/EF/F7/FF) na W-1 */
} en_DBGAPI_STACK_DECODE_TYPE;


/**
 * @brief Informace o jednom decoded slotu stacku (V3, disasm-back heuristika).
 *
 * Vyplněno handlerem CMD_STACK_DUMP, pokud byl předán `decode_buf` pole
 * o velikosti `decode_count`. Pole je indexované per řádek hex dump
 * tabulky (= shoda s buf semantikou: index 0 = nejvyšší adresa).
 *
 * Decode platí jen pro word-oriented zobrazení (lichý SP = byte mode,
 * decode_buf se nevyplňuje, type zůstane NONE).
 *
 * @field type    Druh detekované call/rst instrukce (viz enum). NONE = ne.
 * @field target  Cílová adresa CALL/RST (= operand 16-bit nn nebo RST n*8).
 *                Platí jen při type != NONE.
 * @field opcode  Hodnota opcode bajtu nalezeného před návratovou adresou
 *                (= W-3 pro CALL, W-1 pro RST). Slouží UI pro tooltipy.
 */
typedef struct st_DBGAPI_STACK_DECODE_INFO
{
    uint8_t  type;
    uint8_t  opcode;
    uint16_t target;
} st_DBGAPI_STACK_DECODE_INFO;


/**
 * @brief Parametr pro CMD_STACK_DUMP - hex dump paměti zásobníku.
 *
 * Handler vyplní `buf` čtením paměti přes debugger_memory_read_byte
 * (banking-aware, bez side effects).
 *
 * Adresování okna - dva režimy podle `lines_above`:
 *  - `lines_above > 0` (= SP-anchored mode, preferovaný pro stack panel):
 *    Handler ignoruje vstupní `addr` a sám spočítá
 *    `base = sp + lines_above * 2`, takže okno je vždy konzistentní
 *    s aktuálním SP v témž ticku (= žádný 1-tick lag oproti `sp_now`).
 *    Vypočtenou base zapíše zpět do `addr` jako OUT hodnotu - UI tak
 *    má jistotu kde okno začíná. Buffer je naplněn DESC: `buf[0]` je
 *    bajt na adrese `base`, `buf[i]` na adrese `base - i`. Velikost
 *    okna je dána `len`.
 *  - `lines_above == 0` (= absolute mode, legacy): Handler použije
 *    `addr` jak ji caller předal a naplní buf ASC od této adresy
 *    (`buf[i] = read(addr + i)`). Vhodné pro fixní inspekci konkrétní
 *    adresy bez návaznosti na SP.
 *
 * Dodatečně handler do `sp_now` zapíše aktuální hodnotu SP v okamžiku
 * zpracování (= UI nemusí dělat zvláštní GET_REG round-trip). Bit
 * `sp_odd` říká, zda je SP lichý (= UI přepne na byte-oriented zobrazení
 * místo word-oriented default).
 *
 * Buffer `buf` vlastní caller (= UI alokuje pole o velikosti `len`).
 * Handler ho jen naplňuje, neuvolňuje.
 *
 * Limity:
 *  - len bývá 256 B (= V0 hex dump 40 řádků × 2 B + rezerva). Větší
 *    hodnoty handler neodmítá, ale UI by si měla cap nastavit (= zbytečně
 *    drahá round-trip data).
 *  - addr je 16-bit, adresování přes banking respektuje aktuální mapping.
 *    Wrap kolem 0xFFFF (= addr+len > 0xFFFF) přečte z 0x0000 (= dáno
 *    debugger_memory_read_byte cast na uint16_t).
 *
 * @field addr        (INOUT) Počáteční adresa okna (= nejvyšší zobrazená;
 *                    stack roste dolů, tabulka řazena addr DESC). V SP-anchored
 *                    mode (lines_above > 0) je hodnota přepsána handlerem.
 *                    V absolute mode (lines_above == 0) je vstupní hodnota
 *                    použita beze změny.
 * @field len         Počet bajtů ke čtení.
 * @field lines_above (IN) Pokud > 0, SP-anchored mode: handler spočítá
 *                    `addr = sp + lines_above * 2`. Pokud == 0, absolute
 *                    mode (= legacy chování).
 * @field buf         Buffer alokovaný caller-em pro výstupní data.
 * @field sp_now      (OUT) Aktuální hodnota SP v okamžiku zpracování.
 * @field sp_odd      (OUT) 1 = SP je lichý (UI fallback na byte-oriented).
 * @field decode_buf  (INOUT) Volitelné pole st_DBGAPI_STACK_DECODE_INFO o
 *                    velikosti `decode_count`. Pokud != NULL a SP je sudý
 *                    (word-mode), handler pro každý řádek tabulky (index
 *                    odpovídá indexu v `buf` v krocích step=2) zjistí
 *                    word kandidát `W = mem[addr] | (mem[addr-1] << 8)` a
 *                    udělá disasm-back lookup (W-3 pro CALL, W-1 pro RST).
 *                    Při NONE se vyplní type=NONE. Pokud NULL, handler
 *                    decode přeskočí. Caller vlastní buffer.
 * @field decode_count (IN) Velikost pole `decode_buf` (= obvykle počet řádků
 *                    hex dump tabulky). Pokud > len/2, handler omezí na len/2.
 */
typedef struct st_DBGAPI_STACK_DUMP_PARAM
{
    uint16_t addr;
    uint16_t len;
    uint16_t lines_above;
    uint8_t *buf;
    uint16_t sp_now;
    uint8_t  sp_odd;
    st_DBGAPI_STACK_DECODE_INFO *decode_buf;
    uint16_t decode_count;
} st_DBGAPI_STACK_DUMP_PARAM;


/**
 * @brief Snapshot info o jednom stack regionu pro UI.
 *
 * Cache friendly POD - UI alokuje pole o velikosti DBGAPI_STACK_REGIONS_MAX
 * a handler v dbgapi.c ho naplní podle aktuálního g_stack_regions[].
 *
 * @field name                 Jméno regionu ('\0'-terminated, max 32 B).
 * @field base                 Vrchol (nejvyšší adresa).
 * @field limit                Dno (nejnižší adresa).
 * @field watermark            Nejnižší SP zaznamenaný v regionu.
 * @field push_count           Počet PUSH-like událostí v regionu.
 * @field pop_count            Počet POP-like událostí v regionu.
 * @field current_sp_in_region 1 = aktuální SP padá do <limit..base>.
 */
typedef struct st_DBGAPI_STACK_REGION_INFO
{
    char     name[ 32 ];
    uint16_t base;
    uint16_t limit;
    uint16_t watermark;
    uint64_t push_count;
    uint64_t pop_count;
    uint8_t  current_sp_in_region;
} st_DBGAPI_STACK_REGION_INFO;


/**
 * @brief Maximální počet regionů přenosný v jednom LIST snapshotu.
 *
 * Musí odpovídat STACK_REGIONS_MAX v stack_regions.h (= 8). Duplikace
 * tady kvůli izolaci dbgapi_cmdrq.h od EMU-specifických headerů
 * (caller UI nemusí includovat stack_regions.h jen kvůli velikosti
 * pole).
 */
#define DBGAPI_STACK_REGIONS_MAX 8


/**
 * @brief Parametr pro CMD_STACK_REGIONS_LIST.
 *
 * Caller alokuje strukturu a předá pres data_ptr. Handler v dbgapi.c
 * naplní `count` (= aktuální počet platných regionů) a `regions[0..count-1]`
 * snapshot daty z g_stack_regions[]. Pole nad count je nedotčené.
 *
 * Navíc handler vyplní `sp_now` (= aktuální SP) - UI s tím dopočte
 * "current_sp_in_region" markery v dropdownu.
 *
 * @field count      (OUT) Počet platných regionů (0..DBGAPI_STACK_REGIONS_MAX).
 * @field sp_now     (OUT) Aktuální SP v okamžiku snapshotu.
 * @field regions    (OUT) Pole snapshotů jednotlivých regionů.
 */
typedef struct st_DBGAPI_STACK_REGIONS_LIST_PARAM
{
    int                          count;
    uint16_t                     sp_now;
    st_DBGAPI_STACK_REGION_INFO  regions[ DBGAPI_STACK_REGIONS_MAX ];
} st_DBGAPI_STACK_REGIONS_LIST_PARAM;


/**
 * @brief Parametr pro CMD_STACK_REGIONS_ADD.
 *
 * Validace name + base/limit dělá stack_regions_add (= core API).
 * Handler předá výsledek (index nebo -1) zpět přes `result_index`.
 *
 * @field name             (IN)  Jméno regionu, '\0'-terminated.
 * @field base             (IN)  Vrchol regionu.
 * @field limit            (IN)  Dno regionu (base > limit).
 * @field result_index     (OUT) Index 0..MAX-1 při úspěchu, -1 při chybě.
 */
typedef struct st_DBGAPI_STACK_REGIONS_ADD_PARAM
{
    char     name[ 32 ];
    uint16_t base;
    uint16_t limit;
    int      result_index;
} st_DBGAPI_STACK_REGIONS_ADD_PARAM;


/**
 * @brief Parametr pro CMD_STACK_REGIONS_EDIT (V7).
 *
 * Edituje existující region na indexu @c idx. Validace + overlap detekce
 * proti ostatním regionům probíhá v stack_regions_edit (= core API).
 * Při úspěšné editaci handler resetuje watermark + counters (= staré stats
 * neplatí pro nový rozsah).
 *
 * @field idx     (IN)  Index editovaného regionu (0..count-1).
 * @field name    (IN)  Nový label, '\0'-terminated.
 * @field base    (IN)  Nový vrchol regionu.
 * @field limit   (IN)  Nové dno (base > limit).
 * @field success (OUT) true = editace OK, false = invalid args / duplicate
 *                      name / overlap / idx mimo rozsah. Stejnou informaci
 *                      handler signalizuje i přes rq->success (= konzistentní
 *                      s ostatními STACK_REGIONS_* příkazy).
 */
typedef struct st_DBGAPI_STACK_REGIONS_EDIT_PARAM
{
    uint8_t  idx;
    char     name[ 32 ];
    uint16_t base;
    uint16_t limit;
    bool     success;
} st_DBGAPI_STACK_REGIONS_EDIT_PARAM;


/**
 * @brief Parametr pro CMD_STACK_REGIONS_REMOVE a CMD_STACK_REGIONS_RESET_WATERMARK.
 *
 * Sdílená struktura - oba příkazy potřebují jen index. Úspěch handler
 * signalizuje přes rq->success.
 *
 * @field index  (IN) Index regionu k odebrání / resetu.
 */
typedef struct st_DBGAPI_STACK_REGIONS_REMOVE_PARAM
{
    int index;
} st_DBGAPI_STACK_REGIONS_REMOVE_PARAM;


/**
 * @brief Vzorek SP history pro přenos UI <-> EMU.
 *
 * Stejný layout jako st_STACK_HISTORY_SAMPLE v stack_history.h, ale
 * duplikovaný tady aby UI nemuselo includovat emu-specifický header.
 *
 * @field cycles  Snapshot cpu->total_cycles v okamžiku samplování.
 * @field sp      Hodnota SP.
 */
typedef struct st_DBGAPI_STACK_HISTORY_SAMPLE
{
    uint32_t cycles;
    uint16_t sp;
    uint16_t _pad;
} st_DBGAPI_STACK_HISTORY_SAMPLE;


/**
 * @brief Maximální počet vzorků přenosný v jednom GET snapshotu.
 *
 * Musí odpovídat STACK_HISTORY_SIZE v stack_history.h (= 4096). Duplikace
 * tady kvůli izolaci dbgapi_cmdrq.h od EMU-specifických headerů.
 */
#define DBGAPI_STACK_HISTORY_MAX 4096


/**
 * @brief Parametr pro CMD_STACK_HISTORY_ENABLE.
 *
 * Při enable = 0 handler navíc vyprázdní ring buffer (= clean state pro
 * další zapnutí). Při enable = 1 jen nastaví flag.
 *
 * @field enable  (IN) 0 = vypnout recording, 1 = zapnout.
 */
typedef struct st_DBGAPI_STACK_HISTORY_ENABLE_PARAM
{
    uint8_t enable;
} st_DBGAPI_STACK_HISTORY_ENABLE_PARAM;


/**
 * @brief Parametr pro CMD_STACK_HISTORY_GET - bulk snapshot ring bufferu.
 *
 * Caller alokuje pole `samples` o velikosti `max_count`, předá pres
 * data_ptr a po návratu čte `count` (= kolik vzorků handler skutečně
 * naplnil). Vzorky jsou v pořadí "oldest first" (= samples[0] je nejstarší
 * zaznamenaný vzorek v okénku, samples[count-1] nejnovější).
 *
 * Doplňkové výstupy:
 *  - `active`  = aktuální stav recording flagu (= UI ho používá pro
 *               synchronizaci s checkboxem v sticky headeru).
 *  - `slope`   = lineární regrese SP/cycle přes posledních `slope_window`
 *               vzorků (= "stack creep" indikátor). Záporná hodnota = SP
 *               v čase klesá.
 *  - `slope_window` (IN) = velikost okénka pro slope; pokud > count, použije
 *               se count.
 *
 * @field max_count     (IN)  Velikost samples pole.
 * @field slope_window  (IN)  Window pro slope (typicky 256).
 * @field count         (OUT) Počet skutečně naplněných vzorků (0..max_count).
 * @field active        (OUT) 1 = recording aktivní, 0 = vypnutý.
 * @field slope         (OUT) Slope (SP/cycle). 0 = nedostatek dat.
 * @field samples       (OUT) Pole vzorků, caller-allocated.
 */
typedef struct st_DBGAPI_STACK_HISTORY_GET_PARAM
{
    uint32_t max_count;
    uint32_t slope_window;
    uint32_t count;
    uint8_t  active;
    float    slope;
    st_DBGAPI_STACK_HISTORY_SAMPLE *samples;
} st_DBGAPI_STACK_HISTORY_GET_PARAM;


/**
 * @brief Plochý snapshot st_BPT pro CMD_BP_UPDATE / CMD_BP_CREATE_WITH_INIT.
 *
 * Selektivní zápis: caller naplní jen ty fieldy, které chce přepsat, a v
 * `update_mask` nastaví odpovídající bity DBGAPI_BP_UM_*. Handler v
 * dbgapi.c projde mask a aplikuje jen vyžádané změny voláním existujících
 * `breakpoints_set_*()` setterů (= single source of truth pro mutaci
 * `g_breakpoints`).
 *
 * Pro CMD_BP_UPDATE: `id` musí ukazovat na existující BP. Pokud
 * `breakpoints_find_by_id(id) == NULL`, handler vrátí success = false a
 * žádnou změnu neaplikuje.
 *
 * Pro CMD_BP_CREATE_WITH_INIT: caller předává `id = -1` na vstupu, handler
 * volá `breakpoints_add_auto(addr, name, parent)` a po úspěchu naplní `id`
 * přiděleným ID + aplikuje update_mask. Pokud `breakpoints_add_auto`
 * selže, handler vrátí success = false a id zůstane -1.
 *
 * String lifetime: `name`, `event_name`, `expr`, `action` jako `const
 * char*` mají lifetime trvání sync cmd (= UI strana drží alokaci do
 * návratu `dbgapi_ui_submit_cmd_sync`). Handler uvnitř setterů provede
 * `g_strdup`. NULL = "nastav prázdnou hodnotu" (= legitimní pro
 * action/expr/name = clear).
 *
 * Enum fieldy uloženy jako uint8_t pro minimalizaci závislosti
 * dbgapi_cmdrq.h na breakpoints.h. Handler valid-checkuje rozsah +
 * castuje na konkrétní enum při volání setteru. Mimo rozsah = success
 * false (= nemodifikuje, vrací chybu).
 *
 * Color fieldy bg_rgb / fg_rgb jsou aplikovány společně přes
 * `breakpoints_set_colors(bg, fg)` - 1 bit UM_COLORS pokrývá oba.
 *
 * Match modes a IM filter fieldy uplatňují stejnou logiku co UI: aplikuj
 * všechny vyžádané pole bez ohledu na BPT_TYPE (= zachová hodnoty při
 * přepnutí typu - viz `working_copy_apply` komentář v bpt_edit_panel.cpp).
 *
 * @invariant Pokud update_mask == 0, handler nic neaplikuje a vrátí
 *            success = true (= no-op je validní).
 */
typedef struct st_DBGAPI_BP_UPDATE_PARAM
{
    /* === Cíl / výsledek === */
    int id;                  /**< UPDATE: vstupní ID existujícího BP. CREATE: -1 vstup, naplní handler. */
    uint64_t update_mask;    /**< Bity DBGAPI_BP_UM_* - které fieldy aplikovat */

    /* === Identifikace (5 bitů) === */
    bool enabled;            /**< UM_ENABLED */
    bool auto_name;          /**< UM_AUTO_NAME */
    const char *name;        /**< UM_NAME (NULL = clear) */
    uint32_t bg_rgb;         /**< UM_COLORS - aplikuje s fg_rgb společně */
    uint32_t fg_rgb;         /**< UM_COLORS */
    int parent;              /**< UM_PARENT (-1 = root, jinak group ID) */

    /* === Smart core (14 bitů) === */
    uint8_t type;            /**< UM_TYPE - cast na en_BPT_TYPE */
    uint16_t addr;           /**< UM_ADDR */
    uint16_t addr_end;       /**< UM_ADDR_END */
    uint8_t zone;            /**< UM_ZONE - cast na en_BP_ZONE */
    uint8_t bank_id;         /**< UM_BANK_ID */
    uint16_t port;           /**< UM_PORT */
    const char *event_name;  /**< UM_EVENT_NAME (NULL = clear) */
    uint8_t event_trigger;   /**< UM_EVENT_TRIGGER - cast na en_BP_EVENT_TRIGGER */
    uint16_t sp_threshold;   /**< UM_SP_THRESHOLD */
    const char *expr;        /**< UM_EXPR (NULL = clear) */
    const char *action;      /**< UM_ACTION (NULL = clear) */
    uint32_t hit_count;      /**< UM_HIT_COUNT */
    uint32_t skip_count;     /**< UM_SKIP_COUNT */
    bool edge_triggered;     /**< UM_EDGE_TRIGGERED */

    /* === Match modes (11 bitů) === */
    uint8_t addr_match_mode; /**< UM_ADDR_MATCH_MODE - cast na en_BP_MATCH_MODE */
    uint16_t addr_mask;      /**< UM_ADDR_MASK */
    uint8_t port_match_mode; /**< UM_PORT_MATCH_MODE - cast na en_BP_MATCH_MODE */
    uint16_t port_end;       /**< UM_PORT_END */
    uint16_t port_mask;      /**< UM_PORT_MASK */
    uint8_t port_mode;       /**< UM_PORT_MODE - cast na en_BP_PORT_MODE */
    uint8_t bank_match_mode; /**< UM_BANK_MATCH_MODE - cast na en_BP_MATCH_MODE */
    uint8_t bank_id_end;     /**< UM_BANK_ID_END */
    uint8_t bank_id_mask;    /**< UM_BANK_ID_MASK */
    uint8_t sp_mode;         /**< UM_SP_MODE - cast na en_BP_SP_MODE */
    uint16_t sp_upper;       /**< UM_SP_UPPER */

    /* === IRQ A8 vector / ISR filter (8 bitů) === */
    bool im2_vector_enabled;        /**< UM_IM2_VECTOR_FILTER s im2_vector_addr */
    uint16_t im2_vector_addr;       /**< UM_IM2_VECTOR_FILTER */
    uint8_t im2_vector_match_mode;  /**< UM_IM2_VECTOR_MATCH_MODE - cast na en_BP_MATCH_MODE */
    uint16_t im2_vector_addr_end;   /**< UM_IM2_VECTOR_ADDR_END */
    uint16_t im2_vector_mask;       /**< UM_IM2_VECTOR_MASK */
    bool im2_isr_enabled;           /**< UM_IM2_ISR_FILTER s im2_isr_addr */
    uint16_t im2_isr_addr;          /**< UM_IM2_ISR_FILTER */
    uint8_t im2_isr_match_mode;     /**< UM_IM2_ISR_MATCH_MODE - cast na en_BP_MATCH_MODE */
    uint16_t im2_isr_addr_end;      /**< UM_IM2_ISR_ADDR_END */
    uint16_t im2_isr_mask;          /**< UM_IM2_ISR_MASK */

    /* === IRQ A8.5 IM discriminator + RST filter (4 bity) === */
    bool im0_enabled;        /**< UM_IM0_ENABLED */
    bool im1_enabled;        /**< UM_IM1_ENABLED */
    bool im2_enabled;        /**< UM_IM2_ENABLED */
    uint8_t im0_rst_mask;    /**< UM_IM0_RST_MASK */

    /* === IRQ_SIG (1 bit) === */
    uint8_t irq_sig_source_mask; /**< UM_IRQ_SIG_SOURCE_MASK */
} st_DBGAPI_BP_UPDATE_PARAM;

/* ============================================================================
 * Bity update_mask pro CMD_BP_UPDATE / CMD_BP_CREATE_WITH_INIT.
 *
 * Caller OR-uje vybrané bity do update_mask. Handler iteruje bity v
 * pořadí a volá odpovídající breakpoints_set_*() setter. Nezávisle aplikované
 * pole (= 1 bit, 1 setter) má 1:1 mapping. Compound bits (COLORS,
 * IM2_VECTOR_FILTER, IM2_ISR_FILTER) volají setter beroucí 2 argumenty.
 *
 * Bitová pozice je stabilní součást ABI - nové fieldy přidávat na konec
 * (= bit 43+). NEpřemapovávat existující bity.
 * ============================================================================ */

/* Identifikace */
#define DBGAPI_BP_UM_ENABLED            (UINT64_C(1) << 0)
#define DBGAPI_BP_UM_AUTO_NAME          (UINT64_C(1) << 1)
#define DBGAPI_BP_UM_NAME               (UINT64_C(1) << 2)
#define DBGAPI_BP_UM_COLORS             (UINT64_C(1) << 3)  /**< bg_rgb + fg_rgb */
#define DBGAPI_BP_UM_PARENT             (UINT64_C(1) << 4)

/* Smart core */
#define DBGAPI_BP_UM_TYPE               (UINT64_C(1) << 5)
#define DBGAPI_BP_UM_ADDR               (UINT64_C(1) << 6)
#define DBGAPI_BP_UM_ADDR_END           (UINT64_C(1) << 7)
#define DBGAPI_BP_UM_ZONE               (UINT64_C(1) << 8)
#define DBGAPI_BP_UM_BANK_ID            (UINT64_C(1) << 9)
#define DBGAPI_BP_UM_PORT               (UINT64_C(1) << 10)
#define DBGAPI_BP_UM_EVENT_NAME         (UINT64_C(1) << 11)
#define DBGAPI_BP_UM_EVENT_TRIGGER      (UINT64_C(1) << 12)
#define DBGAPI_BP_UM_SP_THRESHOLD       (UINT64_C(1) << 13)
#define DBGAPI_BP_UM_EXPR               (UINT64_C(1) << 14)
#define DBGAPI_BP_UM_ACTION             (UINT64_C(1) << 15)
#define DBGAPI_BP_UM_HIT_COUNT          (UINT64_C(1) << 16)
#define DBGAPI_BP_UM_SKIP_COUNT         (UINT64_C(1) << 17)
#define DBGAPI_BP_UM_EDGE_TRIGGERED     (UINT64_C(1) << 18)

/* Match modes */
#define DBGAPI_BP_UM_ADDR_MATCH_MODE    (UINT64_C(1) << 19)
#define DBGAPI_BP_UM_ADDR_MASK          (UINT64_C(1) << 20)
#define DBGAPI_BP_UM_PORT_MATCH_MODE    (UINT64_C(1) << 21)
#define DBGAPI_BP_UM_PORT_END           (UINT64_C(1) << 22)
#define DBGAPI_BP_UM_PORT_MASK          (UINT64_C(1) << 23)
#define DBGAPI_BP_UM_PORT_MODE          (UINT64_C(1) << 24)
#define DBGAPI_BP_UM_BANK_MATCH_MODE    (UINT64_C(1) << 25)
#define DBGAPI_BP_UM_BANK_ID_END        (UINT64_C(1) << 26)
#define DBGAPI_BP_UM_BANK_ID_MASK       (UINT64_C(1) << 27)
#define DBGAPI_BP_UM_SP_MODE            (UINT64_C(1) << 28)
#define DBGAPI_BP_UM_SP_UPPER           (UINT64_C(1) << 29)

/* IRQ A8 */
#define DBGAPI_BP_UM_IM2_VECTOR_FILTER     (UINT64_C(1) << 30)  /**< im2_vector_enabled + im2_vector_addr */
#define DBGAPI_BP_UM_IM2_VECTOR_MATCH_MODE (UINT64_C(1) << 31)
#define DBGAPI_BP_UM_IM2_VECTOR_ADDR_END   (UINT64_C(1) << 32)
#define DBGAPI_BP_UM_IM2_VECTOR_MASK       (UINT64_C(1) << 33)
#define DBGAPI_BP_UM_IM2_ISR_FILTER        (UINT64_C(1) << 34)  /**< im2_isr_enabled + im2_isr_addr */
#define DBGAPI_BP_UM_IM2_ISR_MATCH_MODE    (UINT64_C(1) << 35)
#define DBGAPI_BP_UM_IM2_ISR_ADDR_END      (UINT64_C(1) << 36)
#define DBGAPI_BP_UM_IM2_ISR_MASK          (UINT64_C(1) << 37)

/* IRQ A8.5 */
#define DBGAPI_BP_UM_IM0_ENABLED        (UINT64_C(1) << 38)
#define DBGAPI_BP_UM_IM1_ENABLED        (UINT64_C(1) << 39)
#define DBGAPI_BP_UM_IM2_ENABLED        (UINT64_C(1) << 40)
#define DBGAPI_BP_UM_IM0_RST_MASK       (UINT64_C(1) << 41)

/* IRQ_SIG */
#define DBGAPI_BP_UM_IRQ_SIG_SOURCE_MASK (UINT64_C(1) << 42)

/**
 * @brief Parametr pro CMD_BP_SET_ENABLED - quick toggle enabled flag.
 *
 * Forwarder na `breakpoints_set_enabled(id, enabled)`. Použití: BP list
 * checkbox, disasm right-click toggle. Atomic, žádný side-effect na
 * ostatní fieldy. Pokud BP s id neexistuje -> success = false.
 */
typedef struct st_DBGAPI_BP_SET_ENABLED_PARAM
{
    int id;        /**< ID existujícího BP */
    bool enabled;  /**< Nový stav */
} st_DBGAPI_BP_SET_ENABLED_PARAM;

/**
 * @brief Parametr pro CMD_BP_SET_PARENT - quick reparent (drag-drop).
 *
 * Forwarder na `breakpoints_set_parent(id, parent_id)`. Použití:
 * drag-drop přesun BP mezi skupinami v BP tree, "Clear parent" v context
 * menu. `parent_id = -1` = root (= bez group).
 */
typedef struct st_DBGAPI_BP_SET_PARENT_PARAM
{
    int id;         /**< ID existujícího BP */
    int parent_id;  /**< -1 = root, jinak ID existující skupiny */
} st_DBGAPI_BP_SET_PARENT_PARAM;

/**
 * @brief Parametr pro CMD_BPGRP_ADD - přidání nové skupiny.
 *
 * Forwarder na `breakpoints_group_add(name, parent)`. Po úspěchu handler
 * naplní `id` přiděleným ID nové skupiny. Pokud add selže (= name NULL,
 * cycle, parent neexistuje), vrátí success = false a `id` zůstane -1.
 *
 * String lifetime: `name` const char* musí být platný do návratu sync cmd.
 * Handler interně provede g_strdup ve `breakpoints_group_add`.
 */
typedef struct st_DBGAPI_BPGRP_ADD_PARAM
{
    const char *name;  /**< Jméno skupiny */
    int parent;        /**< -1 = root, jinak ID existující rodičovské skupiny */
    int id;            /**< Výstup: přidělené ID (vstup ignored) */
} st_DBGAPI_BPGRP_ADD_PARAM;

/**
 * @brief Parametr pro CMD_BPGRP_REMOVE - odebrání skupiny podle ID.
 *
 * Forwarder na `breakpoints_group_remove(id)`. Existující děti (BPs +
 * sub-skupiny) jsou hendlovány backendovou logikou
 * (cascading delete / reparent to root - viz breakpoints.c).
 */
typedef struct st_DBGAPI_BPGRP_REMOVE_PARAM
{
    int id;  /**< ID existující skupiny */
} st_DBGAPI_BPGRP_REMOVE_PARAM;

/**
 * @brief Plochý snapshot st_BPTGROUP pro CMD_BPGRP_UPDATE.
 *
 * Selektivní zápis: caller naplní jen fieldy které chce přepsat a v
 * `update_mask` nastaví odpovídající bity DBGAPI_BPGRP_UM_*. Handler v
 * dbgapi.c volá existující `breakpoints_group_set_*()` settery.
 *
 * Pokud `breakpoints_group_find_by_id(id) == NULL`, handler vrátí
 * success = false a žádnou změnu neaplikuje.
 *
 * String `name` má lifetime trvání sync cmd (= UI strana drží alokaci do
 * návratu). NULL je legitimní pro UM_NAME = clear (= setter uloží
 * prázdný string).
 *
 * Color fieldy bg_rgb / fg_rgb se aplikují společně přes
 * `breakpoints_group_set_colors(bg, fg)` - 1 bit UM_COLORS pokrývá oba.
 */
typedef struct st_DBGAPI_BPGRP_UPDATE_PARAM
{
    int id;                /**< ID existující skupiny */
    uint64_t update_mask;  /**< Bity DBGAPI_BPGRP_UM_* */

    bool enabled;          /**< UM_ENABLED */
    const char *name;      /**< UM_NAME (NULL = clear) */
    uint32_t bg_rgb;       /**< UM_COLORS - aplikuje s fg_rgb společně */
    uint32_t fg_rgb;       /**< UM_COLORS */
    int parent;            /**< UM_PARENT (-1 = root, jinak group ID) */
} st_DBGAPI_BPGRP_UPDATE_PARAM;

/* ============================================================================
 * Bity update_mask pro CMD_BPGRP_UPDATE.
 *
 * Stabilní součást ABI - nové fieldy přidávat na konec (bit 4+),
 * NEpřemapovávat existující.
 * ============================================================================ */

#define DBGAPI_BPGRP_UM_ENABLED         (UINT64_C(1) << 0)
#define DBGAPI_BPGRP_UM_NAME            (UINT64_C(1) << 1)
#define DBGAPI_BPGRP_UM_COLORS          (UINT64_C(1) << 2)  /**< bg_rgb + fg_rgb */
#define DBGAPI_BPGRP_UM_PARENT          (UINT64_C(1) << 3)


/* ============================================================================
 * Event Viewer (mutant event-viewer, Vlna 1) - dbgapi parametry
 *
 * Paralelní k existujícím trace-suite a stack history kanálům. Capacity
 * a mask jsou uložené ve struct kvůli budoucímu rozšíření (= snadné
 * přidávat fieldy bez breakování ABI).
 *
 * Layout @c st_EVENTLOG_EVENT je veřejný součást API (24 B, viz
 * eventlog.h). Caller alokuje buffer ve své paměti, handler ho jen
 * vyplní.
 * ============================================================================ */

/**
 * @brief Parametr pro CMD_EVENTLOG_SET_CAPACITY.
 *
 * Caller předává požadovanou velikost ringu. Handler hodnotu interně
 * clampuje do @c [EVENTLOG_MIN_CAPACITY..EVENTLOG_MAX_CAPACITY] a
 * vrátí výslednou (post-clamp) hodnotu v @c capacity_after.
 *
 * @field capacity        (IN)  Požadovaná velikost ringu.
 * @field capacity_after  (OUT) Skutečně nastavená velikost.
 */
typedef struct st_DBGAPI_EVENTLOG_CAPACITY_PARAM
{
    uint32_t capacity;
    uint32_t capacity_after;
} st_DBGAPI_EVENTLOG_CAPACITY_PARAM;

/**
 * @brief Parametr pro CMD_EVENTLOG_SET_MASK.
 *
 * @field mask  (IN) Nová bitmask povolených kategorií (bit i = kategorie i).
 */
typedef struct st_DBGAPI_EVENTLOG_MASK_PARAM
{
    uint64_t mask;
} st_DBGAPI_EVENTLOG_MASK_PARAM;

/**
 * @brief Parametr pro CMD_EVENTLOG_GET_EVENT.
 *
 * Caller předává logický index (0 = oldest) a alokovaný buffer pro
 * 24 B event záznam. Handler vyplní @c found = 1 a obsah @c event,
 * nebo @c found = 0 pokud @c idx >= count.
 *
 * @field idx     (IN)  Logický index v ringu.
 * @field found   (OUT) 1 = event nalezen, 0 = idx mimo rozsah.
 * @field event   (OUT) Vyplněný record (validní pokud found == 1).
 */
typedef struct st_DBGAPI_EVENTLOG_GET_EVENT_PARAM
{
    uint32_t idx;
    uint8_t  found;
    uint8_t  _pad[ 3 ];
    /* 24 B raw record - layout shodný s st_EVENTLOG_EVENT
     * (eventlog.h). Caller si může pole castnout, případně přečíst
     * jednotlivé bajty (offsety jsou stabilní součást API). */
    uint64_t pxclk_total;
    uint32_t screens_total;
    uint32_t pxclk_in_screen;
    uint8_t  category;
    uint8_t  subtype;
    uint16_t pc;
    uint32_t payload;
} st_DBGAPI_EVENTLOG_GET_EVENT_PARAM;

/**
 * @brief Parametr pro CMD_GET_CALLSTACK - snapshot shadow stacku + statistiky.
 *
 * Snapshot pattern: handler (= emu vlákno) alokuje pole entries přes
 * @c callstack_snapshot_get() (= g_malloc) a předá pointer + count zpět
 * caller-ovi. UI po vykreslení MUSÍ uvolnit pole přes
 * @c callstack_snapshot_free() (= g_free). Statistiky jsou kopírovány
 * inline do struct (= žádná dodatečná alokace).
 *
 * Ownership pravidla:
 *  - Při návratu z dbgapi_ui_submit_cmd_sync s success == true vlastní
 *    UI pointer @c entries (i pokud count == 0 -> entries == NULL).
 *  - UI MUSÍ entries uvolnit přes callstack_snapshot_free, ne přes
 *    g_free / free přímo. (Implementace dnes interně volá g_free; helper
 *    chrání před změnou alokátoru v budoucnu.)
 *  - Při success == false handler garantuje entries == NULL a count == 0.
 *
 * @field entries  (OUT) Pole zkopírovaných entries (callee-allocated).
 *                       NULL pokud count == 0 nebo při chybě.
 * @field count    (OUT) Počet entries (0..CALLSTACK_MAX_DEPTH).
 * @field stats    (OUT) Statistiky (current_depth, max_depth_reached,
 *                       divergence_count, sp_swap_count, overflow_count).
 *                       Layout shodný s st_CALLSTACK_STATS - typu-pun přes
 *                       int/uint32_t pole.
 * @field active     (OUT) g_callstack_active snapshot (= UI sync s checkboxem).
 * @field cycles_now (OUT) cpu->total_cycles v okamžiku snapshotu. UI pak
 *                         spočítá Cyc-in = cycles_now - entry.cycles_at_entry
 *                         (= reálné cycles uvnitř každého frame). 0 pokud
 *                         g_mzarch_main.cpu == NULL.
 */
typedef struct st_DBGAPI_CALLSTACK_GET_PARAM
{
    /* OUT: pointer na pole struktur st_CALLSTACK_ENTRY (callstack.h).
     * Typován jako void* aby dbgapi_cmdrq.h nemusel includovat
     * callstack.h (= cyklická závislost prevence). Caller cast na
     * st_CALLSTACK_ENTRY*. */
    void   *entries;
    int     count;
    /* Inline statistiky (= layout st_CALLSTACK_STATS): */
    int     current_depth;
    int     max_depth_reached;
    uint32_t divergence_count;    /* total = trampoline + longjmp + mismatch */
    uint32_t diverg_trampoline;
    uint32_t diverg_longjmp;
    uint32_t diverg_mismatch;
    uint32_t sp_swap_count;
    uint32_t overflow_count;
    uint32_t stack_discard_count;
    uint8_t active;
    uint8_t _pad[ 3 ];
    uint64_t cycles_now;
} st_DBGAPI_CALLSTACK_GET_PARAM;


/**
 * @brief Payload pro DBGAPI_CMD_GET_PROFILER.
 *
 * Sync handler v EMU vlákně alokuje pole entries přes
 * profiler_snapshot_get (callee-allocated, g_malloc). Caller (UI)
 * vlastní vrácený pointer a po dokončení renderování ho MUSÍ
 * uvolnit zpětnou cestou (= zrekonstruovat st_PROF_SNAPSHOT z
 * entries + entry_count a zavolat profiler_snapshot_free).
 *
 * Statistiky kopírujeme inline (= žádná dodatečná alokace nad
 * rámec entries pole). Layout zbývajících polí kopíruje
 * st_PROF_STATS (profiler.h) bez entry_count (= ten je už
 * vyjádřený dedikovaným fieldem entry_count nahoře).
 *
 * @field entries     (OUT) Pole st_PROF_ENTRY (callee-allocated).
 *                          NULL pokud entry_count == 0 nebo při chybě.
 *                          Typován void* aby dbgapi_cmdrq.h nemusel
 *                          includovat profiler.h (prevence cyklické
 *                          závislosti). Caller cast na st_PROF_ENTRY*.
 * @field entry_count (OUT) Počet entries (0..N).
 * @field active             (OUT) g_profiler_active snapshot (= UI sync s
 *                          checkboxem).
 * @field total_cycles_64    (OUT) 64-bit extended cycles counter od resetu.
 * @field total_calls        (OUT) Celkový počet on_enter eventů.
 * @field irq_entries        (OUT) Z toho IRQ accept events.
 * @field unmatched_returns  (OUT) DIVERGENT exit nebo pop nad prázdným.
 * @field max_depth_reached  (OUT) Nejvyšší g_prof_depth od resetu.
 * @field overflow_count     (OUT) Push pokus nad PROFILER_MAX_DEPTH.
 */
typedef struct st_DBGAPI_PROFILER_GET_PARAM
{
    /* OUT: pointer na pole st_PROF_ENTRY (profiler.h). Typován void*
     * aby dbgapi_cmdrq.h nemusel includovat profiler.h
     * (prevence cyklické závislosti). Caller cast na st_PROF_ENTRY*. */
    void   *entries;
    int     entry_count;
    /* Inline statistiky (= layout st_PROF_STATS bez entry_count): */
    uint8_t  active;
    uint8_t  _pad[ 7 ];
    uint64_t total_cycles_64;
    uint64_t total_calls;
    uint32_t irq_entries;
    uint32_t unmatched_returns;
    uint32_t max_depth_reached;
    uint32_t overflow_count;
} st_DBGAPI_PROFILER_GET_PARAM;


/**
 * @brief Payload pro DBGAPI_CMD_PROFILER_SET_ACTIVE.
 *
 * Sync handler v EMU vlákně volá profiler_set_active(p->active != 0).
 * V handleru je volání bezpečné (= safe-point, listener slot
 * manipulace mimo hot path).
 *
 * @field active (IN) 0 = vypnout profiler, !=0 = zapnout.
 */
typedef struct st_DBGAPI_PROFILER_SET_ACTIVE_PARAM
{
    uint8_t  active;        /**< IN: 0/1 = vypnout/zapnout profiler. */
    uint8_t  _pad[ 7 ];     /**< Padding (zarovnání na 8 bajtů). */
} st_DBGAPI_PROFILER_SET_ACTIVE_PARAM;


/* Výsledek CMD_BP_LIST */
typedef struct st_DBGAPI_BP_LIST_RESULT
{
    int count;     /* Počet breakpointů */
    int max_count; /* Velikost pole bp[] (musí nastavit klient) */
    struct
    {
        uint16_t addr; /* Adresa */
        int id;        /* ID */
        bool enabled;  /* Aktivní? */
    } bp[];            /* Flexibilní pole */
} st_DBGAPI_BP_LIST_RESULT;

#endif /* DBGAPI_CMDRQ_H */
