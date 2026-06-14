/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file test_mcs48.c
 * @brief Jednotkové testy jádra MCS-48 (8050) pro plotter MZ-1P16.
 *
 * Pokrytí:
 *  - aritmetika/logika nad A: ADD/ADDC (CY, AC), ANL/ORL/XRL, INC/DEC,
 *    CLR/CPL A, SWAP, DA A (BCD korekce vč. AC i CY)
 *  - rotace: RL/RR/RLC/RRC (CY)
 *  - přesuny: MOV A,#/Rr/@Rr/PSW, MOV Rr,#/A, MOV @Rr,#/A, XCH, XCHD
 *  - I/O: IN/OUTL/ANL/ORL nad P1/P2/BUS, INS A,BUS
 *  - skoky: JMP, CALL/RET, RETR, DJNZ, JMPP, podmíněné Jcc
 *    (JC/JNC/JZ/JT0/JNT0/JT1/JNT1/JF0/JF1/JTF/JNI/JBb)
 *  - stack: push/pop, wrap při 8 úrovních, SP v PSW
 *  - banky: SEL RB0/RB1 (R0-R7 vidí jinou RAM)
 *  - 12-bit adresace: DBF + SEL MB0/MB1 + JMP/CALL do horní 2 KB
 *  - timer: prescaler /32 -> TF, timer INT na 007h, counter mód
 *  - přerušení: /INT skok na 003h + RETR
 *  - tabulky v ROM: MOVP A,@A, MOVP3 A,@A
 *
 * Standalone test - mcs48 je samostatná knihovna, řídíme ji syntetickými
 * mikroprogramy (pole bajtů jako 4 KB ROM) + přímým čtením stavu.
 */

#include "unity.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "emulator/hw-generic/mz1p16/mcs48.h"


/* ===== Testovací fixture ===== */

static st_MCS48 g_cpu;
static uint8_t  g_rom[MCS48_ROM_SIZE];


void setUp(void)
{
    memset(g_rom, 0, sizeof(g_rom));
    mcs48_init(&g_cpu, g_rom);
}


void tearDown(void)
{
    /* nic - ROM je statická */
}


/**
 * @brief Nahraje program do ROM od dané adresy a resetuje CPU na PC=0.
 * @param addr Cílová adresa v ROM.
 * @param code Bajty programu.
 * @param len  Délka.
 */
static void load(uint16_t addr, const uint8_t *code, size_t len)
{
    memcpy(&g_rom[addr], code, len);
}


/* ===================================================================== */
/* Aritmetika a flagy                                                    */
/* ===================================================================== */

void test_add_immediate_sets_carry_and_ac(void)
{
    /* MOV A,#0xFF ; ADD A,#0x01  -> A=0x00, CY=1, AC=1 */
    uint8_t prog[] = { 0x23, 0xFF, 0x03, 0x01 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 4);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_cpu.A);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_CY);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_AC);
}


void test_add_half_carry_only(void)
{
    /* MOV A,#0x0F ; ADD A,#0x01 -> A=0x10, AC=1, CY=0 */
    uint8_t prog[] = { 0x23, 0x0F, 0x03, 0x01 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 4);
    TEST_ASSERT_EQUAL_HEX8(0x10, g_cpu.A);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_AC);
    TEST_ASSERT_FALSE(g_cpu.PSW & MCS48_PSW_CY);
}


void test_addc_uses_carry(void)
{
    /* MOV A,#0xFF; ADD A,#1 (CY=1); MOV A,#0; ADDC A,#0 -> A=1 */
    uint8_t prog[] = { 0x23, 0xFF, 0x03, 0x01, 0x23, 0x00, 0x13, 0x00 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 10);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_cpu.A);
}


void test_add_reg_sets_carry_and_ac(void)
{
    /* MOV R1,#0x88; MOV A,#0x88; ADD A,R1 -> A=0x10, CY=1, AC=1
       (0x88+0x88 = 0x110; přenos z bit3 0x8+0x8=0x10 -> AC). */
    uint8_t prog[] = { 0xB9, 0x88, 0x23, 0x88, 0x69 };  /* MOV R1,#88; MOV A,#88; ADD A,R1 */
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 5);
    TEST_ASSERT_EQUAL_HEX8(0x10, g_cpu.A);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_CY);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_AC);
}


void test_add_indirect_half_carry(void)
{
    /* MOV R0,#0x40; MOV @R0,#0x01; MOV A,#0x0F; ADD A,@R0
       -> A=0x10, AC=1, CY=0. */
    uint8_t prog[] = { 0xB8, 0x40, 0xB0, 0x01, 0x23, 0x0F, 0x60 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 8);
    TEST_ASSERT_EQUAL_HEX8(0x10, g_cpu.A);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_AC);
    TEST_ASSERT_FALSE(g_cpu.PSW & MCS48_PSW_CY);
}


void test_addc_reg_uses_carry(void)
{
    /* MOV A,#0xFF; ADD A,#0x01 (CY=1); MOV R2,#0x00; MOV A,#0x00;
       ADDC A,R2 -> A = 0 + 0 + CY(1) = 0x01, CY=0, AC=0. */
    uint8_t prog[] = { 0x23, 0xFF, 0x03, 0x01, 0xBA, 0x00, 0x23, 0x00, 0x7A };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 11);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_cpu.A);
    TEST_ASSERT_FALSE(g_cpu.PSW & MCS48_PSW_CY);
    TEST_ASSERT_FALSE(g_cpu.PSW & MCS48_PSW_AC);
}


void test_addc_indirect_uses_carry(void)
{
    /* MOV A,#0xFF; ADD A,#0x01 (CY=1); MOV R1,#0x41; MOV @R1,#0x0F;
       MOV A,#0x00; ADDC A,@R1 -> A = 0x00 + 0x0F + CY(1) = 0x10, AC=1, CY=0. */
    uint8_t prog[] = { 0x23, 0xFF, 0x03, 0x01,
                       0xB9, 0x41, 0xB1, 0x0F,
                       0x23, 0x00, 0x71 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 13);
    TEST_ASSERT_EQUAL_HEX8(0x10, g_cpu.A);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_AC);
    TEST_ASSERT_FALSE(g_cpu.PSW & MCS48_PSW_CY);
}


void test_da_a_overflow_lower_nibble(void)
{
    /* Regrese pro DA A overflow bug: A=0xFA, AC=0, CY=0.
       Dolní nibble 0xA>9 -> +6 = 0x100 (přeteče přes 0xFF). Test horního
       nibblu MUSÍ vidět přetečení -> +0x60 -> 0x160; A=0x60, CY=1.
       Před opravou test (a&0xF0)>0x90 selhal (0x100&0xF0=0x00) a vyšlo
       chybně A=0x00, CY=0. */
    uint8_t prog[] = { 0x23, 0xFA, 0x57 };  /* MOV A,#0xFA; DA A */
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 3);
    TEST_ASSERT_EQUAL_HEX8(0x60, g_cpu.A);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_CY);
}


void test_da_a_overflow_ff(void)
{
    /* A=0xFF, AC=0, CY=0: dolní 0xF>9 -> +6 = 0x105; horní >9 -> +0x60
       -> 0x165; A=0x65, CY=1. */
    uint8_t prog[] = { 0x23, 0xFF, 0x57 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 3);
    TEST_ASSERT_EQUAL_HEX8(0x65, g_cpu.A);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_CY);
}


void test_logic_ops(void)
{
    /* MOV A,#0xF0; ORL A,#0x0F -> 0xFF; ANL A,#0x3C -> 0x3C; XRL A,#0xFF -> 0xC3 */
    uint8_t prog[] = { 0x23, 0xF0, 0x43, 0x0F, 0x53, 0x3C, 0xD3, 0xFF };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 10);
    TEST_ASSERT_EQUAL_HEX8(0xC3, g_cpu.A);
}


void test_inc_dec_clr_cpl_a(void)
{
    /* CLR A; INC A; INC A; DEC A; CPL A -> A = ~1 = 0xFE */
    uint8_t prog[] = { 0x27, 0x17, 0x17, 0x07, 0x37 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 5);
    TEST_ASSERT_EQUAL_HEX8(0xFE, g_cpu.A);
}


void test_swap_a(void)
{
    /* MOV A,#0x4B; SWAP A -> 0xB4 */
    uint8_t prog[] = { 0x23, 0x4B, 0x47 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 4);
    TEST_ASSERT_EQUAL_HEX8(0xB4, g_cpu.A);
}


void test_da_a_lower_nibble(void)
{
    /* 0x09 + 0x08 = 0x11 binárně; DA A koriguje na 0x17 (BCD 9+8=17) */
    uint8_t prog[] = { 0x23, 0x09, 0x03, 0x08, 0x57 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 6);
    TEST_ASSERT_EQUAL_HEX8(0x17, g_cpu.A);
}


void test_da_a_upper_nibble_carry(void)
{
    /* BCD 0x99 + 0x01 = 0x9A bin; DA -> +6 = 0xA0; horní >9 -> +0x60 = 0x100
       => A=0x00, CY=1 (BCD 99+1=100). */
    uint8_t prog[] = { 0x23, 0x99, 0x03, 0x01, 0x57 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 6);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_cpu.A);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_CY);
}


/* ===================================================================== */
/* Rotace                                                                */
/* ===================================================================== */

void test_rl_rr(void)
{
    /* MOV A,#0x81; RL A -> 0x03; RR A -> 0x81 zpět */
    uint8_t prog[] = { 0x23, 0x81, 0xE7, 0x77 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 3);  /* MOV(2)+RL(1) */
    TEST_ASSERT_EQUAL_HEX8(0x03, g_cpu.A);
    mcs48_step(&g_cpu);    /* RR */
    TEST_ASSERT_EQUAL_HEX8(0x81, g_cpu.A);
}


void test_rlc_rrc_through_carry(void)
{
    /* CLR C; MOV A,#0x80; RLC A -> A=0x00, CY=1; RRC A -> A=0x80, CY=0 */
    uint8_t prog[] = { 0x97, 0x23, 0x80, 0xF7, 0x67 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 4);  /* CLR C + MOV + RLC */
    TEST_ASSERT_EQUAL_HEX8(0x00, g_cpu.A);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_CY);
    mcs48_step(&g_cpu);    /* RRC */
    TEST_ASSERT_EQUAL_HEX8(0x80, g_cpu.A);
    TEST_ASSERT_FALSE(g_cpu.PSW & MCS48_PSW_CY);
}


/* ===================================================================== */
/* Přesuny, registry, nepřímá adresace                                   */
/* ===================================================================== */

void test_mov_reg_and_back(void)
{
    /* MOV R3,#0x55; MOV A,R3 -> A=0x55 */
    uint8_t prog[] = { 0xBB, 0x55, 0xFB };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 3);
    TEST_ASSERT_EQUAL_HEX8(0x55, g_cpu.A);
    TEST_ASSERT_EQUAL_HEX8(0x55, mcs48_get_reg(&g_cpu, 3));
}


void test_mov_indirect(void)
{
    /* MOV R0,#0x40; MOV @R0,#0xAB; MOV A,@R0 -> A=0xAB; ram[0x40]=0xAB */
    uint8_t prog[] = { 0xB8, 0x40, 0xB0, 0xAB, 0xF0 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 6);
    TEST_ASSERT_EQUAL_HEX8(0xAB, g_cpu.A);
    TEST_ASSERT_EQUAL_HEX8(0xAB, g_cpu.ram[0x40]);
}


void test_xch_and_xchd(void)
{
    /* MOV R2,#0x12; MOV A,#0x34; XCH A,R2 -> A=0x12, R2=0x34 */
    uint8_t prog[] = { 0xBA, 0x12, 0x23, 0x34, 0x2A };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 6);
    TEST_ASSERT_EQUAL_HEX8(0x12, g_cpu.A);
    TEST_ASSERT_EQUAL_HEX8(0x34, mcs48_get_reg(&g_cpu, 2));

    /* XCHD: MOV R0,#0x50; MOV @R0,#0xAB; MOV A,#0x3C; XCHD A,@R0
       -> A dolní nibble <-> ram dolní nibble: A=0x3B, ram=0xAC */
    setUp();
    uint8_t prog2[] = { 0xB8, 0x50, 0xB0, 0xAB, 0x23, 0x3C, 0x30 };
    load(0, prog2, sizeof(prog2));
    mcs48_run(&g_cpu, 9);
    TEST_ASSERT_EQUAL_HEX8(0x3B, g_cpu.A);
    TEST_ASSERT_EQUAL_HEX8(0xAC, g_cpu.ram[0x50]);
}


void test_mov_psw(void)
{
    /* MOV A,#0xF0; MOV PSW,A -> PSW = 0xF0 | bit3 = 0xF8; MOV A,PSW */
    uint8_t prog[] = { 0x23, 0xF0, 0xD7, 0xC7 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 5);
    TEST_ASSERT_EQUAL_HEX8(0xF8, g_cpu.A);
    TEST_ASSERT_TRUE(g_cpu.PSW & MCS48_PSW_BIT3);
}


/* ===================================================================== */
/* I/O porty                                                             */
/* ===================================================================== */

void test_out_in_ports(void)
{
    /* MOV A,#0x5A; OUTL P1,A; (P1 latch=0x5A) */
    uint8_t prog[] = { 0x23, 0x5A, 0x39 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 4);
    TEST_ASSERT_EQUAL_HEX8(0x5A, g_cpu.P1);

    /* IN A,P2 čte vstupní hodnotu */
    setUp();
    mcs48_set_port_input(&g_cpu, MCS48_PORT_P2, 0xC3);
    uint8_t prog2[] = { 0x0A };  /* IN A,P2 */
    load(0, prog2, sizeof(prog2));
    mcs48_step(&g_cpu);
    TEST_ASSERT_EQUAL_HEX8(0xC3, g_cpu.A);
}


void test_anl_orl_port_latch(void)
{
    /* MOV A,#0xFF; OUTL P1,A; ANL P1,#0x0F; ORL P1,#0x40 -> 0x4F */
    uint8_t prog[] = { 0x23, 0xFF, 0x39, 0x99, 0x0F, 0x89, 0x40 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 9);
    TEST_ASSERT_EQUAL_HEX8(0x4F, g_cpu.P1);
}


void test_ins_outl_bus(void)
{
    /* MOV A,#0x77; OUTL BUS,A */
    uint8_t prog[] = { 0x23, 0x77, 0x02 };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 4);
    TEST_ASSERT_EQUAL_HEX8(0x77, g_cpu.BUS);

    setUp();
    mcs48_set_port_input(&g_cpu, MCS48_PORT_BUS, 0x88);
    uint8_t prog2[] = { 0x08 };  /* INS A,BUS */
    load(0, prog2, sizeof(prog2));
    mcs48_step(&g_cpu);
    TEST_ASSERT_EQUAL_HEX8(0x88, g_cpu.A);
}


/* ===================================================================== */
/* Skoky a stack                                                         */
/* ===================================================================== */

void test_jmp(void)
{
    /* JMP 0x123 ; na 0x123: MOV A,#0xEE */
    uint8_t prog[] = { 0x24, 0x23 };  /* addhi=1 (op 0x24), addlo=0x23 */
    load(0, prog, sizeof(prog));
    uint8_t tgt[] = { 0x23, 0xEE };
    load(0x123, tgt, sizeof(tgt));
    mcs48_step(&g_cpu);
    TEST_ASSERT_EQUAL_HEX16(0x123, g_cpu.PC);
    mcs48_step(&g_cpu);
    TEST_ASSERT_EQUAL_HEX8(0xEE, g_cpu.A);
}


void test_call_ret(void)
{
    /* CALL 0x100; (po návratu) MOV A,#0x01
       na 0x100: MOV A,#0x99; RET
       CALL 0x100: page 1 -> addhi=1 -> opcode (1<<5)|0x14 = 0x34, addlo=0x00 */
    uint8_t prog[] = { 0x34, 0x00, 0x23, 0x01 };  /* CALL 0x100 (op 0x34) */
    load(0, prog, sizeof(prog));
    uint8_t sub[] = { 0x23, 0x99, 0x83 };
    load(0x100, sub, sizeof(sub));
    mcs48_step(&g_cpu);                 /* CALL */
    TEST_ASSERT_EQUAL_HEX16(0x100, g_cpu.PC);
    TEST_ASSERT_EQUAL_UINT8(1, g_cpu.PSW & MCS48_PSW_SP);  /* SP po push = 1 */
    mcs48_step(&g_cpu);                 /* MOV A,#0x99 */
    mcs48_step(&g_cpu);                 /* RET */
    TEST_ASSERT_EQUAL_HEX16(0x0002, g_cpu.PC);  /* návrat za CALL */
    TEST_ASSERT_EQUAL_UINT8(0, g_cpu.PSW & MCS48_PSW_SP);
    mcs48_step(&g_cpu);                 /* MOV A,#0x01 */
    TEST_ASSERT_EQUAL_HEX8(0x01, g_cpu.A);
}


void test_stack_wrap_8_levels(void)
{
    /* 8x CALL bez RET -> SP se wrapuje (3 bity, 0-7 -> 0). */
    /* Program: na 0..: CALL self-ish. Použijeme jednoduchou rutinu, která
       jen volá další úroveň, ale my voláme přímo přes push opakovaně.
       Místo toho otestujeme přímo stack_push přes 8 CALL na stejnou adresu,
       kde sub jen NOP (žádný RET), a kontrolujeme SP=0 po 8. úrovni. */
    /* na 0x200: NOP (stop) */
    uint8_t sub[] = { 0x00 };
    load(0x200, sub, sizeof(sub));
    /* 8 CALL 0x200 v řadě? CALL ale skočí, takže neprovedeme 8 za sebou.
       Otestujeme přes opakované volání mcs48_step s předvyplněným programem,
       kde každý slot dělá CALL 0x200 a 0x200 dělá RET na další CALL... to je
       složité. Místo toho test SP wrap přímo: simulujeme 8 push přes CALL +
       manuální reset PC. */
    for (int i = 0; i < 8; i++) {
        uint8_t one[] = { 0x14, 0x00 };  /* CALL 0x100 */
        load(0, one, sizeof(one));
        g_cpu.PC = 0;
        mcs48_step(&g_cpu);  /* provede push, SP++ */
    }
    /* Po 8 pushech (SP 0->1->...->7->0) je SP zpět na 0. */
    TEST_ASSERT_EQUAL_UINT8(0, g_cpu.PSW & MCS48_PSW_SP);
}


void test_djnz(void)
{
    /* MOV R0,#3; loop: INC A; DJNZ R0,loop -> A=3 po 3 iteracích */
    /* layout: 0:MOV R0,#3 (B8 03); 2:INC A (17); 3:DJNZ R0,addr=2 (E8 02) */
    uint8_t prog[] = { 0xB8, 0x03, 0x17, 0xE8, 0x02 };
    load(0, prog, sizeof(prog));
    /* spustíme dost cyklů */
    mcs48_run(&g_cpu, 30);
    TEST_ASSERT_EQUAL_HEX8(0x03, g_cpu.A);
    TEST_ASSERT_EQUAL_HEX8(0x00, mcs48_get_reg(&g_cpu, 0));
}


void test_conditional_jumps(void)
{
    /* JZ: CLR A; JZ taken */
    uint8_t prog[] = { 0x27, 0xC6, 0x10 };  /* CLR A; JZ 0x10 */
    load(0, prog, sizeof(prog));
    mcs48_step(&g_cpu);  /* CLR A */
    mcs48_step(&g_cpu);  /* JZ */
    TEST_ASSERT_EQUAL_HEX16(0x10, g_cpu.PC);

    /* JC: CLR C; JC nottaken -> pokračuje; CPL C; JC taken */
    setUp();
    uint8_t prog2[] = { 0x97, 0xF6, 0x20, 0xA7, 0xF6, 0x30 };
    load(0, prog2, sizeof(prog2));
    mcs48_step(&g_cpu);  /* CLR C */
    mcs48_step(&g_cpu);  /* JC 0x20 - netaken, PC=3 */
    TEST_ASSERT_EQUAL_HEX16(0x03, g_cpu.PC);
    mcs48_step(&g_cpu);  /* CPL C -> CY=1 */
    mcs48_step(&g_cpu);  /* JC 0x30 - taken */
    TEST_ASSERT_EQUAL_HEX16(0x30, g_cpu.PC);
}


void test_jnz(void)
{
    /* JNZ (96h) - protějšek JZ (C6h). Regrese fáze 2: opcode 96h chyběl
     * v dispatchi (default = 1-bajtový NOP), což rozsynchronizovalo
     * instrukční proud reálného firmwaru plotteru. Ověřeno empiricky =
     * 96h je JNZ addr (2-bajtový skok na PC0-7 <- addr když A != 0). */

    /* A != 0 -> skok proveden */
    uint8_t prog[] = { 0x23, 0x55, 0x96, 0x10 };  /* MOV A,#0x55; JNZ 0x10 */
    load(0, prog, sizeof(prog));
    mcs48_step(&g_cpu);  /* MOV A,#0x55 */
    mcs48_step(&g_cpu);  /* JNZ 0x10 - taken */
    TEST_ASSERT_EQUAL_HEX16(0x10, g_cpu.PC);

    /* A == 0 -> skok NEproveden, propadne za 2-bajtovou instrukci na PC=4 */
    setUp();
    uint8_t prog2[] = { 0x27, 0x96, 0x10, 0x00 };  /* CLR A; JNZ 0x10; NOP */
    load(0, prog2, sizeof(prog2));
    mcs48_step(&g_cpu);  /* CLR A */
    mcs48_step(&g_cpu);  /* JNZ 0x10 - netaken */
    TEST_ASSERT_EQUAL_HEX16(0x03, g_cpu.PC);  /* za 2-bajtovou JNZ */
}


void test_jb_bit_jump(void)
{
    /* MOV A,#0x80; JB7 taken (bit7 set) */
    uint8_t prog[] = { 0x23, 0x80, 0xF2, 0x40 };  /* JB7 = op 0xF2 */
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 4);
    TEST_ASSERT_EQUAL_HEX16(0x40, g_cpu.PC);
}


void test_jt0_jt1_jni(void)
{
    /* JNT0: T0=0 -> taken */
    uint8_t prog[] = { 0x26, 0x10 };  /* JNT0 0x10 */
    load(0, prog, sizeof(prog));
    g_cpu.T0 = 0;
    mcs48_step(&g_cpu);
    TEST_ASSERT_EQUAL_HEX16(0x10, g_cpu.PC);

    /* JT1: T1=1 -> taken */
    setUp();
    uint8_t prog2[] = { 0x56, 0x20 };  /* JT1 0x20 */
    load(0, prog2, sizeof(prog2));
    g_cpu.T1 = 1;
    mcs48_step(&g_cpu);
    TEST_ASSERT_EQUAL_HEX16(0x20, g_cpu.PC);

    /* JNI: INT=0 -> taken */
    setUp();
    uint8_t prog3[] = { 0x86, 0x30 };  /* JNI 0x30 */
    load(0, prog3, sizeof(prog3));
    g_cpu.INT = 0;
    mcs48_step(&g_cpu);
    TEST_ASSERT_EQUAL_HEX16(0x30, g_cpu.PC);
}


void test_jf0_jf1(void)
{
    /* CPL F0; JF0 taken */
    uint8_t prog[] = { 0x95, 0xB6, 0x10 };  /* CPL F0; JF0 0x10 */
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 3);
    TEST_ASSERT_EQUAL_HEX16(0x10, g_cpu.PC);

    /* CPL F1; JF1 taken */
    setUp();
    uint8_t prog2[] = { 0xB5, 0x76, 0x20 };  /* CPL F1; JF1 0x20 */
    load(0, prog2, sizeof(prog2));
    mcs48_run(&g_cpu, 3);
    TEST_ASSERT_EQUAL_HEX16(0x20, g_cpu.PC);
}


void test_jmpp(void)
{
    /* MOV A,#0x40; JMPP @A : JMPP čte ROM(PC[8-11]:A). PC po načtení JMPP
       opcode = 3 (stránka 0). ROM(0x00:A=0x40) = g_rom[0x40] (mimo kód). */
    uint8_t prog[] = { 0x23, 0x40, 0xB3 };  /* MOV A,#0x40; JMPP @A */
    load(0, prog, sizeof(prog));
    g_rom[0x40] = 0x55;  /* hodnota tabulky -> nový PC[0-7] */
    mcs48_run(&g_cpu, 4);
    TEST_ASSERT_EQUAL_HEX16(0x55, g_cpu.PC);
}


void test_movp_movp3(void)
{
    /* MOVP A,@A: MOV A,#0x10; MOVP A,@A -> A = ROM(stránka:0x10).
       PC po MOVP opcode = 3, horní nibble 0 -> ROM(0x010). */
    uint8_t prog[] = { 0x23, 0x10, 0xA3 };
    load(0, prog, sizeof(prog));
    g_rom[0x010] = 0x7E;
    mcs48_run(&g_cpu, 4);
    TEST_ASSERT_EQUAL_HEX8(0x7E, g_cpu.A);

    /* MOVP3 A,@A: A=0x20; MOVP3 -> ROM(0x300|0x20) = ROM(0x320) */
    setUp();
    uint8_t prog2[] = { 0x23, 0x20, 0xE3 };
    load(0, prog2, sizeof(prog2));
    g_rom[0x320] = 0x3D;
    mcs48_run(&g_cpu, 4);
    TEST_ASSERT_EQUAL_HEX8(0x3D, g_cpu.A);
}


/* ===================================================================== */
/* Registrové banky                                                     */
/* ===================================================================== */

void test_register_banks(void)
{
    /* SEL RB0; MOV R0,#0x11; SEL RB1; MOV R0,#0x22;
       SEL RB0; MOV A,R0 -> 0x11; ram[0]=0x11, ram[0x18]=0x22 */
    uint8_t prog[] = {
        0xC5, 0xB8, 0x11,   /* SEL RB0; MOV R0,#0x11 */
        0xD5, 0xB8, 0x22,   /* SEL RB1; MOV R0,#0x22 */
        0xC5, 0xF8          /* SEL RB0; MOV A,R0 */
    };
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 12);
    TEST_ASSERT_EQUAL_HEX8(0x11, g_cpu.A);
    TEST_ASSERT_EQUAL_HEX8(0x11, g_cpu.ram[0x00]);
    TEST_ASSERT_EQUAL_HEX8(0x22, g_cpu.ram[0x18]);
}


/* ===================================================================== */
/* 12-bit adresace (DBF / SEL MB)                                       */
/* ===================================================================== */

void test_sel_mb_jmp_high_2k(void)
{
    /* SEL MB1; JMP 0x000 -> PC = 0x800 (A11 z DBF). */
    uint8_t prog[] = { 0xF5, 0x04, 0x00 };  /* SEL MB1; JMP 0x000 */
    load(0, prog, sizeof(prog));
    mcs48_step(&g_cpu);  /* SEL MB1 */
    mcs48_step(&g_cpu);  /* JMP */
    TEST_ASSERT_EQUAL_HEX16(0x800, g_cpu.PC);
}


void test_sel_mb_call_ret_full_12bit(void)
{
    /* SEL MB1; CALL 0x010 -> PC=0x810; tam RET -> návrat do 0x003 (12-bit). */
    uint8_t prog[] = { 0xF5, 0x14, 0x10 };  /* SEL MB1; CALL 0x010 */
    load(0, prog, sizeof(prog));
    uint8_t sub[] = { 0x83 };  /* RET na 0x810 */
    load(0x810, sub, sizeof(sub));
    mcs48_step(&g_cpu);  /* SEL MB1 */
    mcs48_step(&g_cpu);  /* CALL */
    TEST_ASSERT_EQUAL_HEX16(0x810, g_cpu.PC);
    mcs48_step(&g_cpu);  /* RET */
    TEST_ASSERT_EQUAL_HEX16(0x003, g_cpu.PC);
}


void test_jmp_call_a11_forced_zero_in_isr(void)
{
    /* Regrese: během obsluhy přerušení je A11 NUCENĚ 0 - JMP i CALL v ISR
     * adresují bank 0, i když je DBF=1 (SEL MB1). Bez tohoto chování se
     * firmware plotteru ztrácel: timer ISR dělá CALL 0x7EB (bank 0), ale s
     * DBF=1 z přerušeného kódu by skočil na 0xFEB -> do prázdné oblasti ROM
     * -> přes wrap a JMP zpět doprostřed instrukce (deformace kresby "zuby"). */

    /* --- JMP varianta --- */
    uint8_t prog[] = { 0xF5, 0x05, 0x00, 0x00 };  /* SEL MB1; EN I; NOP; NOP */
    load(0, prog, sizeof(prog));
    uint8_t isr_jmp[] = { 0x04, 0x10 };           /* na 0x003: JMP 0x010 */
    load(0x003, isr_jmp, sizeof(isr_jmp));
    mcs48_step(&g_cpu);   /* SEL MB1 -> DBF=1 */
    mcs48_step(&g_cpu);   /* EN I */
    g_cpu.INT = 0;        /* aktivuj /INT */
    mcs48_step(&g_cpu);   /* dispatch -> 0x003, in_isr=true */
    TEST_ASSERT_TRUE(g_cpu.in_isr);
    TEST_ASSERT_TRUE(g_cpu.MB);             /* DBF zůstává 1 (potlačí se jen A11) */
    mcs48_step(&g_cpu);   /* JMP 0x010 -> bank 0, NE 0x810 */
    TEST_ASSERT_EQUAL_HEX16(0x010, g_cpu.PC);

    /* --- CALL varianta (čerstvé jádro) --- */
    setUp();
    uint8_t isr_call[] = { 0x14, 0x10 };          /* na 0x003: CALL 0x010 */
    load(0, prog, sizeof(prog));
    load(0x003, isr_call, sizeof(isr_call));
    mcs48_step(&g_cpu);   /* SEL MB1 */
    mcs48_step(&g_cpu);   /* EN I */
    g_cpu.INT = 0;
    mcs48_step(&g_cpu);   /* dispatch */
    mcs48_step(&g_cpu);   /* CALL 0x010 -> bank 0, NE 0x810 */
    TEST_ASSERT_EQUAL_HEX16(0x010, g_cpu.PC);
}


/* ===================================================================== */
/* Timer / counter                                                      */
/* ===================================================================== */

void test_timer_prescaler_overflow_sets_tf(void)
{
    /* MOV A,#0xFF; MOV T,A; STRT T; pak NOPy.
       Timer = 0xFF, po 32 strojových cyklech (prescaler) tickne -> 0x00 -> TF.
       Po STRT je potřeba 32 cyklů timer tiku, aby přetekl. */
    uint8_t prog[] = { 0x23, 0xFF, 0x62, 0x55 };  /* MOV A,#FF; MOV T,A; STRT T */
    /* zbytek ROM = NOP (0x00) */
    load(0, prog, sizeof(prog));
    mcs48_run(&g_cpu, 4);  /* nastav timer, spusť */
    TEST_ASSERT_FALSE(g_cpu.TF);
    /* potřebujeme 32 strojových cyklů pro 1 tick (FF->00). */
    mcs48_run(&g_cpu, 40);
    TEST_ASSERT_TRUE(g_cpu.TF);
}


void test_jtf_clears_tf(void)
{
    g_cpu.TF = true;
    uint8_t prog[] = { 0x16, 0x10 };  /* JTF 0x10 */
    load(0, prog, sizeof(prog));
    mcs48_step(&g_cpu);
    TEST_ASSERT_EQUAL_HEX16(0x10, g_cpu.PC);
    TEST_ASSERT_FALSE(g_cpu.TF);  /* JTF nuluje TF */
}


void test_timer_interrupt_vector_007(void)
{
    /* Setup: EN TCNTI; MOV A,#0xFF; MOV T,A; STRT T; pak JMP 0x010 (těsná
       smyčka mimo vektory). ISR na 0x007 nastaví R7=0xA5 jako značku.
       Hlavní smyčka na 0x010 je self-JMP (čekání na přerušení).
       Po přetečení timeru (32 cyklů) a ETI skočí CPU na 0x007. */
    uint8_t prog[] = { 0x25, 0x23, 0xFF, 0x62, 0x55, 0x04, 0x10 };
    /* EN TCNTI; MOV A,#FF; MOV T,A; STRT T; JMP 0x010 */
    load(0, prog, sizeof(prog));
    uint8_t isr[] = { 0xBF, 0xA5, 0x93 };  /* 0x007: MOV R7,#A5; RETR */
    load(0x007, isr, sizeof(isr));
    uint8_t loop[] = { 0x04, 0x10 };       /* 0x010: JMP 0x010 (self) */
    load(0x010, loop, sizeof(loop));
    mcs48_run(&g_cpu, 6);   /* setup -> spadne do smyčky 0x010 */
    mcs48_run(&g_cpu, 80);  /* dotiká přetečení + dispatch INT + ISR */
    TEST_ASSERT_EQUAL_HEX8(0xA5, mcs48_get_reg(&g_cpu, 7));  /* ISR proběhla */
}


void test_tf_stays_set_in_timer_isr(void)
{
    /* Reálný MCS-48: vstup do timer ISR shodí interní žádost, ale viditelný
       TF zůstane nastavený dokud ho nevynuluje JTF / nové přetečení.
       Setup jako timer_interrupt_vector_007, ale ISR jen RETR (netestuje TF);
       po dispatchi do ISR musí být TF stále true. */
    uint8_t prog[] = { 0x25, 0x23, 0xFF, 0x62, 0x55, 0x04, 0x10 };
    /* EN TCNTI; MOV A,#FF; MOV T,A; STRT T; JMP 0x010 */
    load(0, prog, sizeof(prog));
    uint8_t isr[] = { 0x93 };               /* 0x007: RETR (TF netestuje) */
    load(0x007, isr, sizeof(isr));
    uint8_t loop[] = { 0x04, 0x10 };        /* 0x010: JMP 0x010 (self) */
    load(0x010, loop, sizeof(loop));
    mcs48_run(&g_cpu, 6);    /* setup -> smyčka */
    /* doběhni do okamžiku přetečení a dispatchu, ale ne moc dlouho, ať
       nestihne JTF (žádné JTF v programu není) */
    mcs48_run(&g_cpu, 40);
    /* v tomto bodě timer přetekl, ISR se dispatchla (in_isr nebo už proběhla
       RETR). TF musí být stále nastaven (jen timer_int_req se shodil). */
    TEST_ASSERT_TRUE(g_cpu.TF);
    TEST_ASSERT_FALSE(g_cpu.timer_int_req);
}


void test_counter_mode_t1_edges(void)
{
    /* STRT CNT; MOV T,A=0xFE; spočítej 2 sestupné hrany na T1 -> přeteče. */
    g_cpu.timer = 0xFE;
    uint8_t prog[] = { 0x45, 0x00 };  /* STRT CNT; NOP */
    load(0, prog, sizeof(prog));
    mcs48_step(&g_cpu);  /* STRT CNT, T1_prev = T1(0) */

    /* hrana 1: T1 1->0 */
    g_cpu.T1 = 1; mcs48_step(&g_cpu);  /* NOP, T1_prev=1 */
    g_cpu.T1 = 0; mcs48_step(&g_cpu);  /* NOP, detekce 1->0 -> tick: 0xFE->0xFF */
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_cpu.timer);
    /* hrana 2 */
    g_cpu.T1 = 1; mcs48_step(&g_cpu);
    g_cpu.T1 = 0; mcs48_step(&g_cpu);  /* tick: 0xFF->0x00 -> TF */
    TEST_ASSERT_EQUAL_HEX8(0x00, g_cpu.timer);
    TEST_ASSERT_TRUE(g_cpu.TF);
}


/* ===================================================================== */
/* Externí přerušení /INT                                                */
/* ===================================================================== */

void test_external_interrupt_vector_003_and_retr(void)
{
    /* EN I; pak hlavní smyčka NOP. Po aktivaci /INT (=0) skočí na 0x003. */
    uint8_t prog[] = { 0x05, 0x00, 0x00 };  /* EN I; NOP; NOP */
    load(0, prog, sizeof(prog));
    uint8_t isr[] = { 0x23, 0x5C, 0x93 };  /* na 0x003: MOV A,#0x5C; RETR */
    load(0x003, isr, sizeof(isr));
    mcs48_step(&g_cpu);   /* EN I */
    g_cpu.INT = 0;        /* aktivuj /INT */
    mcs48_step(&g_cpu);   /* dispatch -> PC=0x003, in_isr=true */
    TEST_ASSERT_EQUAL_HEX16(0x003, g_cpu.PC);
    TEST_ASSERT_TRUE(g_cpu.in_isr);
    mcs48_step(&g_cpu);   /* MOV A,#0x5C */
    TEST_ASSERT_EQUAL_HEX8(0x5C, g_cpu.A);
    g_cpu.INT = 1;        /* uvolni /INT, aby se ISR neopakovala */
    mcs48_step(&g_cpu);   /* RETR */
    TEST_ASSERT_FALSE(g_cpu.in_isr);
    TEST_ASSERT_EQUAL_HEX16(0x001, g_cpu.PC);  /* návrat za EN I */
}


void test_interrupt_suppressed_during_isr(void)
{
    /* Během ISR se další /INT nedispatchuje, dokud RETR. */
    uint8_t prog[] = { 0x05, 0x00 };  /* EN I; NOP */
    load(0, prog, sizeof(prog));
    /* ISR na 0x003: NOP; NOP; RETR (a /INT zůstane aktivní) */
    uint8_t isr[] = { 0x00, 0x00, 0x93 };
    load(0x003, isr, sizeof(isr));
    mcs48_step(&g_cpu);   /* EN I */
    g_cpu.INT = 0;
    mcs48_step(&g_cpu);   /* dispatch do ISR */
    TEST_ASSERT_TRUE(g_cpu.in_isr);
    mcs48_step(&g_cpu);   /* NOP v ISR - žádný re-dispatch */
    TEST_ASSERT_EQUAL_HEX16(0x004, g_cpu.PC);
    TEST_ASSERT_TRUE(g_cpu.in_isr);
}


/* ===== main ===== */

int main(void)
{
    UNITY_BEGIN();

    /* aritmetika + flagy */
    RUN_TEST(test_add_immediate_sets_carry_and_ac);
    RUN_TEST(test_add_half_carry_only);
    RUN_TEST(test_addc_uses_carry);
    RUN_TEST(test_add_reg_sets_carry_and_ac);
    RUN_TEST(test_add_indirect_half_carry);
    RUN_TEST(test_addc_reg_uses_carry);
    RUN_TEST(test_addc_indirect_uses_carry);
    RUN_TEST(test_da_a_overflow_lower_nibble);
    RUN_TEST(test_da_a_overflow_ff);
    RUN_TEST(test_logic_ops);
    RUN_TEST(test_inc_dec_clr_cpl_a);
    RUN_TEST(test_swap_a);
    RUN_TEST(test_da_a_lower_nibble);
    RUN_TEST(test_da_a_upper_nibble_carry);

    /* rotace */
    RUN_TEST(test_rl_rr);
    RUN_TEST(test_rlc_rrc_through_carry);

    /* přesuny */
    RUN_TEST(test_mov_reg_and_back);
    RUN_TEST(test_mov_indirect);
    RUN_TEST(test_xch_and_xchd);
    RUN_TEST(test_mov_psw);

    /* I/O */
    RUN_TEST(test_out_in_ports);
    RUN_TEST(test_anl_orl_port_latch);
    RUN_TEST(test_ins_outl_bus);

    /* skoky + stack */
    RUN_TEST(test_jmp);
    RUN_TEST(test_call_ret);
    RUN_TEST(test_stack_wrap_8_levels);
    RUN_TEST(test_djnz);
    RUN_TEST(test_conditional_jumps);
    RUN_TEST(test_jnz);
    RUN_TEST(test_jb_bit_jump);
    RUN_TEST(test_jt0_jt1_jni);
    RUN_TEST(test_jf0_jf1);
    RUN_TEST(test_jmpp);
    RUN_TEST(test_movp_movp3);

    /* banky */
    RUN_TEST(test_register_banks);

    /* 12-bit adresace */
    RUN_TEST(test_sel_mb_jmp_high_2k);
    RUN_TEST(test_sel_mb_call_ret_full_12bit);
    RUN_TEST(test_jmp_call_a11_forced_zero_in_isr);

    /* timer */
    RUN_TEST(test_timer_prescaler_overflow_sets_tf);
    RUN_TEST(test_jtf_clears_tf);
    RUN_TEST(test_timer_interrupt_vector_007);
    RUN_TEST(test_tf_stays_set_in_timer_isr);
    RUN_TEST(test_counter_mode_t1_edges);

    /* přerušení */
    RUN_TEST(test_external_interrupt_vector_003_and_retr);
    RUN_TEST(test_interrupt_suppressed_during_isr);

    return UNITY_END();
}
