/* lz4invcomp —— LZ4Inv 压缩器（与 lz4inv.c 的解压器配对）
 *   参考: AnimeStudio/LZ4/LZ4Inv.cs
 *   ① token 隔位: lit = val & 0b00110011, enc = (val & 0b11001100) >> 2
 *      写 token: val = (litEnc<<?) ... 需按位交错
 *   ② offset 大端: off = p[0] << 8 | p[1]
 *
 *   token 位交错（写入端）:
 *     litLen(低2位) 放 bit0,1  → lit 的 bit0,1
 *     matchLen(低2位) 放 bit2,3 → enc 的 bit0,1（整体在 bit2,3）
 *     litLen(高2位) 放 bit4,5
 *     matchLen(高2位) 放 bit6,7
 *   ⇒ val = (litLen&3) | ((matchLen&3)<<2) | (((litLen>>2)&3)<<4) | (((matchLen>>2)&3)<<6)
 *
 *   用法: lz4invcomp <in> <out> [minMatch=4]
 *   注意: 长度 15 时用扩展字节；matchLen 存储的是 (ml-4)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define HASH_BITS 15
#define HASH_SIZE (1<<HASH_BITS)
static int32_t htab[HASH_SIZE];

static uint32_t rd32(const uint8_t*p){return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t hash4(uint32_t v){ return (v*2654435761u)>>(32-HASH_BITS); }

static long lz4inv_compress(const uint8_t*src, long slen, uint8_t*dst, long dcap){
    long ip=0, op=0;
    for(int i=0;i<HASH_SIZE;i++) htab[i]=-1;
    if(slen<13){ /* 太短: 全字面量 */
        goto literal_tail;
    }
    long anchor=0;
    while(ip < slen-12){
        uint32_t v=rd32(src+ip);
        uint32_t h=hash4(v);
        int32_t ref=htab[h];
        htab[h]=ip;
        if(ref<0 || ip-ref>65535 || memcmp(src+ref,src+ip,4)!=0){ ip++; continue; }
        /* 找到匹配 */
        long litLen = ip-anchor;
        long mlen=4;
        while(ip+mlen<slen && src[ref+mlen]==src[ip+mlen] && mlen<65535+4) mlen++;
        long off=ip-ref;
        long mlCode=mlen-4;
        /* 写 token */
        long litC = litLen<15?litLen:15;
        long mlC  = mlCode<15?mlCode:15;
        uint8_t tok = (uint8_t)((litC&3) | ((mlC&3)<<2) | (((litC>>2)&3)<<4) | (((mlC>>2)&3)<<6));
        if(op+1>dcap) return -1;
        dst[op++]=tok;
        /* 字面量长度扩展 */
        if(litC==15){ long r=litLen-15; while(r>=255){ if(op+1>dcap)return -1; dst[op++]=255; r-=255; } if(op+1>dcap)return -1; dst[op++]=(uint8_t)r; }
        /* 字面量 */
        if(op+litLen>dcap) return -1;
        memcpy(dst+op,src+anchor,litLen); op+=litLen;
        /* offset 大端 */
        if(op+2>dcap) return -1;
        dst[op++]=(uint8_t)(off>>8); dst[op++]=(uint8_t)(off&0xFF);
        /* 匹配长度扩展 */
        if(mlC==15){ long r=mlCode-15; while(r>=255){ if(op+1>dcap)return -1; dst[op++]=255; r-=255; } if(op+1>dcap)return -1; dst[op++]=(uint8_t)r; }
        ip+=mlen; anchor=ip;
    }
literal_tail:
    {
        long litLen=slen-anchor;
        long litC = litLen<15?litLen:15;
        uint8_t tok=(uint8_t)((litC&3) | (((litC>>2)&3)<<4));
        if(op+1>dcap) return -1;
        dst[op++]=tok;
        if(litC==15){ long r=litLen-15; while(r>=255){ if(op+1>dcap)return -1; dst[op++]=255; r-=255; } if(op+1>dcap)return -1; dst[op++]=(uint8_t)r; }
        if(op+litLen>dcap) return -1;
        memcpy(dst+op,src+anchor,litLen); op+=litLen;
    }
    return op;
}

/* 自测：压缩后立即解压，验证一致 */
static long lz4inv_decompress(const uint8_t*in,long inLen,uint8_t*out,long cap){
    const uint8_t*ip=in,*iend=in+inLen; uint8_t*op=out,*oend=out+cap;
    while(ip<iend){
        uint8_t tk=*ip++;
        uint8_t lit=tk&0x33, enc=(tk&0xCC)>>2;
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
    if(c<3){ fprintf(stderr,"用法: lz4invcomp <in> <out>\n"); return 2; }
    FILE*f=fopen(v[1],"rb"); if(!f){perror(v[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*src=malloc(n); if(fread(src,1,n,f)!=(size_t)n) return 1; fclose(f);
    long cap=n*2+1024; uint8_t*dst=malloc(cap);
    long o=lz4inv_compress(src,n,dst,cap);
    printf("  LZ4Inv 压缩: %ld → %ld 字节 (%.1f%%)\n", n, o, 100.0*o/n);
    if(o>0){
        uint8_t*chk=malloc(n+64);
        long d=lz4inv_decompress(dst,o,chk,n+64);
        printf("  自测解压: %ld 字节 %s\n", d, (d==n&&!memcmp(src,chk,n))?"★ 完全一致":"✗ 不一致");
        FILE*g=fopen(v[2],"wb"); fwrite(dst,1,o,g); fclose(g);
        printf("  ✓ 已写出 %s\n", v[2]);
        free(chk);
    }
    free(src); free(dst);
    return o>0?0:1;
}
