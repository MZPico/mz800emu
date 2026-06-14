/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file pctrace.c
 * @brief Sleduje návštěvy zvolených PC oblastí během běhu firmwaru MZ-1P16.
 *
 * Diagnostika: zjišťuje, zda firmware během daného scénáře (T0/T1 nastavení)
 * vůbec vstoupí do charset/command dispatch oblasti (0x223-0x247) a do
 * vektorového kreslicího enginu (0x061-0x10D). Vypisuje počty navštívení a
 * první adresu, ze které se do dispatcheru skočilo.
 *
 * Použití: pctrace <sekundy> <T0> <T1>
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "emulator/hw-generic/mz1p16/mcs48.h"
#include "emulator/hw-generic/mz1p16/mz1p16_rom.h"
#define CPS 400000.0
static st_MCS48 cpu;
static uint8_t hd=0xFF;
static uint8_t rd(st_MCS48*c,int p,void*x){(void)c;(void)x;if(p==MCS48_PORT_P2)return hd;return 0xFF;}
static void wr(st_MCS48*c,int p,uint8_t v,void*x){(void)c;(void)p;(void)v;(void)x;}
int main(int argc,char**argv){
    double s=(argc>1)?atof(argv[1]):200.0;
    int t0=(argc>2)?atoi(argv[2]):1;
    int t1=(argc>3)?atoi(argv[3]):0;
    mcs48_init(&cpu,mz1p16_rom_data());
    cpu.on_port_read=rd; cpu.on_port_write=wr;
    cpu.INT=1; cpu.T0=t0; cpu.T1=t1; cpu.T1_prev=t1;
    cpu.P1_in=0xFF; cpu.P2_in=0xFF; cpu.BUS_in=0xFF;
    long total=(long)(s*CPS),done=0;
    long disp=0, eng=0, gen22f=0;
    uint16_t first_disp_from=0xFFFF;
    uint16_t prevpc=0;
    while(done<total){
        uint16_t pc=cpu.PC;
        if(pc>=0x223 && pc<=0x247){ disp++; if(first_disp_from==0xFFFF)first_disp_from=prevpc; }
        if(pc==0x22F) gen22f++;
        if(pc>=0x061 && pc<=0x10D) eng++;
        prevpc=pc;
        done+=mcs48_step(&cpu);
    }
    printf("scenario T0=%d T1=%d run=%.0fs:\n",t0,t1,s);
    printf("  dispatch(223-247) visits: %ld (first from PC %03X)\n",disp,first_disp_from);
    printf("  JMPP@A at 22F visits:     %ld\n",gen22f);
    printf("  vector engine(061-10D):   %ld\n",eng);
    return 0;
}
