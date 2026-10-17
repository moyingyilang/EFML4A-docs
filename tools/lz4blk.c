/* lz4blk —— 原始 LZ4 块解压（不带 frame 头）
 *   用法: lz4blk <in> <out> [maxOut]
 *   格式: token(1B 高4=字面量长度,低4=匹配长度) + 字面量 + offset(2B LE) + 匹配
 *         长度 15 时接扩展字节（每个 255 累加，直到 <255）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static long lz4_block(const uint8_t* in, long inLen, uint8_t* out, long outCap){
    const uint8_t* ip = in;
    const uint8_t* iend = in + inLen;
    uint8_t* op = out;
    uint8_t* oend = out + outCap;
    while(ip < iend){
        uint8_t token = *ip++;
        long litLen = token >> 4;
        if(litLen == 15){
            uint8_t s;
            do { if(ip>=iend) goto done; s=*ip++; litLen+=s; } while(s==255);
        }
        if(litLen > (iend-ip)) goto done;
        if(op+litLen > oend) goto done;
        memcpy(op, ip, litLen); ip += litLen; op += litLen;
        if(ip >= iend) break;                     /* 最后一段字面量后结束 */
        if(ip+2 > iend) goto done;
        long offset = ip[0] | (ip[1]<<8);
        ip += 2;
        if(offset == 0 || offset > (op-out)) goto done;
        long matchLen = token & 0x0F;
        if(matchLen == 15){
            uint8_t s;
            do { if(ip>=iend) goto done; s=*ip++; matchLen+=s; } while(s==255);
        }
        matchLen += 4;
        if(op+matchLen > oend) goto done;
        {
            const uint8_t* mp = op - offset;
            for(long i=0;i<matchLen;i++) *op++ = *mp++;
        }
    }
done:
    return op - out;
}
int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,"用法: lz4blk <in> <out> [maxOut=16MB]\n"); return 2; }
    long cap = argc>3? strtol(argv[3],0,0) : 16L*1024*1024;
    FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*in=malloc(n); if(fread(in,1,n,f)!=(size_t)n) return 1; fclose(f);
    uint8_t*out=malloc(cap);
    long o = lz4_block(in,n,out,cap);
    printf("  输入 %ld 字节 ⇒ 解压 %ld 字节\n", n, o);
    if(o>0){
        printf("  前 32 字节: ");
        for(long i=0;i<32&&i<o;i++) printf("%02x ", out[i]);
        printf("\n");
        FILE*g=fopen(argv[2],"wb"); fwrite(out,1,o,g); fclose(g);
        printf("  ✓ 已写出 %s\n", argv[2]);
    } else {
        printf("  ✗ 解压失败（数据格式不符）\n");
    }
    free(in); free(out);
    return o>0?0:1;
}
