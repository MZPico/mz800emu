/**
 * @file   iasm_opcodes.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Tabulky opkódů Z80 pro inline assembler — generováno skriptem build_iasm_opcodes.pl.
 *
 * Obsahuje třístupňovou hierarchii tabulek:
 * - iasm_chars[] — dispatch podle prvního písmene příkazu
 * - iasm_commands_X[] — příkazy začínající na dané písmeno
 * - iasm_opts_CMD[] — varianty konkrétní instrukce (parametry, opkódy)
 *
 * Datová část je automaticky generována z disassemblerových tabulek CPU emulátoru
 * a neměla by být upravována ručně. Pro regeneraci viz build_iasm_opcodes.pl.
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

#ifndef IASM_OPCODES_H
#define	IASM_OPCODES_H

#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * @brief Varianta instrukce — popis parametrů a opkódový řetězec.
     *
     * Každý záznam popisuje jednu variantu instrukce (např. "LD A,(IX+$)").
     * Terminační záznam má hodnoty { -1, -1, -1, NULL, NULL }.
     */
    typedef struct {
        int variables;     /**< Počet číselných proměnných (0, 1, nebo 2). */
        int llen;          /**< Délka parametru vlevo od proměnné, nebo celková délka. */
        int rlen;          /**< Délka parametru vpravo od proměnné, nebo 0. */
        const char *param; /**< Textový parametr (bez příkazu), např. "A,(IX+$)". */
        const char *opcode;/**< Opkódový řetězec, např. "DD$5E". */
    } iasm_opts_t;

    /**
     * @brief Příkaz (mnemonic) s ukazatelem na pole variant.
     *
     * Terminační záznam má hodnoty { NULL, NULL }.
     */
    typedef struct {
        const char *command;     /**< Celý příkaz (uppercase), např. "LD". */
        const iasm_opts_t *opts; /**< Pole variant instrukce. */
    } iasm_commands_t;

    /**
     * @brief Dispatch tabulka — mapování prvního písmene na pole příkazů.
     *
     * Terminační záznam má hodnoty { 0x00, NULL }.
     */
    typedef struct {
        const char c;                    /**< První písmeno příkazu ('A'-'Z'). */
        const iasm_commands_t *commands; /**< Pole příkazů začínajících na toto písmeno. */
    } iasm_chars_t;


    /**
     * @defgroup iasm_opcode_tables Automaticky generované tabulky opkódů
     * @brief Následuje automaticky generovaný seznam opkódů z build_iasm_opcodes.pl.
     *
     * Tabulky pokrývají kompletní instrukční sadu Z80 včetně undocumented instrukcí
     * (IXH/IXL/IYH/IYL, SLL, LD r,ROT (IX+d), LD r,SET/RES n,(IX+d)).
     * @{
     */
static const iasm_opts_t iasm_opts_ADC [] = {
	{ 0, 6, 0, "A,(HL)", "8E" },
	{ 0, 3, 0, "A,A", "8F" },
	{ 0, 3, 0, "A,B", "88" },
	{ 0, 3, 0, "A,C", "89" },
	{ 0, 3, 0, "A,D", "8A" },
	{ 0, 3, 0, "A,E", "8B" },
	{ 0, 3, 0, "A,H", "8C" },
	{ 0, 5, 0, "A,IXH", "DD8C" },
	{ 0, 5, 0, "A,IXL", "DD8D" },
	{ 0, 5, 0, "A,IYH", "FD8C" },
	{ 0, 5, 0, "A,IYL", "FD8D" },
	{ 0, 3, 0, "A,L", "8D" },
	{ 0, 5, 0, "HL,BC", "ED4A" },
	{ 0, 5, 0, "HL,DE", "ED5A" },
	{ 0, 5, 0, "HL,HL", "ED6A" },
	{ 0, 5, 0, "HL,SP", "ED7A" },
	{ 1, 2, 0, "A,#", "CE#" },
	{ 1, 6, 1, "A,(IX+$)", "DD$8E" },
	{ 1, 6, 1, "A,(IY+$)", "FD$8E" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_ADD [] = {
	{ 0, 6, 0, "A,(HL)", "86" },
	{ 0, 3, 0, "A,A", "87" },
	{ 0, 3, 0, "A,B", "80" },
	{ 0, 3, 0, "A,C", "81" },
	{ 0, 3, 0, "A,D", "82" },
	{ 0, 3, 0, "A,E", "83" },
	{ 0, 3, 0, "A,H", "84" },
	{ 0, 5, 0, "A,IXH", "DD84" },
	{ 0, 5, 0, "A,IXL", "DD85" },
	{ 0, 5, 0, "A,IYH", "FD84" },
	{ 0, 5, 0, "A,IYL", "FD85" },
	{ 0, 3, 0, "A,L", "85" },
	{ 0, 5, 0, "HL,BC", "09" },
	{ 0, 5, 0, "HL,DE", "19" },
	{ 0, 5, 0, "HL,HL", "29" },
	{ 0, 5, 0, "HL,SP", "39" },
	{ 0, 5, 0, "IX,BC", "DD09" },
	{ 0, 5, 0, "IX,DE", "DD19" },
	{ 0, 5, 0, "IX,IX", "DD29" },
	{ 0, 5, 0, "IX,SP", "DD39" },
	{ 0, 5, 0, "IY,BC", "FD09" },
	{ 0, 5, 0, "IY,DE", "FD19" },
	{ 0, 5, 0, "IY,IY", "FD29" },
	{ 0, 5, 0, "IY,SP", "FD39" },
	{ 1, 2, 0, "A,#", "C6#" },
	{ 1, 6, 1, "A,(IX+$)", "DD$86" },
	{ 1, 6, 1, "A,(IY+$)", "FD$86" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_AND [] = {
	{ 0, 4, 0, "(HL)", "A6" },
	{ 0, 1, 0, "A", "A7" },
	{ 0, 1, 0, "B", "A0" },
	{ 0, 1, 0, "C", "A1" },
	{ 0, 1, 0, "D", "A2" },
	{ 0, 1, 0, "E", "A3" },
	{ 0, 1, 0, "H", "A4" },
	{ 0, 3, 0, "IXH", "DDA4" },
	{ 0, 3, 0, "IXL", "DDA5" },
	{ 0, 3, 0, "IYH", "FDA4" },
	{ 0, 3, 0, "IYL", "FDA5" },
	{ 0, 1, 0, "L", "A5" },
	{ 1, 0, 0, "#", "E6#" },
	{ 1, 4, 1, "(IX+$)", "DD$A6" },
	{ 1, 4, 1, "(IY+$)", "FD$A6" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_A [] = {
	{ "ADC", iasm_opts_ADC },
	{ "ADD", iasm_opts_ADD },
	{ "AND", iasm_opts_AND },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_BIT [] = {
	{ 0, 6, 0, "0,(HL)", "CB46" },
	{ 0, 3, 0, "0,A", "CB47" },
	{ 0, 3, 0, "0,B", "CB40" },
	{ 0, 3, 0, "0,C", "CB41" },
	{ 0, 3, 0, "0,D", "CB42" },
	{ 0, 3, 0, "0,E", "CB43" },
	{ 0, 3, 0, "0,H", "CB44" },
	{ 0, 3, 0, "0,L", "CB45" },
	{ 0, 6, 0, "1,(HL)", "CB4E" },
	{ 0, 3, 0, "1,A", "CB4F" },
	{ 0, 3, 0, "1,B", "CB48" },
	{ 0, 3, 0, "1,C", "CB49" },
	{ 0, 3, 0, "1,D", "CB4A" },
	{ 0, 3, 0, "1,E", "CB4B" },
	{ 0, 3, 0, "1,H", "CB4C" },
	{ 0, 3, 0, "1,L", "CB4D" },
	{ 0, 6, 0, "2,(HL)", "CB56" },
	{ 0, 3, 0, "2,A", "CB57" },
	{ 0, 3, 0, "2,B", "CB50" },
	{ 0, 3, 0, "2,C", "CB51" },
	{ 0, 3, 0, "2,D", "CB52" },
	{ 0, 3, 0, "2,E", "CB53" },
	{ 0, 3, 0, "2,H", "CB54" },
	{ 0, 3, 0, "2,L", "CB55" },
	{ 0, 6, 0, "3,(HL)", "CB5E" },
	{ 0, 3, 0, "3,A", "CB5F" },
	{ 0, 3, 0, "3,B", "CB58" },
	{ 0, 3, 0, "3,C", "CB59" },
	{ 0, 3, 0, "3,D", "CB5A" },
	{ 0, 3, 0, "3,E", "CB5B" },
	{ 0, 3, 0, "3,H", "CB5C" },
	{ 0, 3, 0, "3,L", "CB5D" },
	{ 0, 6, 0, "4,(HL)", "CB66" },
	{ 0, 3, 0, "4,A", "CB67" },
	{ 0, 3, 0, "4,B", "CB60" },
	{ 0, 3, 0, "4,C", "CB61" },
	{ 0, 3, 0, "4,D", "CB62" },
	{ 0, 3, 0, "4,E", "CB63" },
	{ 0, 3, 0, "4,H", "CB64" },
	{ 0, 3, 0, "4,L", "CB65" },
	{ 0, 6, 0, "5,(HL)", "CB6E" },
	{ 0, 3, 0, "5,A", "CB6F" },
	{ 0, 3, 0, "5,B", "CB68" },
	{ 0, 3, 0, "5,C", "CB69" },
	{ 0, 3, 0, "5,D", "CB6A" },
	{ 0, 3, 0, "5,E", "CB6B" },
	{ 0, 3, 0, "5,H", "CB6C" },
	{ 0, 3, 0, "5,L", "CB6D" },
	{ 0, 6, 0, "6,(HL)", "CB76" },
	{ 0, 3, 0, "6,A", "CB77" },
	{ 0, 3, 0, "6,B", "CB70" },
	{ 0, 3, 0, "6,C", "CB71" },
	{ 0, 3, 0, "6,D", "CB72" },
	{ 0, 3, 0, "6,E", "CB73" },
	{ 0, 3, 0, "6,H", "CB74" },
	{ 0, 3, 0, "6,L", "CB75" },
	{ 0, 6, 0, "7,(HL)", "CB7E" },
	{ 0, 3, 0, "7,A", "CB7F" },
	{ 0, 3, 0, "7,B", "CB78" },
	{ 0, 3, 0, "7,C", "CB79" },
	{ 0, 3, 0, "7,D", "CB7A" },
	{ 0, 3, 0, "7,E", "CB7B" },
	{ 0, 3, 0, "7,H", "CB7C" },
	{ 0, 3, 0, "7,L", "CB7D" },
	{ 1, 6, 1, "0,(IX+$)", "DDCB$45" },
	{ 1, 6, 1, "0,(IY+$)", "FDCB$45" },
	{ 1, 6, 1, "1,(IX+$)", "DDCB$4D" },
	{ 1, 6, 1, "1,(IY+$)", "FDCB$4D" },
	{ 1, 6, 1, "2,(IX+$)", "DDCB$55" },
	{ 1, 6, 1, "2,(IY+$)", "FDCB$55" },
	{ 1, 6, 1, "3,(IX+$)", "DDCB$5D" },
	{ 1, 6, 1, "3,(IY+$)", "FDCB$5D" },
	{ 1, 6, 1, "4,(IX+$)", "DDCB$65" },
	{ 1, 6, 1, "4,(IY+$)", "FDCB$65" },
	{ 1, 6, 1, "5,(IX+$)", "DDCB$6D" },
	{ 1, 6, 1, "5,(IY+$)", "FDCB$6D" },
	{ 1, 6, 1, "6,(IX+$)", "DDCB$75" },
	{ 1, 6, 1, "6,(IY+$)", "FDCB$75" },
	{ 1, 6, 1, "7,(IX+$)", "DDCB$7D" },
	{ 1, 6, 1, "7,(IY+$)", "FDCB$7D" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_B [] = {
	{ "BIT", iasm_opts_BIT },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_CALL [] = {
	{ 1, 0, 0, "@", "CD@" },
	{ 1, 2, 0, "C,@", "DC@" },
	{ 1, 2, 0, "M,@", "FC@" },
	{ 1, 3, 0, "NC,@", "D4@" },
	{ 1, 3, 0, "NZ,@", "C4@" },
	{ 1, 2, 0, "P,@", "F4@" },
	{ 1, 3, 0, "PE,@", "EC@" },
	{ 1, 3, 0, "PO,@", "E4@" },
	{ 1, 2, 0, "Z,@", "CC@" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_CCF [] = {
	{ 0, 0, 0, NULL, "3F" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_CP [] = {
	{ 0, 4, 0, "(HL)", "BE" },
	{ 0, 1, 0, "A", "BF" },
	{ 0, 1, 0, "B", "B8" },
	{ 0, 1, 0, "C", "B9" },
	{ 0, 1, 0, "D", "BA" },
	{ 0, 1, 0, "E", "BB" },
	{ 0, 1, 0, "H", "BC" },
	{ 0, 3, 0, "IXH", "DDBC" },
	{ 0, 3, 0, "IXL", "DDBD" },
	{ 0, 3, 0, "IYH", "FDBC" },
	{ 0, 3, 0, "IYL", "FDBD" },
	{ 0, 1, 0, "L", "BD" },
	{ 1, 0, 0, "#", "FE#" },
	{ 1, 4, 1, "(IX+$)", "DD$BE" },
	{ 1, 4, 1, "(IY+$)", "FD$BE" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_CPD [] = {
	{ 0, 0, 0, NULL, "EDA9" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_CPDR [] = {
	{ 0, 0, 0, NULL, "EDB9" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_CPI [] = {
	{ 0, 0, 0, NULL, "EDA1" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_CPIR [] = {
	{ 0, 0, 0, NULL, "EDB1" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_CPL [] = {
	{ 0, 0, 0, NULL, "2F" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_C [] = {
	{ "CALL", iasm_opts_CALL },
	{ "CCF", iasm_opts_CCF },
	{ "CP", iasm_opts_CP },
	{ "CPD", iasm_opts_CPD },
	{ "CPDR", iasm_opts_CPDR },
	{ "CPI", iasm_opts_CPI },
	{ "CPIR", iasm_opts_CPIR },
	{ "CPL", iasm_opts_CPL },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_DAA [] = {
	{ 0, 0, 0, NULL, "27" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_DEC [] = {
	{ 0, 4, 0, "(HL)", "35" },
	{ 0, 1, 0, "A", "3D" },
	{ 0, 1, 0, "B", "05" },
	{ 0, 2, 0, "BC", "0B" },
	{ 0, 1, 0, "C", "0D" },
	{ 0, 1, 0, "D", "15" },
	{ 0, 2, 0, "DE", "1B" },
	{ 0, 1, 0, "E", "1D" },
	{ 0, 1, 0, "H", "25" },
	{ 0, 2, 0, "HL", "2B" },
	{ 0, 2, 0, "IX", "DD2B" },
	{ 0, 3, 0, "IXH", "DD25" },
	{ 0, 3, 0, "IXL", "DD2D" },
	{ 0, 2, 0, "IY", "FD2B" },
	{ 0, 3, 0, "IYH", "FD25" },
	{ 0, 3, 0, "IYL", "FD2D" },
	{ 0, 1, 0, "L", "2D" },
	{ 0, 2, 0, "SP", "3B" },
	{ 1, 4, 1, "(IX+$)", "DD$35" },
	{ 1, 4, 1, "(IY+$)", "FD$35" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_DI [] = {
	{ 0, 0, 0, NULL, "F3" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_DJNZ [] = {
	{ 1, 0, 0, "%", "10%" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_D [] = {
	{ "DAA", iasm_opts_DAA },
	{ "DEC", iasm_opts_DEC },
	{ "DI", iasm_opts_DI },
	{ "DJNZ", iasm_opts_DJNZ },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_EI [] = {
	{ 0, 0, 0, NULL, "FB" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_EX [] = {
	{ 0, 7, 0, "(SP),HL", "E3" },
	{ 0, 7, 0, "(SP),IX", "DDE3" },
	{ 0, 7, 0, "(SP),IY", "FDE3" },
	{ 0, 6, 0, "AF,AF'", "08" },
	{ 0, 5, 0, "DE,HL", "EB" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_EXX [] = {
	{ 0, 0, 0, NULL, "D9" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_E [] = {
	{ "EI", iasm_opts_EI },
	{ "EX", iasm_opts_EX },
	{ "EXX", iasm_opts_EXX },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_HALT [] = {
	{ 0, 0, 0, NULL, "76" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_H [] = {
	{ "HALT", iasm_opts_HALT },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_IM [] = {
	{ 0, 1, 0, "0", "ED46" },
	{ 0, 1, 0, "1", "ED56" },
	{ 0, 1, 0, "2", "ED5E" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_IN [] = {
	{ 0, 5, 0, "A,(C)", "ED78" },
	{ 0, 5, 0, "B,(C)", "ED40" },
	{ 0, 5, 0, "C,(C)", "ED48" },
	{ 0, 5, 0, "D,(C)", "ED50" },
	{ 0, 5, 0, "E,(C)", "ED58" },
	{ 0, 5, 0, "H,(C)", "ED60" },
	{ 0, 5, 0, "L,(C)", "ED68" },
	{ 1, 3, 1, "A,(#)", "DB#" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_INC [] = {
	{ 0, 4, 0, "(HL)", "34" },
	{ 0, 1, 0, "A", "3C" },
	{ 0, 1, 0, "B", "04" },
	{ 0, 2, 0, "BC", "03" },
	{ 0, 1, 0, "C", "0C" },
	{ 0, 1, 0, "D", "14" },
	{ 0, 2, 0, "DE", "13" },
	{ 0, 1, 0, "E", "1C" },
	{ 0, 1, 0, "H", "24" },
	{ 0, 2, 0, "HL", "23" },
	{ 0, 2, 0, "IX", "DD23" },
	{ 0, 3, 0, "IXH", "DD24" },
	{ 0, 3, 0, "IXL", "DD2C" },
	{ 0, 2, 0, "IY", "FD23" },
	{ 0, 3, 0, "IYH", "FD24" },
	{ 0, 3, 0, "IYL", "FD2C" },
	{ 0, 1, 0, "L", "2C" },
	{ 0, 2, 0, "SP", "33" },
	{ 1, 4, 1, "(IX+$)", "DD$34" },
	{ 1, 4, 1, "(IY+$)", "FD$34" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_IND [] = {
	{ 0, 0, 0, NULL, "EDAA" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_INDR [] = {
	{ 0, 0, 0, NULL, "EDBA" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_INI [] = {
	{ 0, 0, 0, NULL, "EDA2" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_INIR [] = {
	{ 0, 0, 0, NULL, "EDB2" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_IN_F [] = {
	{ 0, 3, 0, "(C)", "ED70" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_I [] = {
	{ "IM", iasm_opts_IM },
	{ "IN", iasm_opts_IN },
	{ "INC", iasm_opts_INC },
	{ "IND", iasm_opts_IND },
	{ "INDR", iasm_opts_INDR },
	{ "INI", iasm_opts_INI },
	{ "INIR", iasm_opts_INIR },
	{ "IN_F", iasm_opts_IN_F },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_JP [] = {
	{ 0, 2, 0, "HL", "E9" },
	{ 0, 2, 0, "IX", "DDE9" },
	{ 0, 2, 0, "IY", "FDE9" },
	{ 1, 0, 0, "@", "C3@" },
	{ 1, 2, 0, "C,@", "DA@" },
	{ 1, 2, 0, "M,@", "FA@" },
	{ 1, 3, 0, "NC,@", "D2@" },
	{ 1, 3, 0, "NZ,@", "C2@" },
	{ 1, 2, 0, "P,@", "F2@" },
	{ 1, 3, 0, "PE,@", "EA@" },
	{ 1, 3, 0, "PO,@", "E2@" },
	{ 1, 2, 0, "Z,@", "CA@" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_JR [] = {
	{ 1, 0, 0, "%", "18%" },
	{ 1, 2, 0, "C,%", "38%" },
	{ 1, 3, 0, "NC,%", "30%" },
	{ 1, 3, 0, "NZ,%", "20%" },
	{ 1, 2, 0, "Z,%", "28%" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_J [] = {
	{ "JP", iasm_opts_JP },
	{ "JR", iasm_opts_JR },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_LD [] = {
	{ 0, 6, 0, "(BC),A", "02" },
	{ 0, 6, 0, "(DE),A", "12" },
	{ 0, 6, 0, "(HL),A", "77" },
	{ 0, 6, 0, "(HL),B", "70" },
	{ 0, 6, 0, "(HL),C", "71" },
	{ 0, 6, 0, "(HL),D", "72" },
	{ 0, 6, 0, "(HL),E", "73" },
	{ 0, 6, 0, "(HL),H", "74" },
	{ 0, 6, 0, "(HL),L", "75" },
	{ 0, 6, 0, "A,(BC)", "0A" },
	{ 0, 6, 0, "A,(DE)", "1A" },
	{ 0, 6, 0, "A,(HL)", "7E" },
	{ 0, 3, 0, "A,A", "7F" },
	{ 0, 3, 0, "A,B", "78" },
	{ 0, 3, 0, "A,C", "79" },
	{ 0, 3, 0, "A,D", "7A" },
	{ 0, 3, 0, "A,E", "7B" },
	{ 0, 3, 0, "A,H", "7C" },
	{ 0, 5, 0, "A,IXH", "DD7C" },
	{ 0, 5, 0, "A,IXL", "DD7D" },
	{ 0, 5, 0, "A,IYH", "FD7C" },
	{ 0, 5, 0, "A,IYL", "FD7D" },
	{ 0, 3, 0, "A,L", "7D" },
	{ 0, 6, 0, "B,(HL)", "46" },
	{ 0, 3, 0, "B,A", "47" },
	{ 0, 3, 0, "B,B", "40" },
	{ 0, 3, 0, "B,C", "41" },
	{ 0, 3, 0, "B,D", "42" },
	{ 0, 3, 0, "B,E", "43" },
	{ 0, 3, 0, "B,H", "44" },
	{ 0, 5, 0, "B,IXH", "DD44" },
	{ 0, 5, 0, "B,IXL", "DD45" },
	{ 0, 5, 0, "B,IYH", "FD44" },
	{ 0, 5, 0, "B,IYL", "FD45" },
	{ 0, 3, 0, "B,L", "45" },
	{ 0, 6, 0, "C,(HL)", "4E" },
	{ 0, 3, 0, "C,A", "4F" },
	{ 0, 3, 0, "C,B", "48" },
	{ 0, 3, 0, "C,C", "49" },
	{ 0, 3, 0, "C,D", "4A" },
	{ 0, 3, 0, "C,E", "4B" },
	{ 0, 3, 0, "C,H", "4C" },
	{ 0, 5, 0, "C,IXH", "DD4C" },
	{ 0, 5, 0, "C,IXL", "DD4D" },
	{ 0, 5, 0, "C,IYH", "FD4C" },
	{ 0, 5, 0, "C,IYL", "FD4D" },
	{ 0, 3, 0, "C,L", "4D" },
	{ 0, 6, 0, "D,(HL)", "56" },
	{ 0, 3, 0, "D,A", "57" },
	{ 0, 3, 0, "D,B", "50" },
	{ 0, 3, 0, "D,C", "51" },
	{ 0, 3, 0, "D,D", "52" },
	{ 0, 3, 0, "D,E", "53" },
	{ 0, 3, 0, "D,H", "54" },
	{ 0, 5, 0, "D,IXH", "DD54" },
	{ 0, 5, 0, "D,IXL", "DD55" },
	{ 0, 5, 0, "D,IYH", "FD54" },
	{ 0, 5, 0, "D,IYL", "FD55" },
	{ 0, 3, 0, "D,L", "55" },
	{ 0, 6, 0, "E,(HL)", "5E" },
	{ 0, 3, 0, "E,A", "5F" },
	{ 0, 3, 0, "E,B", "58" },
	{ 0, 3, 0, "E,C", "59" },
	{ 0, 3, 0, "E,D", "5A" },
	{ 0, 3, 0, "E,E", "5B" },
	{ 0, 3, 0, "E,H", "5C" },
	{ 0, 5, 0, "E,IXH", "DD5C" },
	{ 0, 5, 0, "E,IXL", "DD5D" },
	{ 0, 5, 0, "E,IYH", "FD5C" },
	{ 0, 5, 0, "E,IYL", "FD5D" },
	{ 0, 3, 0, "E,L", "5D" },
	{ 0, 6, 0, "H,(HL)", "66" },
	{ 0, 3, 0, "H,A", "67" },
	{ 0, 3, 0, "H,B", "60" },
	{ 0, 3, 0, "H,C", "61" },
	{ 0, 3, 0, "H,D", "62" },
	{ 0, 3, 0, "H,E", "63" },
	{ 0, 3, 0, "H,H", "64" },
	{ 0, 3, 0, "H,L", "65" },
	{ 0, 3, 0, "I,A", "ED47" },
	{ 0, 5, 0, "IXH,A", "DD67" },
	{ 0, 5, 0, "IXH,B", "DD60" },
	{ 0, 5, 0, "IXH,C", "DD61" },
	{ 0, 5, 0, "IXH,D", "DD62" },
	{ 0, 5, 0, "IXH,E", "DD63" },
	{ 0, 7, 0, "IXH,IXH", "DD64" },
	{ 0, 7, 0, "IXH,IXL", "DD65" },
	{ 0, 5, 0, "IXL,A", "DD6F" },
	{ 0, 5, 0, "IXL,B", "DD68" },
	{ 0, 5, 0, "IXL,C", "DD69" },
	{ 0, 5, 0, "IXL,D", "DD6A" },
	{ 0, 5, 0, "IXL,E", "DD6B" },
	{ 0, 7, 0, "IXL,IXH", "DD6C" },
	{ 0, 7, 0, "IXL,IXL", "DD6D" },
	{ 0, 5, 0, "IYH,A", "FD67" },
	{ 0, 5, 0, "IYH,B", "FD60" },
	{ 0, 5, 0, "IYH,C", "FD61" },
	{ 0, 5, 0, "IYH,D", "FD62" },
	{ 0, 5, 0, "IYH,E", "FD63" },
	{ 0, 7, 0, "IYH,IYH", "FD64" },
	{ 0, 7, 0, "IYH,IYL", "FD65" },
	{ 0, 5, 0, "IYL,A", "FD6F" },
	{ 0, 5, 0, "IYL,B", "FD68" },
	{ 0, 5, 0, "IYL,C", "FD69" },
	{ 0, 5, 0, "IYL,D", "FD6A" },
	{ 0, 5, 0, "IYL,E", "FD6B" },
	{ 0, 7, 0, "IYL,IYH", "FD6C" },
	{ 0, 7, 0, "IYL,IYL", "FD6D" },
	{ 0, 6, 0, "L,(HL)", "6E" },
	{ 0, 3, 0, "L,A", "6F" },
	{ 0, 3, 0, "L,B", "68" },
	{ 0, 3, 0, "L,C", "69" },
	{ 0, 3, 0, "L,D", "6A" },
	{ 0, 3, 0, "L,E", "6B" },
	{ 0, 3, 0, "L,H", "6C" },
	{ 0, 3, 0, "L,L", "6D" },
	{ 0, 5, 0, "SP,HL", "F9" },
	{ 0, 5, 0, "SP,IX", "DDF9" },
	{ 0, 5, 0, "SP,IY", "FDF9" },
	{ 1, 1, 3, "(@),A", "32@" },
	{ 1, 1, 4, "(@),BC", "ED43@" },
	{ 1, 1, 4, "(@),DE", "ED53@" },
	{ 1, 1, 4, "(@),HL", "22@" },
	{ 1, 1, 4, "(@),IX", "DD22@" },
	{ 1, 1, 4, "(@),IY", "FD22@" },
	{ 1, 1, 4, "(@),SP", "ED73@" },
	{ 1, 5, 0, "(HL),#", "36#" },
	{ 2, 4, 2, "(IX+$),#", "DD36$#" },
	{ 1, 4, 3, "(IX+$),A", "DD$77" },
	{ 1, 4, 3, "(IX+$),B", "DD$70" },
	{ 1, 4, 3, "(IX+$),C", "DD$71" },
	{ 1, 4, 3, "(IX+$),D", "DD$72" },
	{ 1, 4, 3, "(IX+$),E", "DD$73" },
	{ 1, 4, 3, "(IX+$),H", "DD$74" },
	{ 1, 4, 3, "(IX+$),L", "DD$75" },
	{ 2, 4, 2, "(IY+$),#", "FD36$#" },
	{ 1, 4, 3, "(IY+$),A", "FD$77" },
	{ 1, 4, 3, "(IY+$),B", "FD$70" },
	{ 1, 4, 3, "(IY+$),C", "FD$71" },
	{ 1, 4, 3, "(IY+$),D", "FD$72" },
	{ 1, 4, 3, "(IY+$),E", "FD$73" },
	{ 1, 4, 3, "(IY+$),H", "FD$74" },
	{ 1, 4, 3, "(IY+$),L", "FD$75" },
	{ 1, 2, 0, "A,#", "3E#" },
	{ 1, 3, 1, "A,(@)", "3A@" },
	{ 1, 6, 1, "A,(IX+$)", "DD$7E" },
	{ 1, 6, 1, "A,(IY+$)", "FD$7E" },
	{ 1, 12, 1, "A,RES 0,(IX+$)", "DDCB$87" },
	{ 1, 12, 1, "A,RES 0,(IY+$)", "FDCB$87" },
	{ 1, 12, 1, "A,RES 1,(IX+$)", "DDCB$8F" },
	{ 1, 12, 1, "A,RES 1,(IY+$)", "FDCB$8F" },
	{ 1, 12, 1, "A,RES 2,(IX+$)", "DDCB$97" },
	{ 1, 12, 1, "A,RES 2,(IY+$)", "FDCB$97" },
	{ 1, 12, 1, "A,RES 3,(IX+$)", "DDCB$9F" },
	{ 1, 12, 1, "A,RES 3,(IY+$)", "FDCB$9F" },
	{ 1, 12, 1, "A,RES 4,(IX+$)", "DDCB$A7" },
	{ 1, 12, 1, "A,RES 4,(IY+$)", "FDCB$A7" },
	{ 1, 12, 1, "A,RES 5,(IX+$)", "DDCB$AF" },
	{ 1, 12, 1, "A,RES 5,(IY+$)", "FDCB$AF" },
	{ 1, 12, 1, "A,RES 6,(IX+$)", "DDCB$B7" },
	{ 1, 12, 1, "A,RES 6,(IY+$)", "FDCB$B7" },
	{ 1, 12, 1, "A,RES 7,(IX+$)", "DDCB$BF" },
	{ 1, 12, 1, "A,RES 7,(IY+$)", "FDCB$BF" },
	{ 1, 9, 1, "A,RL (IX+$)", "DDCB$17" },
	{ 1, 9, 1, "A,RL (IY+$)", "FDCB$17" },
	{ 1, 10, 1, "A,RLC (IX+$)", "DDCB$07" },
	{ 1, 10, 1, "A,RLC (IY+$)", "FDCB$07" },
	{ 1, 9, 1, "A,RR (IX+$)", "DDCB$1F" },
	{ 1, 9, 1, "A,RR (IY+$)", "FDCB$1F" },
	{ 1, 10, 1, "A,RRC (IX+$)", "DDCB$0F" },
	{ 1, 10, 1, "A,RRC (IY+$)", "FDCB$0F" },
	{ 1, 12, 1, "A,SET 0,(IX+$)", "DDCB$C7" },
	{ 1, 12, 1, "A,SET 0,(IY+$)", "FDCB$C7" },
	{ 1, 12, 1, "A,SET 1,(IX+$)", "DDCB$CF" },
	{ 1, 12, 1, "A,SET 1,(IY+$)", "FDCB$CF" },
	{ 1, 12, 1, "A,SET 2,(IX+$)", "DDCB$D7" },
	{ 1, 12, 1, "A,SET 2,(IY+$)", "FDCB$D7" },
	{ 1, 12, 1, "A,SET 3,(IX+$)", "DDCB$DF" },
	{ 1, 12, 1, "A,SET 3,(IY+$)", "FDCB$DF" },
	{ 1, 12, 1, "A,SET 4,(IX+$)", "DDCB$E7" },
	{ 1, 12, 1, "A,SET 4,(IY+$)", "FDCB$E7" },
	{ 1, 12, 1, "A,SET 5,(IX+$)", "DDCB$EF" },
	{ 1, 12, 1, "A,SET 5,(IY+$)", "FDCB$EF" },
	{ 1, 12, 1, "A,SET 6,(IX+$)", "DDCB$F7" },
	{ 1, 12, 1, "A,SET 6,(IY+$)", "FDCB$F7" },
	{ 1, 12, 1, "A,SET 7,(IX+$)", "DDCB$FF" },
	{ 1, 12, 1, "A,SET 7,(IY+$)", "FDCB$FF" },
	{ 1, 10, 1, "A,SLA (IX+$)", "DDCB$27" },
	{ 1, 10, 1, "A,SLA (IY+$)", "FDCB$27" },
	{ 1, 10, 1, "A,SLL (IX+$)", "DDCB$37" },
	{ 1, 10, 1, "A,SLL (IY+$)", "FDCB$37" },
	{ 1, 10, 1, "A,SRA (IX+$)", "DDCB$2F" },
	{ 1, 10, 1, "A,SRA (IY+$)", "FDCB$2F" },
	{ 1, 10, 1, "A,SRL (IX+$)", "DDCB$3F" },
	{ 1, 10, 1, "A,SRL (IY+$)", "FDCB$3F" },
	{ 1, 2, 0, "B,#", "06#" },
	{ 1, 6, 1, "B,(IX+$)", "DD$46" },
	{ 1, 6, 1, "B,(IY+$)", "FD$46" },
	{ 1, 12, 1, "B,RES 0,(IX+$)", "DDCB$80" },
	{ 1, 12, 1, "B,RES 0,(IY+$)", "FDCB$80" },
	{ 1, 12, 1, "B,RES 1,(IX+$)", "DDCB$88" },
	{ 1, 12, 1, "B,RES 1,(IY+$)", "FDCB$88" },
	{ 1, 12, 1, "B,RES 2,(IX+$)", "DDCB$90" },
	{ 1, 12, 1, "B,RES 2,(IY+$)", "FDCB$90" },
	{ 1, 12, 1, "B,RES 3,(IX+$)", "DDCB$98" },
	{ 1, 12, 1, "B,RES 3,(IY+$)", "FDCB$98" },
	{ 1, 12, 1, "B,RES 4,(IX+$)", "DDCB$A0" },
	{ 1, 12, 1, "B,RES 4,(IY+$)", "FDCB$A0" },
	{ 1, 12, 1, "B,RES 5,(IX+$)", "DDCB$A8" },
	{ 1, 12, 1, "B,RES 5,(IY+$)", "FDCB$A8" },
	{ 1, 12, 1, "B,RES 6,(IX+$)", "DDCB$B0" },
	{ 1, 12, 1, "B,RES 6,(IY+$)", "FDCB$B0" },
	{ 1, 12, 1, "B,RES 7,(IX+$)", "DDCB$B8" },
	{ 1, 12, 1, "B,RES 7,(IY+$)", "FDCB$B8" },
	{ 1, 9, 1, "B,RL (IX+$)", "DDCB$10" },
	{ 1, 9, 1, "B,RL (IY+$)", "FDCB$10" },
	{ 1, 10, 1, "B,RLC (IX+$)", "DDCB$00" },
	{ 1, 10, 1, "B,RLC (IY+$)", "FDCB$00" },
	{ 1, 9, 1, "B,RR (IX+$)", "DDCB$18" },
	{ 1, 9, 1, "B,RR (IY+$)", "FDCB$18" },
	{ 1, 10, 1, "B,RRC (IX+$)", "DDCB$08" },
	{ 1, 10, 1, "B,RRC (IY+$)", "FDCB$08" },
	{ 1, 12, 1, "B,SET 0,(IX+$)", "DDCB$C0" },
	{ 1, 12, 1, "B,SET 0,(IY+$)", "FDCB$C0" },
	{ 1, 12, 1, "B,SET 1,(IX+$)", "DDCB$C8" },
	{ 1, 12, 1, "B,SET 1,(IY+$)", "FDCB$C8" },
	{ 1, 12, 1, "B,SET 2,(IX+$)", "DDCB$D0" },
	{ 1, 12, 1, "B,SET 2,(IY+$)", "FDCB$D0" },
	{ 1, 12, 1, "B,SET 3,(IX+$)", "DDCB$D8" },
	{ 1, 12, 1, "B,SET 3,(IY+$)", "FDCB$D8" },
	{ 1, 12, 1, "B,SET 4,(IX+$)", "DDCB$E0" },
	{ 1, 12, 1, "B,SET 4,(IY+$)", "FDCB$E0" },
	{ 1, 12, 1, "B,SET 5,(IX+$)", "DDCB$E8" },
	{ 1, 12, 1, "B,SET 5,(IY+$)", "FDCB$E8" },
	{ 1, 12, 1, "B,SET 6,(IX+$)", "DDCB$F0" },
	{ 1, 12, 1, "B,SET 6,(IY+$)", "FDCB$F0" },
	{ 1, 12, 1, "B,SET 7,(IX+$)", "DDCB$F8" },
	{ 1, 12, 1, "B,SET 7,(IY+$)", "FDCB$F8" },
	{ 1, 10, 1, "B,SLA (IX+$)", "DDCB$20" },
	{ 1, 10, 1, "B,SLA (IY+$)", "FDCB$20" },
	{ 1, 10, 1, "B,SLL (IX+$)", "DDCB$30" },
	{ 1, 10, 1, "B,SLL (IY+$)", "FDCB$30" },
	{ 1, 10, 1, "B,SRA (IX+$)", "DDCB$28" },
	{ 1, 10, 1, "B,SRA (IY+$)", "FDCB$28" },
	{ 1, 10, 1, "B,SRL (IX+$)", "DDCB$38" },
	{ 1, 10, 1, "B,SRL (IY+$)", "FDCB$38" },
	{ 1, 4, 1, "BC,(@)", "ED4B@" },
	{ 1, 3, 0, "BC,@", "01@" },
	{ 1, 2, 0, "C,#", "0E#" },
	{ 1, 6, 1, "C,(IX+$)", "DD$4E" },
	{ 1, 6, 1, "C,(IY+$)", "FD$4E" },
	{ 1, 12, 1, "C,RES 0,(IX+$)", "DDCB$81" },
	{ 1, 12, 1, "C,RES 0,(IY+$)", "FDCB$81" },
	{ 1, 12, 1, "C,RES 1,(IX+$)", "DDCB$89" },
	{ 1, 12, 1, "C,RES 1,(IY+$)", "FDCB$89" },
	{ 1, 12, 1, "C,RES 2,(IX+$)", "DDCB$91" },
	{ 1, 12, 1, "C,RES 2,(IY+$)", "FDCB$91" },
	{ 1, 12, 1, "C,RES 3,(IX+$)", "DDCB$99" },
	{ 1, 12, 1, "C,RES 3,(IY+$)", "FDCB$99" },
	{ 1, 12, 1, "C,RES 4,(IX+$)", "DDCB$A1" },
	{ 1, 12, 1, "C,RES 4,(IY+$)", "FDCB$A1" },
	{ 1, 12, 1, "C,RES 5,(IX+$)", "DDCB$A9" },
	{ 1, 12, 1, "C,RES 5,(IY+$)", "FDCB$A9" },
	{ 1, 12, 1, "C,RES 6,(IX+$)", "DDCB$B1" },
	{ 1, 12, 1, "C,RES 6,(IY+$)", "FDCB$B1" },
	{ 1, 12, 1, "C,RES 7,(IX+$)", "DDCB$B9" },
	{ 1, 12, 1, "C,RES 7,(IY+$)", "FDCB$B9" },
	{ 1, 9, 1, "C,RL (IX+$)", "DDCB$11" },
	{ 1, 9, 1, "C,RL (IY+$)", "FDCB$11" },
	{ 1, 10, 1, "C,RLC (IX+$)", "DDCB$01" },
	{ 1, 10, 1, "C,RLC (IY+$)", "FDCB$01" },
	{ 1, 9, 1, "C,RR (IX+$)", "DDCB$19" },
	{ 1, 9, 1, "C,RR (IY+$)", "FDCB$19" },
	{ 1, 10, 1, "C,RRC (IX+$)", "DDCB$09" },
	{ 1, 10, 1, "C,RRC (IY+$)", "FDCB$09" },
	{ 1, 12, 1, "C,SET 0,(IX+$)", "DDCB$C1" },
	{ 1, 12, 1, "C,SET 0,(IY+$)", "FDCB$C1" },
	{ 1, 12, 1, "C,SET 1,(IX+$)", "DDCB$C9" },
	{ 1, 12, 1, "C,SET 1,(IY+$)", "FDCB$C9" },
	{ 1, 12, 1, "C,SET 2,(IX+$)", "DDCB$D1" },
	{ 1, 12, 1, "C,SET 2,(IY+$)", "FDCB$D1" },
	{ 1, 12, 1, "C,SET 3,(IX+$)", "DDCB$D9" },
	{ 1, 12, 1, "C,SET 3,(IY+$)", "FDCB$D9" },
	{ 1, 12, 1, "C,SET 4,(IX+$)", "DDCB$E1" },
	{ 1, 12, 1, "C,SET 4,(IY+$)", "FDCB$E1" },
	{ 1, 12, 1, "C,SET 5,(IX+$)", "DDCB$E9" },
	{ 1, 12, 1, "C,SET 5,(IY+$)", "FDCB$E9" },
	{ 1, 12, 1, "C,SET 6,(IX+$)", "DDCB$F1" },
	{ 1, 12, 1, "C,SET 6,(IY+$)", "FDCB$F1" },
	{ 1, 12, 1, "C,SET 7,(IX+$)", "DDCB$F9" },
	{ 1, 12, 1, "C,SET 7,(IY+$)", "FDCB$F9" },
	{ 1, 10, 1, "C,SLA (IX+$)", "DDCB$21" },
	{ 1, 10, 1, "C,SLA (IY+$)", "FDCB$21" },
	{ 1, 10, 1, "C,SLL (IX+$)", "DDCB$31" },
	{ 1, 10, 1, "C,SLL (IY+$)", "FDCB$31" },
	{ 1, 10, 1, "C,SRA (IX+$)", "DDCB$29" },
	{ 1, 10, 1, "C,SRA (IY+$)", "FDCB$29" },
	{ 1, 10, 1, "C,SRL (IX+$)", "DDCB$39" },
	{ 1, 10, 1, "C,SRL (IY+$)", "FDCB$39" },
	{ 1, 2, 0, "D,#", "16#" },
	{ 1, 6, 1, "D,(IX+$)", "DD$56" },
	{ 1, 6, 1, "D,(IY+$)", "FD$56" },
	{ 1, 12, 1, "D,RES 0,(IX+$)", "DDCB$82" },
	{ 1, 12, 1, "D,RES 0,(IY+$)", "FDCB$82" },
	{ 1, 12, 1, "D,RES 1,(IX+$)", "DDCB$8A" },
	{ 1, 12, 1, "D,RES 1,(IY+$)", "FDCB$8A" },
	{ 1, 12, 1, "D,RES 2,(IX+$)", "DDCB$92" },
	{ 1, 12, 1, "D,RES 2,(IY+$)", "FDCB$92" },
	{ 1, 12, 1, "D,RES 3,(IX+$)", "DDCB$9A" },
	{ 1, 12, 1, "D,RES 3,(IY+$)", "FDCB$9A" },
	{ 1, 12, 1, "D,RES 4,(IX+$)", "DDCB$A2" },
	{ 1, 12, 1, "D,RES 4,(IY+$)", "FDCB$A2" },
	{ 1, 12, 1, "D,RES 5,(IX+$)", "DDCB$AA" },
	{ 1, 12, 1, "D,RES 5,(IY+$)", "FDCB$AA" },
	{ 1, 12, 1, "D,RES 6,(IX+$)", "DDCB$B2" },
	{ 1, 12, 1, "D,RES 6,(IY+$)", "FDCB$B2" },
	{ 1, 12, 1, "D,RES 7,(IX+$)", "DDCB$BA" },
	{ 1, 12, 1, "D,RES 7,(IY+$)", "FDCB$BA" },
	{ 1, 9, 1, "D,RL (IX+$)", "DDCB$12" },
	{ 1, 9, 1, "D,RL (IY+$)", "FDCB$12" },
	{ 1, 10, 1, "D,RLC (IX+$)", "DDCB$02" },
	{ 1, 10, 1, "D,RLC (IY+$)", "FDCB$02" },
	{ 1, 9, 1, "D,RR (IX+$)", "DDCB$1A" },
	{ 1, 9, 1, "D,RR (IY+$)", "FDCB$1A" },
	{ 1, 10, 1, "D,RRC (IX+$)", "DDCB$0A" },
	{ 1, 10, 1, "D,RRC (IY+$)", "FDCB$0A" },
	{ 1, 12, 1, "D,SET 0,(IX+$)", "DDCB$C2" },
	{ 1, 12, 1, "D,SET 0,(IY+$)", "FDCB$C2" },
	{ 1, 12, 1, "D,SET 1,(IX+$)", "DDCB$CA" },
	{ 1, 12, 1, "D,SET 1,(IY+$)", "FDCB$CA" },
	{ 1, 12, 1, "D,SET 2,(IX+$)", "DDCB$D2" },
	{ 1, 12, 1, "D,SET 2,(IY+$)", "FDCB$D2" },
	{ 1, 12, 1, "D,SET 3,(IX+$)", "DDCB$DA" },
	{ 1, 12, 1, "D,SET 3,(IY+$)", "FDCB$DA" },
	{ 1, 12, 1, "D,SET 4,(IX+$)", "DDCB$E2" },
	{ 1, 12, 1, "D,SET 4,(IY+$)", "FDCB$E2" },
	{ 1, 12, 1, "D,SET 5,(IX+$)", "DDCB$EA" },
	{ 1, 12, 1, "D,SET 5,(IY+$)", "FDCB$EA" },
	{ 1, 12, 1, "D,SET 6,(IX+$)", "DDCB$F2" },
	{ 1, 12, 1, "D,SET 6,(IY+$)", "FDCB$F2" },
	{ 1, 12, 1, "D,SET 7,(IX+$)", "DDCB$FA" },
	{ 1, 12, 1, "D,SET 7,(IY+$)", "FDCB$FA" },
	{ 1, 10, 1, "D,SLA (IX+$)", "DDCB$22" },
	{ 1, 10, 1, "D,SLA (IY+$)", "FDCB$22" },
	{ 1, 10, 1, "D,SLL (IX+$)", "DDCB$32" },
	{ 1, 10, 1, "D,SLL (IY+$)", "FDCB$32" },
	{ 1, 10, 1, "D,SRA (IX+$)", "DDCB$2A" },
	{ 1, 10, 1, "D,SRA (IY+$)", "FDCB$2A" },
	{ 1, 10, 1, "D,SRL (IX+$)", "DDCB$3A" },
	{ 1, 10, 1, "D,SRL (IY+$)", "FDCB$3A" },
	{ 1, 4, 1, "DE,(@)", "ED5B@" },
	{ 1, 3, 0, "DE,@", "11@" },
	{ 1, 2, 0, "E,#", "1E#" },
	{ 1, 6, 1, "E,(IX+$)", "DD$5E" },
	{ 1, 6, 1, "E,(IY+$)", "FD$5E" },
	{ 1, 12, 1, "E,RES 0,(IX+$)", "DDCB$83" },
	{ 1, 12, 1, "E,RES 0,(IY+$)", "FDCB$83" },
	{ 1, 12, 1, "E,RES 1,(IX+$)", "DDCB$8B" },
	{ 1, 12, 1, "E,RES 1,(IY+$)", "FDCB$8B" },
	{ 1, 12, 1, "E,RES 2,(IX+$)", "DDCB$93" },
	{ 1, 12, 1, "E,RES 2,(IY+$)", "FDCB$93" },
	{ 1, 12, 1, "E,RES 3,(IX+$)", "DDCB$9B" },
	{ 1, 12, 1, "E,RES 3,(IY+$)", "FDCB$9B" },
	{ 1, 12, 1, "E,RES 4,(IX+$)", "DDCB$A3" },
	{ 1, 12, 1, "E,RES 4,(IY+$)", "FDCB$A3" },
	{ 1, 12, 1, "E,RES 5,(IX+$)", "DDCB$AB" },
	{ 1, 12, 1, "E,RES 5,(IY+$)", "FDCB$AB" },
	{ 1, 12, 1, "E,RES 6,(IX+$)", "DDCB$B3" },
	{ 1, 12, 1, "E,RES 6,(IY+$)", "FDCB$B3" },
	{ 1, 12, 1, "E,RES 7,(IX+$)", "DDCB$BB" },
	{ 1, 12, 1, "E,RES 7,(IY+$)", "FDCB$BB" },
	{ 1, 9, 1, "E,RL (IX+$)", "DDCB$13" },
	{ 1, 9, 1, "E,RL (IY+$)", "FDCB$13" },
	{ 1, 10, 1, "E,RLC (IX+$)", "DDCB$03" },
	{ 1, 10, 1, "E,RLC (IY+$)", "FDCB$03" },
	{ 1, 9, 1, "E,RR (IX+$)", "DDCB$1B" },
	{ 1, 9, 1, "E,RR (IY+$)", "FDCB$1B" },
	{ 1, 10, 1, "E,RRC (IX+$)", "DDCB$0B" },
	{ 1, 10, 1, "E,RRC (IY+$)", "FDCB$0B" },
	{ 1, 12, 1, "E,SET 0,(IX+$)", "DDCB$C3" },
	{ 1, 12, 1, "E,SET 0,(IY+$)", "FDCB$C3" },
	{ 1, 12, 1, "E,SET 1,(IX+$)", "DDCB$CB" },
	{ 1, 12, 1, "E,SET 1,(IY+$)", "FDCB$CB" },
	{ 1, 12, 1, "E,SET 2,(IX+$)", "DDCB$D3" },
	{ 1, 12, 1, "E,SET 2,(IY+$)", "FDCB$D3" },
	{ 1, 12, 1, "E,SET 3,(IX+$)", "DDCB$DB" },
	{ 1, 12, 1, "E,SET 3,(IY+$)", "FDCB$DB" },
	{ 1, 12, 1, "E,SET 4,(IX+$)", "DDCB$E3" },
	{ 1, 12, 1, "E,SET 4,(IY+$)", "FDCB$E3" },
	{ 1, 12, 1, "E,SET 5,(IX+$)", "DDCB$EB" },
	{ 1, 12, 1, "E,SET 5,(IY+$)", "FDCB$EB" },
	{ 1, 12, 1, "E,SET 6,(IX+$)", "DDCB$F3" },
	{ 1, 12, 1, "E,SET 6,(IY+$)", "FDCB$F3" },
	{ 1, 12, 1, "E,SET 7,(IX+$)", "DDCB$FB" },
	{ 1, 12, 1, "E,SET 7,(IY+$)", "FDCB$FB" },
	{ 1, 10, 1, "E,SLA (IX+$)", "DDCB$23" },
	{ 1, 10, 1, "E,SLA (IY+$)", "FDCB$23" },
	{ 1, 10, 1, "E,SLL (IX+$)", "DDCB$33" },
	{ 1, 10, 1, "E,SLL (IY+$)", "FDCB$33" },
	{ 1, 10, 1, "E,SRA (IX+$)", "DDCB$2B" },
	{ 1, 10, 1, "E,SRA (IY+$)", "FDCB$2B" },
	{ 1, 10, 1, "E,SRL (IX+$)", "DDCB$3B" },
	{ 1, 10, 1, "E,SRL (IY+$)", "FDCB$3B" },
	{ 1, 2, 0, "H,#", "26#" },
	{ 1, 6, 1, "H,(IX+$)", "DD$66" },
	{ 1, 6, 1, "H,(IY+$)", "FD$66" },
	{ 1, 12, 1, "H,RES 0,(IX+$)", "DDCB$84" },
	{ 1, 12, 1, "H,RES 0,(IY+$)", "FDCB$84" },
	{ 1, 12, 1, "H,RES 1,(IX+$)", "DDCB$8C" },
	{ 1, 12, 1, "H,RES 1,(IY+$)", "FDCB$8C" },
	{ 1, 12, 1, "H,RES 2,(IX+$)", "DDCB$94" },
	{ 1, 12, 1, "H,RES 2,(IY+$)", "FDCB$94" },
	{ 1, 12, 1, "H,RES 3,(IX+$)", "DDCB$9C" },
	{ 1, 12, 1, "H,RES 3,(IY+$)", "FDCB$9C" },
	{ 1, 12, 1, "H,RES 4,(IX+$)", "DDCB$A4" },
	{ 1, 12, 1, "H,RES 4,(IY+$)", "FDCB$A4" },
	{ 1, 12, 1, "H,RES 5,(IX+$)", "DDCB$AC" },
	{ 1, 12, 1, "H,RES 5,(IY+$)", "FDCB$AC" },
	{ 1, 12, 1, "H,RES 6,(IX+$)", "DDCB$B4" },
	{ 1, 12, 1, "H,RES 6,(IY+$)", "FDCB$B4" },
	{ 1, 12, 1, "H,RES 7,(IX+$)", "DDCB$BC" },
	{ 1, 12, 1, "H,RES 7,(IY+$)", "FDCB$BC" },
	{ 1, 9, 1, "H,RL (IX+$)", "DDCB$14" },
	{ 1, 9, 1, "H,RL (IY+$)", "FDCB$14" },
	{ 1, 10, 1, "H,RLC (IX+$)", "DDCB$04" },
	{ 1, 10, 1, "H,RLC (IY+$)", "FDCB$04" },
	{ 1, 9, 1, "H,RR (IX+$)", "DDCB$1C" },
	{ 1, 9, 1, "H,RR (IY+$)", "FDCB$1C" },
	{ 1, 10, 1, "H,RRC (IX+$)", "DDCB$0C" },
	{ 1, 10, 1, "H,RRC (IY+$)", "FDCB$0C" },
	{ 1, 12, 1, "H,SET 0,(IX+$)", "DDCB$C4" },
	{ 1, 12, 1, "H,SET 0,(IY+$)", "FDCB$C4" },
	{ 1, 12, 1, "H,SET 1,(IX+$)", "DDCB$CC" },
	{ 1, 12, 1, "H,SET 1,(IY+$)", "FDCB$CC" },
	{ 1, 12, 1, "H,SET 2,(IX+$)", "DDCB$D4" },
	{ 1, 12, 1, "H,SET 2,(IY+$)", "FDCB$D4" },
	{ 1, 12, 1, "H,SET 3,(IX+$)", "DDCB$DC" },
	{ 1, 12, 1, "H,SET 3,(IY+$)", "FDCB$DC" },
	{ 1, 12, 1, "H,SET 4,(IX+$)", "DDCB$E4" },
	{ 1, 12, 1, "H,SET 4,(IY+$)", "FDCB$E4" },
	{ 1, 12, 1, "H,SET 5,(IX+$)", "DDCB$EC" },
	{ 1, 12, 1, "H,SET 5,(IY+$)", "FDCB$EC" },
	{ 1, 12, 1, "H,SET 6,(IX+$)", "DDCB$F4" },
	{ 1, 12, 1, "H,SET 6,(IY+$)", "FDCB$F4" },
	{ 1, 12, 1, "H,SET 7,(IX+$)", "DDCB$FC" },
	{ 1, 12, 1, "H,SET 7,(IY+$)", "FDCB$FC" },
	{ 1, 10, 1, "H,SLA (IX+$)", "DDCB$24" },
	{ 1, 10, 1, "H,SLA (IY+$)", "FDCB$24" },
	{ 1, 10, 1, "H,SLL (IX+$)", "DDCB$34" },
	{ 1, 10, 1, "H,SLL (IY+$)", "FDCB$34" },
	{ 1, 10, 1, "H,SRA (IX+$)", "DDCB$2C" },
	{ 1, 10, 1, "H,SRA (IY+$)", "FDCB$2C" },
	{ 1, 10, 1, "H,SRL (IX+$)", "DDCB$3C" },
	{ 1, 10, 1, "H,SRL (IY+$)", "FDCB$3C" },
	{ 1, 4, 1, "HL,(@)", "2A@" },
	{ 1, 3, 0, "HL,@", "21@" },
	{ 1, 4, 1, "IX,(@)", "DD2A@" },
	{ 1, 3, 0, "IX,@", "DD21@" },
	{ 1, 4, 0, "IXH,#", "DD26#" },
	{ 1, 4, 0, "IXL,#", "DD2E#" },
	{ 1, 4, 1, "IY,(@)", "FD2A@" },
	{ 1, 3, 0, "IY,@", "FD21@" },
	{ 1, 4, 0, "IYH,#", "FD26#" },
	{ 1, 4, 0, "IYL,#", "FD2E#" },
	{ 1, 2, 0, "L,#", "2E#" },
	{ 1, 6, 1, "L,(IX+$)", "DD$6E" },
	{ 1, 6, 1, "L,(IY+$)", "FD$6E" },
	{ 1, 12, 1, "L,RES 0,(IX+$)", "DDCB$85" },
	{ 1, 12, 1, "L,RES 0,(IY+$)", "FDCB$85" },
	{ 1, 12, 1, "L,RES 1,(IX+$)", "DDCB$8D" },
	{ 1, 12, 1, "L,RES 1,(IY+$)", "FDCB$8D" },
	{ 1, 12, 1, "L,RES 2,(IX+$)", "DDCB$95" },
	{ 1, 12, 1, "L,RES 2,(IY+$)", "FDCB$95" },
	{ 1, 12, 1, "L,RES 3,(IX+$)", "DDCB$9D" },
	{ 1, 12, 1, "L,RES 3,(IY+$)", "FDCB$9D" },
	{ 1, 12, 1, "L,RES 4,(IX+$)", "DDCB$A5" },
	{ 1, 12, 1, "L,RES 4,(IY+$)", "FDCB$A5" },
	{ 1, 12, 1, "L,RES 5,(IX+$)", "DDCB$AD" },
	{ 1, 12, 1, "L,RES 5,(IY+$)", "FDCB$AD" },
	{ 1, 12, 1, "L,RES 6,(IX+$)", "DDCB$B5" },
	{ 1, 12, 1, "L,RES 6,(IY+$)", "FDCB$B5" },
	{ 1, 12, 1, "L,RES 7,(IX+$)", "DDCB$BD" },
	{ 1, 12, 1, "L,RES 7,(IY+$)", "FDCB$BD" },
	{ 1, 9, 1, "L,RL (IX+$)", "DDCB$15" },
	{ 1, 9, 1, "L,RL (IY+$)", "FDCB$15" },
	{ 1, 10, 1, "L,RLC (IX+$)", "DDCB$05" },
	{ 1, 10, 1, "L,RLC (IY+$)", "FDCB$05" },
	{ 1, 9, 1, "L,RR (IX+$)", "DDCB$1D" },
	{ 1, 9, 1, "L,RR (IY+$)", "FDCB$1D" },
	{ 1, 10, 1, "L,RRC (IX+$)", "DDCB$0D" },
	{ 1, 10, 1, "L,RRC (IY+$)", "FDCB$0D" },
	{ 1, 12, 1, "L,SET 0,(IX+$)", "DDCB$C5" },
	{ 1, 12, 1, "L,SET 0,(IY+$)", "FDCB$C5" },
	{ 1, 12, 1, "L,SET 1,(IX+$)", "DDCB$CD" },
	{ 1, 12, 1, "L,SET 1,(IY+$)", "FDCB$CD" },
	{ 1, 12, 1, "L,SET 2,(IX+$)", "DDCB$D5" },
	{ 1, 12, 1, "L,SET 2,(IY+$)", "FDCB$D5" },
	{ 1, 12, 1, "L,SET 3,(IX+$)", "DDCB$DD" },
	{ 1, 12, 1, "L,SET 3,(IY+$)", "FDCB$DD" },
	{ 1, 12, 1, "L,SET 4,(IX+$)", "DDCB$E5" },
	{ 1, 12, 1, "L,SET 4,(IY+$)", "FDCB$E5" },
	{ 1, 12, 1, "L,SET 5,(IX+$)", "DDCB$ED" },
	{ 1, 12, 1, "L,SET 5,(IY+$)", "FDCB$ED" },
	{ 1, 12, 1, "L,SET 6,(IX+$)", "DDCB$F5" },
	{ 1, 12, 1, "L,SET 6,(IY+$)", "FDCB$F5" },
	{ 1, 12, 1, "L,SET 7,(IX+$)", "DDCB$FD" },
	{ 1, 12, 1, "L,SET 7,(IY+$)", "FDCB$FD" },
	{ 1, 10, 1, "L,SLA (IX+$)", "DDCB$25" },
	{ 1, 10, 1, "L,SLA (IY+$)", "FDCB$25" },
	{ 1, 10, 1, "L,SLL (IX+$)", "DDCB$35" },
	{ 1, 10, 1, "L,SLL (IY+$)", "FDCB$35" },
	{ 1, 10, 1, "L,SRA (IX+$)", "DDCB$2D" },
	{ 1, 10, 1, "L,SRA (IY+$)", "FDCB$2D" },
	{ 1, 10, 1, "L,SRL (IX+$)", "DDCB$3D" },
	{ 1, 10, 1, "L,SRL (IY+$)", "FDCB$3D" },
	{ 1, 4, 1, "SP,(@)", "ED7B@" },
	{ 1, 3, 0, "SP,@", "31@" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_LDD [] = {
	{ 0, 0, 0, NULL, "EDA8" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_LDDR [] = {
	{ 0, 0, 0, NULL, "EDB8" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_LDI [] = {
	{ 0, 0, 0, NULL, "EDA0" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_LDIR [] = {
	{ 0, 0, 0, NULL, "EDB0" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_LD_A_I [] = {
	{ 0, 0, 0, NULL, "ED57" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_LD_A_R [] = {
	{ 0, 0, 0, NULL, "ED5F" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_LD_R_A [] = {
	{ 0, 0, 0, NULL, "ED4F" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_L [] = {
	{ "LD", iasm_opts_LD },
	{ "LDD", iasm_opts_LDD },
	{ "LDDR", iasm_opts_LDDR },
	{ "LDI", iasm_opts_LDI },
	{ "LDIR", iasm_opts_LDIR },
	{ "LD_A_I", iasm_opts_LD_A_I },
	{ "LD_A_R", iasm_opts_LD_A_R },
	{ "LD_R_A", iasm_opts_LD_R_A },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_NEG [] = {
	{ 0, 0, 0, NULL, "ED44" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_NOP [] = {
	{ 0, 0, 0, NULL, "00" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_N [] = {
	{ "NEG", iasm_opts_NEG },
	{ "NOP", iasm_opts_NOP },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_OR [] = {
	{ 0, 4, 0, "(HL)", "B6" },
	{ 0, 1, 0, "A", "B7" },
	{ 0, 1, 0, "B", "B0" },
	{ 0, 1, 0, "C", "B1" },
	{ 0, 1, 0, "D", "B2" },
	{ 0, 1, 0, "E", "B3" },
	{ 0, 1, 0, "H", "B4" },
	{ 0, 3, 0, "IXH", "DDB4" },
	{ 0, 3, 0, "IXL", "DDB5" },
	{ 0, 3, 0, "IYH", "FDB4" },
	{ 0, 3, 0, "IYL", "FDB5" },
	{ 0, 1, 0, "L", "B5" },
	{ 1, 0, 0, "#", "F6#" },
	{ 1, 4, 1, "(IX+$)", "DD$B6" },
	{ 1, 4, 1, "(IY+$)", "FD$B6" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_OTDR [] = {
	{ 0, 0, 0, NULL, "EDBB" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_OTIR [] = {
	{ 0, 0, 0, NULL, "EDB3" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_OUT [] = {
	{ 0, 5, 0, "(C),0", "ED71" },
	{ 0, 5, 0, "(C),A", "ED79" },
	{ 0, 5, 0, "(C),B", "ED41" },
	{ 0, 5, 0, "(C),C", "ED49" },
	{ 0, 5, 0, "(C),D", "ED51" },
	{ 0, 5, 0, "(C),E", "ED59" },
	{ 0, 5, 0, "(C),H", "ED61" },
	{ 0, 5, 0, "(C),L", "ED69" },
	{ 1, 1, 3, "(#),A", "D3#" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_OUTD [] = {
	{ 0, 0, 0, NULL, "EDAB" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_OUTI [] = {
	{ 0, 0, 0, NULL, "EDA3" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_O [] = {
	{ "OR", iasm_opts_OR },
	{ "OTDR", iasm_opts_OTDR },
	{ "OTIR", iasm_opts_OTIR },
	{ "OUT", iasm_opts_OUT },
	{ "OUTD", iasm_opts_OUTD },
	{ "OUTI", iasm_opts_OUTI },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_POP [] = {
	{ 0, 2, 0, "AF", "F1" },
	{ 0, 2, 0, "BC", "C1" },
	{ 0, 2, 0, "DE", "D1" },
	{ 0, 2, 0, "HL", "E1" },
	{ 0, 2, 0, "IX", "DDE1" },
	{ 0, 2, 0, "IY", "FDE1" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_PUSH [] = {
	{ 0, 2, 0, "AF", "F5" },
	{ 0, 2, 0, "BC", "C5" },
	{ 0, 2, 0, "DE", "D5" },
	{ 0, 2, 0, "HL", "E5" },
	{ 0, 2, 0, "IX", "DDE5" },
	{ 0, 2, 0, "IY", "FDE5" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_P [] = {
	{ "POP", iasm_opts_POP },
	{ "PUSH", iasm_opts_PUSH },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_RES [] = {
	{ 0, 6, 0, "0,(HL)", "CB86" },
	{ 0, 3, 0, "0,A", "CB87" },
	{ 0, 3, 0, "0,B", "CB80" },
	{ 0, 3, 0, "0,C", "CB81" },
	{ 0, 3, 0, "0,D", "CB82" },
	{ 0, 3, 0, "0,E", "CB83" },
	{ 0, 3, 0, "0,H", "CB84" },
	{ 0, 3, 0, "0,L", "CB85" },
	{ 0, 6, 0, "1,(HL)", "CB8E" },
	{ 0, 3, 0, "1,A", "CB8F" },
	{ 0, 3, 0, "1,B", "CB88" },
	{ 0, 3, 0, "1,C", "CB89" },
	{ 0, 3, 0, "1,D", "CB8A" },
	{ 0, 3, 0, "1,E", "CB8B" },
	{ 0, 3, 0, "1,H", "CB8C" },
	{ 0, 3, 0, "1,L", "CB8D" },
	{ 0, 6, 0, "2,(HL)", "CB96" },
	{ 0, 3, 0, "2,A", "CB97" },
	{ 0, 3, 0, "2,B", "CB90" },
	{ 0, 3, 0, "2,C", "CB91" },
	{ 0, 3, 0, "2,D", "CB92" },
	{ 0, 3, 0, "2,E", "CB93" },
	{ 0, 3, 0, "2,H", "CB94" },
	{ 0, 3, 0, "2,L", "CB95" },
	{ 0, 6, 0, "3,(HL)", "CB9E" },
	{ 0, 3, 0, "3,A", "CB9F" },
	{ 0, 3, 0, "3,B", "CB98" },
	{ 0, 3, 0, "3,C", "CB99" },
	{ 0, 3, 0, "3,D", "CB9A" },
	{ 0, 3, 0, "3,E", "CB9B" },
	{ 0, 3, 0, "3,H", "CB9C" },
	{ 0, 3, 0, "3,L", "CB9D" },
	{ 0, 6, 0, "4,(HL)", "CBA6" },
	{ 0, 3, 0, "4,A", "CBA7" },
	{ 0, 3, 0, "4,B", "CBA0" },
	{ 0, 3, 0, "4,C", "CBA1" },
	{ 0, 3, 0, "4,D", "CBA2" },
	{ 0, 3, 0, "4,E", "CBA3" },
	{ 0, 3, 0, "4,H", "CBA4" },
	{ 0, 3, 0, "4,L", "CBA5" },
	{ 0, 6, 0, "5,(HL)", "CBAE" },
	{ 0, 3, 0, "5,A", "CBAF" },
	{ 0, 3, 0, "5,B", "CBA8" },
	{ 0, 3, 0, "5,C", "CBA9" },
	{ 0, 3, 0, "5,D", "CBAA" },
	{ 0, 3, 0, "5,E", "CBAB" },
	{ 0, 3, 0, "5,H", "CBAC" },
	{ 0, 3, 0, "5,L", "CBAD" },
	{ 0, 6, 0, "6,(HL)", "CBB6" },
	{ 0, 3, 0, "6,A", "CBB7" },
	{ 0, 3, 0, "6,B", "CBB0" },
	{ 0, 3, 0, "6,C", "CBB1" },
	{ 0, 3, 0, "6,D", "CBB2" },
	{ 0, 3, 0, "6,E", "CBB3" },
	{ 0, 3, 0, "6,H", "CBB4" },
	{ 0, 3, 0, "6,L", "CBB5" },
	{ 0, 6, 0, "7,(HL)", "CBBE" },
	{ 0, 3, 0, "7,A", "CBBF" },
	{ 0, 3, 0, "7,B", "CBB8" },
	{ 0, 3, 0, "7,C", "CBB9" },
	{ 0, 3, 0, "7,D", "CBBA" },
	{ 0, 3, 0, "7,E", "CBBB" },
	{ 0, 3, 0, "7,H", "CBBC" },
	{ 0, 3, 0, "7,L", "CBBD" },
	{ 1, 6, 1, "0,(IX+$)", "DDCB$86" },
	{ 1, 6, 1, "0,(IY+$)", "FDCB$86" },
	{ 1, 6, 1, "1,(IX+$)", "DDCB$8E" },
	{ 1, 6, 1, "1,(IY+$)", "FDCB$8E" },
	{ 1, 6, 1, "2,(IX+$)", "DDCB$96" },
	{ 1, 6, 1, "2,(IY+$)", "FDCB$96" },
	{ 1, 6, 1, "3,(IX+$)", "DDCB$9E" },
	{ 1, 6, 1, "3,(IY+$)", "FDCB$9E" },
	{ 1, 6, 1, "4,(IX+$)", "DDCB$A6" },
	{ 1, 6, 1, "4,(IY+$)", "FDCB$A6" },
	{ 1, 6, 1, "5,(IX+$)", "DDCB$AE" },
	{ 1, 6, 1, "5,(IY+$)", "FDCB$AE" },
	{ 1, 6, 1, "6,(IX+$)", "DDCB$B6" },
	{ 1, 6, 1, "6,(IY+$)", "FDCB$B6" },
	{ 1, 6, 1, "7,(IX+$)", "DDCB$BE" },
	{ 1, 6, 1, "7,(IY+$)", "FDCB$BE" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RET [] = {
	{ 0, 0, 0, NULL, "C9" },
	{ 0, 1, 0, "C", "D8" },
	{ 0, 1, 0, "M", "F8" },
	{ 0, 2, 0, "NC", "D0" },
	{ 0, 2, 0, "NZ", "C0" },
	{ 0, 1, 0, "P", "F0" },
	{ 0, 2, 0, "PE", "E8" },
	{ 0, 2, 0, "PO", "E0" },
	{ 0, 1, 0, "Z", "C8" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RETI [] = {
	{ 0, 0, 0, NULL, "ED4D" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RETN [] = {
	{ 0, 0, 0, NULL, "ED45" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RL [] = {
	{ 0, 4, 0, "(HL)", "CB16" },
	{ 0, 1, 0, "A", "CB17" },
	{ 0, 1, 0, "B", "CB10" },
	{ 0, 1, 0, "C", "CB11" },
	{ 0, 1, 0, "D", "CB12" },
	{ 0, 1, 0, "E", "CB13" },
	{ 0, 1, 0, "H", "CB14" },
	{ 0, 1, 0, "L", "CB15" },
	{ 1, 4, 1, "(IX+$)", "DDCB$16" },
	{ 1, 4, 1, "(IY+$)", "FDCB$16" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RLA [] = {
	{ 0, 0, 0, NULL, "17" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RLC [] = {
	{ 0, 4, 0, "(HL)", "CB06" },
	{ 0, 1, 0, "A", "CB07" },
	{ 0, 1, 0, "B", "CB00" },
	{ 0, 1, 0, "C", "CB01" },
	{ 0, 1, 0, "D", "CB02" },
	{ 0, 1, 0, "E", "CB03" },
	{ 0, 1, 0, "H", "CB04" },
	{ 0, 1, 0, "L", "CB05" },
	{ 1, 4, 1, "(IX+$)", "DDCB$06" },
	{ 1, 4, 1, "(IY+$)", "FDCB$06" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RLCA [] = {
	{ 0, 0, 0, NULL, "07" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RLD [] = {
	{ 0, 0, 0, NULL, "ED6F" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RR [] = {
	{ 0, 4, 0, "(HL)", "CB1E" },
	{ 0, 1, 0, "A", "CB1F" },
	{ 0, 1, 0, "B", "CB18" },
	{ 0, 1, 0, "C", "CB19" },
	{ 0, 1, 0, "D", "CB1A" },
	{ 0, 1, 0, "E", "CB1B" },
	{ 0, 1, 0, "H", "CB1C" },
	{ 0, 1, 0, "L", "CB1D" },
	{ 1, 4, 1, "(IX+$)", "DDCB$1E" },
	{ 1, 4, 1, "(IY+$)", "FDCB$1E" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RRA [] = {
	{ 0, 0, 0, NULL, "1F" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RRC [] = {
	{ 0, 4, 0, "(HL)", "CB0E" },
	{ 0, 1, 0, "A", "CB0F" },
	{ 0, 1, 0, "B", "CB08" },
	{ 0, 1, 0, "C", "CB09" },
	{ 0, 1, 0, "D", "CB0A" },
	{ 0, 1, 0, "E", "CB0B" },
	{ 0, 1, 0, "H", "CB0C" },
	{ 0, 1, 0, "L", "CB0D" },
	{ 1, 4, 1, "(IX+$)", "DDCB$0E" },
	{ 1, 4, 1, "(IY+$)", "FDCB$0E" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RRCA [] = {
	{ 0, 0, 0, NULL, "0F" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RRD [] = {
	{ 0, 0, 0, NULL, "ED67" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_RST [] = {
	{ 0, 4, 0, "0x00", "C7" },
	{ 0, 4, 0, "0x08", "CF" },
	{ 0, 4, 0, "0x10", "D7" },
	{ 0, 4, 0, "0x18", "DF" },
	{ 0, 4, 0, "0x20", "E7" },
	{ 0, 4, 0, "0x28", "EF" },
	{ 0, 4, 0, "0x30", "F7" },
	{ 0, 4, 0, "0x38", "FF" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_R [] = {
	{ "RES", iasm_opts_RES },
	{ "RET", iasm_opts_RET },
	{ "RETI", iasm_opts_RETI },
	{ "RETN", iasm_opts_RETN },
	{ "RL", iasm_opts_RL },
	{ "RLA", iasm_opts_RLA },
	{ "RLC", iasm_opts_RLC },
	{ "RLCA", iasm_opts_RLCA },
	{ "RLD", iasm_opts_RLD },
	{ "RR", iasm_opts_RR },
	{ "RRA", iasm_opts_RRA },
	{ "RRC", iasm_opts_RRC },
	{ "RRCA", iasm_opts_RRCA },
	{ "RRD", iasm_opts_RRD },
	{ "RST", iasm_opts_RST },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_SBC [] = {
	{ 0, 6, 0, "A,(HL)", "9E" },
	{ 0, 3, 0, "A,A", "9F" },
	{ 0, 3, 0, "A,B", "98" },
	{ 0, 3, 0, "A,C", "99" },
	{ 0, 3, 0, "A,D", "9A" },
	{ 0, 3, 0, "A,E", "9B" },
	{ 0, 3, 0, "A,H", "9C" },
	{ 0, 5, 0, "A,IXH", "DD9C" },
	{ 0, 5, 0, "A,IXL", "DD9D" },
	{ 0, 5, 0, "A,IYH", "FD9C" },
	{ 0, 5, 0, "A,IYL", "FD9D" },
	{ 0, 3, 0, "A,L", "9D" },
	{ 0, 5, 0, "HL,BC", "ED42" },
	{ 0, 5, 0, "HL,DE", "ED52" },
	{ 0, 5, 0, "HL,HL", "ED62" },
	{ 0, 5, 0, "HL,SP", "ED72" },
	{ 1, 2, 0, "A,#", "DE#" },
	{ 1, 6, 1, "A,(IX+$)", "DD$9E" },
	{ 1, 6, 1, "A,(IY+$)", "FD$9E" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_SCF [] = {
	{ 0, 0, 0, NULL, "37" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_SET [] = {
	{ 0, 6, 0, "0,(HL)", "CBC6" },
	{ 0, 3, 0, "0,A", "CBC7" },
	{ 0, 3, 0, "0,B", "CBC0" },
	{ 0, 3, 0, "0,C", "CBC1" },
	{ 0, 3, 0, "0,D", "CBC2" },
	{ 0, 3, 0, "0,E", "CBC3" },
	{ 0, 3, 0, "0,H", "CBC4" },
	{ 0, 3, 0, "0,L", "CBC5" },
	{ 0, 6, 0, "1,(HL)", "CBCE" },
	{ 0, 3, 0, "1,A", "CBCF" },
	{ 0, 3, 0, "1,B", "CBC8" },
	{ 0, 3, 0, "1,C", "CBC9" },
	{ 0, 3, 0, "1,D", "CBCA" },
	{ 0, 3, 0, "1,E", "CBCB" },
	{ 0, 3, 0, "1,H", "CBCC" },
	{ 0, 3, 0, "1,L", "CBCD" },
	{ 0, 6, 0, "2,(HL)", "CBD6" },
	{ 0, 3, 0, "2,A", "CBD7" },
	{ 0, 3, 0, "2,B", "CBD0" },
	{ 0, 3, 0, "2,C", "CBD1" },
	{ 0, 3, 0, "2,D", "CBD2" },
	{ 0, 3, 0, "2,E", "CBD3" },
	{ 0, 3, 0, "2,H", "CBD4" },
	{ 0, 3, 0, "2,L", "CBD5" },
	{ 0, 6, 0, "3,(HL)", "CBDE" },
	{ 0, 3, 0, "3,A", "CBDF" },
	{ 0, 3, 0, "3,B", "CBD8" },
	{ 0, 3, 0, "3,C", "CBD9" },
	{ 0, 3, 0, "3,D", "CBDA" },
	{ 0, 3, 0, "3,E", "CBDB" },
	{ 0, 3, 0, "3,H", "CBDC" },
	{ 0, 3, 0, "3,L", "CBDD" },
	{ 0, 6, 0, "4,(HL)", "CBE6" },
	{ 0, 3, 0, "4,A", "CBE7" },
	{ 0, 3, 0, "4,B", "CBE0" },
	{ 0, 3, 0, "4,C", "CBE1" },
	{ 0, 3, 0, "4,D", "CBE2" },
	{ 0, 3, 0, "4,E", "CBE3" },
	{ 0, 3, 0, "4,H", "CBE4" },
	{ 0, 3, 0, "4,L", "CBE5" },
	{ 0, 6, 0, "5,(HL)", "CBEE" },
	{ 0, 3, 0, "5,A", "CBEF" },
	{ 0, 3, 0, "5,B", "CBE8" },
	{ 0, 3, 0, "5,C", "CBE9" },
	{ 0, 3, 0, "5,D", "CBEA" },
	{ 0, 3, 0, "5,E", "CBEB" },
	{ 0, 3, 0, "5,H", "CBEC" },
	{ 0, 3, 0, "5,L", "CBED" },
	{ 0, 6, 0, "6,(HL)", "CBF6" },
	{ 0, 3, 0, "6,A", "CBF7" },
	{ 0, 3, 0, "6,B", "CBF0" },
	{ 0, 3, 0, "6,C", "CBF1" },
	{ 0, 3, 0, "6,D", "CBF2" },
	{ 0, 3, 0, "6,E", "CBF3" },
	{ 0, 3, 0, "6,H", "CBF4" },
	{ 0, 3, 0, "6,L", "CBF5" },
	{ 0, 6, 0, "7,(HL)", "CBFE" },
	{ 0, 3, 0, "7,A", "CBFF" },
	{ 0, 3, 0, "7,B", "CBF8" },
	{ 0, 3, 0, "7,C", "CBF9" },
	{ 0, 3, 0, "7,D", "CBFA" },
	{ 0, 3, 0, "7,E", "CBFB" },
	{ 0, 3, 0, "7,H", "CBFC" },
	{ 0, 3, 0, "7,L", "CBFD" },
	{ 1, 6, 1, "0,(IX+$)", "DDCB$C6" },
	{ 1, 6, 1, "0,(IY+$)", "FDCB$C6" },
	{ 1, 6, 1, "1,(IX+$)", "DDCB$CE" },
	{ 1, 6, 1, "1,(IY+$)", "FDCB$CE" },
	{ 1, 6, 1, "2,(IX+$)", "DDCB$D6" },
	{ 1, 6, 1, "2,(IY+$)", "FDCB$D6" },
	{ 1, 6, 1, "3,(IX+$)", "DDCB$DE" },
	{ 1, 6, 1, "3,(IY+$)", "FDCB$DE" },
	{ 1, 6, 1, "4,(IX+$)", "DDCB$E6" },
	{ 1, 6, 1, "4,(IY+$)", "FDCB$E6" },
	{ 1, 6, 1, "5,(IX+$)", "DDCB$EE" },
	{ 1, 6, 1, "5,(IY+$)", "FDCB$EE" },
	{ 1, 6, 1, "6,(IX+$)", "DDCB$F6" },
	{ 1, 6, 1, "6,(IY+$)", "FDCB$F6" },
	{ 1, 6, 1, "7,(IX+$)", "DDCB$FE" },
	{ 1, 6, 1, "7,(IY+$)", "FDCB$FE" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_SLA [] = {
	{ 0, 4, 0, "(HL)", "CB26" },
	{ 0, 1, 0, "A", "CB27" },
	{ 0, 1, 0, "B", "CB20" },
	{ 0, 1, 0, "C", "CB21" },
	{ 0, 1, 0, "D", "CB22" },
	{ 0, 1, 0, "E", "CB23" },
	{ 0, 1, 0, "H", "CB24" },
	{ 0, 1, 0, "L", "CB25" },
	{ 1, 4, 1, "(IX+$)", "DDCB$26" },
	{ 1, 4, 1, "(IY+$)", "FDCB$26" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_SLL [] = {
	{ 0, 4, 0, "(HL)", "CB36" },
	{ 0, 1, 0, "A", "CB37" },
	{ 0, 1, 0, "B", "CB30" },
	{ 0, 1, 0, "C", "CB31" },
	{ 0, 1, 0, "D", "CB32" },
	{ 0, 1, 0, "E", "CB33" },
	{ 0, 1, 0, "H", "CB34" },
	{ 0, 1, 0, "L", "CB35" },
	{ 1, 4, 1, "(IX+$)", "DDCB$36" },
	{ 1, 4, 1, "(IY+$)", "FDCB$36" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_SRA [] = {
	{ 0, 4, 0, "(HL)", "CB2E" },
	{ 0, 1, 0, "A", "CB2F" },
	{ 0, 1, 0, "B", "CB28" },
	{ 0, 1, 0, "C", "CB29" },
	{ 0, 1, 0, "D", "CB2A" },
	{ 0, 1, 0, "E", "CB2B" },
	{ 0, 1, 0, "H", "CB2C" },
	{ 0, 1, 0, "L", "CB2D" },
	{ 1, 4, 1, "(IX+$)", "DDCB$2E" },
	{ 1, 4, 1, "(IY+$)", "FDCB$2E" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_SRL [] = {
	{ 0, 4, 0, "(HL)", "CB3E" },
	{ 0, 1, 0, "A", "CB3F" },
	{ 0, 1, 0, "B", "CB38" },
	{ 0, 1, 0, "C", "CB39" },
	{ 0, 1, 0, "D", "CB3A" },
	{ 0, 1, 0, "E", "CB3B" },
	{ 0, 1, 0, "H", "CB3C" },
	{ 0, 1, 0, "L", "CB3D" },
	{ 1, 4, 1, "(IX+$)", "DDCB$3E" },
	{ 1, 4, 1, "(IY+$)", "FDCB$3E" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_opts_t iasm_opts_SUB [] = {
	{ 0, 4, 0, "(HL)", "96" },
	{ 0, 1, 0, "A", "97" },
	{ 0, 1, 0, "B", "90" },
	{ 0, 1, 0, "C", "91" },
	{ 0, 1, 0, "D", "92" },
	{ 0, 1, 0, "E", "93" },
	{ 0, 1, 0, "H", "94" },
	{ 0, 3, 0, "IXH", "DD94" },
	{ 0, 3, 0, "IXL", "DD95" },
	{ 0, 3, 0, "IYH", "FD94" },
	{ 0, 3, 0, "IYL", "FD95" },
	{ 0, 1, 0, "L", "95" },
	{ 1, 0, 0, "#", "D6#" },
	{ 1, 4, 1, "(IX+$)", "DD$96" },
	{ 1, 4, 1, "(IY+$)", "FD$96" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_S [] = {
	{ "SBC", iasm_opts_SBC },
	{ "SCF", iasm_opts_SCF },
	{ "SET", iasm_opts_SET },
	{ "SLA", iasm_opts_SLA },
	{ "SLL", iasm_opts_SLL },
	{ "SRA", iasm_opts_SRA },
	{ "SRL", iasm_opts_SRL },
	{ "SUB", iasm_opts_SUB },
	{ NULL, NULL }
};

static const iasm_opts_t iasm_opts_XOR [] = {
	{ 0, 4, 0, "(HL)", "AE" },
	{ 0, 1, 0, "A", "AF" },
	{ 0, 1, 0, "B", "A8" },
	{ 0, 1, 0, "C", "A9" },
	{ 0, 1, 0, "D", "AA" },
	{ 0, 1, 0, "E", "AB" },
	{ 0, 1, 0, "H", "AC" },
	{ 0, 3, 0, "IXH", "DDAC" },
	{ 0, 3, 0, "IXL", "DDAD" },
	{ 0, 3, 0, "IYH", "FDAC" },
	{ 0, 3, 0, "IYL", "FDAD" },
	{ 0, 1, 0, "L", "AD" },
	{ 1, 0, 0, "#", "EE#" },
	{ 1, 4, 1, "(IX+$)", "DD$AE" },
	{ 1, 4, 1, "(IY+$)", "FD$AE" },
	{ -1, -1, -1, NULL, NULL }
};

static const iasm_commands_t iasm_commands_X [] = {
	{ "XOR", iasm_opts_XOR },
	{ NULL, NULL }
};

static const iasm_chars_t iasm_chars [] = {
	{ 'A', iasm_commands_A },
	{ 'B', iasm_commands_B },
	{ 'C', iasm_commands_C },
	{ 'D', iasm_commands_D },
	{ 'E', iasm_commands_E },
	{ 'H', iasm_commands_H },
	{ 'I', iasm_commands_I },
	{ 'J', iasm_commands_J },
	{ 'L', iasm_commands_L },
	{ 'N', iasm_commands_N },
	{ 'O', iasm_commands_O },
	{ 'P', iasm_commands_P },
	{ 'R', iasm_commands_R },
	{ 'S', iasm_commands_S },
	{ 'X', iasm_commands_X },
	{ 0x00, NULL }
};

/** @} */ /* konec skupiny iasm_opcode_tables */


#ifdef	__cplusplus
}
#endif

#endif	/* IASM_OPCODES_H */

