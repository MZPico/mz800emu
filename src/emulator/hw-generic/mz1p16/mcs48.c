/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file mcs48.c
 * @brief Jádro mikrokontroléru Intel MCS-48 (8050) - implementace.
 *
 * Kompletní instrukční sada rodiny MCS-48 (varianta 8050), timer/counter
 * s děličkou /32, externí a timer přerušení, přepínání registrových bank,
 * stack v RAM a 12-bit adresace přes DBF/SEL MB.
 *
 * Dispatch je realizován jedním switch přes 256 opcodů. U každého opcode je
 * komentář s mnemonikou, počtem bajtů, cyklů a dotčenými flagy. Zdroj
 * sémantiky: Intel MCS-48 Family User's Manual.
 *
 * Konvence v komentářích u opcodů: "[hex] mnemo | bajty | cykly | flagy".
 *
 * Místa s dosud nejistou sémantikou (přesné reset úrovně portů, čtení
 * quasi-bidir portů, timing prescaleru při STRT/STOP) jsou označena
 * [neověřeno] / (hypotéza).
 */

#include "mcs48.h"

#include <string.h>

/* ------------------------------------------------------------------------- */
/* Pomocné inline funkce                                                     */
/* ------------------------------------------------------------------------- */

/**
 * @brief Načte bajt z ROM na 12-bit adrese.
 * @param c    Instance jádra.
 * @param addr Adresa (maskuje se na 12 bitů).
 * @return Bajt z ROM.
 */
static inline uint8_t rom_rd(const st_MCS48 *c, uint16_t addr)
{
    return c->rom[addr & MCS48_PC_MASK];
}

/**
 * @brief Načte instrukční/operandový bajt na PC a inkrementuje PC.
 * @param c Instance jádra.
 * @return Načtený bajt.
 */
static inline uint8_t fetch(st_MCS48 *c)
{
    uint8_t b = rom_rd(c, c->PC);
    c->PC = (uint16_t)((c->PC + 1) & MCS48_PC_MASK);
    return b;
}

/**
 * @brief Vrátí index v RAM pro registr Rr aktuální banky.
 *
 * RB0 -> 0-7, RB1 -> 0x18-0x1F (dle PSW.BS).
 */
uint8_t mcs48_reg_index(const st_MCS48 *c, uint8_t r)
{
    uint8_t base = (c->PSW & MCS48_PSW_BS) ? 0x18u : 0x00u;
    return (uint8_t)(base + (r & 0x07u));
}

uint8_t mcs48_get_reg(const st_MCS48 *c, uint8_t r)
{
    return c->ram[mcs48_reg_index(c, r)];
}

void mcs48_set_reg(st_MCS48 *c, uint8_t r, uint8_t v)
{
    c->ram[mcs48_reg_index(c, r)] = v;
}

/**
 * @brief Vrátí RAM adresu, na kterou ukazuje @R0 nebo @R1.
 *
 * Pointer registry R0/R1 adresují celých 256 B RAM dolními 8 bity
 * (8050 má 256 B RAM, ukazatel adresuje 8 bitů, 0-0FFh).
 *
 * @param c Instance jádra.
 * @param r 0 = @R0, 1 = @R1.
 * @return Index do RAM.
 */
static inline uint8_t indirect_addr(const st_MCS48 *c, uint8_t r)
{
    return mcs48_get_reg(c, r & 0x01u);
}

/* ---- Flagy ---- */

/** @brief Nastaví/zruší Carry v PSW. */
static inline void set_cy(st_MCS48 *c, bool v)
{
    if (v) c->PSW |= MCS48_PSW_CY; else c->PSW &= (uint8_t)~MCS48_PSW_CY;
}

/** @brief Nastaví/zruší Auxiliary Carry v PSW. */
static inline void set_ac(st_MCS48 *c, bool v)
{
    if (v) c->PSW |= MCS48_PSW_AC; else c->PSW &= (uint8_t)~MCS48_PSW_AC;
}

/** @brief Vrátí stav Carry. */
static inline bool get_cy(const st_MCS48 *c)
{
    return (c->PSW & MCS48_PSW_CY) != 0;
}

/* ---- Stack ---- */

/**
 * @brief Push 12-bit PC + horní nibble PSW na stack (CALL/INT).
 *
 * Stack leží v RAM 08h-17h, 8 úrovní × 2 bajty. SP jsou bity 0-2 PSW.
 * Formát: byte0 = PC bity 0-7; byte1 = PC bity 8-11 (dolní nibble) +
 * PSW bity 4-7 (horní nibble). Po uložení SP++ (s wrap přes 3 bity).
 *
 * @param c Instance jádra.
 */
static void stack_push(st_MCS48 *c)
{
    uint8_t sp = c->PSW & MCS48_PSW_SP;
    uint8_t slot = (uint8_t)(0x08u + sp * 2u);
    c->ram[slot]     = (uint8_t)(c->PC & 0xFFu);
    c->ram[slot + 1] = (uint8_t)(((c->PC >> 8) & 0x0Fu) | (c->PSW & 0xF0u));
    sp = (uint8_t)((sp + 1u) & 0x07u);
    c->PSW = (uint8_t)((c->PSW & ~MCS48_PSW_SP) | sp);
}

/**
 * @brief Pop PC ze stacku (RET) a volitelně PSW horní nibble (RETR).
 *
 * SP-- (wrap), pak načte PC (12-bit) z dvojice bajtů. Pokud @p restore_psw,
 * obnoví i PSW bity 4-7 (CY/AC/F0/BS) - to dělá RETR.
 *
 * @param c           Instance jádra.
 * @param restore_psw true pro RETR, false pro RET.
 */
static void stack_pop(st_MCS48 *c, bool restore_psw)
{
    uint8_t sp = (uint8_t)((c->PSW & MCS48_PSW_SP) - 1u) & 0x07u;
    uint8_t slot = (uint8_t)(0x08u + sp * 2u);
    uint8_t lo = c->ram[slot];
    uint8_t hi = c->ram[slot + 1];
    c->PC = (uint16_t)(((hi & 0x0Fu) << 8) | lo);
    /* SP do PSW */
    c->PSW = (uint8_t)((c->PSW & ~MCS48_PSW_SP) | sp);
    if (restore_psw) {
        c->PSW = (uint8_t)((c->PSW & 0x0Fu) | (hi & 0xF0u));
    }
    /* invariant: bit3 PSW vždy 1 */
    c->PSW |= MCS48_PSW_BIT3;
}

/* ---- Porty ---- */

/**
 * @brief Přečte vstupní hodnotu portu (s případným hook callbackem).
 *
 * [neověřeno] Quasi-bidir port čtení reálného HW vrací AND výstupního latche
 * s pin-level vstupem. Zde vracíme čistě vstupní hodnotu (resp. hook), což je
 * pro plotter dostatečné; přesná quasi-bidir sémantika je zjednodušena.
 *
 * @param c    Instance jádra.
 * @param port en_MCS48_Port.
 * @return Pin-level hodnota portu.
 */
static uint8_t port_read(st_MCS48 *c, int port)
{
    if (c->on_port_read) {
        return c->on_port_read(c, port, c->ctx);
    }
    switch (port) {
        case MCS48_PORT_BUS: return c->BUS_in;
        case MCS48_PORT_P1:  return c->P1_in;
        case MCS48_PORT_P2:  return c->P2_in;
        default:             return 0xFFu;
    }
}

/**
 * @brief Zapíše výstupní latch portu (s případným hook callbackem).
 * @param c    Instance jádra.
 * @param port en_MCS48_Port.
 * @param val  Nová hodnota latche.
 */
static void port_write(st_MCS48 *c, int port, uint8_t val)
{
    switch (port) {
        case MCS48_PORT_BUS: c->BUS = val; break;
        case MCS48_PORT_P1:  c->P1  = val; break;
        case MCS48_PORT_P2:  c->P2  = val; break;
        default: break;
    }
    if (c->on_port_write) {
        c->on_port_write(c, port, val, c->ctx);
    }
}

/** @brief Vrátí aktuální výstupní latch portu (pro ANL/ORL Pp,#). */
static uint8_t port_latch(const st_MCS48 *c, int port)
{
    switch (port) {
        case MCS48_PORT_BUS: return c->BUS;
        case MCS48_PORT_P1:  return c->P1;
        case MCS48_PORT_P2:  return c->P2;
        default:             return 0xFFu;
    }
}

/* ---- ALU operace nad akumulátorem ---- */

/**
 * @brief ADD/ADDC: A = A + operand (+ carry). Nastaví CY a AC.
 *
 * CY = přenos z bitu 7. AC = přenos z bitu 3 (BCD).
 *
 * @param c       Instance jádra.
 * @param operand Sčítanec.
 * @param with_c  true = ADDC (přičti i aktuální CY).
 */
static void op_add(st_MCS48 *c, uint8_t operand, bool with_c)
{
    uint8_t carry_in = (with_c && get_cy(c)) ? 1u : 0u;
    uint16_t sum = (uint16_t)c->A + operand + carry_in;
    uint8_t half = (uint8_t)((c->A & 0x0Fu) + (operand & 0x0Fu) + carry_in);
    set_ac(c, (half & 0x10u) != 0);
    set_cy(c, (sum & 0x100u) != 0);
    c->A = (uint8_t)sum;
}

/* ------------------------------------------------------------------------- */
/* Timer / counter                                                           */
/* ------------------------------------------------------------------------- */

/**
 * @brief Inkrementuje timer registr a při přetečení nastaví TF.
 *
 * Přetečení (0FFh->00h) nastaví viditelný flag TF a zároveň interní žádost
 * o timer přerušení @c timer_int_req (pokud je timer přerušení povoleno přes
 * EN TCNTI). TF se hardwarově nuluje až instrukcí JTF nebo dalším přetečením,
 * zatímco @c timer_int_req shodí vstup do timer ISR (viz service_interrupt).
 *
 * @param c Instance jádra.
 */
static inline void timer_tick(st_MCS48 *c)
{
    c->timer = (uint8_t)(c->timer + 1u);
    if (c->timer == 0x00u) {
        c->TF = true;
        if (c->ETI) {
            c->timer_int_req = true;
        }
    }
}

/**
 * @brief Posune timer/counter o jeden vykonaný strojový cyklus.
 *
 * Timer mód: prescaler /32 - každých 32 strojových cyklů jeden tick.
 * Counter mód: tick na sestupnou hranu vstupu T1 (detekce přes T1_prev).
 * [neověřeno] Přesné chování prescaleru při STRT/STOP (zda se nuluje) -
 * zde se prescaler nuluje při STRT T (viz dispatch), jinak běží dál.
 *
 * @param c       Instance jádra.
 * @param n_cyc   Počet vykonaných strojových cyklů (1 nebo 2).
 */
static void timer_advance(st_MCS48 *c, int n_cyc)
{
    if (!c->timer_run) {
        /* counter mód detekuje hranu i bez "běhu" timeru? Ne - STRT CNT
         * nastaví timer_run=true a counter_mode=true. Pokud neběží, jen
         * aktualizuj T1_prev. */
        c->T1_prev = c->T1;
        return;
    }

    if (c->counter_mode) {
        /* Counter: sestupná hrana T1 (1 -> 0). Vyhodnocuje se na hranicích
         * vykonaných cyklů; jemnější timing není pro plotter kritický. */
        if (c->T1_prev != 0 && c->T1 == 0) {
            timer_tick(c);
        }
        c->T1_prev = c->T1;
    } else {
        /* Timer: prescaler /32. */
        for (int i = 0; i < n_cyc; i++) {
            c->prescaler = (uint8_t)(c->prescaler + 1u);
            if (c->prescaler >= 32u) {
                c->prescaler = 0u;
                timer_tick(c);
            }
        }
        c->T1_prev = c->T1;
    }
}

/* ------------------------------------------------------------------------- */
/* Veřejné API: init / reset / run                                           */
/* ------------------------------------------------------------------------- */

void mcs48_init(st_MCS48 *c, const uint8_t *rom4k)
{
    memset(c, 0, sizeof(*c));
    c->rom = rom4k;
    mcs48_reset(c);
}

void mcs48_reset(st_MCS48 *c)
{
    c->PC = 0;
    c->A = 0;
    /* PSW: bit3=1, BS=0, SP=0, ostatní 0. */
    c->PSW = MCS48_PSW_BIT3;
    c->F1 = false;
    c->EI = false;
    c->ETI = false;
    c->in_isr = false;
    c->MB = false;
    c->TF = false;
    c->timer_int_req = false;
    c->timer_run = false;
    c->counter_mode = false;
    c->prescaler = 0;
    /* Výstupní latche portů -> 0FFh (quasi-bidir reset stav) [neověřeno] */
    c->P1 = 0xFFu;
    c->P2 = 0xFFu;
    c->BUS = 0xFFu;
    c->T1_prev = c->T1;
    /* RAM a timer registr se resetem nemění (zachováno z init memset). */
}

/* ------------------------------------------------------------------------- */
/* Přerušení                                                                 */
/* ------------------------------------------------------------------------- */

/**
 * @brief Vyhodnotí čekající přerušení a případně skočí do vektoru.
 *
 * Priorita: externí /INT (vektor 003h) má přednost před timer (007h).
 * Skok se provede jen pokud nejsme již v ISR a příslušné přerušení je
 * povoleno. Push PC+PSW jako u CALL, in_isr=true.
 * [neověřeno] Přesná priorita a latching /INT (úroveň vs hrana) dle manuálu;
 * /INT je úrovňové (aktivní L), TF je latch. Zde: /INT aktivní = c->INT==0.
 *
 * Timer přerušení: vyhodnocuje se interní žádost @c timer_int_req (oddělená
 * od viditelného flagu TF). Vstup do timer ISR shodí @c timer_int_req, aby se
 * obsluha neopakovala, ale viditelné TF zůstane nastavené - reálný MCS-48 při
 * přijetí přerušení shodí jen interní interrupt-request latch, TF nuluje až
 * instrukce JTF nebo nové přetečení.
 *
 * @param c Instance jádra.
 * @return true pokud byl proveden skok do ISR (spotřeba 2 cykly).
 */
static bool service_interrupt(st_MCS48 *c)
{
    if (c->in_isr) {
        return false;
    }
    if (c->EI && c->INT == 0) {
        stack_push(c);
        c->PC = 0x003u;
        c->in_isr = true;
        return true;
    }
    if (c->ETI && c->timer_int_req) {
        stack_push(c);
        c->PC = 0x007u;
        c->in_isr = true;
        /* Shoď jen interní žádost; viditelný TF drží do JTF / nového
         * přetečení (reálné HW chování). */
        c->timer_int_req = false;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* Dispatch jedné instrukce                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief Dekóduje a vykoná jeden opcode (bez kontroly přerušení).
 * @param c Instance jádra.
 * @return Počet spotřebovaných strojových cyklů.
 */
static int execute_one(st_MCS48 *c)
{
    uint8_t op = fetch(c);
    int cyc = 1;

    switch (op) {

    /* ===== Skupina 0x0_ ===== */
    case 0x00: /* 00 NOP | 1 | 1 | - */
        break;
    case 0x02: /* 02 OUTL BUS,A | 1 | 2 | - */
        port_write(c, MCS48_PORT_BUS, c->A); cyc = 2; break;
    case 0x03: /* 03 ADD A,# | 2 | 2 | CY,AC */
        op_add(c, fetch(c), false); cyc = 2; break;
    case 0x04: case 0x24: case 0x44: case 0x64: /* x4 JMP add | 2 | 2 | - */
    case 0x84: case 0xA4: case 0xC4: case 0xE4: {
        uint8_t addlo = fetch(c);
        uint16_t addhi = (uint16_t)(op >> 5);          /* bity 7-5 = addhi */
        /* DBF = A11, ale během obsluhy přerušení je A11 nuceně 0: ISR vždy
         * adresuje bank 0 (0-2047) bez ohledu na DBF, dokud RETR neobnoví
         * původní A11 (Intel MCS-48 manual). */
        uint16_t a11 = ( c->MB && !c->in_isr ) ? 0x800u : 0x000u;
        c->PC = (uint16_t)((a11 | (addhi << 8) | addlo) & MCS48_PC_MASK);
        cyc = 2; break;
    }
    case 0x05: /* 05 EN I | 1 | 1 | - */
        c->EI = true; break;
    case 0x07: /* 07 DEC A | 1 | 1 | - */
        c->A = (uint8_t)(c->A - 1u); break;
    case 0x08: /* 08 INS A,BUS | 1 | 2 | - */
        c->A = port_read(c, MCS48_PORT_BUS); cyc = 2; break;
    case 0x09: /* 09 IN A,P1 | 1 | 2 | - */
        c->A = port_read(c, MCS48_PORT_P1); cyc = 2; break;
    case 0x0A: /* 0A IN A,P2 | 1 | 2 | - */
        c->A = port_read(c, MCS48_PORT_P2); cyc = 2; break;

    /* ===== Skupina 0x1_ ===== */
    case 0x10: case 0x11: /* 1R INC @Rr | 1 | 1 | - */
        { uint8_t a = indirect_addr(c, op & 1u); c->ram[a]++; } break;
    case 0x12: case 0x32: case 0x52: case 0x72:  /* JBb addr | 2 | 2 | - */
    case 0x92: case 0xB2: case 0xD2: case 0xF2: {
        uint8_t addr = fetch(c);
        uint8_t b = (uint8_t)(op >> 5);             /* bity 7-5 = b */
        if (c->A & (1u << b)) {
            c->PC = (uint16_t)((c->PC & 0xF00u) | addr);
        }
        cyc = 2; break;
    }
    case 0x13: /* 13 ADDC A,# | 2 | 2 | CY,AC */
        op_add(c, fetch(c), true); cyc = 2; break;
    case 0x14: case 0x34: case 0x54: case 0x74: /* x4 CALL add | 2 | 2 | - */
    case 0x94: case 0xB4: case 0xD4: case 0xF4: {
        uint8_t addlo = fetch(c);
        uint16_t addhi = (uint16_t)(op >> 5);
        /* A11 nuceně 0 během obsluhy přerušení (viz JMP výše). */
        uint16_t a11 = ( c->MB && !c->in_isr ) ? 0x800u : 0x000u;
        stack_push(c);
        c->PC = (uint16_t)((a11 | (addhi << 8) | addlo) & MCS48_PC_MASK);
        cyc = 2; break;
    }
    case 0x15: /* 15 DIS I | 1 | 1 | - */
        c->EI = false; break;
    case 0x16: /* 16 JTF addr | 2 | 2 | - (nuluje TF) */
        { uint8_t addr = fetch(c);
          if (c->TF) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          /* JTF nuluje viditelný TF; shodíme i interní žádost o timer INT,
           * aby SW-polling přes JTF (místo přerušení) nezanechal žádost,
           * která by později vyvolala zbytečné vektorování na 007h. */
          c->TF = false;
          c->timer_int_req = false; cyc = 2; } break;
    case 0x17: /* 17 INC A | 1 | 1 | - */
        c->A = (uint8_t)(c->A + 1u); break;
    case 0x18: case 0x19: case 0x1A: case 0x1B: /* 1R INC Rr | 1 | 1 | - */
    case 0x1C: case 0x1D: case 0x1E: case 0x1F:
        mcs48_set_reg(c, op & 7u, (uint8_t)(mcs48_get_reg(c, op & 7u) + 1u));
        break;

    /* ===== Skupina 0x2_ ===== */
    case 0x20: case 0x21: /* 2R XCH A,@Rr | 1 | 1 | - */
        { uint8_t a = indirect_addr(c, op & 1u);
          uint8_t t = c->ram[a]; c->ram[a] = c->A; c->A = t; } break;
    case 0x23: /* 23 MOV A,# | 2 | 2 | - */
        c->A = fetch(c); cyc = 2; break;
    case 0x25: /* 25 EN TCNTI | 1 | 1 | - */
        c->ETI = true; break;
    case 0x26: /* 26 JNT0 addr | 2 | 2 | - (skok pokud T0=0) */
        { uint8_t addr = fetch(c);
          if (c->T0 == 0) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0x27: /* 27 CLR A | 1 | 1 | - */
        c->A = 0; break;
    case 0x28: case 0x29: case 0x2A: case 0x2B: /* 2R XCH A,Rr | 1 | 1 | - */
    case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        { uint8_t t = mcs48_get_reg(c, op & 7u);
          mcs48_set_reg(c, op & 7u, c->A); c->A = t; } break;

    /* ===== Skupina 0x3_ ===== */
    case 0x30: case 0x31: /* 3R XCHD A,@Rr | 1 | 1 | - (jen dolní nibble) */
        /* Prohození jen dolních nibblů A a @Rr dle specifikace rodiny
         * MCS-48; horní nibbly obou operandů zůstávají beze změny. */
        { uint8_t a = indirect_addr(c, op & 1u);
          uint8_t m = c->ram[a];
          uint8_t lo_a = c->A & 0x0Fu;
          c->A = (uint8_t)((c->A & 0xF0u) | (m & 0x0Fu));
          c->ram[a] = (uint8_t)((m & 0xF0u) | lo_a); } break;
    case 0x35: /* 35 DIS TCNTI | 1 | 1 | - */
        c->ETI = false; break;
    case 0x36: /* 36 JT0 addr | 2 | 2 | - (skok pokud T0=1) */
        { uint8_t addr = fetch(c);
          if (c->T0 != 0) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0x37: /* 37 CPL A | 1 | 1 | - */
        c->A = (uint8_t)~c->A; break;
    case 0x39: /* 39 OUTL P1,A | 1 | 2 | - */
        port_write(c, MCS48_PORT_P1, c->A); cyc = 2; break;
    case 0x3A: /* 3A OUTL P2,A | 1 | 2 | - */
        port_write(c, MCS48_PORT_P2, c->A); cyc = 2; break;

    /* ===== Skupina 0x4_ ===== */
    case 0x40: case 0x41: /* 4R ORL A,@Rr | 1 | 1 | - */
        c->A |= c->ram[indirect_addr(c, op & 1u)]; break;
    case 0x42: /* 42 MOV A,T | 1 | 1 | - */
        c->A = c->timer; break;
    case 0x43: /* 43 ORL A,# | 2 | 2 | - */
        c->A |= fetch(c); cyc = 2; break;
    case 0x45: /* 45 STRT CNT | 1 | 1 | - */
        c->timer_run = true; c->counter_mode = true; c->T1_prev = c->T1; break;
    case 0x46: /* 46 JNT1 addr | 2 | 2 | - (skok pokud T1=0) */
        { uint8_t addr = fetch(c);
          if (c->T1 == 0) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0x47: /* 47 SWAP A | 1 | 1 | - (prohodí nibbly) */
        c->A = (uint8_t)(((c->A & 0x0Fu) << 4) | ((c->A & 0xF0u) >> 4)); break;
    case 0x48: case 0x49: case 0x4A: case 0x4B: /* 4R ORL A,Rr | 1 | 1 | - */
    case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        c->A |= mcs48_get_reg(c, op & 7u); break;

    /* ===== Skupina 0x5_ ===== */
    case 0x50: case 0x51: /* 5R ANL A,@Rr | 1 | 1 | - */
        c->A &= c->ram[indirect_addr(c, op & 1u)]; break;
    case 0x53: /* 53 ANL A,# | 2 | 2 | - */
        c->A &= fetch(c); cyc = 2; break;
    case 0x55: /* 55 STRT T | 1 | 1 | - (timer mód, nuluje prescaler) */
        c->timer_run = true; c->counter_mode = false; c->prescaler = 0; break;
    case 0x56: /* 56 JT1 addr | 2 | 2 | - (skok pokud T1=1) */
        { uint8_t addr = fetch(c);
          if (c->T1 != 0) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0x57: /* 57 DA A | 1 | 1 | CY,AC (BCD korekce) */
        { /* Pokud A0-3 > 9 nebo AC=1: A += 6; pak pokud A4-7 > 9 nebo CY=1:
           * A += 0x60 (a nastaví CY).
           *
           * Mezivýpočet držíme v širším typu (uint), aby první korekce +6
           * mohla přetéct přes 0FFh a test horního nibblu se prováděl na
           * úplné hodnotě vč. přetečení. Test "horní nibble > 9" je proto
           * "a > 09Fh" - to zahrnuje i a > 0FFh (např. A=0FAh + 6 = 100h),
           * kde by maskování na 8 bitů přetečení skrylo a druhá korekce
           * +60h by se neprovedla. */
          unsigned a = c->A;
          if (((a & 0x0Fu) > 9u) || (c->PSW & MCS48_PSW_AC)) {
              a += 0x06u;
          }
          if ((a > 0x9Fu) || get_cy(c)) {
              a += 0x60u;
              set_cy(c, true);
          }
          /* [neověřeno] DA A nuluje CY pokud nedošlo k přenosu? Manuál:
           * CY se nastaví při přenosu z horního nibblu; nenuluje se jinak.
           * Ponecháváme stávající CY pokud nedošlo k +0x60. */
          c->A = (uint8_t)a; } break;
    case 0x58: case 0x59: case 0x5A: case 0x5B: /* 5R ANL A,Rr | 1 | 1 | - */
    case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        c->A &= mcs48_get_reg(c, op & 7u); break;

    /* ===== Skupina 0x6_ ===== */
    case 0x60: case 0x61: /* 6R ADD A,@Rr | 1 | 1 | CY,AC */
        op_add(c, c->ram[indirect_addr(c, op & 1u)], false); break;
    case 0x62: /* 62 MOV T,A | 1 | 1 | - */
        c->timer = c->A; break;
    case 0x65: /* 65 STOP TCNT | 1 | 1 | - */
        c->timer_run = false; break;
    case 0x67: /* 67 RRC A | 1 | 1 | CY (rotace vpravo přes carry) */
        { uint8_t old_cy = get_cy(c) ? 0x80u : 0x00u;
          set_cy(c, (c->A & 0x01u) != 0);
          c->A = (uint8_t)((c->A >> 1) | old_cy); } break;
    case 0x68: case 0x69: case 0x6A: case 0x6B: /* 6R ADD A,Rr | 1 | 1 | CY,AC */
    case 0x6C: case 0x6D: case 0x6E: case 0x6F:
        op_add(c, mcs48_get_reg(c, op & 7u), false); break;

    /* ===== Skupina 0x7_ ===== */
    case 0x70: case 0x71: /* 7R ADDC A,@Rr | 1 | 1 | CY,AC */
        op_add(c, c->ram[indirect_addr(c, op & 1u)], true); break;
    case 0x75: /* 75 ENT0 CLK | 1 | 1 | - (T0 jako hodinový výstup) */
        /* [neověřeno] Pro plotter pravděpodobně nepoužito; no-op model. */
        break;
    case 0x76: /* 76 JF1 addr | 2 | 2 | - */
        { uint8_t addr = fetch(c);
          if (c->F1) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0x77: /* 77 RR A | 1 | 1 | - (rotace vpravo) */
        c->A = (uint8_t)((c->A >> 1) | ((c->A & 0x01u) << 7)); break;
    case 0x78: case 0x79: case 0x7A: case 0x7B: /* 7R ADDC A,Rr | 1 | 1 | CY,AC */
    case 0x7C: case 0x7D: case 0x7E: case 0x7F:
        op_add(c, mcs48_get_reg(c, op & 7u), true); break;

    /* ===== Skupina 0x8_ ===== */
    case 0x80: case 0x81: /* 8R MOVX A,@Rr | 1 | 2 | - (externí RAM) */
        /* [neověřeno] Plotter pravděpodobně nemá externí RAM; model čte
         * vstupní hodnotu BUS jako placeholder. */
        c->A = port_read(c, MCS48_PORT_BUS); cyc = 2; break;
    case 0x83: /* 83 RET | 1 | 2 | - */
        stack_pop(c, false); cyc = 2; break;
    case 0x85: /* 85 CLR F0 | 1 | 1 | F0 */
        c->PSW &= (uint8_t)~MCS48_PSW_F0; break;
    case 0x86: /* 86 JNI addr | 2 | 2 | - (skok pokud /INT vstup = 0) */
        { uint8_t addr = fetch(c);
          if (c->INT == 0) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0x88: /* 88 ORL BUS,# | 2 | 2 | - */
        { uint8_t imm = fetch(c);
          port_write(c, MCS48_PORT_BUS,
                     (uint8_t)(port_latch(c, MCS48_PORT_BUS) | imm));
          cyc = 2; } break;
    case 0x89: /* 89 ORL P1,# | 2 | 2 | - */
        { uint8_t imm = fetch(c);
          port_write(c, MCS48_PORT_P1,
                     (uint8_t)(port_latch(c, MCS48_PORT_P1) | imm));
          cyc = 2; } break;
    case 0x8A: /* 8A ORL P2,# | 2 | 2 | - */
        { uint8_t imm = fetch(c);
          port_write(c, MCS48_PORT_P2,
                     (uint8_t)(port_latch(c, MCS48_PORT_P2) | imm));
          cyc = 2; } break;

    /* ===== Skupina 0x9_ ===== */
    case 0x90: case 0x91: /* 9R MOVX @Rr,A | 1 | 2 | - (externí RAM) */
        /* [neověřeno] Viz MOVX A,@Rr; model zapisuje na BUS latch. */
        port_write(c, MCS48_PORT_BUS, c->A); cyc = 2; break;
    case 0x93: /* 93 RETR | 1 | 2 | obnoví PSW horní nibble, ukončí ISR */
        stack_pop(c, true); c->in_isr = false; cyc = 2; break;
    case 0x95: /* 95 CPL F0 | 1 | 1 | F0 */
        c->PSW ^= MCS48_PSW_F0; break;
    case 0x96: /* 96 JNZ addr | 2 | 2 | - (skok pokud A != 0) */
        /* Protějšek JZ (C6h): 2-bajtový podmíněný skok, PC0-7 <- addr když
         * A != 0; při A == 0 propadne na následující instrukci. */
        { uint8_t addr = fetch(c);
          if (c->A != 0) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0x97: /* 97 CLR C | 1 | 1 | CY */
        set_cy(c, false); break;
    case 0x98: /* 98 ANL BUS,# | 2 | 2 | - */
        { uint8_t imm = fetch(c);
          port_write(c, MCS48_PORT_BUS,
                     (uint8_t)(port_latch(c, MCS48_PORT_BUS) & imm));
          cyc = 2; } break;
    case 0x99: /* 99 ANL P1,# | 2 | 2 | - */
        { uint8_t imm = fetch(c);
          port_write(c, MCS48_PORT_P1,
                     (uint8_t)(port_latch(c, MCS48_PORT_P1) & imm));
          cyc = 2; } break;
    case 0x9A: /* 9A ANL P2,# | 2 | 2 | - */
        { uint8_t imm = fetch(c);
          port_write(c, MCS48_PORT_P2,
                     (uint8_t)(port_latch(c, MCS48_PORT_P2) & imm));
          cyc = 2; } break;

    /* ===== Skupina 0xA_ ===== */
    case 0xA0: case 0xA1: /* AR MOV @Rr,A | 1 | 1 | - */
        c->ram[indirect_addr(c, op & 1u)] = c->A; break;
    case 0xA3: /* A3 MOVP A,@A | 1 | 2 | - (tabulka v aktuální stránce) */
        /* A <- ROM(PC[8-11]:A). PC již ukazuje za opcode; horní 4 bity PC.
         * [neověřeno] Chování na hranici stránky: pokud opcode MOVP leží na
         * adrese xxFFh, PC po fetchi inkrementuje do následující stránky
         * (PC[8-11] se změní), takže tabulka se čte z jiné stránky, než kde
         * leží opcode. Reálné HW pravděpodobně používá PC po inkrementu
         * (tj. stránku následující instrukce) - zde implementováno právě tak.
         * Pro plotter pravděpodobně nekritické (tabulky nezačínají na xxFFh). */
        c->A = rom_rd(c, (uint16_t)((c->PC & 0xF00u) | c->A)); cyc = 2; break;
    case 0xA5: /* A5 CLR F1 | 1 | 1 | F1 */
        c->F1 = false; break;
    case 0xA7: /* A7 CPL C | 1 | 1 | CY */
        set_cy(c, !get_cy(c)); break;
    case 0xA8: case 0xA9: case 0xAA: case 0xAB: /* AR MOV Rr,A | 1 | 1 | - */
    case 0xAC: case 0xAD: case 0xAE: case 0xAF:
        mcs48_set_reg(c, op & 7u, c->A); break;

    /* ===== Skupina 0xB_ ===== */
    case 0xB0: case 0xB1: /* BR MOV @Rr,# | 2 | 2 | - */
        c->ram[indirect_addr(c, op & 1u)] = fetch(c); cyc = 2; break;
    case 0xB3: /* B3 JMPP @A | 1 | 2 | - (nepřímý skok v rámci stránky) */
        /* PC[0-7] <- ROM(PC[8-11]:A). */
        { uint8_t target = rom_rd(c, (uint16_t)((c->PC & 0xF00u) | c->A));
          c->PC = (uint16_t)((c->PC & 0xF00u) | target); cyc = 2; } break;
    case 0xB5: /* B5 CPL F1 | 1 | 1 | F1 */
        c->F1 = !c->F1; break;
    case 0xB6: /* B6 JF0 addr | 2 | 2 | - */
        { uint8_t addr = fetch(c);
          if (c->PSW & MCS48_PSW_F0) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: /* BR MOV Rr,# | 2 | 2 | - */
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        mcs48_set_reg(c, op & 7u, fetch(c)); cyc = 2; break;

    /* ===== Skupina 0xC_ ===== */
    case 0xC5: /* C5 SEL RB0 | 1 | 1 | BS */
        c->PSW &= (uint8_t)~MCS48_PSW_BS; break;
    case 0xC6: /* C6 JZ addr | 2 | 2 | - */
        { uint8_t addr = fetch(c);
          if (c->A == 0) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0xC7: /* C7 MOV A,PSW | 1 | 1 | - */
        c->A = c->PSW; break;
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: /* CR DEC Rr | 1 | 1 | - */
    case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        mcs48_set_reg(c, op & 7u, (uint8_t)(mcs48_get_reg(c, op & 7u) - 1u));
        break;

    /* ===== Skupina 0xD_ ===== */
    case 0xD0: case 0xD1: /* DR XRL A,@Rr | 1 | 1 | - */
        c->A ^= c->ram[indirect_addr(c, op & 1u)]; break;
    case 0xD3: /* D3 XRL A,# | 2 | 2 | - */
        c->A ^= fetch(c); cyc = 2; break;
    case 0xD5: /* D5 SEL RB1 | 1 | 1 | BS */
        c->PSW |= MCS48_PSW_BS; break;
    case 0xD7: /* D7 MOV PSW,A | 1 | 1 | celé PSW */
        c->PSW = (uint8_t)(c->A | MCS48_PSW_BIT3); break;
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: /* DR XRL A,Rr | 1 | 1 | - */
    case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        c->A ^= mcs48_get_reg(c, op & 7u); break;

    /* ===== Skupina 0xE_ ===== */
    case 0xE3: /* E3 MOVP3 A,@A | 1 | 2 | - (tabulka ve stránce 3) */
        /* A <- ROM(0011:A) = ROM(0x300 | A). Stránka je pevně 3 nezávisle na
         * PC, takže problém s hranicí stránky (viz MOVP) zde nevzniká.
         * [neověřeno] chování při čtení z 0x300..0x3FF mimo platnou ROM. */
        c->A = rom_rd(c, (uint16_t)(0x300u | c->A)); cyc = 2; break;
    case 0xE5: /* E5 SEL MB0 | 1 | 1 | DBF=0 */
        c->MB = false; break;
    case 0xE6: /* E6 JNC addr | 2 | 2 | - (skok pokud CY=0) */
        { uint8_t addr = fetch(c);
          if (!get_cy(c)) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0xE7: /* E7 RL A | 1 | 1 | - (rotace vlevo) */
        c->A = (uint8_t)((c->A << 1) | ((c->A & 0x80u) >> 7)); break;
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: /* ER DJNZ Rr,addr | 2 | 2 | - */
    case 0xEC: case 0xED: case 0xEE: case 0xEF:
        { uint8_t addr = fetch(c);
          uint8_t v = (uint8_t)(mcs48_get_reg(c, op & 7u) - 1u);
          mcs48_set_reg(c, op & 7u, v);
          if (v != 0) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;

    /* ===== Skupina 0xF_ ===== */
    case 0xF0: case 0xF1: /* FR MOV A,@Rr | 1 | 1 | - */
        c->A = c->ram[indirect_addr(c, op & 1u)]; break;
    case 0xF5: /* F5 SEL MB1 | 1 | 1 | DBF=1 */
        c->MB = true; break;
    case 0xF6: /* F6 JC addr | 2 | 2 | - (skok pokud CY=1) */
        { uint8_t addr = fetch(c);
          if (get_cy(c)) { c->PC = (uint16_t)((c->PC & 0xF00u) | addr); }
          cyc = 2; } break;
    case 0xF7: /* F7 RLC A | 1 | 1 | CY (rotace vlevo přes carry) */
        { uint8_t old_cy = get_cy(c) ? 0x01u : 0x00u;
          set_cy(c, (c->A & 0x80u) != 0);
          c->A = (uint8_t)((c->A << 1) | old_cy); } break;
    case 0xF8: case 0xF9: case 0xFA: case 0xFB: /* FR MOV A,Rr | 1 | 1 | - */
    case 0xFC: case 0xFD: case 0xFE: case 0xFF:
        c->A = mcs48_get_reg(c, op & 7u); break;

    /* ===== 8243 I/O expander (porty P4-P7) - NEIMPLEMENTOVÁNO ===== */
    /* MOVD A,Pp (0C-0F), MOVD Pp,A (3C-3F), ORLD Pp,A (8C-8F),
     * ANLD Pp,A (9C-9F). Plotter MZ-1P16 8243 expander pravděpodobně
     * nepoužívá [neověřeno]. Vykonáme jako 2-cyklový no-op, aby firmware
     * nezhavaroval na neznámém opcode. */
    case 0x0C: case 0x0D: case 0x0E: case 0x0F:
    case 0x3C: case 0x3D: case 0x3E: case 0x3F:
    case 0x8C: case 0x8D: case 0x8E: case 0x8F:
    case 0x9C: case 0x9D: case 0x9E: case 0x9F:
        cyc = 2; break;

    default:
        /* Mezera v opcode mapě (nedefinovaný/ilegální opcode rodiny MCS-48).
         * Nepřiřazené opcody v 8048/8050 ISA:
         * 01 06 0B 22 33 38 3B 63 66 73 82 87 8B 9B A2 A6 B7 C0 C1 C2 C3 D6
         * E0 E1 E2 F3. Chováme se jako NOP (1 cyklus). [neověřeno] reálné HW
         * chování ilegálních opcodů. */
        break;
    }

    return cyc;
}

int mcs48_step(st_MCS48 *c)
{
    /* Nejprve zkus obsloužit čekající přerušení. Dispatch trvá 2 strojové
     * cykly; během nich timer/counter běží dál, proto i tato větev volá
     * timer_advance, ať timer postupuje konzistentně jako u běžné instrukce. */
    if (service_interrupt(c)) {
        timer_advance(c, 2);
        c->cycles += 2;
        return 2;
    }

    int cyc = execute_one(c);
    timer_advance(c, cyc);
    c->cycles += (uint64_t)cyc;
    return cyc;
}

int mcs48_run(st_MCS48 *c, int cyc)
{
    int done = 0;
    while (done < cyc) {
        done += mcs48_step(c);
    }
    return done;
}

void mcs48_set_port_input(st_MCS48 *c, int port, uint8_t val)
{
    switch (port) {
        case MCS48_PORT_BUS: c->BUS_in = val; break;
        case MCS48_PORT_P1:  c->P1_in = val; break;
        case MCS48_PORT_P2:  c->P2_in = val; break;
        default: break;
    }
}
