/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file disasm.c
 * @brief Minimální MCS-48 (8050) disassembler firmwaru MZ-1P16 (diagnostika).
 *
 * Standalone nástroj pro analýzu embeddovaného firmwaru plotteru. Dekóduje
 * opcody stejnou tabulkou jako jádro mcs48.c (mnemonika, délka, operand).
 * Slouží k lokalizaci kreslicích/idle rutin a větvení (JNT1/JT0/JNI), ne
 * k produkci výstupu.
 *
 * Použití:
 *   disasm.exe <from_hex> <to_hex>   -- vypíše rozsah adres
 *   disasm.exe scan {T1|T0|JNI|JTF}  -- vypíše všechna místa čtení daného vstupu
 *
 * @note Disassembler je lineární (od adresy), nerozlišuje kód a data - ve
 *       smíšených oblastech (např. ROM stránka 3) může dekódovat data jako kód.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emulator/hw-generic/mz1p16/mz1p16_rom.h"

static const uint8_t *R;

/* vrati delku instrukce (1/2) a textovy zapis */
static int dis1(uint16_t a, char *out)
{
    uint8_t op = R[a & 0xFFF];
    uint8_t b1 = R[(a+1) & 0xFFF];
    int len = 1;
    const char *m = NULL;
    char buf[40];
    switch (op) {
    case 0x00: m="NOP"; break;
    case 0x02: m="OUTL BUS,A"; break;
    case 0x03: sprintf(buf,"ADD A,#%02X",b1); len=2; m=buf; break;
    case 0x04: case 0x24: case 0x44: case 0x64:
    case 0x84: case 0xA4: case 0xC4: case 0xE4:
        sprintf(buf,"JMP %X%02X",(op>>5),b1); len=2; m=buf; break;
    case 0x05: m="EN I"; break;
    case 0x07: m="DEC A"; break;
    case 0x08: m="INS A,BUS"; break;
    case 0x09: m="IN A,P1"; break;
    case 0x0A: m="IN A,P2"; break;
    case 0x10: case 0x11: sprintf(buf,"INC @R%d",op&1); m=buf; break;
    case 0x12: case 0x32: case 0x52: case 0x72:
    case 0x92: case 0xB2: case 0xD2: case 0xF2:
        sprintf(buf,"JB%d %02X",(op>>5),b1); len=2; m=buf; break;
    case 0x13: sprintf(buf,"ADDC A,#%02X",b1); len=2; m=buf; break;
    case 0x14: case 0x34: case 0x54: case 0x74:
    case 0x94: case 0xB4: case 0xD4: case 0xF4:
        sprintf(buf,"CALL %X%02X",(op>>5),b1); len=2; m=buf; break;
    case 0x15: m="DIS I"; break;
    case 0x16: sprintf(buf,"JTF %02X",b1); len=2; m=buf; break;
    case 0x17: m="INC A"; break;
    case 0x18: case 0x19: case 0x1A: case 0x1B:
    case 0x1C: case 0x1D: case 0x1E: case 0x1F:
        sprintf(buf,"INC R%d",op&7); m=buf; break;
    case 0x20: case 0x21: sprintf(buf,"XCH A,@R%d",op&1); m=buf; break;
    case 0x23: sprintf(buf,"MOV A,#%02X",b1); len=2; m=buf; break;
    case 0x25: m="EN TCNTI"; break;
    case 0x26: sprintf(buf,"JNT0 %02X",b1); len=2; m=buf; break;
    case 0x27: m="CLR A"; break;
    case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        sprintf(buf,"XCH A,R%d",op&7); m=buf; break;
    case 0x30: case 0x31: sprintf(buf,"XCHD A,@R%d",op&1); m=buf; break;
    case 0x35: m="DIS TCNTI"; break;
    case 0x36: sprintf(buf,"JT0 %02X",b1); len=2; m=buf; break;
    case 0x37: m="CPL A"; break;
    case 0x39: m="OUTL P1,A"; break;
    case 0x3A: m="OUTL P2,A"; break;
    case 0x40: case 0x41: sprintf(buf,"ORL A,@R%d",op&1); m=buf; break;
    case 0x42: m="MOV A,T"; break;
    case 0x43: sprintf(buf,"ORL A,#%02X",b1); len=2; m=buf; break;
    case 0x45: m="STRT CNT"; break;
    case 0x46: sprintf(buf,"JNT1 %02X",b1); len=2; m=buf; break;
    case 0x47: m="SWAP A"; break;
    case 0x48: case 0x49: case 0x4A: case 0x4B:
    case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        sprintf(buf,"ORL A,R%d",op&7); m=buf; break;
    case 0x50: case 0x51: sprintf(buf,"ANL A,@R%d",op&1); m=buf; break;
    case 0x53: sprintf(buf,"ANL A,#%02X",b1); len=2; m=buf; break;
    case 0x55: m="STRT T"; break;
    case 0x56: sprintf(buf,"JT1 %02X",b1); len=2; m=buf; break;
    case 0x57: m="DA A"; break;
    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        sprintf(buf,"ANL A,R%d",op&7); m=buf; break;
    case 0x60: case 0x61: sprintf(buf,"ADD A,@R%d",op&1); m=buf; break;
    case 0x62: m="MOV T,A"; break;
    case 0x65: m="STOP TCNT"; break;
    case 0x67: m="RRC A"; break;
    case 0x68: case 0x69: case 0x6A: case 0x6B:
    case 0x6C: case 0x6D: case 0x6E: case 0x6F:
        sprintf(buf,"ADD A,R%d",op&7); m=buf; break;
    case 0x70: case 0x71: sprintf(buf,"ADDC A,@R%d",op&1); m=buf; break;
    case 0x75: m="ENT0 CLK"; break;
    case 0x76: sprintf(buf,"JF1 %02X",b1); len=2; m=buf; break;
    case 0x77: m="RR A"; break;
    case 0x78: case 0x79: case 0x7A: case 0x7B:
    case 0x7C: case 0x7D: case 0x7E: case 0x7F:
        sprintf(buf,"ADDC A,R%d",op&7); m=buf; break;
    case 0x80: case 0x81: sprintf(buf,"MOVX A,@R%d",op&1); m=buf; break;
    case 0x83: m="RET"; break;
    case 0x85: m="CLR F0"; break;
    case 0x86: sprintf(buf,"JNI %02X",b1); len=2; m=buf; break;
    case 0x88: sprintf(buf,"ORL BUS,#%02X",b1); len=2; m=buf; break;
    case 0x89: sprintf(buf,"ORL P1,#%02X",b1); len=2; m=buf; break;
    case 0x8A: sprintf(buf,"ORL P2,#%02X",b1); len=2; m=buf; break;
    case 0x90: case 0x91: sprintf(buf,"MOVX @R%d,A",op&1); m=buf; break;
    case 0x93: m="RETR"; break;
    case 0x95: m="CPL F0"; break;
    case 0x96: sprintf(buf,"JNZ %02X",b1); len=2; m=buf; break;
    case 0x97: m="CLR C"; break;
    case 0x98: sprintf(buf,"ANL BUS,#%02X",b1); len=2; m=buf; break;
    case 0x99: sprintf(buf,"ANL P1,#%02X",b1); len=2; m=buf; break;
    case 0x9A: sprintf(buf,"ANL P2,#%02X",b1); len=2; m=buf; break;
    case 0xA0: case 0xA1: sprintf(buf,"MOV @R%d,A",op&1); m=buf; break;
    case 0xA3: m="MOVP A,@A"; break;
    case 0xA5: m="CLR F1"; break;
    case 0xA7: m="CPL C"; break;
    case 0xA8: case 0xA9: case 0xAA: case 0xAB:
    case 0xAC: case 0xAD: case 0xAE: case 0xAF:
        sprintf(buf,"MOV R%d,A",op&7); m=buf; break;
    case 0xB0: case 0xB1: sprintf(buf,"MOV @R%d,#%02X",op&1,b1); len=2; m=buf; break;
    case 0xB3: m="JMPP @A"; break;
    case 0xB5: m="CPL F1"; break;
    case 0xB6: sprintf(buf,"JF0 %02X",b1); len=2; m=buf; break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        sprintf(buf,"MOV R%d,#%02X",op&7,b1); len=2; m=buf; break;
    case 0xC5: m="SEL RB0"; break;
    case 0xC6: sprintf(buf,"JZ %02X",b1); len=2; m=buf; break;
    case 0xC7: m="MOV A,PSW"; break;
    case 0xC8: case 0xC9: case 0xCA: case 0xCB:
    case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        sprintf(buf,"DEC R%d",op&7); m=buf; break;
    case 0xD0: case 0xD1: sprintf(buf,"XRL A,@R%d",op&1); m=buf; break;
    case 0xD3: sprintf(buf,"XRL A,#%02X",b1); len=2; m=buf; break;
    case 0xD5: m="SEL RB1"; break;
    case 0xD7: m="MOV PSW,A"; break;
    case 0xD8: case 0xD9: case 0xDA: case 0xDB:
    case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        sprintf(buf,"XRL A,R%d",op&7); m=buf; break;
    case 0xE3: m="MOVP3 A,@A"; break;
    case 0xE5: m="SEL MB0"; break;
    case 0xE6: sprintf(buf,"JNC %02X",b1); len=2; m=buf; break;
    case 0xE7: m="RL A"; break;
    case 0xE8: case 0xE9: case 0xEA: case 0xEB:
    case 0xEC: case 0xED: case 0xEE: case 0xEF:
        sprintf(buf,"DJNZ R%d,%02X",op&7,b1); len=2; m=buf; break;
    case 0xF0: case 0xF1: sprintf(buf,"MOV A,@R%d",op&1); m=buf; break;
    case 0xF5: m="SEL MB1"; break;
    case 0xF6: sprintf(buf,"JC %02X",b1); len=2; m=buf; break;
    case 0xF7: m="RLC A"; break;
    case 0xF8: case 0xF9: case 0xFA: case 0xFB:
    case 0xFC: case 0xFD: case 0xFE: case 0xFF:
        sprintf(buf,"MOV A,R%d",op&7); m=buf; break;
    default: sprintf(buf,"??? %02X",op); m=buf; break;
    }
    sprintf(out,"%-16s", m);
    return len;
}

int main(int argc, char **argv)
{
    R = mz1p16_rom_data();
    if (argc >= 2 && strcmp(argv[1],"scan")==0) {
        const char *what = (argc>2)?argv[2]:"T1";
        for (int a=0; a<0xFFF; a++) {
            uint8_t op = R[a];
            int hit=0;
            if (strcmp(what,"T1")==0 && (op==0x46||op==0x56)) hit=1;
            if (strcmp(what,"T0")==0 && (op==0x26||op==0x36)) hit=1;
            if (strcmp(what,"JNI")==0 && op==0x86) hit=1;
            if (strcmp(what,"JTF")==0 && op==0x16) hit=1;
            if (hit) { char o[64]; dis1(a,o); printf("%03X: %s\n",a,o); }
        }
        return 0;
    }
    uint16_t from = (argc>1)?(uint16_t)strtol(argv[1],0,16):0;
    uint16_t to   = (argc>2)?(uint16_t)strtol(argv[2],0,16):(from+0x40);
    uint16_t a=from;
    while (a<=to) {
        char o[64]; int len=dis1(a,o);
        if (len==2) printf("%03X: %02X %02X   %s\n",a,R[a&0xFFF],R[(a+1)&0xFFF],o);
        else        printf("%03X: %02X      %s\n",a,R[a&0xFFF],o);
        a+=len;
    }
    return 0;
}
