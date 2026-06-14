/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file mz1p16.c
 * @brief Mechanika plotteru Sharp MZ-1P16 nad jádrem 8050 - implementace.
 *
 * Model X-Y perového plotteru řízeného interním 8050. Napojení na jádro je
 * přes hooky portů; tento modul interpretuje výstupy na BUS/P1 jako pohyb
 * krokových motorů, zvedání pera a změnu barvy a akumuluje nakreslené tahy.
 *
 * Chování firmwaru:
 *
 * 1) STEPPER SEKVENCE (X-osa, BUS dolní nibble):
 *    Firmware při pohybu vozíku cyklicky vystavuje na dolní nibble BUS
 *    posloupnost fází:
 *        0xA -> 0x6 -> 0x5 -> 0x9 -> (0xA) ...
 *    (bitově 1010 -> 0110 -> 0101 -> 1001).
 *    Pro Y-osu (horní nibble) předpokládáme TOTOŽNOU 4-fázovou sekvenci
 *    [neověřeno].
 *
 * 2) SMĚR A LEVÝ DORAZ:
 *    Firmware při startu napevno odkroká ~554 kroků jedním směrem a pak u
 *    dorazu osciluje s amplitudou ~28 kroků (opakovaný náraz, perioda ~65k
 *    instrukcí). Jde o open-loop homing proti MECHANICKÉMU DORAZU. Aby doraz
 *    odpovídal LEVÉMU okraji (xpos = 0), je směrová konvence sekvence
 *    A->6->5->9 brána jako -1 krok (REV, tj. doleva); opačné pořadí
 *    9->5->6->A jako +1 (doprava). Pozici klampujeme na xpos >= 0 (vozík
 *    nemůže za levý okraj). Vozík tak najede na levý doraz a osciluje kolem
 *    xpos = 0, čímž opakovaně vjíždí do pen-change zóny.
 *
 * 3) PERO / P1:
 *    P1.0 = 0 -> zvedni pero; P1.1 = 0 -> spusť pero. Po resetu P1 = 0FFh ->
 *    pero nahoře. Firmware hned po bootu pulzne P1 = 0FEh (P1.0=0) a zpět
 *    (pen test). Při kresbě shazuje P1.1 (0FCh). Model: pero DOLE, pokud je
 *    shozen bit P1.1 (drive spuštění); jinak nahoře.
 *
 * 4) BUSY / P1.4:
 *    P1.4 = 1 znamená BUSY hlášené hostu. Firmware u dorazu vystavuje
 *    P1 = 0EFh (P1.4=0) - ne-busy [přesné časování BUSY proti handshaku
 *    neověřeno]. Model: m->busy = (P1.4 == 1).
 *
 * 5) HANDSHAKE:
 *    Firmware NEpoužívá externí /INT přerušení (EI=0 po celou dobu, vektor
 *    003h se nevolá); data z hosta čte pollingem. Strobe modelujeme pulzem
 *    /INT (úroveň), data na P2. ETI (timer INT) je zapnuto - timer pacuje
 *    krokování (firmware polluje JTF).
 *
 * 6) VSTUPNÍ SENZORY:
 *    Plotter NEMÁ home senzor čtený CPU - homing je open-loop (viz bod 2).
 *    Vstupní bity P1.2-P1.7 a T0/T1 drží klidovou úroveň (1). Firmware je
 *    čte (IN A,P1), ale na žádném z nich pohyb nezávisí.
 */

#include "mz1p16.h"

#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* Dekodér směru kroku                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief 4-fázová sekvence stepperu (dolní nibble BUS).
 *
 * Index v tomto poli roste ve směru, který firmware vystavuje při pohybu
 * vozíku (viz file-level komentář bod 1). Sousední index (+1 mod 4) = krok
 * vpřed, (-1 mod 4) = krok vzad. Hodnota mimo sekvenci = neplatný přechod
 * (ignorujeme, jen aktualizujeme fázi).
 */
static const uint8_t MZ1P16_PHASE_SEQ[4] = { 0xA, 0x6, 0x5, 0x9 };

/**
 * @brief Fyzický směr X osy vůči postupu v MZ1P16_PHASE_SEQ.
 *
 * Postup A->6->5->9 (raw +1) odpovídá pohybu DOLEVA (k levému dorazu) - viz
 * file-level komentář bod 2. Proto raw +1 = xpos -1.
 */
#define MZ1P16_X_DIR (-1)

/**
 * @brief Fyzický směr Y osy vůči postupu v sekvenci.
 *
 * [neověřeno] Konvence +1 = posuv papíru vpřed. Znaménko Y osy je dosud
 * neověřené, případně se doladí.
 */
#define MZ1P16_Y_DIR (+1)

/**
 * @brief Vrátí index fáze v MZ1P16_PHASE_SEQ, nebo -1 pokud není v sekvenci.
 * @param phase Nibble fáze (0-15).
 * @return 0-3 nebo -1.
 */
static int phase_index(uint8_t phase)
{
    for (int i = 0; i < 4; i++) {
        if (MZ1P16_PHASE_SEQ[i] == phase) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Vyhodnotí změnu fáze stepperu a vrátí RAW směr kroku v sekvenci.
 *
 * Porovná novou fázi s předchozí proti fázové sekvenci. Vrací raw směr
 * (postup v sekvenci = +1), NEaplikuje ho na pozici - fyzický směr
 * osy a klampování řeší volající (viz on_port_write), který zná osu.
 *
 * @param st        Stav stepperu (aktualizuje jen phase/have_phase).
 * @param new_phase Nová fáze (nibble).
 * @return +1 (postup v sekvenci), -1 (zpět v sekvenci), 0 (beze změny /
 *         neplatný přechod).
 */
static int stepper_apply(st_MZ1P16_Stepper *st, uint8_t new_phase)
{
    if (!st->have_phase) {
        st->phase = new_phase;
        st->have_phase = true;
        return 0;
    }
    if (new_phase == st->phase) {
        return 0;
    }

    int old_i = phase_index(st->phase);
    int new_i = phase_index(new_phase);
    int dir = 0;

    if (old_i >= 0 && new_i >= 0) {
        int diff = (new_i - old_i) & 3;  /* 0..3 modulo 4 */
        if (diff == 1) {
            dir = +1;  /* další v sekvenci */
        } else if (diff == 3) {
            dir = -1;  /* předchozí v sekvenci */
        } else {
            /* diff==2: skok o dvě fáze (half/full nejednoznačný) -
             * neurčujeme směr, jen posuneme fázi. [neověřeno zda nastává] */
            dir = 0;
        }
    }

    st->phase = new_phase;
    return dir;
}

/* ------------------------------------------------------------------------- */
/* Kreslení                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief Zaznamená tah z předchozí do aktuální pozice, je-li pero dole.
 *
 * Volá se po každém kroku motoru. Pokud je pero spuštěné a pozice se změnila,
 * přidá úsečku do bufferu. Při plném bufferu inkrementuje @c dropped.
 *
 * @param m       Instance mechaniky.
 * @param px,py   Pozice před krokem.
 */
static void record_stroke(st_MZ1P16 *m, int32_t px, int32_t py)
{
    if (!m->pen_down) {
        return;
    }
    if (px == m->sx.pos && py == m->sy.pos) {
        return;
    }
    if (m->n_strokes >= MZ1P16_MAX_STROKES) {
        m->dropped++;
        return;
    }
    st_MZ1P16_Stroke *s = &m->strokes[m->n_strokes++];
    s->x0 = px; s->y0 = py;
    s->x1 = m->sx.pos; s->y1 = m->sy.pos;
    s->color = m->color;
}

/* ------------------------------------------------------------------------- */
/* Levý doraz + pen-change zóna (rotace bubnu)                               */
/* ------------------------------------------------------------------------- */

/**
 * @brief Aplikuje fyzický krok X osy: klampuje na levý doraz a řeší pen-change.
 *
 * Posune xpos (sx.pos) o @p phys_dir kroků (po 1), klampuje na xpos >= 0
 * (levý mechanický doraz). Po posunu vyhodnotí pen-change zónu: při PŘECHODU
 * zvenku dovnitř (xpos klesne pod MZ1P16_PENCHANGE_ZONE) otočí buben na další
 * barvu (index +1 mod 4) - latch zajistí jen jednu rotaci na vjezd; latch se
 * zruší, až vozík zónu opustí. Tím power-on homing (opakované nárazy o doraz)
 * postupně prorotuje všechna 4 pera.
 *
 * @param m        Instance mechaniky.
 * @param phys_dir Fyzický směr (+1 = doprava, -1 = doleva, 0 = beze změny).
 */
static void x_step_apply(st_MZ1P16 *m, int phys_dir)
{
    if (phys_dir == 0) {
        return;
    }
    int32_t new_pos = m->sx.pos + phys_dir;
    if (new_pos < 0) {
        new_pos = 0;  /* levý mechanický doraz */
    }
    m->sx.pos = new_pos;

    bool in_zone_now = (m->sx.pos < MZ1P16_PENCHANGE_ZONE);
    if (in_zone_now && !m->in_penchange) {
        /* Vjezd do zóny -> mechanický náraz otočí buben na další pero. */
        m->color = (uint8_t)((m->color + 1u) % MZ1P16_NUM_COLORS);
        m->color_changes++;
    }
    m->in_penchange = in_zone_now;
}

/* ------------------------------------------------------------------------- */
/* Hooky portů                                                               */
/* ------------------------------------------------------------------------- */

/**
 * @brief Hook zápisu portu z jádra 8050.
 *
 * BUS: dolní nibble -> X stepper (vozík), horní nibble -> Y stepper (papír).
 *      Raw směr ze sekvence se převede na fyzický směr osy (MZ1P16_X_DIR /
 *      MZ1P16_Y_DIR), X se klampuje na levý doraz a řeší pen-change zónu.
 * P1:  bit0=0 -> zvedni pero; bit1=0 -> spusť pero; bit4 -> BUSY.
 * P2:  výstup na P2 (firmware sem typicky nepíše data k hostu) - ignorujeme.
 *
 * @param c    Jádro.
 * @param port en_MCS48_Port.
 * @param val  Nová hodnota latche.
 * @param ctx  st_MZ1P16*.
 */
static void on_port_write(st_MCS48 *c, int port, uint8_t val, void *ctx)
{
    (void)c;
    st_MZ1P16 *m = (st_MZ1P16 *)ctx;

    if (port == MCS48_PORT_BUS) {
        int32_t px = m->sx.pos, py = m->sy.pos;
        uint8_t xlo = (uint8_t)(val & 0x0F);
        uint8_t yhi = (uint8_t)((val >> 4) & 0x0F);
        /* Osa se zpracuje JEN když je její nibble nenulový. Nibble = 0 znamená
         * de-energizované coily (pauza dané osy, typicky když firmware hýbe tou
         * druhou) - osu ZMRAZÍME a NEaktualizujeme předchozí fázi. Jinak by se
         * fáze přepsala na 0 a při obnovení pohybu (0 -> platná fáze) by se
         * ZTRATIL krok -> znaky stlačené/deformované. */
        int rawx = (xlo != 0) ? stepper_apply(&m->sx, xlo) : 0;
        int rawy = (yhi != 0) ? stepper_apply(&m->sy, yhi) : 0;

        int dx = rawx * MZ1P16_X_DIR;  /* fyzický směr X (vozík) */
        int dy = rawy * MZ1P16_Y_DIR;  /* fyzický směr Y (papír) */

        if (dx) {
            x_step_apply(m, dx);  /* clamp + pen-change */
        }
        if (dy) {
            m->sy.pos += dy;
        }

        if (dx > 0) m->x_steps_fwd++; else if (dx < 0) m->x_steps_rev++;
        if (dy > 0) m->y_steps_fwd++; else if (dy < 0) m->y_steps_rev++;

        if (dx || dy) {
            record_stroke(m, px, py);
            if (m->log_phases) {
                fprintf(stderr,
                    "[mz1p16] BUS=%02X X=%X Y=%X dx=%+d dy=%+d xpos=%ld ypos=%ld pen=%d color=%d\n",
                    val, val & 0x0F, (val >> 4) & 0x0F, dx, dy,
                    (long)m->sx.pos, (long)m->sy.pos, m->pen_down, m->color);
            }
        }
    } else if (port == MCS48_PORT_P1) {
        /* Pero je LATCH ovládaný dvěma drive linkami (set/reset), ne úroveň:
         *   - pulz P1.1 = 0 (FD) -> SPUSŤ pero (latch DOWN); pero pak kreslí
         *     během následného pohybu, dokud ho někdo nezvedne,
         *   - pulz P1.0 = 0 (FE) -> ZVEDNI pero (latch UP); použito před
         *     návratem vozíku, aby nekreslil čáru.
         * Po resetu P1 = 0FFh -> pero drží výchozí stav (nahoře). */
        /* Pen latch ovládají JEDNOBITOVÉ pulzy na P1.0/P1.1:
         *   - P1.1=0 & P1.0=1 (FD) -> SPUSŤ pero (down),
         *   - P1.0=0 & P1.1=1 (FE) -> ZVEDNI pero (up).
         * Když jsou OBA bity stejné (oba 0 nebo oba 1), NEJDE o čistý pulz ->
         * latch DRŽÍ stávající stav (hold). Pozor: firmware během kresby zapisuje
         * i hodnoty s oběma pen bity 0 (např. 0ECh) - to NESMÍ zvednout pero
         * (jinak zmizí přítlak uprostřed tahu, např. kružnice). */
        uint8_t pen_bits = val & 0x03u;
        if (pen_bits == 0x01u) {          /* FD: P1.1=0, P1.0=1 -> down */
            if (!m->pen_down) m->pen_downs++;
            m->pen_down = true;
        } else if (pen_bits == 0x02u) {   /* FE: P1.0=0, P1.1=1 -> up */
            m->pen_down = false;
        }
        /* pen_bits 0x00 (oba low) i 0x03 (oba high) = hold (beze změny). */

        /* BUSY na P1.4 (1 = busy). */
        m->busy = ((val & 0x10u) != 0u);
    }
    /* P2 zápis: ignorujeme (host čte data jinde). */
}

/**
 * @brief Hook čtení portu jádrem 8050.
 *
 * Porty P1 a BUS jsou quasi-obousměrné: firmware na ně zapisuje (pero, BUSY,
 * steppery) a zpětně si je čte (IN A,P1 / INS A,BUS) ve svém stavovém automatu
 * (homing, ready). Bez externího vstupu pin čte naposledy zapsaný LATCH - proto
 * vracíme výstupní latch jádra (c->P1 / c->BUS), NE 0FFh. P2 nese data od
 * hosta.
 *
 * @param c    Jádro.
 * @param port en_MCS48_Port.
 * @param ctx  st_MZ1P16*.
 * @return Pin-level hodnota portu.
 */
static uint8_t on_port_read(st_MCS48 *c, int port, void *ctx)
{
    st_MZ1P16 *m = (st_MZ1P16 *)ctx;
    switch (port) {
        case MCS48_PORT_P2:  m->host_data_read = true; return m->host_data; /* data od hosta */
        case MCS48_PORT_P1:  return c->P1;        /* read-back výstupního latche */
        case MCS48_PORT_BUS: return c->BUS;       /* read-back výstupního latche */
        default:             return 0xFFu;
    }
}

/* ------------------------------------------------------------------------- */
/* Veřejné API                                                               */
/* ------------------------------------------------------------------------- */

void mz1p16_init(st_MZ1P16 *m, st_MCS48 *cpu)
{
    memset(m, 0, sizeof(*m));
    m->cpu = cpu;
    m->color = 0;
    m->pen_down = false;
    m->busy = false;
    m->host_data = 0xFFu;

    cpu->on_port_write = on_port_write;
    cpu->on_port_read  = on_port_read;
    cpu->ctx = m;

    /* Klidové úrovně vstupů 8050 (HW model): /INT, T0, T1 = vysoká (klid),
     * vstupní pin-levely portů = 0FFh. Bez tohoto by jádro po resetu vidělo
     * vstupy = 0 (memset), což může vést k jinému chování firmwaru (aktivní
     * strobe/senzory). Vstupy P1/P2/BUS sice přebírá hook on_port_read, ale
     * T0/T1/INT čte jádro přímo - musí být na klidu. */
    cpu->INT = 1;
    cpu->T0  = 1;
    cpu->T1  = 1;
    cpu->T1_prev = 1;
    cpu->P1_in  = 0xFFu;
    cpu->P2_in  = 0xFFu;
    cpu->BUS_in = 0xFFu;
}

void mz1p16_clear_drawing(st_MZ1P16 *m)
{
    m->n_strokes = 0;
    m->dropped = 0;
    m->x_steps_fwd = m->x_steps_rev = 0;
    m->y_steps_fwd = m->y_steps_rev = 0;
    m->pen_downs = 0;
    m->color_changes = 0;
}

long mz1p16_host_send_byte(st_MZ1P16 *m, uint8_t byte, long settle_steps)
{
    long executed = 0;

    m->host_data = byte;
    /* STROBE aktivní (low). Pulz držíme krátce, pak uvolníme. */
    m->cpu->INT = 0;
    for (int i = 0; i < 30; i++) { mcs48_step(m->cpu); executed++; }
    m->cpu->INT = 1;
    for (long i = 0; i < settle_steps; i++) { mcs48_step(m->cpu); executed++; }

    return executed;
}
