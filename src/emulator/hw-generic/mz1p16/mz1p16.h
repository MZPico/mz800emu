/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file mz1p16.h
 * @brief Mechanika plotteru Sharp MZ-1P16 nad jádrem 8050 - veřejné API.
 *
 * Modul propojuje jádro mikrokontroléru MCS-48 (8050, modul mcs48) s modelem
 * mechaniky perového X-Y plotteru: dvojice krokových motorů (X = vozík pera,
 * Y = posuv papíru), zvedání/spouštění pera, čtyřbarevný karusel a akumulace
 * nakreslených tahů do bufferu.
 *
 * Napojení na 8050 je přes hooky portů (mcs48_port_write_fn / read_fn):
 *  - BUS dolní nibble = fáze X stepperu, horní nibble = fáze Y stepperu;
 *  - P1 = řízení pera a stavové výstupy (BUSY) + vstupní senzory;
 *  - P2 = datová sběrnice z hosta (Centronics data);
 *  - /INT = STROBE z hosta; T0/T1 = senzory mechaniky.
 *
 * Dekodér směru kroku vychází ze 4-fázové sekvence vystavované firmwarem
 * (viz mz1p16.c).
 *
 * @note Modul je arch-independent.
 */

#ifndef MZ1P16_H
#define MZ1P16_H

#include <stdint.h>
#include <stdbool.h>

#include "emulator/hw-generic/mz1p16/mcs48.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Počet barev karuselu (4 pera). */
#define MZ1P16_NUM_COLORS 4

/** Kapacita bufferu zaznamenaných tahů (úseček). Plotter kreslí hustě
 *  (celý charset = tisíce úseček), buffer musí pojmout celou stránku. */
#define MZ1P16_MAX_STROKES 131072

/**
 * @brief Šířka levé pen-change zóny v krocích X.
 *
 * Vozík s xpos < tohoto prahu je v zóně výměny pera. Při vjezdu do zóny
 * (přechod ven->dovnitř) se otočí buben na další barvu. Firmware u levého
 * dorazu osciluje s amplitudou ~28 kroků; práh musí být menší než tato
 * amplituda, aby vozík zónu mezi nárazy opustil a latch se resetoval.
 */
#define MZ1P16_PENCHANGE_ZONE 20

/**
 * @brief Šířka papíru v krocích X (dojezd vozíku od levého dorazu).
 *
 * X=0 je levý mechanický doraz = levý okraj papíru; vozík dojede ~554 kroků.
 * Slouží UI náhledu jako pevná šířka papíru (tisk zleva doprava, bez
 * vystředění). [přibližné - doladit proti reálu]
 */
#define MZ1P16_PAPER_WIDTH_STEPS 554

/**
 * @brief Pořadí barev karuselu (index 0-3).
 *
 * Index barvy odpovídá pozici rotace bubnu. Konkrétní přiřazení barev k
 * indexům (černá, modrá, zelená, červená) je [neověřeno].
 */
typedef enum en_MZ1P16_Color {
    MZ1P16_COLOR_BLACK = 0, /**< Černá (výchozí po power-on). */
    MZ1P16_COLOR_BLUE  = 1, /**< Modrá. */
    MZ1P16_COLOR_GREEN = 2, /**< Zelená. */
    MZ1P16_COLOR_RED   = 3  /**< Červená. */
} en_MZ1P16_Color;

/**
 * @brief Jedna úsečka (tah perem) v souřadnicích kroků.
 *
 * Souřadnice jsou v krocích motorů (nikoliv mm); přepočet na fyzické jednotky
 * je věcí pozdější fáze. Tah vzniká pohybem se spuštěným perem.
 */
typedef struct st_MZ1P16_Stroke {
    int32_t x0; /**< Počáteční X (kroky). */
    int32_t y0; /**< Počáteční Y (kroky). */
    int32_t x1; /**< Koncové X (kroky). */
    int32_t y1; /**< Koncové Y (kroky). */
    uint8_t color; /**< Index barvy 0-3 v okamžiku kreslení. */
} st_MZ1P16_Stroke;

/**
 * @brief Stav jednoho krokového motoru (4-fázový unipolární).
 *
 * Drží poslední vystavenou fázi (nibble) a akumulovanou pozici v krocích.
 * Směr kroku se určuje porovnáním nové fáze s předchozí proti fázové sekvenci
 * (viz mz1p16.c).
 */
typedef struct st_MZ1P16_Stepper {
    uint8_t phase;     /**< Poslední platná fáze (nibble 0-15). */
    bool    have_phase;/**< Už byla zaznamenána výchozí fáze? */
    int32_t pos;       /**< Akumulovaná pozice v krocích. */
} st_MZ1P16_Stepper;

/**
 * @brief Stav celé mechaniky plotteru MZ-1P16.
 *
 * Invarianty:
 *  - color je vždy 0..MZ1P16_NUM_COLORS-1.
 *  - n_strokes <= MZ1P16_MAX_STROKES (další tahy se po zaplnění zahazují).
 *  - cpu ukazuje na inicializované jádro, jehož ctx == tento objekt.
 */
typedef struct st_MZ1P16 {
    st_MCS48 *cpu;     /**< Jádro 8050 (vlastník = volající). */

    bool    active;    /**< Plotter připojený/zapnutý (integrace do emu). Když
                            false, integrační vrstva jádro nekrokuje a plotter
                            se chová jako odpojený. */

    st_MZ1P16_Stepper sx; /**< X motor (vozík pera). */
    st_MZ1P16_Stepper sy; /**< Y motor (posuv papíru). */

    bool    pen_down;  /**< Pero spuštěné (kreslí)? */
    uint8_t color;     /**< Aktivní barva 0-3. */
    bool    busy;      /**< Stav BUSY hlášený hostu (P1.4 = 1). */

    /* Pen-change zóna (levý mechanický doraz) */
    bool    in_penchange; /**< Vozík je právě v levé pen-change zóně (latch). */

    /* Akumulovaná kresba */
    st_MZ1P16_Stroke strokes[MZ1P16_MAX_STROKES]; /**< Buffer tahů. */
    uint32_t n_strokes; /**< Počet platných tahů. */
    uint32_t dropped;   /**< Počet zahozených tahů (buffer plný). */

    /* Vstupní linky z hosta (Centronics) */
    uint8_t  host_data; /**< Data na sběrnici (-> P2 plotteru). */
    bool     host_data_read; /**< Firmware právě přečetl P2 (host_data) - pro synchronní doručení bajtu. */

    /* Statistika a diagnostika */
    uint64_t x_steps_fwd; /**< Počet kroků X vpřed. */
    uint64_t x_steps_rev; /**< Počet kroků X vzad. */
    uint64_t y_steps_fwd; /**< Počet kroků Y vpřed. */
    uint64_t y_steps_rev; /**< Počet kroků Y vzad. */
    uint64_t pen_downs;   /**< Počet spuštění pera. */
    uint64_t color_changes; /**< Počet změn barvy. */

    /* Diagnostické logování fází stepperu */
    bool    log_phases;   /**< Zapnout textový log fází na stderr? */
} st_MZ1P16;

/**
 * @brief Inicializuje mechaniku a napojí ji na jádro 8050.
 *
 * Vynuluje stav mechaniky, nastaví hooky portů jádra (on_port_write,
 * on_port_read) a ctx tak, aby jádro volalo tento modul. Pozice motorů =0,
 * pero nahoře, barva 0.
 *
 * @param m   Instance mechaniky (nesmí být NULL).
 * @param cpu Inicializované jádro 8050 (nesmí být NULL); jeho hooky a ctx
 *            tato funkce přepíše.
 * @post cpu->on_port_write a cpu->on_port_read ukazují na interní handlery,
 *       cpu->ctx == m.
 */
void mz1p16_init(st_MZ1P16 *m, st_MCS48 *cpu);

/**
 * @brief Vynuluje akumulovanou kresbu a statistiku (ne pozice motorů).
 * @param m Instance mechaniky.
 */
void mz1p16_clear_drawing(st_MZ1P16 *m);

/**
 * @brief Pošle hostovi jeden bajt přes Centronics handshake.
 *
 * Položí data na vstup P2, pulzne /INT (STROBE) a nechá jádro odběhnout
 * @p settle_steps instrukcí, aby firmware bajt zpracoval.
 *
 * @param m            Instance mechaniky.
 * @param byte         Datový bajt.
 * @param settle_steps Počet instrukcí jádra na zpracování po pulzu.
 * @return Počet instrukcí, které jádro skutečně vykonalo.
 */
long mz1p16_host_send_byte(st_MZ1P16 *m, uint8_t byte, long settle_steps);

#ifdef __cplusplus
}
#endif

#endif /* MZ1P16_H */
