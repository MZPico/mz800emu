/**
 * @file membrowser_edited.h
 * @brief Recently-edited bytes storage (per Memory Browser instance).
 *
 * Drží seznam bytů nedávno editovaných v aktuální "session" - od posledního
 * region switch nebo od explicitního Clear. Hex view podle této informace
 * vykresluje vizuální badge (žlutý "*") před hex byte aby si uživatel
 * pamatoval, které byty právě měnil.
 *
 * Storage je global file-static (jeden ring per instance), klíčované
 * trojicí (instance_idx, region_kind, region_sub_id, addr). Při překročení
 * limit kapacity (@ref MB_EDITED_MAX_PER_INSTANCE) se nejstarší záznam
 * vyhodí - nový vyhrává nad starým "FIFO" stylem.
 *
 * Lookup is_marked() je O(N) lineární scan, ale N je max 1024 a volá se jen
 * pokud edited_count > 0 (= žádný overhead pokud uživatel nic needituje).
 *
 * Threading: UI-thread only.
 *
 * Lifetime: globální storage, žádný explicit init / shutdown. Volat
 * @ref membrowser_edited_clear_all při shutdown / mzarch switch.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_EDITED_H
#define MEMBROWSER_EDITED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Max počet držených (region, addr) položek per instance.
 *
 * 1024 odpovídá zhruba 64 hex řádek (16 B/řádek) - dostatek pro typickou
 * interaktivní edit session. Po překročení se nejstarší záznam vyhodí
 * (ring overwrite stylem).
 */
#define MB_EDITED_MAX_PER_INSTANCE 1024


/**
 * @brief Označí byte (instance, region, addr) jako "recently edited".
 *
 * Pokud už existuje stejný (instance, kind, sub_id, addr) - no-op
 * (existující @c original_byte se PONECHÁ = "session original" zůstává
 * tou nejstarší hodnotou před prvním edit).
 *
 * Pokud je instance plná (= MB_EDITED_MAX_PER_INSTANCE záznamů), nejstarší
 * záznam (index 0) se zahodí a nový se přidá na konec.
 *
 * V1.5+: @c original_byte = hodnota bytu PŘED tímto write. Caller načte
 * přes @c read_bytes před @c write_bytes. Slouží pro auto-unmark detection
 * (@ref membrowser_edited_check_unmark) - když undo/redo/další edit vrátí
 * byte na tuto hodnotu, badge zmizí.
 *
 * @param instance_idx  0..MB_INSTANCE_COUNT-1.
 * @param region_kind   en_REGION_KIND.
 * @param region_sub_id Disambiguator.
 * @param addr          Offset v rámci regionu.
 * @param original_byte Hodnota bytu PŘED tímto write (pro session-original tracking).
 */
void membrowser_edited_mark ( int instance_idx,
                               int region_kind, int region_sub_id,
                               uint32_t addr, uint8_t original_byte );


/**
 * @brief Označí range bytů jako "recently edited" (per-byte mark).
 *
 * Volá se po fill operaci. Pro velmi velký range (> capacity) zaznamená
 * jen posledních MB_EDITED_MAX_PER_INSTANCE bytů (nejstarší se průběžně
 * vyhazují).
 *
 * V1.5+: @p originals = pole @p length bajtů PŘED write (caller načte
 * @c read_bytes do tmp bufferu před @c write_bytes). Pokud NULL, originals
 * se zaplní nulami - auto-unmark pak nebude pro tento range fungovat
 * spolehlivě (= byte na hodnotě 0x00 se omylem odmaskuje).
 *
 * @param instance_idx  0..MB_INSTANCE_COUNT-1.
 * @param region_kind   en_REGION_KIND.
 * @param region_sub_id Disambiguator.
 * @param start_addr    První offset v range.
 * @param length        Počet bytů.
 * @param originals     Pole length bajtů (pre-write hodnoty), nebo NULL.
 */
void membrowser_edited_mark_range ( int instance_idx,
                                     int region_kind, int region_sub_id,
                                     uint32_t start_addr, uint32_t length,
                                     const uint8_t *originals );


/**
 * @brief Pokud existuje záznam (instance, region, addr) A stored
 *        @c original_byte == @p current_byte, záznam odebrat (= "edit
 *        returned to session-original value, badge zmizí").
 *
 * Volá se po každém úspěšném @c write_bytes (HEX/ASCII typing, undo,
 * redo, fill, char inserter) s hodnotou KTERÁ JE TEĎ V REGIONU (= post-write
 * read result, nebo new_byte pro single-byte write).
 *
 * Fast path: pokud instance má 0 záznamů, vrátí false bez scan.
 *
 * @return true = záznam byl odebrán (byte se vrátil na original); false jinak.
 */
bool membrowser_edited_check_unmark ( int instance_idx,
                                       int region_kind, int region_sub_id,
                                       uint32_t addr, uint8_t current_byte );


/**
 * @brief Range varianta @ref membrowser_edited_check_unmark - per-byte
 *        check + unmark pro pole délky @p length.
 *
 * @param current_bytes Pole length aktuálních hodnot bytů (post-write).
 */
void membrowser_edited_check_unmark_range ( int instance_idx,
                                             int region_kind, int region_sub_id,
                                             uint32_t start_addr, uint32_t length,
                                             const uint8_t *current_bytes );


/**
 * @brief Test zda byte je v "recently edited" seznamu pro danou instanci.
 *
 * Fast path: pokud instance má 0 záznamů, vrátí false bez scan.
 *
 * @param instance_idx  0..MB_INSTANCE_COUNT-1.
 * @param region_kind   en_REGION_KIND.
 * @param region_sub_id Disambiguator.
 * @param addr          Offset v rámci regionu.
 * @return              true = byte byl recently edited a má být vizuálně
 *                      zvýrazněn; false = needitovaný nebo už byl clearnut.
 */
bool membrowser_edited_is_marked ( int instance_idx,
                                    int region_kind, int region_sub_id,
                                    uint32_t addr );


/**
 * @brief Počet recently-edited záznamů pro instance (pro UI badge / debug).
 *
 * @param instance_idx  0..MB_INSTANCE_COUNT-1.
 * @return              0..MB_EDITED_MAX_PER_INSTANCE.
 */
int membrowser_edited_count_for_instance ( int instance_idx );


/**
 * @brief Vymaže recently-edited záznamy pro danou instanci.
 *
 * Volá se automaticky při region switch detekci (= nový region = nová
 * session). Lze volat i z toolbar Clear button.
 *
 * @param instance_idx  0..MB_INSTANCE_COUNT-1.
 */
void membrowser_edited_clear_for_instance ( int instance_idx );


/**
 * @brief Vymaže recently-edited záznamy napříč všemi instancemi.
 *
 * Volat při shutdown / mzarch switch / explicit "Clear all" akce.
 */
void membrowser_edited_clear_all ( void );


#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_EDITED_H */
