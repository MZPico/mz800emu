/**
 * @file membrowser_annotations.h
 * @brief Per-byte/per-range komentáře k paměti (V6).
 *
 * Storage: in-memory lineární pole s klíčem (region_kind, sub_id, addr).
 * Persist přes vlastní text soubor "membrowser_annotations.txt" v
 * pracovním adresáři (= odkud byl spuštěn emulátor) - jednorázový load
 * při startu + jednorázový save při shutdown. Žádná integrace s cfgmain
 * (jeho typový systém INT/BOOL/TEXT není pro 2D mapu vhodný).
 *
 * Formát perzistenci (1 anotace per řádek):
 *   kind\tsub_id\taddr\tcolor_rgba8\ttext\n
 * Pole oddělená tabem. text může obsahovat \\n escape pro newline,
 * \\t pro tab, \\\\ pro literal backslash.
 *
 * V6.1+ plánováno:
 *   - per-byte hover tooltip v hex view (integrace s DrawList hit-test)
 *   - color tag picker
 *   - range annotations (start..end místo single byte)
 *
 * V6 limitace:
 *   - jen single-byte annotace
 *   - tooltip jen v "Annotations" panelu (list view), ne přímo v hex grid
 *   - color je perzistovaná ale UI ji v V6 jen zobrazí jako swatch v listu
 *
 * Threading: UI-thread only.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_ANNOTATIONS_H
#define MEMBROWSER_ANNOTATIONS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Maximální délka textu jedné annotace (včetně ukončovací \0).
 *
 * 256 stačí na 2-3 věty komentáře. Delší poznámky patří do CDL Notes
 * nebo do externí dokumentace.
 */
#define MB_ANNOT_TEXT_MAX 256


/**
 * @brief Maximální počet annotací držených v paměti.
 *
 * 1024 je dostatek pro pracovní session - vyšší limit by patrně
 * indikoval špatné použití (radši nahradit dispatch tabulkou).
 */
#define MB_ANNOT_MAX_ENTRIES 1024


/**
 * @brief Jeden annotation záznam.
 *
 * Vlastník bytes textu = annotations storage (fixed-size buffer per slot).
 * Color je RGBA 8 bit packed: 0xRRGGBBAA (alpha 0xFF = fully opaque).
 */
typedef struct st_MB_ANNOTATION
{
    bool used;                       /**< 1 = slot obsazený. */
    int region_kind;                 /**< en_REGION_KIND. */
    int region_sub_id;               /**< Disambiguator. */
    uint32_t addr;                   /**< Offset v regionu. */
    uint32_t color_rgba;             /**< 0xRRGGBBAA. */
    char text[MB_ANNOT_TEXT_MAX];    /**< NUL-terminated UTF-8. */
} st_MB_ANNOTATION;


/**
 * @brief Přidá / přepíše annotation pro daný klíč.
 *
 * Pokud annotation pro (region_kind, sub_id, addr) už existuje, přepíše
 * její text + color. Jinak alokuje volný slot. Pokud žádný volný slot
 * není (= MAX_ENTRIES dosažen), vrací -1.
 *
 * @param region_kind   en_REGION_KIND.
 * @param region_sub_id Disambiguator.
 * @param addr          Offset.
 * @param text          UTF-8 string (truncated na MB_ANNOT_TEXT_MAX-1).
 * @param color_rgba    0xRRGGBBAA color tag (0 = výchozí neutral).
 * @return 0 OK (přepis nebo nový), -1 storage plný.
 */
int membrowser_annotations_set ( int region_kind, int region_sub_id,
                                  uint32_t addr,
                                  const char *text, uint32_t color_rgba );


/**
 * @brief Najde annotation pro daný klíč.
 *
 * @return Pointer na annotation (vlastněný storage, nemodifikovat) nebo
 *         NULL pokud žádná není.
 */
const st_MB_ANNOTATION *membrowser_annotations_find ( int region_kind,
                                                       int region_sub_id,
                                                       uint32_t addr );


/**
 * @brief Smaže annotation pro daný klíč.
 *
 * @return true pokud byla smazána, false pokud neexistovala.
 */
bool membrowser_annotations_remove ( int region_kind, int region_sub_id,
                                      uint32_t addr );


/**
 * @brief Vrací počet aktivních annotací (debug / status).
 */
int membrowser_annotations_count ( void );


/**
 * @brief Najde i-tou annotation v iterace pořadí (lineární scan).
 *
 * @param idx Index 0..membrowser_annotations_count()-1.
 * @return Pointer na annotation nebo NULL pokud idx mimo rozsah.
 */
const st_MB_ANNOTATION *membrowser_annotations_at ( int idx );


/**
 * @brief Načte annotace z perzistentního souboru.
 *
 * Volat jednou při startu (z debugger_init nebo membrowser_window_init).
 * Pokud soubor neexistuje / je prázdný, je no-op. Při syntax chybě
 * záznam ignoruje, validní zbytek dorobí.
 *
 * @param filename  Absolutní cesta k souboru.
 * @return Počet načtených annotací (>=0).
 */
int membrowser_annotations_load_from_file ( const char *filename );


/**
 * @brief Uloží všechny annotace do perzistentního souboru.
 *
 * Volat při shutdown nebo na vyžádání (uživatelské Save tlačítko).
 *
 * @param filename  Absolutní cesta.
 * @return 0 OK, -1 chyba zápisu.
 */
int membrowser_annotations_save_to_file ( const char *filename );


/**
 * @brief Vymaže všechny annotace v paměti (= reset).
 */
void membrowser_annotations_clear_all ( void );


#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_ANNOTATIONS_H */
