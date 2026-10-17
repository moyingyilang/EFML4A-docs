/* 聚合依赖（LZ4Inv / LZ4 双向）+ 辅助 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define HBITS 17
#define HSIZE (1<<HBITS)
#define CHAIN 16
static int32_t htab[HSIZE][CHAIN]; static int32_t hcnt[HSIZE];
static uint32_t rd32(const uint8_t*p){return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t hash4(uint32_t v){return (v*2654435761u)>>(32-HBITS);}

/* ---- LZ4Inv 解压 ---- */
long lz4inv_decompress(const uint8_t*in,long inLen,uint8_t*out,long outCap){
    const uint8_t*ip=in,*iend=in+inLen; uint8_t*op=out,*oend=out+outCap;
    while(ip<iend){
        uint8_t val=*ip++;
        uint8_t lit=val&0x33, enc=(val&0xCC)>>2;
        long ll=(lit&3)|(lit>>2), ml=(enc&3)|(enc>>2);
        if(ll==15){uint8_t s;do{if(ip>=iend)return -1;s=*ip++;ll+=s;}while(s==255);}
        if(ll>(iend-ip)) return -1;
        if(op+ll>oend){ memcpy(op,ip,(size_t)(oend-op)); op=oend; break; }  /* ★ 截断即停 */
        memcpy(op,ip,ll);ip+=ll;op+=ll;
        if(ip>=iend)break;
        if(ip+2>iend)return -1;
        long off=(ip[0]<<8)|ip[1]; ip+=2;
        if(off==0||off>(op-out))return -1;
        if(ml==15){uint8_t s;do{if(ip>=iend)return -1;s=*ip++;ml+=s;}while(s==255);}
        ml+=4;
        if(op+ml>oend)return -1;
        {const uint8_t*mp=op-off;for(long i=0;i<ml;i++)*op++=*mp++;}
    }
    return op-out;
}
/* ---- LZ4Inv 压缩（v4: HBITS=20, CHAIN=1024, lazy matching） ---- */
#ifndef HBITS
#define HBITS 20
#endif
#ifndef CHAIN
#define CHAIN 1024
#endif
#define MAXOFF 65535
static int32_t (*v4tab)[CHAIN] = NULL;
static int32_t *v4cnt = NULL;
static void v4ins(uint32_t h, int32_t pos){
    if (v4cnt[h] < CHAIN) v4tab[h][v4cnt[h]++] = pos;
    else { memmove(&v4tab[h][0], &v4tab[h][1], sizeof(int32_t)*(CHAIN-1)); v4tab[h][CHAIN-1] = pos; }
}
static long v4find(const uint8_t *src, long ip, long slen, long *outOff){
    uint32_t h = hash4(rd32(src + ip));
    long bestLen = 0, bestOff = 0; int n = v4cnt[h]; long maxL = slen - ip;
    for (int k = n - 1; k >= 0; k--){
        int32_t ref = v4tab[h][k]; long off = ip - ref;
        if (off <= 0 || off > MAXOFF) continue;
        if (src[ref + bestLen] != src[ip + bestLen]) continue;
        long ml = 0; while (ml < maxL && src[ref + ml] == src[ip + ml]) ml++;
        if (ml > bestLen){ bestLen = ml; bestOff = off; if (ml >= 256) break; }
    }
    *outOff = bestOff; return bestLen;
}
long lz4inv_compress(const uint8_t*src, long n, uint8_t*dst, long cap){
    if (!v4tab){
        v4tab = malloc(sizeof(int32_t) * (size_t)(1u<<HBITS) * CHAIN);
        v4cnt = calloc((size_t)1u<<HBITS, sizeof(int32_t));
        if (!v4tab || !v4cnt) return -1;
    } else { memset(v4cnt, 0, sizeof(int32_t) * (size_t)(1u<<HBITS)); }
    long ip = 0, op = 0, anchor = 0;
    while (ip + 12 < n){
        long off1 = 0, len1 = v4find(src, ip, n, &off1);
        v4ins(hash4(rd32(src + ip)), (int32_t)ip);
        if (len1 >= 4 && ip + 1 + 12 < n){
            long off2 = 0, len2 = v4find(src, ip + 1, n, &off2);
            if (len2 > len1 + 1){ ip++; continue; }
        }
        if (len1 < 4){ ip++; continue; }
        long litLen = ip - anchor, mlCode = len1 - 4;
        long litC = litLen < 15 ? litLen : 15, mlC = mlCode < 15 ? mlCode : 15;
        if (op + 1 > cap) return -1;
        dst[op++] = (uint8_t)((litC & 3) | ((mlC & 3) << 2) | (((litC >> 2) & 3) << 4) | (((mlC >> 2) & 3) << 6));
        if (litC == 15){ long r = litLen - 15; while (r >= 255){ if (op+1>cap) return -1; dst[op++]=255; r-=255; } if (op+1>cap) return -1; dst[op++]=(uint8_t)r; }
        if (op + litLen > cap) return -1; memcpy(dst + op, src + anchor, litLen); op += litLen;
        if (op + 2 > cap) return -1; dst[op++] = (uint8_t)(off1 >> 8); dst[op++] = (uint8_t)(off1 & 0xFF);
        if (mlC == 15){ long r = mlCode - 15; while (r >= 255){ if (op+1>cap) return -1; dst[op++]=255; r-=255; } if (op+1>cap) return -1; dst[op++]=(uint8_t)r; }
        long end = ip + len1;
        for (long t = ip + 1; t < end && t + 12 < n; t++) v4ins(hash4(rd32(src + t)), (int32_t)t);
        ip = end; anchor = ip;
    }
    { long litLen = n - anchor; long litC = litLen < 15 ? litLen : 15;
      if (op + 1 > cap) return -1;
      dst[op++] = (uint8_t)((litC & 3) | (((litC >> 2) & 3) << 4));
      if (litC == 15){ long r = litLen - 15; while (r >= 255){ if (op+1>cap) return -1; dst[op++]=255; r-=255; } if (op+1>cap) return -1; dst[op++]=(uint8_t)r; }
      if (op + litLen > cap) return -1; memcpy(dst + op, src + anchor, litLen); op += litLen; }
    return op;
}
/* ---- 标准 LZ4 解压 ---- */
long lz4_decompress(const uint8_t*in,long inLen,uint8_t*out,long outCap){
    const uint8_t*ip=in,*iend=in+inLen; uint8_t*op=out,*oend=out+outCap;
    while(ip<iend){
        uint8_t tk=*ip++; long ll=tk>>4, ml=tk&0x0F;
        if(ll==15){uint8_t s;do{if(ip>=iend)return -1;s=*ip++;ll+=s;}while(s==255);}
        if(ll>(iend-ip)) return -1;
        if(op+ll>oend){ memcpy(op,ip,(size_t)(oend-op)); op=oend; break; }  /* ★ 截断即停 */
        memcpy(op,ip,ll);ip+=ll;op+=ll;
        if(op>=oend) break;   /* ★ 输出填满即停（与游戏一致） */
        if(ip>=iend)break;
        if(ip+2>iend)return -1;
        long off=ip[0]|(ip[1]<<8); ip+=2;
        if(off==0||off>(op-out))return -1;
        if(ml==15){uint8_t s;do{if(ip>=iend)return -1;s=*ip++;ml+=s;}while(s==255);}
        ml+=4;
        if(op+ml>oend)return -1;
        {const uint8_t*mp=op-off;for(long i=0;i<ml;i++)*op++=*mp++;}
    }
    return op-out;
}
/* ---- 标准 LZ4 压缩 ---- */
long lz4_compress(const uint8_t*src,long slen,uint8_t*dst,long dcap){
    long ip=0,op=0,anchor=0; memset(hcnt,0,sizeof hcnt);
    if(slen<13) goto tail;
    while(ip<slen-12){
        uint32_t h=hash4(rd32(src+ip));
        long bestLen=0,bestOff=0; int n=hcnt[h];
        for(int k=0;k<n;k++){
            int32_t ref=htab[h][k]; long off=ip-ref;
            if(off<=0||off>65535) continue;
            if(memcmp(src+ref,src+ip,4)!=0) continue;
            long ml=4,maxL=slen-ip;
            while(ml<maxL&&src[ref+ml]==src[ip+ml]) ml++;
            if(ml>bestLen){bestLen=ml;bestOff=off;if(ml>=128)break;}
        }
        if(hcnt[h]<CHAIN) htab[h][hcnt[h]++]=(int32_t)ip;
        else {memmove(&htab[h][0],&htab[h][1],sizeof(int32_t)*(CHAIN-1));htab[h][CHAIN-1]=(int32_t)ip;}
        if(bestLen<4){ip++;continue;}
        long litLen=ip-anchor, mlCode=bestLen-4;
        long litC=litLen<15?litLen:15, mlC=mlCode<15?mlCode:15;
        if(op+1>dcap)return -1;
        dst[op++]=(uint8_t)((litC<<4)|mlC);
        if(litC==15){long r=litLen-15;while(r>=255){if(op+1>dcap)return -1;dst[op++]=255;r-=255;}if(op+1>dcap)return -1;dst[op++]=(uint8_t)r;}
        if(op+litLen>dcap)return -1;
        memcpy(dst+op,src+anchor,litLen);op+=litLen;
        if(op+2>dcap)return -1;
        dst[op++]=(uint8_t)(bestOff&0xFF); dst[op++]=(uint8_t)(bestOff>>8);
        if(mlC==15){long r=mlCode-15;while(r>=255){if(op+1>dcap)return -1;dst[op++]=255;r-=255;}if(op+1>dcap)return -1;dst[op++]=(uint8_t)r;}
        long end=ip+bestLen;
        for(long t=ip+1;t<end&&t<slen-12;t++){
            uint32_t th=hash4(rd32(src+t));
            if(hcnt[th]<CHAIN)htab[th][hcnt[th]++]=(int32_t)t;
            else{memmove(&htab[th][0],&htab[th][1],sizeof(int32_t)*(CHAIN-1));htab[th][CHAIN-1]=(int32_t)t;}
        }
        ip=end;anchor=ip;
    }
tail:
    {long litLen=slen-anchor;long litC=litLen<15?litLen:15;
     if(op+1>dcap)return -1; dst[op++]=(uint8_t)(litC<<4);
     if(litC==15){long r=litLen-15;while(r>=255){if(op+1>dcap)return -1;dst[op++]=255;r-=255;}if(op+1>dcap)return -1;dst[op++]=(uint8_t)r;}
     if(op+litLen>dcap)return -1; memcpy(dst+op,src+anchor,litLen);op+=litLen;}
    return op;
}
