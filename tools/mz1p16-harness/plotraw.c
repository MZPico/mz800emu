/*
 * Copyright (c) 2026 Michal Hucik
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file plotraw.c
 * @brief Vykreslí pen-down tečky ze stdin (rawx rawy color) do PNG.
 *
 * Diagnostika: čte řádky "x y color" (color volitelný), škáluje na bbox a
 * kreslí tečky. Color cyklus se dá vynutit z pořadí přes --autocolor N
 * (každých N teček nová barva) nebo z 3. sloupce. Slouží k vizuální kontrole
 * SYROVÝCH dat (nezávisle na modelu mz1p16).
 *
 * Použití: plotraw <out.png> [dot_rad] < dots.txt
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crct[256]; static int crcr=0;
static void crci(void){for(uint32_t n=0;n<256;n++){uint32_t c=n;for(int k=0;k<8;k++)c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1);crct[n]=c;}crcr=1;}
static void be32(FILE*f,uint32_t v){uint8_t b[4]={v>>24,v>>16,v>>8,v};fwrite(b,1,4,f);}
static void chunk(FILE*f,const char*t,const uint8_t*d,uint32_t l){be32(f,l);fwrite(t,1,4,f);if(l)fwrite(d,1,l,f);if(!crcr)crci();uint32_t c=0xFFFFFFFFu;for(int i=0;i<4;i++)c=crct[(c^(uint8_t)t[i])&0xFF]^(c>>8);for(uint32_t i=0;i<l;i++)c=crct[(c^d[i])&0xFF]^(c>>8);be32(f,c^0xFFFFFFFFu);}
static uint32_t adler(const uint8_t*b,size_t l){uint32_t a=1,s=0;for(size_t i=0;i<l;i++){a=(a+b[i])%65521u;s=(s+a)%65521u;}return(s<<16)|a;}
static int pngw(const char*p,const uint8_t*rgb,int w,int h){FILE*f=fopen(p,"wb");if(!f)return 0;static const uint8_t sig[8]={137,80,78,71,13,10,26,10};fwrite(sig,1,8,f);uint8_t ih[13];ih[0]=w>>24;ih[1]=w>>16;ih[2]=w>>8;ih[3]=w;ih[4]=h>>24;ih[5]=h>>16;ih[6]=h>>8;ih[7]=h;ih[8]=8;ih[9]=2;ih[10]=ih[11]=ih[12]=0;chunk(f,"IHDR",ih,13);size_t rl=(size_t)h*(1+(size_t)w*3);uint8_t*raw=malloc(rl);size_t pp=0;for(int y=0;y<h;y++){raw[pp++]=0;memcpy(raw+pp,rgb+(size_t)y*w*3,(size_t)w*3);pp+=(size_t)w*3;}size_t mb=65535,nb=(rl+mb-1)/mb,zl=2+rl+nb*5+4;uint8_t*z=malloc(zl);size_t zp=0;z[zp++]=0x78;z[zp++]=0x01;size_t o=0;while(o<rl){size_t ch=rl-o;if(ch>mb)ch=mb;int fin=(o+ch>=rl)?1:0;z[zp++]=fin;z[zp++]=ch&0xFF;z[zp++]=(ch>>8)&0xFF;z[zp++]=~ch&0xFF;z[zp++]=(~ch>>8)&0xFF;memcpy(z+zp,raw+o,ch);zp+=ch;o+=ch;}uint32_t ad=adler(raw,rl);z[zp++]=ad>>24;z[zp++]=ad>>16;z[zp++]=ad>>8;z[zp++]=ad;chunk(f,"IDAT",z,zp);chunk(f,"IEND",NULL,0);free(z);free(raw);fclose(f);return 1;}

typedef struct{long x,y;int c;}Pt;
#define MAXP 200000
static Pt pts[MAXP]; static long np=0;

static void colrgb(int idx,uint8_t*r,uint8_t*g,uint8_t*b){switch(idx&3){case 0:*r=20;*g=20;*b=20;break;case 1:*r=40;*g=90;*b=220;break;case 2:*r=30;*g=170;*b=60;break;case 3:*r=220;*g=50;*b=50;break;}}

int main(int argc,char**argv){
    const char*out=(argc>1)?argv[1]:"/tmp/raw.png";
    int rad=(argc>2)?atoi(argv[2]):1;
    char line[256];
    while(fgets(line,sizeof line,stdin)&&np<MAXP){
        long x,y; int c=0;
        int n=sscanf(line,"%ld %ld %d",&x,&y,&c);
        if(n<2)continue;
        pts[np].x=x;pts[np].y=y;pts[np].c=(n>=3)?c:0;np++;
    }
    if(np<2){fprintf(stderr,"too few pts %ld\n",np);return 1;}
    long minx=pts[0].x,maxx=pts[0].x,miny=pts[0].y,maxy=pts[0].y;
    for(long i=1;i<np;i++){if(pts[i].x<minx)minx=pts[i].x;if(pts[i].x>maxx)maxx=pts[i].x;if(pts[i].y<miny)miny=pts[i].y;if(pts[i].y>maxy)maxy=pts[i].y;}
    int margin=8;
    int W=(int)(maxx-minx+1)+2*margin, H=(int)(maxy-miny+1)+2*margin;
    if(W<16)W=16;if(H<16)H=16;
    uint8_t*px=malloc((size_t)W*H*3);memset(px,0xF6,(size_t)W*H*3);
    for(long i=0;i<np;i++){uint8_t r,g,b;colrgb(pts[i].c,&r,&g,&b);int cx=margin+(int)(pts[i].x-minx),cy=margin+(int)(pts[i].y-miny);for(int dy=-rad;dy<=rad;dy++)for(int dx=-rad;dx<=rad;dx++){if(dx*dx+dy*dy>rad*rad)continue;int xx=cx+dx,yy=cy+dy;if(xx<0||yy<0||xx>=W||yy>=H)continue;uint8_t*q=px+((size_t)yy*W+xx)*3;q[0]=r;q[1]=g;q[2]=b;}}
    if(!pngw(out,px,W,H)){fprintf(stderr,"png fail\n");return 1;}
    fprintf(stderr,"plotraw: %ld pts bbox X[%ld..%ld] Y[%ld..%ld] -> %dx%d %s\n",np,minx,maxx,miny,maxy,W,H,out);
    free(px);return 0;
}
