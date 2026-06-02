/**
 * @file membrowser_fill.h
 * @brief Pattern fill engine pro Memory Browser (V6).
 *
 * Pure C logika - žádný emu / ImGui dependency. Generuje výstupní bajtové
 * pole podle vybraného módu fill operace; vlastní zápis do paměti dělá
 * vyšší vrstva (membrowser_window.cpp) pres st_HEX_VIEW_BACKEND.write_bytes.
 *
 * Undo/redo je oddělený modul (membrowser_undo.{h,c}) - fill engine sám
 * neudržuje historii, vyšší vrstva před voláním fill zachytí bytes do
 * undo snapshotu.
 *
 * Testovatelné jako STANDALONE unit (test_membrowser_fill.c).
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef MEMBROWSER_FILL_H
#define MEMBROWSER_FILL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Mód fill operace.
 *
 * Stabilní číselné hodnoty - persist v cfgmain (last_fill_mode).
 * Nikdy nepřečíslovávat existující; nové módy přidávat na konec před
 * @c MB_FILL_MODE__COUNT.
 */
typedef enum en_MB_FILL_MODE
{
    MB_FILL_MODE_ZERO      = 0,   /**< Vyplň všemi 0x00. */
    MB_FILL_MODE_FF        = 1,   /**< Vyplň všemi 0xFF. */
    MB_FILL_MODE_RAMP      = 2,   /**< Ramp 0x00, 0x01, 0x02, ... wrap mod 256. */
    MB_FILL_MODE_RANDOM    = 3,   /**< Pseudo-random (seedovaný xorshift32). */
    MB_FILL_MODE_PATTERN   = 4,   /**< Opakovaný pattern z parsed user hex stringu. */
    MB_FILL_MODE_INCREMENT = 5,   /**< Increment od startovní hodnoty (start, start+1, ...) wrap mod 256. */
    MB_FILL_MODE__COUNT           /**< Sentinel - počet módů. */
} en_MB_FILL_MODE;


/**
 * @brief Maximální délka uživatelského patternu v bajtech (po parse).
 *
 * 64 bajtů stačí na typické "magic numbers" / krátké signatury bez
 * nutnosti heap alokace. Pokud uživatel zadá víc, parser vrátí chybu.
 */
#define MB_FILL_PATTERN_MAX_BYTES 64


/**
 * @brief Parametrizace fill operace.
 *
 * Lifetime: caller vlastní strukturu (typicky stack). Pole @c pattern
 * se používá jen pro @c MB_FILL_MODE_PATTERN; pro ostatní módy je
 * ignorováno.
 */
typedef struct st_MB_FILL_PARAMS
{
    int mode;                       /**< en_MB_FILL_MODE. */
    uint8_t fixed_value;            /**< MB_FILL_MODE_INCREMENT: startovní hodnota; jinak 0. */
    uint32_t random_seed;           /**< MB_FILL_MODE_RANDOM seed (0 = derive z času caller-side). */
    uint8_t pattern[MB_FILL_PATTERN_MAX_BYTES];
    int pattern_len;                /**< 1..MB_FILL_PATTERN_MAX_BYTES; jinak fill_apply vrací -1. */
} st_MB_FILL_PARAMS;


/**
 * @brief Aplikuje fill operaci na out buffer.
 *
 * @param params  Konfigurace fill operace (NESmí být NULL).
 * @param out     Cílový buffer (NESmí být NULL).
 * @param len     Počet bajtů k vyplnění (0 = no-op, vrací 0).
 * @return 0 při úspěchu, -1 při neplatném @c params (např. mode mimo enum,
 *         pattern_len <= 0 pro PATTERN mode, atd.).
 *
 * @post Při úspěchu out[0..len-1] obsahuje vygenerovaná data dle módu.
 *       Při chybě obsah out je nedefinovaný.
 */
int membrowser_fill_apply ( const st_MB_FILL_PARAMS *params,
                            uint8_t *out, size_t len );


/**
 * @brief Pomocný parser - "AA BB CC dd" -> bajty.
 *
 * Bere řetězec s hex bajty oddělenými mezerou / čárkou / žádným oddělovačem
 * (pro páry "AABBCC"). Bere 2 hex digits per byte. Ignoruje whitespace.
 *
 * @param str         Vstupní C string (NESmí být NULL).
 * @param out         Cíl pro parsed bajty (NESmí být NULL).
 * @param max_bytes   Kapacita @c out (typicky MB_FILL_PATTERN_MAX_BYTES).
 * @param[out] out_count  Počet skutečně naparsovaných bajtů (>=0).
 * @return 0 pokud byl řetězec validní (i prázdný = 0 bajtů), -1 pri syntax chybě
 *         (lichý počet hex digitů, neplatný znak), -2 pokud přesažena @c max_bytes.
 */
int membrowser_fill_parse_pattern ( const char *str,
                                     uint8_t *out, int max_bytes,
                                     int *out_count );


/**
 * @brief Lidsky čitelný název módu (anglicky, pro UI label / log).
 *
 * Vrací statický řetězec - lifetime po dobu procesu. Caller wrapuje
 * v UI vrstvě v @c _() pro gettext překlad.
 *
 * @param mode  en_MB_FILL_MODE.
 * @return      Statický řetězec (nikdy NULL).
 */
const char *membrowser_fill_mode_name ( int mode );


#ifdef __cplusplus
}
#endif

#endif /* MEMBROWSER_FILL_H */
