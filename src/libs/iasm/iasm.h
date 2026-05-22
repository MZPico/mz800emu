/**
 * @file   iasm.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Veřejné API knihovny iasm — Z80 inline assembler.
 *
 * Definuje typy, konstanty a funkci pro překlad textové Z80 instrukce
 * na binární opkód. Knihovna je nezávislá na debuggeru, UI a zbytku
 * emulátoru.
 *
 * @par Changelog:
 * - 2026-03-14: Proběhla kompletní revize a refaktorizace. Vytvořeny unit testy.
 *
 * @par Licence:
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
 */

#ifndef IASM_H
#define IASM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

    /** @brief Maximální délka textové instrukce (+1 pro terminator 0x00).
     *  Nejdelší instrukce: LD A,SET 7,(IY+%11111111) */
#define IASM_MNEMONICS_MAXLEN 25

    /** @brief Maximální počet bajtů v binární instrukci (1–4). */
#define IASM_MAX_INS_BYTES 4

    /** @brief Maximální počet číselných proměnných v jedné instrukci. */
#define IASM_MAX_VARIABLES 2

    /**
     * @brief Chybové kódy vracené assemblerem.
     */
    typedef enum {
        IASM_OK = 0,                   /**< Úspěch — instrukce byla zkompilována. */
        IASM_ERR_INVALID_INSTRUCTION,  /**< Neplatná instrukce nebo neznámý parametr. */
        IASM_ERR_VALUE_OUT_OF_RANGE,   /**< Hodnota mimo rozsah (byte > 0xFF, rel. skok mimo ±127). */
    } iasm_error_t;

    /**
     * @brief Výstup kompilace — binární instrukce.
     */
    typedef struct {
        unsigned length;                    /**< Počet bajtů v instrukci (1–4). */
        uint8_t byte [ IASM_MAX_INS_BYTES ]; /**< Binární data instrukce. */
    } iasm_bin_t;

    /**
     * @brief Zkompiluje textovou Z80 instrukci na binární opkód.
     *
     * Provede tři fáze: parsování textu, vyhledání v tabulce opkódů
     * a generování binárního kódu. Vstup je case-insensitive, podporuje
     * formáty čísel: desítkový, hex (0x/# prefix), binární (% prefix).
     *
     * @param addr            Adresa instrukce — potřebná pro výpočet relativních skoků (JR, DJNZ).
     * @param assemble_txt    Text instrukce, např. "LD A,0x42".
     * @param compiled_output Výstupní buffer pro binární data.
     * @param error           Volitelný ukazatel na chybový kód (může být NULL).
     *                        Při úspěchu se zapíše IASM_OK.
     * @return Počet zkompilovaných bajtů (1–4), nebo 0 při chybě.
     */
    extern unsigned iasm_assemble_line ( uint16_t addr, const char *assemble_txt, iasm_bin_t *compiled_output, iasm_error_t *error );


#ifdef __cplusplus
}
#endif

#endif /* IASM_H */
