/**
 * @file membrowser_snapshot.h
 * @brief Per-region snapshot pro "Δ since snapshot" layer (V1).
 *
 * Drží 1 snapshot per region_id v interní heap mapě. UI volá:
 *   - membrowser_snapshot_take(region_id) - sjede aktuální obsah do bufferu
 *   - membrowser_snapshot_byte(region_id, offset, *out) - vrátí snapshot byte
 *   - membrowser_snapshot_reset(region_id) - smaže snapshot
 *   - membrowser_snapshot_has(region_id) - test zda existuje
 *
 * Storage: dynamicky alokovaný malloc-array per region; max 1 snapshot per
 * region_id (volání take() přepíše předchozí). Cleanup při emulator exit.
 *
 * Threading: jen UI vlákno. Žádný mutex.
 *
 * Memory budget: nejvyšší region je RAMDISK_STD (256 × 64 KB = 16 MB), ale
 * běžné použití snapshot 64 KB Logical Z80 ~ 64 KB. V1 limit: max
 * MEMBROWSER_SNAPSHOT_MAX_BYTES per snapshot = 4 MB (pokrývá vše kromě
 * extrémních Ramdisk regionů).
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_SNAPSHOT_H
#define MEMBROWSER_SNAPSHOT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Horní limit velikosti jednoho snapshot bufferu.
 *
 * Pokud má region víc bajtů, take() pořídí jen prvních
 * MEMBROWSER_SNAPSHOT_MAX_BYTES bajtů (= snapshot je truncated). UI
 * by mohlo zobrazit warning pro super-velké regiony (V2+).
 */
#define MEMBROWSER_SNAPSHOT_MAX_BYTES (4u * 1024u * 1024u)

/**
 * @brief Pořídí snapshot daného regionu (přes dbgapi_regions_read).
 *
 * Pokud existuje předchozí snapshot pro stejné region_id, je nahrazen.
 * Při chybě read (region disconnected) se snapshot neuloží (= jako reset).
 *
 * @param region_id ID z dbgapi_regions_enumerate (musí být platné).
 * @return true při úspěchu, false při chybě.
 */
bool membrowser_snapshot_take ( int region_id );

/**
 * @brief Smaže snapshot pro daný region_id.
 *
 * @return true pokud snapshot existoval, false pokud ne (= no-op).
 */
bool membrowser_snapshot_reset ( int region_id );

/**
 * @brief Test existence snapshotu pro region.
 */
bool membrowser_snapshot_has ( int region_id );

/**
 * @brief Vrátí snapshot byte pro daný offset.
 *
 * @param region_id ID regionu.
 * @param offset    Offset v rámci regionu.
 * @param[out] out  Snapshot hodnota (jen při return true).
 * @return true pokud snapshot existuje A offset spadá do uloženého rozsahu.
 */
bool membrowser_snapshot_byte ( int region_id, uint32_t offset, uint8_t *out );

/**
 * @brief Vrátí velikost snapshot bufferu (= počet uložených bajtů).
 *
 * @return Velikost v bajtech, 0 pokud snapshot neexistuje.
 */
uint32_t membrowser_snapshot_size ( int region_id );

/**
 * @brief Uvolní všechny snapshoty (= shutdown cleanup).
 */
void membrowser_snapshot_cleanup_all ( void );

#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_SNAPSHOT_H */
