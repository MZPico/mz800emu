/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file dasm_export.c
 * @brief Implementace auto-label scanneru pro disassembler V1.
 *
 * Algoritmický popis viz @ref dasm_export.h. Hlavičkové soubory které
 * tento modul vyžaduje:
 *
 *   - libs/dasm-z80/z80_dasm.h : engine API (z80_dasm,
 *     z80_dasm_target_addr, z80_symtab_lookup, z80_flow_type_t)
 *
 * Modul záměrně NEpoužívá sym_db.h ani mhmap.h - zůstává na úrovni
 * dasm-z80 lib + caller-supplied pointer abstrakce. To umožní
 * standalone unit testy bez emu core (TYPE STANDALONE v CMakeLists).
 *
 * Threading: jen UI vlákno (read callback čte přes
 * debugger_memory_read_byte nebo lokální test buffer; sym_db je čistě
 * UI vlákno).
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#include "dasm_export.h"

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/**
 * @brief Klasifikuje typ labelu podle @c z80_flow_type_t instrukce.
 *
 * Mapování dle z80_dasm.h:207-220 (enum z80_flow_type_t):
 *
 *   CALL / CALL_COND / RST       → DASM_LABEL_SUBROUTINE ('S')
 *   JUMP / JUMP_COND             → DASM_LABEL_JUMP ('L')
 *
 * Pro instrukce, které nemají staticky známý target (RET / RETI /
 * RETN / JUMP_INDIRECT / HALT / NORMAL), funkce vrací DASM_LABEL_DATA
 * jako fallback - reálně se ale pro tyto instrukce nikdy nevolá,
 * protože @c z80_dasm_target_addr() pro ně vrací sentinel
 * (uint16_t)-1 a caller takovou instrukci přeskočí dřív, než dojde
 * sem.
 *
 * Pozn.: enum z80_dasm.h NEMÁ samostatné Z80_FLOW_JUMP_REL ani
 * Z80_FLOW_DJNZ - JR a DJNZ spadají pod Z80_FLOW_JUMP (relativní
 * skok je z hlediska flow analýzy obyčejný jump). Plán 02 nesedí v
 * tomto detailu se skutečným enumem.
 *
 * @param i Disassemblovaná instrukce.
 * @return Klasifikační typ labelu.
 */
static dasm_label_type_t classify_target_type(const z80_dasm_inst_t *i)
{
    switch (i->flow) {
        case Z80_FLOW_CALL:
        case Z80_FLOW_CALL_COND:
        case Z80_FLOW_RST:
            return DASM_LABEL_SUBROUTINE;
        case Z80_FLOW_JUMP:
        case Z80_FLOW_JUMP_COND:
            return DASM_LABEL_JUMP;
        default:
            /* LD (nn),A / LD HL,(nn) ... = bez flow change, ale
             * target_addr má smysl pro paměťovou referenci. V1
             * limit: z80_dasm_target_addr() vrací jen jump/call
             * cíle, ne paměťové (= viz comment v z80_dasm.h:672).
             * Tj. tato větev se v praxi nikdy nedostane k Z80_OP_MEM_IMM16
             * cílům - to dělá až F4 cestou přes z80_dasm_resolve_symbols.
             * Pro budoucí doplnění necháváme DATA jako rozumný default. */
            return DASM_LABEL_DATA;
    }
}


/**
 * @brief Číselná priorita konfliktu mezi typy labelu.
 *
 * Vyšší hodnota = vyhrává při kolizi na stejné adrese.
 *
 *   WARN(4) > JUMP(3) > SUBROUTINE(2) > DATA(1)
 *
 * @param t Typ labelu.
 * @return Priorita (0 pro NONE, 1..4 pro klasifikace).
 */
static int label_priority(dasm_label_type_t t)
{
    switch (t) {
        case DASM_LABEL_WARN:       return 4;
        case DASM_LABEL_JUMP:       return 3;
        case DASM_LABEL_SUBROUTINE: return 2;
        case DASM_LABEL_DATA:       return 1;
        default:                    return 0;
    }
}


/**
 * @brief Sestaví defaultní auto-label jméno do bufferu.
 *
 * Format: "<prefix><lowercase hex 4 digits>", např. "Sc020".
 *
 * Prefix se získá přímo z enum hodnoty (= ASCII písmeno 'S'/'L'/'D'/'W').
 *
 * @param[out] out   Cílový buffer.
 * @param      outsz Velikost @p out (must be >= 6 pro typický výstup).
 * @param      addr  Adresa cíle.
 * @param      type  Klasifikace.
 *
 * @note mzdisasm konvenci se nepodařilo dohledat
 *       (~/projects/mz800-knowledge/tools/mzdisasm/ neexistuje) -
 *       lowercase je volba implementátora pro V1, lze sjednotit
 *       později pokud Michal upřesní.
 */
static void make_default_name(char *out, size_t outsz, uint16_t addr,
                              dasm_label_type_t type)
{
    char prefix = (char)type;  /* 'S' / 'L' / 'D' / 'W' */
    snprintf(out, outsz, "%c%04x", prefix, addr);
}


/* Forward decl - definice níže za find_label_at. */
static int find_label_at(const dasm_label_t *labels, int n, uint16_t addr);


/**
 * @brief F8: zaregistruje data-zone label do out pole (nebo eskaluje
 *        existující na zónu).
 *
 * Sémantika data zóny: souvislý rozsah bajtů kde mhmap snapshot říká
 * "čteno (R), neexekutováno (X)". Render fáze (@ref dasm_export_render)
 * pak zónu emituje jako DB direktivy místo disasmu.
 *
 * Pokud na adrese @p start_addr už existuje běžný code label (S/L/D/W
 * z Pass 2), data-zone info se přidá k němu (= label si zachová svůj
 * type a název, jen se navíc označí jako data zóna - typický scénář
 * "data tabulka s ručně přidaným sym_db jménem"). Pokud žádný label
 * neexistuje, vytvoří se nový s @c is_data_zone=true a default jménem
 * "D<addr>".
 *
 * @param out         Buffer labelů.
 * @param n_labels    [in/out] Aktuální počet labelů; zvýší se pokud
 *                    se přidává nový.
 * @param max_labels  Kapacita @p out.
 * @param start_addr  Začátek data zóny.
 * @param end_addr    Konec data zóny (inclusive).
 *
 * @return Index labelu v poli (existující nebo nový), nebo -1 pokud
 *         buffer full a nový se nedá přidat.
 */
static int register_data_zone_label(dasm_label_t *out, int *n_labels,
                                    int max_labels,
                                    uint16_t start_addr, uint16_t end_addr)
{
    int idx = find_label_at(out, *n_labels, start_addr);
    if (idx >= 0) {
        /* Eskaluj existující label na data zónu - zachovej jeho type
         * (např. S z CALL na začátek datové oblasti = vstupní bod do
         * tabulky) a název. */
        out[idx].is_data_zone  = true;
        out[idx].data_zone_end = end_addr;
        return idx;
    }
    if (*n_labels >= max_labels) {
        return -1;
    }
    dasm_label_t *lbl = &out[*n_labels];
    lbl->addr          = start_addr;
    lbl->type          = DASM_LABEL_DATA;
    lbl->from_symdb    = false;
    lbl->is_data_zone  = true;
    lbl->data_zone_end = end_addr;
    make_default_name(lbl->name, sizeof lbl->name, start_addr, DASM_LABEL_DATA);
    (*n_labels)++;
    return *n_labels - 1;
}


/**
 * @brief Najde index labelu na zadané adrese v poli labels.
 *
 * Lineární scan - V1 pesimisticky O(N) na lookup, ale typicky N je
 * < 100 labelů pro běžné ROM range. Pokud by se to ukázalo jako bottleneck,
 * F4+ může přidat hash mapu (UI strana ji už drží jako label_at_addr).
 *
 * @param labels Pole labelů.
 * @param n      Počet validních záznamů.
 * @param addr   Hledaná adresa.
 * @return Index v labels nebo -1 pokud nenalezen.
 */
static int find_label_at(const dasm_label_t *labels, int n, uint16_t addr)
{
    for (int i = 0; i < n; i++) {
        if (labels[i].addr == addr) return i;
    }
    return -1;
}


int dasm_export_collect_labels(
    uint16_t from, uint16_t to,
    z80_dasm_read_fn read_fn, void *read_ud,
    const z80_symtab_t *symdb,
    const mhmap_view_t *cdl,
    dasm_label_t *out, int max_labels)
{
    if (read_fn == NULL || out == NULL || max_labels <= 0) {
        return -1;
    }
    if (from > to) {
        return 0;  /* prázdný rozsah */
    }

    /* Velikost rozsahu (inclusive). Pro from=0x0000, to=0xFFFF = 65536,
     * což přesahuje 16-bit; používáme 32-bit počítadlo. */
    const uint32_t range_size = (uint32_t)to - (uint32_t)from + 1u;

    /* Bitmapa inst_start[i] = "adresa from+i je začátek nějaké instrukce".
     * Při 64KB rozsahu = 64 KB heap. Při typickém 256B-2KB rozsahu = drobnost. */
    uint8_t *inst_start = (uint8_t *)calloc((size_t)range_size, 1);
    if (inst_start == NULL) {
        return -1;
    }

    /* ------------------- Pass 1: zmapuj start adresy ------------------- */
    {
        uint32_t addr = (uint32_t)from;
        while (addr <= (uint32_t)to) {
            z80_dasm_inst_t inst;
            int len = z80_dasm(&inst, read_fn, read_ud, (uint16_t)addr);
            if (len <= 0) {
                /* defenzivní - z80_dasm vrací vždy 1..4 */
                break;
            }
            uint32_t idx = addr - (uint32_t)from;
            if (idx < range_size) {
                inst_start[idx] = 1;
            }
            /* Wrap-around ochrana: pokud addr+len > 0xFFFF, ukončit
             * (Z80 PC by sice wrapnulo, ale pro listing range nemá
             * smysl). */
            if (addr + (uint32_t)len > 0xFFFFu) break;
            addr += (uint32_t)len;
        }
    }

    /* ------------------- Pass 2: detekuj cíle ------------------- */
    int n_labels = 0;
    {
        uint32_t addr = (uint32_t)from;
        while (addr <= (uint32_t)to) {
            z80_dasm_inst_t inst;
            int len = z80_dasm(&inst, read_fn, read_ud, (uint16_t)addr);
            if (len <= 0) break;

            uint16_t target = z80_dasm_target_addr(&inst);

            /* Sentinel: (uint16_t)-1 = 0xFFFF = "není staticky známý cíl"
             * (RET, JP (HL), normální instrukce, ...).
             * POZOR: 0xFFFF je teoreticky platná adresa - sentinel hodnota
             * narážíme jen tehdy, když flow == NORMAL/RET/INDIRECT.
             * Bezpečnostní check: musí být buď CALL/JUMP/RST flow,
             * jinak target ignorujeme. */
            int is_flow_target = 0;
            switch (inst.flow) {
                case Z80_FLOW_CALL:
                case Z80_FLOW_CALL_COND:
                case Z80_FLOW_RST:
                case Z80_FLOW_JUMP:
                case Z80_FLOW_JUMP_COND:
                    is_flow_target = 1;
                    break;
                default:
                    break;
            }
            if (!is_flow_target || target == (uint16_t)0xFFFF) {
                if (addr + (uint32_t)len > 0xFFFFu) break;
                addr += (uint32_t)len;
                continue;
            }

            /* V1 limit: cíle MIMO rozsah nedostávají label (= zůstávají
             * jako raw hex v listingu). F4 případně doplní EQU export
             * pro out-of-range cíle. */
            if (target < from || target > to) {
                if (addr + (uint32_t)len > 0xFFFFu) break;
                addr += (uint32_t)len;
                continue;
            }

            dasm_label_type_t type = classify_target_type(&inst);

            /* Mid-instruction detekce: target není v inst_start mapě
             * → bývá SMC nebo chybná disassemblace. Zachováme WARN
             * klasifikaci jako vizuální indikátor. */
            uint32_t target_idx = (uint32_t)target - (uint32_t)from;
            if (target_idx < range_size && inst_start[target_idx] == 0) {
                type = DASM_LABEL_WARN;
            }

            /* Konflikt resolve: pokud na adrese už label existuje,
             * vyhrává vyšší priorita. */
            int existing_idx = find_label_at(out, n_labels, target);
            if (existing_idx >= 0) {
                int new_prio = label_priority(type);
                int old_prio = label_priority(out[existing_idx].type);
                if (new_prio > old_prio) {
                    out[existing_idx].type = type;
                    /* Pokud je z auto-generated jména (= ne sym_db),
                     * přegeneruj jméno s novým prefixem. Sym_db jméno
                     * se zachovává - jen type se mění (pro barvu).
                     * To zachová politiku "sym_db wins on name,
                     * WARN wins on type". */
                    if (!out[existing_idx].from_symdb) {
                        make_default_name(out[existing_idx].name,
                                          sizeof out[existing_idx].name,
                                          target, type);
                    }
                }
                /* else: nový má nižší prioritu, ignoruj */
            } else {
                /* Nový záznam */
                if (n_labels >= max_labels) {
                    /* Buffer full - přeruš (caller vidí kolik labelů
                     * se vešlo). */
                    break;
                }
                dasm_label_t *lbl = &out[n_labels];
                lbl->addr = target;
                lbl->type = type;
                lbl->from_symdb = false;
                lbl->is_data_zone  = false;  /* F8: default - není data zóna */
                lbl->data_zone_end = 0;
                make_default_name(lbl->name, sizeof lbl->name, target, type);

                /* Sym_db override (F3 path - F2 caller předá NULL).
                 * Sym_db wins on name, type zůstává (= WARN si zachová
                 * type pro vizuální indikátor). */
                if (symdb != NULL) {
                    const char *sym_name = z80_symtab_lookup(symdb, target);
                    if (sym_name != NULL) {
                        strncpy(lbl->name, sym_name, sizeof lbl->name - 1);
                        lbl->name[sizeof lbl->name - 1] = '\0';
                        lbl->from_symdb = true;
                    }
                }
                n_labels++;
            }

            if (addr + (uint32_t)len > 0xFFFFu) break;
            addr += (uint32_t)len;
        }
    }

    /* ------------------- Pass 3 (F8): data zóna detekce ----------------- */
    /* Aktivuje se jen pokud caller předal ne-NULL cdl snapshot. Iteruje
     * range a hledá souvislé úseky kde X==0 && R>0 (klasická heuristika
     * "tohle byly data, ne kód"). Každá zóna se registruje jako
     * dasm_label_t s is_data_zone=true. */
    if (cdl != NULL) {
        bool in_zone = false;
        uint16_t zone_start = 0;
        uint16_t zone_last = 0;  /* poslední adresa kde flag aktivní */

        for (uint32_t a = (uint32_t)from; a <= (uint32_t)to; a++) {
            uint16_t addr16 = (uint16_t)a;
            uint8_t is_exec = cdl_byte_is_exec(cdl, addr16);
            uint8_t is_read = cdl_byte_is_read(cdl, addr16);
            /* data = neexekutováno + čteno */
            bool is_data = (is_exec == 0) && (is_read != 0);

            if (is_data) {
                if (!in_zone) {
                    zone_start = addr16;
                    in_zone = true;
                }
                zone_last = addr16;
            } else if (in_zone) {
                int rc = register_data_zone_label(out, &n_labels,
                                                  max_labels,
                                                  zone_start, zone_last);
                if (rc < 0) {
                    /* Buffer full - přerušíme Pass 3, předchozí labely
                     * (i z Pass 2) zůstávají platné. */
                    in_zone = false;
                    break;
                }
                in_zone = false;
            }
        }
        if (in_zone) {
            /* Range skončil uvnitř data zóny - dotahnout. */
            (void)register_data_zone_label(out, &n_labels, max_labels,
                                           zone_start, zone_last);
        }
    }

    free(inst_start);
    return n_labels;
}


/* =========================================================================
 *  F4: dasm_export_write() - per-dialekt assembler-ready export
 * ========================================================================= */

/**
 * @brief Per-dialekt rendering kontrakt.
 *
 * Render loop v @ref dasm_export_write čte jen tato pole - nikde
 * se nevětví na hodnotu @c dasm_dialect_t (přepínač je jen v
 * @ref pick_dialect_spec).
 *
 * @c hex_word callback dostane 16-bit hodnotu a buffer; vrací
 * dialect-specific zápis (např. pasmo "0C000h", sjasmplus "#C000",
 * sdcc "0xC000"). Buffer musí mít alespoň 16 bajtů (worst case
 * "0xFFFFh" + NUL = 7 + rezerva).
 *
 * @c z80_dasm_hex_style + @c uppercase z @c dasm_export_opts_t
 * řídí formát mnemonic přes @c z80_dasm_format_t.
 *
 * @invariant Všechna textová pole jsou null-terminated literály se
 * statickou lifetime (file-static const).
 */
typedef struct {
    const char *(*hex_word)(uint16_t v, char *buf, size_t n);  /**< Zápis 16-bit hex. */
    const char *(*hex_byte)(uint8_t v, char *buf, size_t n);   /**< F8: zápis 8-bit hex (pro DB direktivy). */
    const char *org_keyword;        /**< "ORG" / ".org". */
    const char *equ_keyword;        /**< "EQU" / ".equ" - rezerva V1.5+. */
    const char *db_keyword;         /**< "DB" / ".byte" - F8 aktivní. */
    const char *dw_keyword;         /**< "DW" / ".word" - rezerva V1.5+. */
    const char *comment_char;       /**< Začátek komentáře (";"). */
    const char *label_terminator;   /**< ":" / "::" (sdcc global). */
    z80_hex_style_t z80_dasm_hex_style; /**< Pro z80_dasm_format_t.hex_style. */
    z80_ix_style_t  z80_dasm_ix_style;  /**< Pro z80_dasm_format_t.ix_iy_style. */
    const char *display_name;       /**< Lidsky čitelný název pro header. */
} dialect_spec_t;


/**
 * @brief Pasmo hex zápis 16-bit hodnoty: "0CE00h" / "01234h" / "0FFFFh".
 *
 * Pasmo (Intel/Zilog) styl: H suffix, leading zero pokud první hex
 * číslice je A-F (jinak by se interpretovala jako identifikátor).
 * Šířka 4 hex číslice (= word). Caller buffer musí mít >= 8 bajtů.
 *
 * @param v   16-bit hodnota.
 * @param buf Cílový buffer.
 * @param n   Velikost @p buf.
 * @return @p buf (= konstanta vrácená pro řetězení do printf).
 */
static const char *pasmo_hex_word(uint16_t v, char *buf, size_t n)
{
    /* Prvních 4 hex znaků: pokud nejvyšší nibble je >= 0xA, prepend "0". */
    const int hi_nibble = (v >> 12) & 0xF;
    if (hi_nibble >= 0xA) {
        snprintf(buf, n, "0%04Xh", v);
    } else {
        snprintf(buf, n, "%04Xh", v);
    }
    return buf;
}


/**
 * @brief Pasmo hex zápis 8-bit hodnoty: "00h" / "0CEh" / "0FFh".
 *
 * F8 - pro DB direktivy v data zónách. Stejné leading-zero pravidlo
 * jako u 16-bit: pokud horní nibble je >= 0xA, prepend "0" aby pasmo
 * neinterpretovalo prefix jako identifikátor.
 *
 * @param v   8-bit hodnota.
 * @param buf Cílový buffer (>= 5 B).
 * @param n   Velikost @p buf.
 * @return @p buf.
 */
static const char *pasmo_hex_byte(uint8_t v, char *buf, size_t n)
{
    const int hi_nibble = (v >> 4) & 0xF;
    if (hi_nibble >= 0xA) {
        snprintf(buf, n, "0%02Xh", v);
    } else {
        snprintf(buf, n, "%02Xh", v);
    }
    return buf;
}


/**
 * @brief Pasmo dialect spec.
 *
 * Reference implementace - F5/F6 stub specifikace dolepí svoje
 * callbacky a keyword tabulky. Pasmo používá Zilog hex H suffix
 * (= Z80_HEX_H_SUFFIX) i v mnemonice (přes z80_dasm_format_t),
 * takže výstup je konzistentní.
 */
static const dialect_spec_t DIALECT_PASMO = {
    .hex_word          = pasmo_hex_word,
    .hex_byte          = pasmo_hex_byte,
    .org_keyword       = "ORG",
    .equ_keyword       = "EQU",
    .db_keyword        = "DB",
    .dw_keyword        = "DW",
    .comment_char      = ";",
    .label_terminator  = ":",
    .z80_dasm_hex_style = Z80_HEX_H_SUFFIX,
    .z80_dasm_ix_style = Z80_IX_ZILOG,
    .display_name      = "pasmo",
};


/**
 * @brief Sjasmplus hex zápis 16-bit hodnoty: "$C000" / "$FFFF" / "$0000".
 *
 * Sjasmplus (6502/Motorola styl) používá prefix $ následovaný hex
 * číslicemi pevné šířky 4 (= word). Žádné leading-zero workaround
 * není potřeba - prefix $ jednoznačně odlišuje hex literal od
 * identifikátoru.
 *
 * Caller buffer musí mít >= 6 bajtů ("$XXXX" + NUL).
 *
 * @param v   16-bit hodnota.
 * @param buf Cílový buffer.
 * @param n   Velikost @p buf.
 * @return @p buf (= konstanta vrácená pro řetězení do printf).
 */
static const char *sjasmplus_hex_word(uint16_t v, char *buf, size_t n)
{
    snprintf(buf, n, "$%04X", v);
    return buf;
}


/**
 * @brief Sjasmplus hex zápis 8-bit hodnoty: "$00" / "$CE" / "$FF".
 *
 * F8 - pro DB direktivy. Prefix @c $ jednoznačně odlišuje od identifikátoru,
 * žádný leading-zero workaround.
 *
 * @param v   8-bit hodnota.
 * @param buf Cílový buffer (>= 4 B).
 * @param n   Velikost @p buf.
 * @return @p buf.
 */
static const char *sjasmplus_hex_byte(uint8_t v, char *buf, size_t n)
{
    snprintf(buf, n, "$%02X", v);
    return buf;
}


/**
 * @brief Sdcc-asz80 hex zápis 16-bit hodnoty: "0xC000" / "0x1234".
 *
 * Sdas-z80 (asxxxx rodina) používá C-style "0x" prefix - identický
 * s @ref Z80_HEX_0X uvnitř engine. Šířka 4 hex číslice (= word).
 * Žádné leading-zero handling oproti pasmu - prefix "0x" jednoznačně
 * odlišuje hodnotu od identifikátoru. Caller buffer musí mít >= 8 bajtů.
 *
 * @param v   16-bit hodnota.
 * @param buf Cílový buffer.
 * @param n   Velikost @p buf.
 * @return @p buf (= konstanta vrácená pro řetězení do printf).
 */
static const char *sdcc_hex_word(uint16_t v, char *buf, size_t n)
{
    snprintf(buf, n, "0x%04X", v);
    return buf;
}


/**
 * @brief Sdcc-asz80 hex zápis 8-bit hodnoty: "0x00" / "0xCE" / "0xFF".
 *
 * F8 - pro .byte direktivy. C-style prefix, žádný leading-zero workaround.
 *
 * @param v   8-bit hodnota.
 * @param buf Cílový buffer (>= 5 B).
 * @param n   Velikost @p buf.
 * @return @p buf.
 */
static const char *sdcc_hex_byte(uint8_t v, char *buf, size_t n)
{
    snprintf(buf, n, "0x%02X", v);
    return buf;
}


/**
 * @brief Sjasmplus dialect spec.
 *
 * Syntax je velmi blízká pasmo - rozdíl je hex styl ($ prefix místo
 * H suffix), keywordy ORG/EQU/DB/DW jsou identické. Comment char
 * záměrně držíme na ';' (sjasmplus podporuje i "//" ale ';' je
 * portable napříč pasmo/sjasmplus exporty).
 *
 * Engine hex_style = Z80_HEX_DOLLAR zajistí konzistentní $ prefix
 * i v operandech mnemoniky (LD SP,$D000 atd.).
 *
 * IX/IY syntax zůstává Zilog "(IX+d)" - F6 zavedl engine field pro
 * Motorola "(d,IX)" variantu (sdcc); sjasmplus má explicit ZILOG.
 */
static const dialect_spec_t DIALECT_SJASMPLUS = {
    .hex_word          = sjasmplus_hex_word,
    .hex_byte          = sjasmplus_hex_byte,
    .org_keyword       = "ORG",
    .equ_keyword       = "EQU",
    .db_keyword        = "DB",
    .dw_keyword        = "DW",
    .comment_char      = ";",
    .label_terminator  = ":",
    .z80_dasm_hex_style = Z80_HEX_DOLLAR,
    .z80_dasm_ix_style = Z80_IX_ZILOG,
    .display_name      = "sjasmplus",
};


/**
 * @brief Sdcc-asz80 dialect spec.
 *
 * Sdas-z80 syntaxe (= ASxxxx toolchain pro SDCC):
 *   - hex: C-style "0x" prefix (sdcc_hex_word + Z80_HEX_0X v mnemonice)
 *   - direktivy: tečková notace ".org", ".byte", ".word"
 *   - EQU forma: aritmetický "=" operátor ("label = value")
 *   - label terminator: "::" pro globální export
 *   - IX/IY indexace: Motorola styl "(d,IX)" / "(-d,IY)" (= F6 engine
 *     rozšíření, Z80_IX_MOTOROLA)
 *
 * V1 minimální výstup: jen @c .org direktiva. Pro plně linkovatelný
 * `.s` soubor sdas-z80 vyžaduje navíc @c .module a @c .area _CODE
 * sekci - V1 neemit, pouze poznámka v header komentáři.
 *
 * @note Pure sdas-z80 syntax (= ne SDCC inline asm v `.c` souborech),
 *       proto neemit `#` prefix u immediate hodnot.
 */
static const dialect_spec_t DIALECT_SDCC_ASZ80 = {
    .hex_word          = sdcc_hex_word,
    .hex_byte          = sdcc_hex_byte,
    .org_keyword       = ".org",
    .equ_keyword       = "=",
    .db_keyword        = ".byte",
    .dw_keyword        = ".word",
    .comment_char      = ";",
    .label_terminator  = "::",
    .z80_dasm_hex_style = Z80_HEX_0X,
    .z80_dasm_ix_style = Z80_IX_MOTOROLA,
    .display_name      = "sdcc-asz80",
};


/**
 * @brief Vrátí pointer na dialect spec, nebo NULL pro nepodporovaný dialekt.
 *
 * V1 F6 dokončeno: všechny 3 dialekty (PASMO, SJASMPLUS, SDCC_ASZ80)
 * naplněny. Default vrátí NULL pro neznámou enum hodnotu (= caller
 * dasm_export_write vrátí -1).
 *
 * Rozhodnutí F4: NEdělat silent fallback na pasmo pro neimplementované
 * dialekty - user by dostal soubor s pasmo syntaxí ale myslel by si
 * že je sjasmplus/sdcc. Čisté -1 a error message je transparentnější.
 *
 * @param d Dialect.
 * @return Pointer na const spec nebo NULL.
 */
static const dialect_spec_t *pick_dialect_spec(dasm_dialect_t d)
{
    switch (d) {
        case DASM_DIALECT_PASMO:      return &DIALECT_PASMO;
        case DASM_DIALECT_SJASMPLUS:  return &DIALECT_SJASMPLUS;
        case DASM_DIALECT_SDCC_ASZ80: return &DIALECT_SDCC_ASZ80;
        default:                       return NULL;
    }
}


/* =========================================================================
 *  F7: Sink abstrakce - sdílený render core pro write (FILE*) i to_string.
 * ========================================================================= */

/**
 * @brief Generický výstupní cíl pro render core.
 *
 * Render core @ref dasm_export_render volá výhradně @c write_str pro
 * každý fragment textu. Konkrétní implementace sink-u (FILE* nebo
 * dynamický buffer) se předá pomocí @c ud opaque pointeru.
 *
 * Návratová hodnota @c write_str: 0 = OK, != 0 = write fail
 * (out-of-memory u string sink, IO error u FILE* sink). Při error
 * render core přeruší smyčku a vrátí @ref DASM_EXPORT_ERR_WRITE_FAIL
 * resp. @ref DASM_EXPORT_ERR_OUT_OF_MEMORY (caller-specifický mapping).
 *
 * @invariant @c write_str nesmí být NULL.
 */
typedef struct {
    int (*write_str)(void *ud, const char *s);  /**< Append null-terminated text. */
    void *ud;                                   /**< Opaque kontext sink-u. */
} dasm_sink_t;


/**
 * @brief FILE* sink: zapíše @p s pomocí @c fputs.
 *
 * @param ud Cast na @c FILE* (vlastní caller @ref dasm_export_write).
 * @param s  Null-terminated text.
 * @return 0 = OK, 1 = fputs vrátil EOF (IO error).
 */
static int sink_file_write_str(void *ud, const char *s)
{
    FILE *fp = (FILE *)ud;
    if (fputs(s, fp) == EOF) return 1;
    return 0;
}


/**
 * @brief Dynamický string buffer pro @ref dasm_export_to_string.
 *
 * Růst přes realloc s 1.5x zvětšováním. Pesimisticky 8 KB starter
 * (pro malé clipboardy bez relokace).
 *
 * @invariant @c buf je null-terminated (poslední bajt vždy '\0', délka
 *            obsahu = @c len bez NUL).
 * @invariant @c cap >= @c len + 1.
 */
typedef struct {
    char  *buf;   /**< Buffer ownership (malloc). */
    size_t len;   /**< Délka aktuálního obsahu bez NUL. */
    size_t cap;   /**< Kapacita bufferu (vč. NUL). */
} dasm_strbuf_t;


/**
 * @brief Inicializuje string buffer s počáteční kapacitou.
 *
 * @param[out] b   Buffer.
 * @param      cap Počáteční kapacita (musí být > 0).
 * @return 0 = OK, 1 = malloc selhal.
 */
static int strbuf_init(dasm_strbuf_t *b, size_t cap)
{
    b->buf = (char *)malloc(cap);
    if (b->buf == NULL) return 1;
    b->buf[0] = '\0';
    b->len = 0;
    b->cap = cap;
    return 0;
}


/**
 * @brief String sink: append @p s s případným realloc růstem.
 *
 * @param ud Cast na @ref dasm_strbuf_t.
 * @param s  Null-terminated text.
 * @return 0 = OK, 1 = realloc selhal.
 */
static int sink_strbuf_write_str(void *ud, const char *s)
{
    dasm_strbuf_t *b = (dasm_strbuf_t *)ud;
    size_t sl = strlen(s);
    size_t need = b->len + sl + 1;
    if (need > b->cap) {
        size_t new_cap = b->cap * 2u;
        if (new_cap < need) new_cap = need;
        char *nb = (char *)realloc(b->buf, new_cap);
        if (nb == NULL) return 1;
        b->buf = nb;
        b->cap = new_cap;
    }
    memcpy(b->buf + b->len, s, sl + 1);  /* vč. NUL */
    b->len += sl;
    return 0;
}


/**
 * @brief printf-style zápis do sink-u přes lokální buffer na zásobníku.
 *
 * Render core formátuje řádky disassembly přes @c snprintf do bufferu
 * (max 256 bajtů na řádek - mnemonika + komentář bytes < 200 znaků).
 * Pak fragment předá sink callbacku.
 *
 * Pokud by formátovaný výstup přesáhl buffer, snprintf vrátí truncated
 * (s NUL). To akceptujeme: 256 bajtů je dostatečně velký pro reálné
 * řádky a v praxi k truncate nedochází.
 *
 * @param sink Sink.
 * @param fmt  printf format string.
 * @return 0 = OK, !=0 = write_str selhal.
 */
static int sink_printf(dasm_sink_t *sink, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return sink->write_str(sink->ud, buf);
}


/**
 * @brief Sdílený render core - zapíše assembler-ready disassembly do sink-u.
 *
 * Po F7 refactoru obsahuje veškerou logiku původního @ref dasm_export_write
 * vyjma fopen/fclose. @ref dasm_export_write i @ref dasm_export_to_string
 * jsou tenké wrappery které jen postaví sink a delegují sem.
 *
 * Při selhání kteréhokoliv sink write callu funkce přeruší a vrátí
 * @ref DASM_EXPORT_ERR_WRITE_FAIL. Caller mapuje na ERR_OUT_OF_MEMORY
 * podle typu sink-u (string sink = OOM, file sink = write fail).
 *
 * @param sink     Cíl výstupu (file nebo string buffer). Nesmí být NULL.
 * @param from     Adresa rozsahu (inclusive).
 * @param to       Adresa rozsahu (inclusive).
 * @param read_fn  Memory read callback. Nesmí být NULL.
 * @param read_ud  User data pro @p read_fn.
 * @param labels   Pole labelů. Může být NULL pokud @p n_labels == 0.
 * @param n_labels Počet labelů.
 * @param opts     Volby formátu. Nesmí být NULL.
 *
 * @return @ref dasm_export_result_t.
 *
 * @pre Všechny pre-condition kontroly volajícího wrapperu - tady jen
 *      dialect spec a symtab.
 */
/**
 * @brief F8: najde data-zone label, který začíná přesně na @p addr.
 *
 * Lineární scan; n_labels typicky < 100. Hledáme jen labely s
 * @c is_data_zone == true.
 *
 * @param labels   Pole labelů.
 * @param n_labels Počet validních záznamů.
 * @param addr     Adresa.
 * @return Pointer na label nebo NULL.
 */
static const dasm_label_t *find_data_zone_starting_at(
    const dasm_label_t *labels, int n_labels, uint16_t addr)
{
    for (int i = 0; i < n_labels; i++) {
        if (labels[i].is_data_zone && labels[i].addr == addr) {
            return &labels[i];
        }
    }
    return NULL;
}


static dasm_export_result_t dasm_export_render(
    dasm_sink_t *sink,
    uint16_t from, uint16_t to,
    z80_dasm_read_fn read_fn, void *read_ud,
    const dasm_label_t *labels, int n_labels,
    const dasm_export_opts_t *opts)
{
    const dialect_spec_t *d = pick_dialect_spec(opts->dialect);
    if (d == NULL) {
        return DASM_EXPORT_ERR_UNSUPPORTED_DIALECT;
    }

    char hexbuf_a[16];
    char hexbuf_b[16];

#define SINK_PRINTF(...) \
    do { if (sink_printf(sink, __VA_ARGS__) != 0) { \
        z80_symtab_destroy(st); return DASM_EXPORT_ERR_WRITE_FAIL; } } while (0)

    /* ---------------- Lokální symtab z labelů pro substituci ---------------- */
    z80_symtab_t *st = z80_symtab_create();
    if (st != NULL) {
        for (int i = 0; i < n_labels; i++) {
            z80_symtab_add(st, labels[i].addr, labels[i].name);
        }
    }
    /* Pokud z80_symtab_create selhal (alloc fail), pokračujeme bez
     * symbol substituce - mnemonic zobrazí raw hex cíle. */

    /* ---------------- Header komentář ---------------- */
    SINK_PRINTF("%s Disassembled by mz800emu Disassembler V1\n",
                d->comment_char);
    SINK_PRINTF("%s Range: %s - %s\n", d->comment_char,
                d->hex_word(from, hexbuf_a, sizeof hexbuf_a),
                d->hex_word(to,   hexbuf_b, sizeof hexbuf_b));
    SINK_PRINTF("%s Dialect: %s\n", d->comment_char, d->display_name);
    SINK_PRINTF("\n");

    /* ---------------- ORG direktiva ---------------- */
    if (opts->include_org) {
        SINK_PRINTF("        %s %s\n\n", d->org_keyword,
                    d->hex_word(from, hexbuf_a, sizeof hexbuf_a));
    }

    /* ---------------- Body ---------------- */
    z80_dasm_format_t fmt;
    z80_dasm_format_default(&fmt);
    fmt.hex_style = d->z80_dasm_hex_style;
    fmt.uppercase = opts->uppercase_mnemonics ? 1 : 0;
    /* F6: per-dialekt IX/IY syntaxe (Zilog "(IX+d)" / Motorola "(d,IX)"). */
    fmt.ix_iy_style = d->z80_dasm_ix_style;

    z80_dasm_inst_t inst;
    uint32_t addr = (uint32_t)from;
    while (addr <= (uint32_t)to) {
        /* F8: Začíná na téhle adrese data zóna? Pokud ano, render jako
         * DB direktivy a posun za konec zóny. Label na začátku se musí
         * vypsat manuálně - z80_dasm path níže by jinak vypsala label
         * až po decode (ale my decode skipujeme). */
        const dasm_label_t *dz =
            find_data_zone_starting_at(labels, n_labels, (uint16_t)addr);
        if (dz != NULL) {
            SINK_PRINTF("%s%s\n", dz->name, d->label_terminator);

            const int BYTES_PER_LINE = 16;
            uint32_t pos = addr;
            const uint32_t dz_end = (uint32_t)dz->data_zone_end;
            char bytebuf[8];
            while (pos <= dz_end) {
                SINK_PRINTF("        %s ", d->db_keyword);
                int emitted = 0;
                while (emitted < BYTES_PER_LINE && pos <= dz_end) {
                    uint8_t b = read_fn((uint16_t)pos, read_ud);
                    if (emitted > 0) {
                        SINK_PRINTF(",%s",
                                    d->hex_byte(b, bytebuf, sizeof bytebuf));
                    } else {
                        SINK_PRINTF("%s",
                                    d->hex_byte(b, bytebuf, sizeof bytebuf));
                    }
                    pos++;
                    emitted++;
                }
                SINK_PRINTF("\n");
            }
            /* Posun za konec zóny. Wrap-around ochrana. */
            if (dz_end >= 0xFFFFu) break;
            addr = dz_end + 1u;
            continue;
        }

        int len = z80_dasm(&inst, read_fn, read_ud, (uint16_t)addr);
        if (len <= 0) break;

        /* Label na této adrese? */
        const char *lbl = (st != NULL) ? z80_symtab_lookup(st, (uint16_t)addr)
                                       : NULL;
        if (lbl != NULL) {
            SINK_PRINTF("%s%s\n", lbl, d->label_terminator);
        }

        /* Mnemonic s symbolovou substitucí. */
        char mnem[80];
        if (st != NULL) {
            z80_dasm_to_str_sym(mnem, sizeof mnem, &inst, &fmt, st);
        } else {
            z80_dasm_to_str(mnem, sizeof mnem, &inst, &fmt);
        }

        /* Volitelný komentář s bytes. */
        if (opts->include_bytes_comment) {
            /* Sestavíme řádek vč. bytes do jednoho bufferu, ať sink
             * dostane atomický fragment (jednodušší error handling). */
            char line[256];
            int off = snprintf(line, sizeof line,
                               "        %-30s %s", mnem, d->comment_char);
            for (int b = 0; b < len && off < (int)sizeof line - 4; b++) {
                int w = snprintf(line + off, sizeof line - (size_t)off,
                                 " %02X",
                                 read_fn((uint16_t)(addr + b), read_ud));
                if (w <= 0) break;
                off += w;
            }
            if (off < (int)sizeof line - 1) {
                line[off++] = '\n';
                line[off]   = '\0';
            }
            if (sink->write_str(sink->ud, line) != 0) {
                z80_symtab_destroy(st);
                return DASM_EXPORT_ERR_WRITE_FAIL;
            }
        } else {
            SINK_PRINTF("        %s\n", mnem);
        }

        if (addr + (uint32_t)len > 0xFFFFu) break;
        addr += (uint32_t)len;
    }

    /* V1: EQU sekce není (collect_labels neobsahuje out-of-range
     * cíle; rezerva V1.5+). */

    z80_symtab_destroy(st);
    return DASM_EXPORT_OK;

#undef SINK_PRINTF
}


int dasm_export_write(
    const char *path,
    uint16_t from, uint16_t to,
    z80_dasm_read_fn read_fn, void *read_ud,
    const dasm_label_t *labels, int n_labels,
    const dasm_export_opts_t *opts)
{
    if (path == NULL || read_fn == NULL || opts == NULL) {
        return DASM_EXPORT_ERR_INVALID_RANGE;
    }
    if (from > to) {
        return DASM_EXPORT_ERR_INVALID_RANGE;
    }

    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        return DASM_EXPORT_ERR_OPEN_FAIL;
    }

    dasm_sink_t sink;
    sink.write_str = sink_file_write_str;
    sink.ud        = fp;

    dasm_export_result_t rc = dasm_export_render(
        &sink, from, to, read_fn, read_ud, labels, n_labels, opts);

    /* fclose se volá v každém případě - i při chybě (částečný obsah). */
    if (fclose(fp) != 0 && rc == DASM_EXPORT_OK) {
        /* close error po úspěšném zápisu = flush selhal = write fail */
        rc = DASM_EXPORT_ERR_WRITE_FAIL;
    }
    return (int)rc;
}


int dasm_export_to_string(
    uint16_t from, uint16_t to,
    z80_dasm_read_fn read_fn, void *read_ud,
    const dasm_label_t *labels, int n_labels,
    const dasm_export_opts_t *opts,
    char **out_str)
{
    if (out_str == NULL) {
        return DASM_EXPORT_ERR_INVALID_RANGE;
    }
    *out_str = NULL;
    if (read_fn == NULL || opts == NULL) {
        return DASM_EXPORT_ERR_INVALID_RANGE;
    }
    if (from > to) {
        return DASM_EXPORT_ERR_INVALID_RANGE;
    }

    dasm_strbuf_t sb;
    if (strbuf_init(&sb, 8192) != 0) {
        return DASM_EXPORT_ERR_OUT_OF_MEMORY;
    }

    dasm_sink_t sink;
    sink.write_str = sink_strbuf_write_str;
    sink.ud        = &sb;

    dasm_export_result_t rc = dasm_export_render(
        &sink, from, to, read_fn, read_ud, labels, n_labels, opts);

    if (rc == DASM_EXPORT_OK) {
        *out_str = sb.buf;  /* transfer ownership */
        return DASM_EXPORT_OK;
    }

    /* String sink WRITE_FAIL = realloc selhal = OOM (file sink by mapoval
     * jinak; my víme že jsme používali string sink). */
    free(sb.buf);
    if (rc == DASM_EXPORT_ERR_WRITE_FAIL) {
        return DASM_EXPORT_ERR_OUT_OF_MEMORY;
    }
    return (int)rc;
}

#endif /* MZ800EMU_CFG_DEBUGGER_ENABLED */
