/* lz4inv —— Unity 的 LZ4 变体（与标准 LZ4 有两处差异）
 *   参考: AnimeStudio/LZ4/LZ4Inv.cs
 *   ① token 隔位解析: lit = val & 0b00110011, enc = (val & 0b11001100) >> 2
 *      长度 = (x & 0b11) | (x >> 2)
 *   ② offset 大端: off = p[0] << 8 | p[1]
 *   用法: lz4inv <in> <out> [maxOut]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static long lz4inv(const uint8_t*in, long inLen, uint8_t*out, long outCap){
    const uint8_t*ip=in, *iend=in+inLen;
    uint8_t*op=out, *oend=out+outCap;
    while(ip<iend){
        uint8_t val=*ip++;
        uint8_t lit = val & 0x33;              /* 0b00110011 */
        uint8_t enc = (val & 0xCC) >> 2;       /* 0b11001100 >> 2 */
        long litLen = (lit & 0x03) | (lit >> 2);
        long matchLen = (enc & 0x03) | (enc >> 2);
        /* 扩展字面量长度 */
        if(litLen == 15){
            uint8_t s;
            do { if(ip>=iend) return -1; s=*ip++; litLen+=s; } while(s==255);
        }
        if(litLen > (iend-ip)) return -1;
        if(op+litLen > oend) return -1;
        memcpy(op,ip,litLen); ip+=litLen; op+=litLen;
        if(ip >= iend) break;
        if(ip+2 > iend) return -1;
        /* ★ offset 大端 */
        long offset = (ip[0]<<8) | ip[1];
        ip += 2;
        if(offset==0 || offset > (op-out)) return -1;
        if(matchLen == 15){
            uint8_t s;
            do { if(ip>=iend) return -1; s=*ip++; matchLen+=s; } while(s==255);
        }
        matchLen += 4;
        if(op+matchLen > oend) return -1;
        { const uint8_t*mp=op-offset; for(long i=0;i<matchLen;i++) *op++=*mp++; }
    }
    return op-out;
}
int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,"用法: lz4inv <in> <out> [maxOut]\n"); return 2; }
    long cap = argc>3? strtol(argv[3],0,0) : (16L*1024*1024);
    FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*in=malloc(n); if(fread(in,1,n,f)!=(size_t)n) return 1; fclose(f);
    uint8_t*out=malloc(cap);
    long o=lz4inv(in,n,out,cap);
    printf("  LZ4Inv: 输入 %ld ⇒ 输出 %ld 字节\n", n, o);
    if(o>0){
        printf("  前 32 字节: ");
        for(long i=0;i<32&&i<o;i++) printf("%02x ", out[i]);
        printf("\n");
        if(memcmp(out,"UnityFS",7)==0) printf("  ★★★ 发现 UnityFS 魔数！\n");
        printf("  前 200 字节可读: ");
        for(long i=0;i<o&&i<200;i++){ putchar((out[i]>=0x20&&out[i]<0x7f)?out[i]:'.'); }
        printf("\n");
        FILE*g=fopen(argv[2],"wb"); fwrite(out,1,o,g); fclose(g);
        printf("  ✓ 已写出 %s\n", argv[2]);
    } else printf("  ✗ 解压失败\n");
    free(in); free(out);
    return o>0?0:1;
}
