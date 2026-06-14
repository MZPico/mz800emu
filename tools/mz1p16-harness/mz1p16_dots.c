/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file mz1p16_dots.c
 * @brief Vypíše jen pen-down tečky self-testu (raw pozice) - hledání periodicity.
 *
 * Nezávislý na modelu mz1p16: dekóduje fáze sám, akumuluje raw X/Y (dir +1 v
 * sekvenci A->6->5->9), a při každém pen-down tapu (P1.1: 1->0) vypíše
 * aktuální raw pozici. Z toho lze odečíst rozteč buněk znaků a řádků a tvar
 * jednoho glyfu (tečková matice).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "emulator/hw-generic/mz1p16/mcs48.h"
#include "emulator/hw-generic/mz1p16/mz1p16_rom.h"

#define CYCLES_PER_SEC 400000.0
static st_MCS48 g_cpu;
static const uint8_t SEQ[4]={0xA,0x6,0x5,0x9};
static int sidx(uint8_t p){for(int i=0;i<4;i++)if(SEQ[i]==p)return i;return -1;}
static uint8_t px=0,py=0; static bool hx=false,hy=false;
static long rawx=0,rawy=0; static int last_pen=1;
static long g_cycle=0;

static long lx=999999,ly=999999;
static void on_write(st_MCS48*c,int port,uint8_t val,void*ctx){
    (void)ctx;(void)c;
    if(port==MCS48_PORT_BUS){
        uint8_t xl=val&0xF, yh=(val>>4)&0xF;
        if(hx&&xl!=px){int o=sidx(px),n=sidx(xl);if(o>=0&&n>=0){int d=(n-o)&3;if(d==1)rawx++;else if(d==3)rawx--;}}
        if(hy&&yh!=py){int o=sidx(py),n=sidx(yh);if(o>=0&&n>=0){int d=(n-o)&3;if(d==1)rawy++;else if(d==3)rawy--;}}
        px=xl;py=yh;hx=hy=true;
        /* Pokud je pero dole během tohoto kroku, zaznamenej pozici (souvislý tah). */
        if(last_pen==0 && (rawx!=lx||rawy!=ly)){printf("%ld %ld %ld\n",rawx,rawy,g_cycle);lx=rawx;ly=rawy;}
    } else if(port==MCS48_PORT_P1){
        int pen=(val&0x02)?1:0;
        if(pen==0 && (rawx!=lx||rawy!=ly)){printf("%ld %ld %ld\n",rawx,rawy,g_cycle);lx=rawx;ly=rawy;}
        last_pen=pen;
    }
}
static uint8_t on_read(st_MCS48*c,int port,void*ctx){(void)c;(void)ctx;return 0xFF;}

int main(int argc,char**argv){
    double run_s=(argc>1)?atof(argv[1]):90.0;
    mcs48_init(&g_cpu,mz1p16_rom_data());
    g_cpu.on_port_write=on_write; g_cpu.on_port_read=on_read;
    g_cpu.INT=1;g_cpu.T0=1;g_cpu.T1=0;g_cpu.T1_prev=0;
    g_cpu.P1_in=0xFF;g_cpu.P2_in=0xFF;g_cpu.BUS_in=0xFF;
    long total=(long)(run_s*CYCLES_PER_SEC);
    while(g_cycle<total) g_cycle+=mcs48_step(&g_cpu);
    return 0;
}
