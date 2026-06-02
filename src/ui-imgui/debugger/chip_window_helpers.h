/**
 * @file chip_window_helpers.h
 * @brief Sdílené ImGui helpery pro per-chip debug okna (FDC, CTC, PPI, PIO, PSG).
 *
 * Vyjmuto z fdc_window.cpp aby per-chip panely (mutant per-chip-panels)
 * mohly sdílet jednotné vykreslení tabulkových řádků: hex / decimal / flag /
 * enum dekód. Cíl je konzistentní vzhled a méně duplicit (4 nová okna × ~60
 * LoC helperů = ~240 LoC duplicita bez extrakce).
 *
 * Konvence:
 *  - Volá se vždy uvnitř `ImGui::BeginTable(...)` se 3 sloupci
 *    (Name / Value / Bin). Helper sám volá `ImGui::TableNextRow()` a
 *    `ImGui::TableSetColumnIndex()`.
 *  - `tooltip` parametr (nullptr = bez tooltipu) zobrazí popup nad jménem
 *    řádku, pokud na něj user najede myší.
 *  - Hodnoty jsou vždy snapshot (volání nemá side-effecty na chipu) - per-chip
 *    panely čtou pouze mirror gettery nebo raw struct fields, nikdy ne
 *    *_read_byte().
 *
 * License: GPLv3.
 */

#ifndef CHIP_WINDOW_HELPERS_H
#define CHIP_WINDOW_HELPERS_H

#include <stdint.h>

#ifdef __cplusplus

/**
 * @brief Vykreslí řádek tabulky: jméno + hex (2 cifry) + binární zápis.
 *
 * Sloupec 0 = jméno (s volitelným tooltipem), sloupec 1 = `0xXX`,
 * sloupec 2 = 8 znaků `0`/`1` reprezentace bitů (MSB-first).
 *
 * @param name     Popisek řádku (typicky název registru / pole).
 * @param value    8-bit hodnota, vykreslí se hex i binárně.
 * @param tooltip  Volitelný popisek (nullptr = bez tooltipu).
 *
 * @pre Volající už zavolal `ImGui::BeginTable(name, 3, ...)`.
 * @post `ImGui::TableNextRow()` + 3× `ImGui::TableSetColumnIndex()` proběhly.
 */
void render_hex8_row(const char *name, uint8_t value, const char *tooltip = nullptr);

/**
 * @brief Vykreslí řádek tabulky: jméno + hex (4 cifry) + decimální zápis
 *        v sloupci Bin.
 *
 * Pro 16-bit hodnoty (např. counter `value` v CTC 8253, `preset_value` apod.)
 * kde bit-binární zobrazení by bylo nepřehledné (16 znaků), ale chceme vidět
 * jak hex, tak dekadicky.
 *
 * Sloupec 0 = jméno (s volitelným tooltipem), sloupec 1 = `0xXXXX`,
 * sloupec 2 = decimální `%u` reprezentace.
 *
 * @param name     Popisek řádku.
 * @param value    16-bit hodnota (do 65535 včetně).
 * @param tooltip  Volitelný popisek.
 *
 * @pre Volající už zavolal `ImGui::BeginTable(name, 3, ...)`.
 */
void render_hex16_row(const char *name, uint16_t value, const char *tooltip = nullptr);

/**
 * @brief Vykreslí řádek tabulky: jméno + decimální hodnota (sloupec Bin
 *        zobrazí "-").
 *
 * Pro pole která nemají bit-by-bit význam (counter, čítač transferu, ...).
 *
 * @param name     Popisek řádku.
 * @param value    Hodnota (unsigned, vykreslí se jako `%u`).
 * @param tooltip  Volitelný popisek.
 *
 * @pre Volající už zavolal `ImGui::BeginTable(name, 3, ...)`.
 */
void render_dec_row(const char *name, unsigned value, const char *tooltip = nullptr);

/**
 * @brief Vykreslí řádek tabulky: jméno + 0/1 flag (sloupec Bin "-").
 *
 * Hodnota != 0 se vykreslí zeleně jako "1", hodnota 0 se vykreslí
 * disabled jako "0" (vizuální zvýraznění aktivního flagu).
 *
 * @param name     Popisek řádku.
 * @param value    0 = neaktivní, jinak aktivní.
 * @param tooltip  Volitelný popisek.
 *
 * @pre Volající už zavolal `ImGui::BeginTable(name, 3, ...)`.
 */
void render_flag_row(const char *name, int value, const char *tooltip = nullptr);

/**
 * @brief Vykreslí řádek tabulky: jméno + enum hodnota dekódovaná na text +
 *        numerická hodnota v Bin sloupci.
 *
 * Pro pole typu `en_CTC_STATE`, `en_PIOZ80_PORT_MODE` atd. Pokud `value`
 * spadá mimo `0..count-1` rozsah, do sloupce Value se vypíše `"???"` a
 * číselná hodnota.
 *
 * @param name        Popisek řádku.
 * @param value       Numerická hodnota enum.
 * @param names       Pole stringových labelů, indexovaných `value`. Musí
 *                    obsahovat alespoň `count` položek. Prvky pole by měly
 *                    být označené `N_(...)` v místě definice (extrakce do
 *                    .pot); runtime překlad vybraného prvku přes `_()`
 *                    provádí helper sám.
 * @param count       Počet platných labelů v `names`. Hodnota mimo rozsah
 *                    bude zobrazena jako `"???"`.
 * @param tooltip     Volitelný popisek.
 *
 * @pre Volající už zavolal `ImGui::BeginTable(name, 3, ...)`.
 * @pre `names != NULL && count > 0`.
 */
void render_enum_row(const char *name, unsigned value, const char *const *names,
                     unsigned count, const char *tooltip = nullptr);

#endif /* __cplusplus */

#endif /* CHIP_WINDOW_HELPERS_H */
