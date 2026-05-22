/**
 * @file   cputrack.h
 * @brief  CPU Tracking Log - jeden event per vykonanou Z80 instrukci.
 *
 * Subsystém trace-suite, který loguje každou dokončenou CPU instrukci
 * jako 12-byte fixed záznam:
 *
 * | Offset | Velikost | Pole                                          |
 * |--------|----------|------------------------------------------------|
 * | 0      | 2 B      | regPC (uint16_t LE)                            |
 * | 2      | 1 B      | insn_length (1-4)                              |
 * | 3      | 4 B      | insn_bytes (jen prvních insn_length valid)     |
 * | 7      | 4 B      | wait_clk (uint32_t LE) - WAIT T-states navíc   |
 * | 11     | 1 B      | reserved (zarovnání na 12 B)                   |
 *
 * @c wait_clk == 0 => běžná instrukce (žádný WAIT).
 * @c wait_clk == 0xFFFFFFFF => "saturated" (např. dlouhá HALT smyčka).
 *
 * Hlavička recordingu (RAM dump + initial regs) se vytvoří v
 * @ref cputrack_start(). Hlavičkové binární soubory:
 *   `<dir>/<name>_initial_ram.bin`     - 64 KB RAM
 *   `<dir>/<name>_initial_vram.bin`    - 32 KB VRAM (jen MZ-800)
 *   `<dir>/<name>_initial_cgram.bin`   - 4 KB CG-RAM (jen MZ-1500)
 *   `<dir>/<name>_initial_memext.bin`  - 512 KB Memext (pokud připojeno)
 *   `<dir>/<name>_initial_rdN.bin`     - per-Ramdisk (pokud aktivní)
 *
 * Per-event záznamy: viz @ref tlog_writer chunk soubory.
 *
 * @section halt_collapse HALT a self-loop collapse
 *
 * HALT, JP $, JR $-2, CALL na sebe (= libovolná instrukce která zanechá
 * PC na stejné adrese a opakuje se) se sbaluje do JEDNOHO eventu s
 * @c wait_clk = celkový čas strávený ve smyčce (do exit přes IRQ /
 * state change). Detection: po @c z80_step() pokud nový PC == prev_PC,
 * suppress emit a akumuluj T-states. Při exit (PC změněno, IRQ accept)
 * emit collapsed event.
 *
 * @author Michal Hucik <hucik@ordoz.com>
 */

#ifndef CPUTRACK_H
#define CPUTRACK_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "tlog_common.h"

/**
 * @brief Konfigurace cputrack subsystému.
 *
 * Drží uživatelské nastavení (mode, dir, name, limity). Ekvivalent
 * mhmap polí v g_debugger.
 */
typedef struct st_CPUTRACK_CONFIG
{
    en_TLOG_MODE mode;            /**< Off / WithWindow / Always */
    char *dir;                    /**< Cílový adresář (g_strdup) */
    char *name;                   /**< Basename (g_strdup) */
    unsigned chunk_mb;            /**< Per-chunk velikost v MB (0 = default) */
    unsigned max_total_mb;        /**< Max total v MB (0 = unlimited) */
    unsigned save_on_exit;        /**< Při ukončení emu vyflushnout zbytek (default 1) */
} st_CPUTRACK_CONFIG;

extern st_CPUTRACK_CONFIG g_cputrack_config;

/**
 * @brief Inicializace cputrack modulu.
 *
 * Volá se z `debugger_init()` po `cfgmain` registraci. Zaregistruje
 * konfigurační klíče (`cputrack_mode`, `cputrack_dir`, ...).
 */
void cputrack_init ( void );

/**
 * @brief Aplikace CLI options (--cputrack-*).
 *
 * Přepíše hodnoty z INI configu hodnotami z `--cputrack-mode`,
 * `--cputrack-dir`, `--cputrack-name` atd.
 */
void cputrack_apply_cli_options ( void );

/**
 * @brief Test, zda je cputrack aktivně recordující.
 *
 * Logika paralelní k @c TEST_DEBUGGER_MHMAP_ACTIVE.
 */
extern int g_cputrack_active;

#define TEST_TRACE_CPUTRACK_ACTIVE (g_cputrack_active)

/**
 * @brief Triggernout přepočet @c g_cputrack_active z mode + debugger state.
 *
 * Volá se při změně cputrack_mode (UI/CLI) a při změně debugger_active
 * (debug okno open/close).
 */
void cputrack_recompute_active ( int debugger_active );

/**
 * @brief Spustit recording (alokovat writer, zapsat header + RAM dump).
 *
 * Volá se interně při přechodu z neaktivního na aktivní stav. Idempotentní.
 *
 * @return 0 OK, -1 error.
 */
int cputrack_start ( void );

/**
 * @brief Zastavit recording (flushnout buffer, zavřít writer).
 *
 * Volá se interně při přechodu z aktivního na neaktivní stav. Idempotentní.
 */
void cputrack_stop ( void );

/**
 * @brief Hot-path hook - emit eventu za právě dokončenou instrukci.
 *
 * Volat z `mzarch_main_emulator_run()` po `z80_step()` POKUD je
 * cputrack aktivní (caller checkuje TEST_TRACE_CPUTRACK_ACTIVE).
 *
 * Implementuje HALT/self-loop collapse:
 *  - Pokud nový PC == prev_PC: suppress emit, akumulovat tstates do
 *    pending_wait_clk
 *  - Pokud změna PC: pokud byly pending T-states, emit s wait_clk=accumulated;
 *    jinak emit s wait_clk=0
 *
 * @param pc                Aktuální PC (po z80_step, = adresa vykonané instr
 *                          BEFORE step, tj. caller drží před voláním).
 *                          Pozn.: caller předá `instruction_addr` (= PC pred step).
 * @param insn_tstates      Počet T-states které vykonaná instr trvala.
 * @param wait_extra_tstates Extra WAIT cycles (mimo standardní instr délku).
 *                          0 = žádný WAIT.
 */
void cputrack_on_instruction_complete ( uint16_t pc,
                                        uint8_t insn_tstates,
                                        uint32_t wait_extra_tstates );

/**
 * @brief Reset stavu collapse logiky (volat při emu reset).
 */
void cputrack_reset_collapse_state ( void );

/**
 * @brief Force flush + finalize (volá se z debugger_exit pokud save_on_exit=1).
 */
void cputrack_finalize ( void );

/**
 * @brief Test, zda byl recording zastaven kvůli max_total_mb limitu.
 *
 * Public wrapper nad @ref tlog_writer_is_truncated() pro interní
 * @c s_writer instanci tohoto subsystému. Použití typ. v UI status
 * indicators (např. titulek SDL okna).
 *
 * @return 1 = truncated (recording stopnut), 0 = stále aktivní recording
 *         nebo neaktivní subsystém.
 */
int cputrack_is_truncated ( void );

#ifdef __cplusplus
}
#endif

#endif /* CPUTRACK_H */
