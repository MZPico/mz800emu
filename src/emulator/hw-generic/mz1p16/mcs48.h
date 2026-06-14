/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file mcs48.h
 * @brief Jádro mikrokontroléru Intel MCS-48 (8050) - veřejné API.
 *
 * Emulace jednočipového mikrokontroléru rodiny Intel MCS-48, konkrétně
 * varianty 8050 (4 KB ROM, 256 B RAM), který řídí plotter Sharp MZ-1P16.
 *
 * Jádro je ROM-agnostické: volající dodá ukazatel na 4 KB ROM. Modul
 * neobsahuje žádnou mechaniku plotteru, Centronics rozhraní ani GUI. Porty
 * P1/P2/BUS jsou implementovány jako výstupní latche plus vstupní pin-level
 * hodnoty, s volitelnými callbacky pro napojení mechaniky.
 *
 * Referenční zdroj ISA: Intel MCS-48 Family User's Manual (1980). Každý
 * opcode je v mcs48.c dokumentován mnemonikou, délkou, počtem cyklů
 * a dotčenými flagy.
 *
 * @note Modul je arch-independent (žádná závislost na MZARCH).
 */

#ifndef MCS48_H
#define MCS48_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Velikost interní ROM 8050 v bajtech (4 KB). */
#define MCS48_ROM_SIZE   4096

/** Velikost interní RAM 8050 v bajtech (256 B). */
#define MCS48_RAM_SIZE   256

/** Maska programového čítače (12 bitů, 0-0FFFh). */
#define MCS48_PC_MASK    0x0FFFu

/* ---- Bity PSW (Program Status Word) ---- */
#define MCS48_PSW_CY     0x80u   /**< Carry. */
#define MCS48_PSW_AC     0x40u   /**< Auxiliary carry (BCD). */
#define MCS48_PSW_F0     0x20u   /**< Uživatelský flag F0. */
#define MCS48_PSW_BS     0x10u   /**< Register Bank Select (0=RB0, 1=RB1). */
#define MCS48_PSW_BIT3   0x08u   /**< Bit 3 - vždy 1 (HW konstanta). */
#define MCS48_PSW_SP     0x07u   /**< Stack pointer (bity 0-2). */

/**
 * @brief Identifikátory portů pro callbacky a accessory.
 *
 * Hodnoty odpovídají číslování použitému v hooku on_port_read/on_port_write.
 */
typedef enum en_MCS48_Port {
    MCS48_PORT_BUS = 0,  /**< BUS / port 0 (DB0-7). */
    MCS48_PORT_P1  = 1,  /**< Port 1. */
    MCS48_PORT_P2  = 2   /**< Port 2. */
} en_MCS48_Port;

struct st_MCS48;

/**
 * @brief Callback pro zápis na port (OUTL/ANL/ORL Pp,A / BUS).
 *
 * Volá se po aktualizaci výstupního latche. Slouží k napojení mechaniky.
 *
 * @param c    Instance jádra.
 * @param port Identifikátor portu (en_MCS48_Port).
 * @param val  Nová hodnota výstupního latche.
 * @param ctx  Uživatelský kontext (st_MCS48::ctx).
 */
typedef void (*mcs48_port_write_fn)(struct st_MCS48 *c, int port,
                                    uint8_t val, void *ctx);

/**
 * @brief Callback pro čtení portu (IN A,Pp / INS A,BUS).
 *
 * Pokud je nastaven, jeho návratová hodnota nahradí interní vstupní hodnotu
 * portu (P1_in/P2_in/BUS_in). Slouží k napojení mechaniky.
 *
 * @param c    Instance jádra.
 * @param port Identifikátor portu (en_MCS48_Port).
 * @param ctx  Uživatelský kontext (st_MCS48::ctx).
 * @return Pin-level hodnota portu, kterou uvidí instrukce čtení.
 */
typedef uint8_t (*mcs48_port_read_fn)(struct st_MCS48 *c, int port, void *ctx);

/**
 * @brief Stav jádra MCS-48 (8050).
 *
 * Drží kompletní programátorsky viditelný stav procesoru plus interní pomocné
 * příznaky emulace (čekající přerušení, v ISR atd.).
 *
 * Invarianty:
 *  - PC je vždy maskováno na 12 bitů (MCS48_PC_MASK).
 *  - PSW bit 3 (MCS48_PSW_BIT3) je vždy nastaven na 1.
 *  - ram[] indexy registrů R0-R7 jsou 0-7 (RB0) nebo 0x18-0x1F (RB1) dle PSW.BS.
 *  - rom ukazuje na alespoň MCS48_ROM_SIZE bajtů; vlastníkem je volající.
 */
typedef struct st_MCS48 {
    uint8_t  A;            /**< Akumulátor. */
    uint16_t PC;           /**< Program counter (12-bit, maskováno MCS48_PC_MASK). */
    uint8_t  PSW;          /**< Program status word (CY/AC/F0/BS/1/SP). */
    uint8_t  ram[MCS48_RAM_SIZE]; /**< Interní RAM (256 B). */

    uint8_t  P1;           /**< Výstupní latch portu P1. */
    uint8_t  P2;           /**< Výstupní latch portu P2. */
    uint8_t  BUS;          /**< Výstupní latch BUS (port 0). */
    uint8_t  P1_in;        /**< Vstupní pin-level hodnota P1. */
    uint8_t  P2_in;        /**< Vstupní pin-level hodnota P2. */
    uint8_t  BUS_in;       /**< Vstupní pin-level hodnota BUS. */

    uint8_t  T0;           /**< Test vstup T0 (0/1). */
    uint8_t  T1;           /**< Test vstup T1 (0/1, i counter vstup). */
    uint8_t  INT;          /**< /INT vstup, úroveň (0 = aktivní). */

    uint8_t  timer;        /**< 8-bit timer/counter registr. */
    uint8_t  prescaler;    /**< Dělička /32 pro timer mód (0-31). */
    bool     timer_run;    /**< STRT T/CNT aktivní (běží čítání). */
    bool     counter_mode; /**< true = counter (hrany T1), false = timer. */
    bool     TF;           /**< Timer flag (přetečení 0FFh->00h). Viditelný flag,
                                nuluje ho jen JTF nebo nové přetečení - NE vstup
                                do timer ISR. */
    bool     timer_int_req; /**< Interní žádost o timer přerušení (latch). Nastaví
                                se s TF při přetečení (pokud EN TCNTI), shodí ji
                                vstup do timer ISR. Oddělené od TF, aby reálné
                                chování TF (drží do JTF) zůstalo zachováno. */
    uint8_t  T1_prev;      /**< Předchozí úroveň T1 (detekce sestupné hrany). */

    bool     F1;           /**< Uživatelský flag F1 (samostatný KO, ne PSW). */
    bool     EI;           /**< Povolení externího /INT přerušení (EN I). */
    bool     ETI;          /**< Povolení timer/counter přerušení (EN TCNTI). */
    bool     in_isr;       /**< V obsluze přerušení (potlačení dalších do RETR). */
    bool     MB;           /**< Memory Bank flag / DBF (A11 pro JMP/CALL). */

    const uint8_t *rom;    /**< Ukazatel na 4 KB ROM (vlastník = volající). */
    uint64_t cycles;       /**< Kumulativní čítač strojových cyklů. */

    mcs48_port_write_fn on_port_write; /**< Volitelný hook zápisu portu. */
    mcs48_port_read_fn  on_port_read;  /**< Volitelný hook čtení portu. */
    void               *ctx;           /**< Kontext pro hooky. */
} st_MCS48;

/**
 * @brief Inicializace jádra a navázání ROM.
 *
 * Vynuluje celý stav, nastaví ukazatel na ROM a provede reset (mcs48_reset).
 * Hooky portů a ctx jsou ponechány nulové (volající si je nastaví po init).
 *
 * @param c    Instance jádra (nesmí být NULL).
 * @param rom4k Ukazatel na alespoň MCS48_ROM_SIZE bajtů ROM (nesmí být NULL).
 */
void mcs48_init(st_MCS48 *c, const uint8_t *rom4k);

/**
 * @brief Reset procesoru (signál RESET).
 *
 * Nastaví PC=0, PSW na reset hodnotu (BS=0, SP=0, bit3=1, ostatní 0),
 * zakáže přerušení, zastaví timer, vynuluje TF/MB/in_isr. Výstupní latche
 * portů se nastaví na 0FFh (quasi-bidir reset stav). [neověřeno] přesná
 * reset úroveň latchů. RAM se nemaže (HW se po resetu nemění).
 *
 * @param c Instance jádra (nesmí být NULL).
 */
void mcs48_reset(st_MCS48 *c);

/**
 * @brief Vykoná jednu instrukci (s případnou obsluhou přerušení).
 *
 * Pokud čeká povolené přerušení a nejsme v ISR, nejprve provede skok do
 * vektoru (003h pro /INT, 007h pro timer). Jinak načte a vykoná jeden opcode.
 *
 * @param c Instance jádra (nesmí být NULL).
 * @return Počet spotřebovaných strojových cyklů (1, 2, nebo 2 u dispatchnutí
 *         přerušení).
 */
int mcs48_step(st_MCS48 *c);

/**
 * @brief Vykonává instrukce, dokud nespotřebuje alespoň zadaný počet cyklů.
 *
 * Opakuje mcs48_step, dokud součet vykonaných cyklů nedosáhne nebo nepřekročí
 * @p cyc. Vrací skutečně spotřebovaný počet cyklů (může být mírně víc než
 * @p cyc, protože poslední instrukce se nedělí).
 *
 * @param c   Instance jádra (nesmí být NULL).
 * @param cyc Cílový počet strojových cyklů (>= 0).
 * @return Skutečně spotřebovaný počet cyklů.
 */
int mcs48_run(st_MCS48 *c, int cyc);

/* ------------------------------------------------------------------------- */
/* Test / debug accessory                                                    */
/* ------------------------------------------------------------------------- */

/**
 * @brief Vrátí index v RAM pro registr Rr aktuální banky.
 * @param c Instance jádra.
 * @param r Číslo registru 0-7.
 * @return Index do c->ram (0-7 pro RB0, 0x18-0x1F pro RB1).
 */
uint8_t mcs48_reg_index(const st_MCS48 *c, uint8_t r);

/**
 * @brief Přečte registr Rr aktuální banky.
 * @param c Instance jádra.
 * @param r Číslo registru 0-7.
 * @return Hodnota registru.
 */
uint8_t mcs48_get_reg(const st_MCS48 *c, uint8_t r);

/**
 * @brief Nastaví registr Rr aktuální banky.
 * @param c Instance jádra.
 * @param r Číslo registru 0-7.
 * @param v Nová hodnota.
 */
void mcs48_set_reg(st_MCS48 *c, uint8_t r, uint8_t v);

/**
 * @brief Nastaví vstupní pin-level hodnotu portu (pro testy/mechaniku).
 * @param c    Instance jádra.
 * @param port Identifikátor portu (en_MCS48_Port).
 * @param val  Hodnota pinů.
 */
void mcs48_set_port_input(st_MCS48 *c, int port, uint8_t val);

#ifdef __cplusplus
}
#endif

#endif /* MCS48_H */
