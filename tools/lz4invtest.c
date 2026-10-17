/* lz4invtest —— LZ4Inv 压缩器自测（压缩→解压→逐字节比对）
 *   用法: lz4invtest <in.bin>
 *   自动定位第一个不同的输出字节 + 其所在的 token
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- 压缩器（与 lz4invcomp3 同算法，CHAIN=16）---- */
#define HBITS 17
#define HSIZE (1<<HBITS)
#define CHAIN 16
static int32_t htab[HSIZE][CHAIN]; static int32_t hcnt[HSIZE];
static uint32_t rd32(const uint8_t*p){return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t hash4(uint32_t v){return (v*2654435761u)>>(32-HBITS);}

static long comp(const uint8_t*src,long slen,uint8_t*dst,long dcap){
    long ip=0,op=0,anchor=0; memset(hcnt,0,sizeof hcnt);
    if(slen<13) goto tail;
    while(ip<slen-12){
        uint32_t h=hash4(rd32(src+ip));
        long bestLen=0,bestOff=0; int n=hcnt[h];
        for(int k=0;k<n;k++){
            int32_t ref=htab[h][k]; long off=ip-ref;
            if(off<=0||off>65535) continue;
            if(memcmp(src+ref,src+ip,4)!=0) continue;
            long ml=4, maxL=slen-ip;
            while(ml<maxL && src[ref+ml]==src[ip+ml]) ml++;
            if(ml>bestLen){bestLen=ml;bestOff=off;if(ml>=128)break;}
        }
        if(hcnt[h]<CHAIN) htab[h][hcnt[h]++]=(int32_t)ip;
        else {memmove(&htab[h][0],&htab[h][1],sizeof(int32_t)*(CHAIN-1));htab[h][CHAIN-1]=(int32_t)ip;}
        if(bestLen<4){ip++;continue;}
        long litLen=ip-anchor, mlCode=bestLen-4;
        long litC=litLen<15?litLen:15, mlC=mlCode<15?mlCode:15;
        if(op+1>dcap)return -1;
        dst[op++]=(uint8_t)((litC&3)|((mlC&3)<<2)|(((litC>>2)&3)<<4)|(((mlC>>2)&3)<<6));
        if(litC==15){long r=litLen-15;while(r>=255){if(op+1>dcap)return -1;dst[op++]=255;r-=255;}if(op+1>dcap)return -1;dst[op++]=(uint8_t)r;}
        if(op+litLen>dcap)return -1;
        memcpy(dst+op,src+anchor,litLen);op+=litLen;
        if(op+2>dcap)return -1;
        dst[op++]=(uint8_t)(bestOff>>8); dst[op++]=(uint8_t)(bestOff&0xFF);
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
     if(op+1>dcap)return -1; dst[op++]=(uint8_t)((litC&3)|(((litC>>2)&3)<<4));
     if(litC==15){long r=litLen-15;while(r>=255){if(op+1>dcap)return -1;dst[op++]=255;r-=255;}if(op+1>dcap)return -1;dst[op++]=(uint8_t)r;}
     if(op+litLen>dcap)return -1; memcpy(dst+op,src+anchor,litLen);op+=litLen;}
    return op;
}
/* ---- 解压器（与 lz4inv.c 同）---- */
static long decomp(const uint8_t*in,long inLen,uint8_t*out,long cap,long*errTok){
    const uint8_t*ip=in,*iend=in+inLen; uint8_t*op=out,*oend=out+cap;
    long tok=0;
    while(ip<iend){
        tok++;
        uint8_t tk=*ip++;
        uint8_t lit=tk&0x33, enc=(tk&0xCC)>>2;
        long ll=(lit&3)|(lit>>2), ml=(enc&3)|(enc>>2);
        if(ll==15){uint8_t s;do{if(ip>=iend){if(errTok)*errTok=tok;return -1;}s=*ip++;ll+=s;}while(s==255);}
        if(ll>(iend-ip)||op+ll>oend){if(errTok)*errTok=tok;return -1;}
        memcpy(op,ip,ll);ip+=ll;op+=ll;
        if(ip>=iend)break;
        if(ip+2>iend){if(errTok)*errTok=tok;return -1;}
        long off=(ip[0]<<8)|ip[1];ip+=2;
        if(off==0||off>(op-out)){if(errTok)*errTok=tok;return -1;}
        if(ml==15){uint8_t s;do{if(ip>=iend){if(errTok)*errTok=tok;return -1;}s=*ip++;ml+=s;}while(s==255);}
        ml+=4;
        if(op+ml>oend){if(errTok)*errTok=tok;return -1;}
        {const uint8_t*mp=op-off;for(long i=0;i<ml;i++)*op++=*mp++;}
    }
    return op-out;
}
int main(int c,char**v){
    if(c<2){fprintf(stderr,"用法: lz4invtest <in.bin>\n");return 2;}
    FILE*f=fopen(v[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t*src=malloc(n);if(fread(src,1,n,f)!=(size_t)n)return 1;fclose(f);
    long cap=n*2+65536; uint8_t*dst=malloc(cap);
    long o=comp(src,n,dst,cap);
    printf("  压缩: %ld → %ld (%.2f%%)\n",n,o,100.0*o/n);
    uint8_t*out=malloc(n+4096); long et=0;
    long d=decomp(dst,o,out,n+4096,&et);
    printf("  解压: %ld (期望 %ld) %s\n",d,n,d==n?"✓":"✗");
    if(d<0){printf("  ★ 解压失败于 token #%ld\n",et);return 1;}
    /* 逐字节比对 */
    long first=-1,diff=0;
    for(long i=0;i<d && i<n;i++) if(out[i]!=src[i]){if(first<0)first=i;diff++;}
    printf("  差异: %ld 字节，首个 @%ld\n",diff,first);
    if(first>=0){
        printf("    原始: "); for(long i=first;i<first+16&&i<n;i++) printf("%02X ",src[i]); printf("\n");
        printf("    解出: "); for(long i=first;i<first+16&&i<d;i++) printf("%02X ",out[i]); printf("\n");
    } else printf("  ★★★ 完全一致！\n");
    free(src);free(dst);free(out);
    return diff==0?0:1;
}
