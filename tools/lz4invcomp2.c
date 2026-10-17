/* lz4invcomp2 —— LZ4Inv 压缩器 v2（链式匹配搜索，提升压缩率）
 *   改进: 每个 hash 槽维护一个候选链（最近 N 个位置），选最长匹配
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define HBITS 16
#define HSIZE (1<<HBITS)
#define CHAIN 8          /* 每个 hash 槽保留的候选数 */

static int32_t htab[HSIZE][CHAIN];
static int32_t hcnt[HSIZE];

static uint32_t rd32(const uint8_t*p){return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t hash4(uint32_t v){ return (v*2654435761u)>>(32-HBITS); }

static long comp(const uint8_t*src,long slen,uint8_t*dst,long dcap){
    long ip=0,op=0,anchor=0;
    memset(hcnt,0,sizeof hcnt);
    if(slen<13) goto tail;
    while(ip < slen-12){
        uint32_t v=rd32(src+ip);
        uint32_t h=hash4(v);
        /* 先搜候选链，找最长匹配 */
        long bestLen=0, bestOff=0;
        int n=hcnt[h];
        for(int k=0;k<n;k++){
            int32_t ref=htab[h][k];
            long off=ip-ref;
            if(off<=0||off>65535) continue;
            /* 快速校验首 4 字节 */
            if(memcmp(src+ref,src+ip,4)!=0) continue;
            long ml=4;
            long maxL = slen-ip;
            while(ml<maxL && src[ref+ml]==src[ip+ml]) ml++;
            if(ml>bestLen){ bestLen=ml; bestOff=off; if(ml>=64) break; }
        }
        /* 把当前位置加入链 */
        if(hcnt[h]<CHAIN) htab[h][hcnt[h]++]=(int32_t)ip;
        else { memmove(&htab[h][0],&htab[h][1],sizeof(int32_t)*(CHAIN-1)); htab[h][CHAIN-1]=(int32_t)ip; }
        if(bestLen<4){ ip++; continue; }
        long litLen=ip-anchor, ml=bestLen, off=bestOff;
        long mlCode=ml-4;
        long litC=litLen<15?litLen:15, mlC=mlCode<15?mlCode:15;
        uint8_t tok=(uint8_t)((litC&3)|((mlC&3)<<2)|(((litC>>2)&3)<<4)|(((mlC>>2)&3)<<6));
        if(op+1>dcap) return -1; dst[op++]=tok;
        if(litC==15){ long r=litLen-15; while(r>=255){ if(op+1>dcap)return -1; dst[op++]=255; r-=255;} if(op+1>dcap)return -1; dst[op++]=(uint8_t)r; }
        if(op+litLen>dcap) return -1;
        memcpy(dst+op,src+anchor,litLen); op+=litLen;
        if(op+2>dcap) return -1;
        dst[op++]=(uint8_t)(off>>8); dst[op++]=(uint8_t)(off&0xFF);
        if(mlC==15){ long r=mlCode-15; while(r>=255){ if(op+1>dcap)return -1; dst[op++]=255; r-=255;} if(op+1>dcap)return -1; dst[op++]=(uint8_t)r; }
        /* 把匹配区间内的位置也加入链（提升后续匹配率） */
        long end=ip+ml;
        for(long t=ip+1;t<end && t<slen-12;t++){
            uint32_t tv=rd32(src+t); uint32_t th=hash4(tv);
            if(hcnt[th]<CHAIN) htab[th][hcnt[th]++]=(int32_t)t;
            else { memmove(&htab[th][0],&htab[th][1],sizeof(int32_t)*(CHAIN-1)); htab[th][CHAIN-1]=(int32_t)t; }
        }
        ip=end; anchor=ip;
    }
tail:
    {
        long litLen=slen-anchor;
        long litC=litLen<15?litLen:15;
        uint8_t tok=(uint8_t)((litC&3)|(((litC>>2)&3)<<4));
        if(op+1>dcap) return -1; dst[op++]=tok;
        if(litC==15){ long r=litLen-15; while(r>=255){ if(op+1>dcap)return -1; dst[op++]=255; r-=255;} if(op+1>dcap)return -1; dst[op++]=(uint8_t)r; }
        if(op+litLen>dcap) return -1;
        memcpy(dst+op,src+anchor,litLen); op+=litLen;
    }
    return op;
}
static long decomp(const uint8_t*in,long inLen,uint8_t*out,long cap){
    const uint8_t*ip=in,*iend=in+inLen; uint8_t*op=out,*oend=out+cap;
    while(ip<iend){
        uint8_t tk=*ip++; uint8_t lit=tk&0x33,enc=(tk&0xCC)>>2;
        long ll=(lit&3)|(lit>>2), ml=(enc&3)|(enc>>2);
        if(ll==15){ uint8_t s; do{ if(ip>=iend) return -1; s=*ip++; ll+=s; }while(s==255); }
        if(ll>(iend-ip)||op+ll>oend) return -1;
        memcpy(op,ip,ll); ip+=ll; op+=ll;
        if(ip>=iend) break;
        if(ip+2>iend) return -1;
        long off=(ip[0]<<8)|ip[1]; ip+=2;
        if(off==0||off>(op-out)) return -1;
        if(ml==15){ uint8_t s; do{ if(ip>=iend) return -1; s=*ip++; ml+=s; }while(s==255); }
        ml+=4;
        if(op+ml>oend) return -1;
        { const uint8_t*mp=op-off; for(long i=0;i<ml;i++) *op++=*mp++; }
    }
    return op-out;
}
int main(int c,char**v){
    if(c<3){ fprintf(stderr,"用法: lz4invcomp2 <in> <out>\n"); return 2; }
    FILE*f=fopen(v[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*src=malloc(n); if(fread(src,1,n,f)!=(size_t)n) return 1; fclose(f);
    long cap=n*2+65536; uint8_t*dst=malloc(cap);
    long o=comp(src,n,dst,cap);
    printf("  v2 压缩: %ld → %ld 字节 (%.2f%%)\n", n,o,100.0*o/n);
    if(o>0){
        uint8_t*chk=malloc(n+64);
        long d=decomp(dst,o,chk,n+64);
        printf("  自测: %ld %s\n", d, (d==n&&!memcmp(src,chk,n))?"★ 一致":"✗");
        FILE*g=fopen(v[2],"wb"); fwrite(dst,1,o,g); fclose(g);
        free(chk);
    }
    free(src); free(dst);
    return o>0?0:1;
}
