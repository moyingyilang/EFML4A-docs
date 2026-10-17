/* vfsdec —— Endfield VFS (.chk = 混淆 UnityFS) 解析器
 *   参考: AnimeStudio/Crypto/VFSUtils.cs + CryptoHelper.cs (公开)
 *   变体: ArknightsEndfield (1.6.4 实测校验通过)
 *   用法: vfsdec <in.chk> [--blocks] [--nodes]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint32_t ror32(uint32_t x,int n){n&=31;return n?((x>>n)|(x<<(32-n))):x;}
static uint32_t rol32(uint32_t x,int n){n&=31;return n?((x<<n)|(x>>(32-n))):x;}
static uint64_t ror64(uint64_t x,int n){n&=63;return n?((x>>n)|(x<<(64-n))):x;}
static uint64_t rol64(uint64_t x,int n){n&=63;return n?((x<<n)|(x>>(64-n))):x;}
static uint32_t be32(const uint8_t*p){return ((uint32_t)p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];}
static uint16_t be16(const uint8_t*p){return (p[0]<<8)|p[1];}
static uint32_t le32(const uint8_t*p){return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}

typedef struct { uint16_t flags; uint32_t uncomp, comp; } Block;

int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"用法: vfsdec <in.chk> [--blocks] [--nodes]\n"); return 2; }
    FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*b=malloc(n); if(fread(b,1,n,f)!=(size_t)n) return 1; fclose(f);
    printf("  文件 %ld 字节\n", n);

    /* 头部校验（变体1） */
    uint32_t a=be32(b), bh=be32(b+4);
    uint32_t hc1=(4*(a^0x4A92F0CD))&0xFFFF0000;
    uint32_t hc2=ror32(a^0x4A92F0CD,14);
    uint32_t hc3=hc1^hc2^0xD8B1E637;
    if(bh!=hc3){ printf("  ✗ 头部校验失败 (期望 0x%08x 实际 0x%08x)\n",hc3,bh); return 1; }
    printf("  ★ 头部校验通过（变体1 ArknightsEndfield）\n");

    long p=8;
    uint16_t compBI2=be16(b+p); p+=2;
    uint32_t flags2=be32(b+p);  p+=4;
    uint32_t encFlags=be32(b+p);p+=4;
    uint32_t size2=be32(b+p);   p+=4;
    uint32_t flags1=be32(b+p);  p+=4;
    uint16_t uncompBI1=be16(b+p);p+=2;
    uint32_t unk=be32(b+p);     p+=4;
    uint16_t uncompBI2=be16(b+p);p+=2;
    uint32_t size1=be32(b+p);   p+=4;
    uint16_t compBI1=be16(b+p); p+=2;
    uint8_t  unk2=b[p];         p+=1;
    (void)unk;(void)unk2;

    uint32_t compBI=((uint32_t)(compBI1^compBI2^0xA121)<<16)|compBI2;
    compBI=ror32(compBI,18)^0xF74324EE;
    uint32_t uncompBI=((uint32_t)(uncompBI1^uncompBI2^0xA121)<<16)|uncompBI2;
    uncompBI=ror32(uncompBI,18)^0xF74324EE;
    uint64_t sz=((uint64_t)(size1^size2^0xDAD76848)<<32)|size2;
    sz=rol64(sz,14)^0xA4F1A11747816520ULL;    /* ← 实测用 rol64(14) */
    uint32_t flags=flags1^flags2^0xA7F49310;
    uint32_t enc=encFlags^flags2;
    printf("  compressedBlocksInfoSize   = %u\n", compBI);
    printf("  uncompressedBlocksInfoSize = %u\n", uncompBI);
    printf("  size                       = %llu\n", (unsigned long long)sz);
    printf("  flags                      = 0x%08x\n", flags);
    printf("  encFlags                   = 0x%08x\n", enc);

    /* 块表：compressed blocks info 紧跟在头部之后(从 p 开始，可能对齐) */
    long bp = p;
    printf("\n  --- 块表 (从 @%ld 开始) ---\n", bp);
    /* encCount 小端读 ^ 0x8A7BF723，然后 ReverseEndianness */
    uint32_t raw = le32(b+bp);
    uint32_t ec = __builtin_bswap32(raw ^ 0x8A7BF723);
    uint16_t lo=(uint16_t)ec, hi=(uint16_t)(ec>>16);
    uint32_t blocksCount = ((uint32_t)(lo^hi)<<16)|lo;
    blocksCount = ror32(blocksCount,18) ^ 0x91CE0A4F;
    printf("    原始 @%ld = 0x%08x  ec=0x%08x  blocksCount=%u\n", bp, raw, ec, blocksCount);
    if(blocksCount>4096){ printf("    ✗ blocksCount 不合理\n"); return 1; }
    bp += 4;
    Block*bl=malloc(sizeof(Block)*(blocksCount?blocksCount:1));
    for(uint32_t i=0;i<blocksCount;i++){
        uint16_t A=be16(b+bp), B=be16(b+bp+2), C=be16(b+bp+4);
        uint16_t eF=be16(b+bp+6)^0x9CD6;
        uint16_t D=be16(b+bp+8);
        bp+=10;
        uint8_t a0=eF&0xFF, a1=eF>>8;
        uint16_t fl=((uint16_t)(a0^a1)<<8)|a0;
        fl = C ^ rol32(fl,14) ^ 0x523F;
        uint32_t un=((uint32_t)(A^C^0xA121)<<16)|C; un=ror32(un,18)^0xF74324EE;
        uint32_t cp=((uint32_t)(B^D^0xA121)<<16)|D; cp=ror32(cp,18)^0xF74324EE;
        bl[i].flags=fl; bl[i].uncomp=un; bl[i].comp=cp;
        printf("    块%u: flags=0x%04x uncomp=%u comp=%u\n", i, fl, un, cp);
    }
    printf("\n  块表结束 @%ld\n", bp);
    free(bl); free(b);
    return 0;
}
