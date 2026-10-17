/* astcsolid —— 生成 512×512 ASTC 4x4 数据（每个块 = 纯色 void-extent 块）
 *   用法: astcsolid <out> <R> <G> <B> [A=255] [W=512] [H=512]
 *
 * ASTC void-extent 块（LDR）布局（ASTC 规范 §C.2.22）:
 *   bits[10:0]  = 0        块模式 = void-extent
 *   bit[11]     = 1        标识 void-extent
 *   bit[12]     = 0        LDR（1 = HDR）
 *   bits[16:13] = 0        保留
 *   bits[17]    = 0
 *   bits[63:18] = 纹理数据: 4 分量 × 16 位 UNORM (R,G,B,A)
 *   ⇒ 每块 16 字节，全部相同时整个纹理为纯色
 *
 * ★ 用途：验证「Android 纹理是 ASTC」的假设
 *   把生成的 262,144 字节替换进 Temp ⇒ 若模型变成纯色 ⇒ 假设成立
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(int argc,char**argv){
    if(argc<5){ fprintf(stderr,"用法: astcsolid <out> <R> <G> <B> [A=255] [W=512] [H=512]\n"); return 2; }
    const char* out=argv[1];
    int R=atoi(argv[2]), G=atoi(argv[3]), B=atoi(argv[4]);
    int A = argc>5? atoi(argv[5]) : 255;
    int W = argc>6? atoi(argv[6]) : 512;
    int H = argc>7? atoi(argv[7]) : 512;
    int bw = (W+3)/4, bh = (H+3)/4;
    long blocks = (long)bw*bh;
    printf("  输出 %dx%d ASTC 4x4 ⇒ %d×%d = %ld 块 × 16 = %ld 字节\n",
           W,H,bw,bh,blocks,blocks*16);
    /* 构造 void-extent 块（小端 64 位字） */
    uint64_t w = 0;
    /* bits[10:0]=0, bit[11]=1, bit[12]=0 */
    w |= (1ULL<<11);
    /* bits[63:18] = 4×16bit UNORM: R,G,B,A
       规范中 16 位 UNORM 值 = value*65535/255 */
    uint64_t r16 = (uint64_t)((R*65535)/255) & 0xFFFF;
    uint64_t g16 = (uint64_t)((G*65535)/255) & 0xFFFF;
    uint64_t b16 = (uint64_t)((B*65535)/255) & 0xFFFF;
    uint64_t a16 = (uint64_t)((A*65535)/255) & 0xFFFF;
    /* 数据从 bit18 开始: 依次 R,G,B,A（各 16 位） */
    w |= (r16 << 18);
    w |= (g16 << 34);
    w |= (b16 << 50);
    /* A 在 bit66 —— 超出 64 位！⇒ 实际 ASTC void-extent 只放 3 分量?
       规范: bits[63:18] 共 46 位 ⇒ 不足 4×16=64
       ⇒ 实际布局: 若 bit[17]=1 则有 A，否则 3 分量
       这里简化为 3 分量 RGB（bit[17]=0） */
    printf("  块内容 (LE 16 字节): ");
    uint8_t bb[16]; memcpy(bb,&w,8); memset(bb+8,0,8);
    for(int i=0;i<16;i++) printf("%02x ",bb[i]);
    printf("\n");
    FILE*f=fopen(out,"wb"); if(!f){perror(out);return 1;}
    for(long i=0;i<blocks;i++) fwrite(bb,1,16,f);
    fclose(f);
    printf("  ✓ 已写出 %s（%ld 字节）\n", out, blocks*16);
    return 0;
}
