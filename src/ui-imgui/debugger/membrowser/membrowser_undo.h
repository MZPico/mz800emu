/**
 * @file membrowser_undo.h
 * @brief Per-region undo/redo stack pro fill / batch write operace (V6).
 *
 * Drží kruhový bufer maximálně @c MB_UNDO_MAX_LEVELS snapshotů per
 * (region_kind, sub_id) klíč. Každý snapshot = (start_offset, len, bytes).
 *
 * Při překročení limit kapacity nejstarší záznam vypadne (ring overwrite).
 * Při novém undo push (po sérii undo + new edit) se redo část stacku
 * zahazuje - standardní VS Code / browser behavior.
 *
 * Lifetime bytes: každý snapshot alokuje vlastní malloc(); stack je vlastní
 * při push (memcpy), free při pop / clear / overwrite.
 *
 * Threading: UI-thread only.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_UNDO_H
#define MEMBROWSER_UNDO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Maximální počet undo úrovní per region key.
 *
 * 10 stačí pro typickou interaktivní práci (více než 10 fill operací
 * v řadě bez commit je vzácné). Pevný limit drží paměťovou stopu pod
 * kontrolou - max ~10 * MB_UNDO_MAX_BYTES per region.
 */
#define MB_UNDO_MAX_LEVELS 10


/**
 * @brief Horní limit délky jednoho undo záznamu v bajtech.
 *
 * Bez limit by uživatel mohl trigger fill přes celý 16 MB Ramdisk =
 * 16 MB undo záznam x 10 levels x N regionů. Omezíme na 1 MB per záznam;
 * delší fill operace se UI ptá uživatele, případně se undo nezapíše
 * (= "operation too large to undo" status).
 */
#define MB_UNDO_MAX_BYTES (1024 * 1024)


/**
 * @brief Jeden undo / redo záznam.
 *
 * Vlastní @c bytes přes malloc - po pop / overwrite se musí free volat.
 */
typedef struct st_MB_UNDO_ENTRY
{
    int region_kind;       /**< en_REGION_KIND. */
    int region_sub_id;     /**< Disambiguator. */
    uint64_t offset;       /**< Start offsetu v regionu. */
    uint32_t len;          /**< Délka záznamu v bajtech (>0). */
    uint8_t *bytes;        /**< Vlastněný buffer (malloc, len bajtů). NULL = invalid záznam. */
} st_MB_UNDO_ENTRY;


/**
 * @brief Push nového undo snapshotu na top stacku pro daný region.
 *
 * Před vlastním write zachytí "current" stav bytes (= co tam bylo) +
 * smaže redo část stacku (nový edit invaliduje redo).
 *
 * Pokud len > @c MB_UNDO_MAX_BYTES, vrací -1 a žádný záznam se nepřidá
 * (caller by měl uživateli ohlásit "too large to undo").
 *
 * Pokud při overwrite by stack přesáhl @c MB_UNDO_MAX_LEVELS, nejstarší
 * (= dno stacku) záznam se zahodí (free) a nový se přidá na top.
 *
 * @param region_kind   en_REGION_KIND.
 * @param region_sub_id Disambiguator.
 * @param offset        Začátek snapshotu v regionu.
 * @param bytes_before  Pointer na bajty PŘED nadcházejícím write (read před
 *                      vlastním fill). NESmí být NULL pokud len > 0.
 * @param len           Délka v bajtech.
 * @return 0 OK, -1 příliš velký záznam, -2 malloc selhal.
 */
int membrowser_undo_push ( int region_kind, int region_sub_id,
                            uint64_t offset, const uint8_t *bytes_before,
                            uint32_t len );


/**
 * @brief Spotřebuje top undo záznam pro daný region a vrátí ho callerovi.
 *
 * Volající (membrowser_window.cpp) následně provede write_bytes back do
 * paměti (= obnoví "before" stav) A push odpovídající záznam do redo stacku.
 *
 * Vlastnictví @c out->bytes přechází na callera; po použití musí volat
 * @ref membrowser_undo_free_entry.
 *
 * @param region_kind   en_REGION_KIND.
 * @param region_sub_id Disambiguator.
 * @param[out] out      Naplní se top záznamem (NESmí být NULL).
 * @return 0 OK, -1 nic není (stack prázdný pro daný region).
 */
int membrowser_undo_pop ( int region_kind, int region_sub_id,
                           st_MB_UNDO_ENTRY *out );


/**
 * @brief Push redo záznam (po undo operaci).
 *
 * Stejná sémantika jako @ref membrowser_undo_push, ale do redo stacku
 * pro daný region. Volá se z UI po úspěšném undo - kopíruje "co bylo
 * teď před undo" pro případný redo.
 *
 * @return 0 OK, -1 too large, -2 malloc fail.
 */
int membrowser_redo_push ( int region_kind, int region_sub_id,
                            uint64_t offset, const uint8_t *bytes_now,
                            uint32_t len );


/**
 * @brief Pop top redo záznam (analog @ref membrowser_undo_pop).
 *
 * @return 0 OK, -1 redo stack prázdný.
 */
int membrowser_redo_pop ( int region_kind, int region_sub_id,
                           st_MB_UNDO_ENTRY *out );


/**
 * @brief Vrací počet undo / redo položek pro daný region (pro UI status).
 *
 * @param region_kind   en_REGION_KIND.
 * @param region_sub_id Disambiguator.
 * @param[out] undo_n   Počet undo úrovní (NULL OK).
 * @param[out] redo_n   Počet redo úrovní (NULL OK).
 */
void membrowser_undo_stats ( int region_kind, int region_sub_id,
                              int *undo_n, int *redo_n );


/**
 * @brief Vymaže celý undo i redo stack napříč všemi regiony.
 *
 * Volat při shutdown / region snapshot reset (po HW reconfigure mohou
 * region IDs ztratit smysl). Volat při změně architektury (mzarch switch).
 */
void membrowser_undo_clear_all ( void );


/**
 * @brief Uvolní bytes alokované v entry (caller-side po consume).
 *
 * @param e  Entry (NULL OK = no-op).
 */
void membrowser_undo_free_entry ( st_MB_UNDO_ENTRY *e );


#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_UNDO_H */
