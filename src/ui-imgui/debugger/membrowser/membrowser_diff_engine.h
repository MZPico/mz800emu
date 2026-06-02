/**
 * @file membrowser_diff_engine.h
 * @brief Diff engine - byte-by-byte porovnání dvou bufferů (V5).
 *
 * Pure logika bez ImGui/dbgapi/SDL závislostí - umožňuje standalone unit
 * test. Engine porovnává dva uint8_t buffery a vrací statistiku +
 * volitelný callback pro per-byte stream (pro export).
 *
 * Sémantika délkového rozdílu:
 *   - Pokud size_a != size_b, porovnává se jen společná prefix část
 *     min(size_a, size_b). Zbývající "tail" jednoho z bufferů se
 *     považuje za "rozdílný" (počítá se do stats.changed) a označuje
 *     se flagem stats.size_mismatch.
 *
 * Threading: žádná global state, čistě reentrant.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_DIFF_ENGINE_H
#define MEMBROWSER_DIFF_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Souhrnná statistika diff porovnání.
 *
 * Naplňuje @ref membrowser_diff_compute. compared_bytes = min(size_a, size_b).
 * changed = počet bajtů v rámci compared_bytes které se liší. Pokud
 * size_a != size_b, size_mismatch = true a engine k changed přičte
 * absolutní rozdíl velikostí (= "tail" delší strany je celý odlišný).
 */
typedef struct st_MB_DIFF_STATS
{
    uint64_t size_a;            /**< Velikost zdroje A v bajtech. */
    uint64_t size_b;            /**< Velikost zdroje B v bajtech. */
    uint64_t compared_bytes;    /**< min(size_a, size_b). */
    uint64_t changed;           /**< Počet odlišných bajtů (vč. tail mismatch). */
    bool     size_mismatch;     /**< size_a != size_b. */
} st_MB_DIFF_STATS;

/**
 * @brief Callback per každý odlišný bajt v prefix porovnání.
 *
 * Volá se z @ref membrowser_diff_compute v rostoucím pořadí offset.
 * Tail mismatch (size_a != size_b) callback nevolá - jeho existence
 * je signalizována přes stats.size_mismatch.
 *
 * @param user      Opaque uživatelský pointer (předán do compute).
 * @param offset    Offset bajtu v rámci porovnání (0..compared_bytes-1).
 * @param byte_a    Hodnota v bufferu A.
 * @param byte_b    Hodnota v bufferu B.
 * @return true = pokračovat, false = ukončit iteraci předčasně.
 */
typedef bool (*membrowser_diff_byte_cb_t) ( void *user,
                                             uint64_t offset,
                                             uint8_t byte_a,
                                             uint8_t byte_b );

/**
 * @brief Porovná dva buffery a naplní statistiku.
 *
 * @param a       Buffer A (smí být NULL pokud size_a == 0).
 * @param size_a  Velikost A v bajtech.
 * @param b       Buffer B (smí být NULL pokud size_b == 0).
 * @param size_b  Velikost B v bajtech.
 * @param cb      Volitelný per-byte callback pro odlišné bajty (NULL = jen stats).
 * @param user    User pointer předaný do cb.
 * @param[out] stats  Vyplněná statistika (NESmí být NULL).
 *
 * @return Počet odlišných bajtů (= stats.changed). Vždy >= 0.
 *
 * @pre stats != NULL
 * @post stats vyplněna i při size_a == 0 && size_b == 0 (= changed=0).
 */
uint64_t membrowser_diff_compute ( const uint8_t *a, uint64_t size_a,
                                    const uint8_t *b, uint64_t size_b,
                                    membrowser_diff_byte_cb_t cb,
                                    void *user,
                                    st_MB_DIFF_STATS *stats );

/**
 * @brief Test zda jeden konkrétní bajt na offset je odlišný.
 *
 * Helper pro UI hot path (per render řádek). Žádný malloc, žádný callback.
 * Pro offset >= compared_bytes vrací true (= tail mismatch).
 *
 * @param a       Buffer A (smí být NULL pokud size_a == 0).
 * @param size_a  Velikost A.
 * @param b       Buffer B.
 * @param size_b  Velikost B.
 * @param offset  Index k testu.
 * @return true = bajty na offsetu se liší (vč. mimo dosah jedné strany).
 *         false = stejné nebo offset mimo obou stran.
 */
bool membrowser_diff_is_changed ( const uint8_t *a, uint64_t size_a,
                                   const uint8_t *b, uint64_t size_b,
                                   uint64_t offset );

#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_DIFF_ENGINE_H */
