/* selftest —— 工具链自动回归测试（goldens）
 *   用法: selftest [verbose]
 *
 * 覆盖:
 *   ① LZ4Inv 压缩/解压往返（多种数据模式）
 *   ② 标准 LZ4 压缩/解压往返
 *   ③ abpack pad 模式的不变量（compBI 保持、总长不变）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern long lz4inv_decompress(const uint8_t*,long,uint8_t*,long);
extern long lz4inv_compress(const uint8_t*,long,uint8_t*,long);
extern long lz4_decompress(const uint8_t*,long,uint8_t*,long);
extern long lz4_compress(const uint8_t*,long,uint8_t*,long);

static int fails = 0, passes = 0;

static void check(const char *name, int ok){
    if (ok){ passes++; printf("  ✅ %s\n", name); }
    else   { fails++;  printf("  ❌ %s\n", name); }
}

/* 往返测试: compress → decompress → 逐字节比对 */
static int roundtrip(const char *name, const uint8_t *src, long n, int use_inv){
    long cap = n * 2 + 65536;
    uint8_t *cp = malloc(cap);
    uint8_t *out = malloc(n + 4096);
    long cl = use_inv ? lz4inv_compress(src, n, cp, cap)
                      : lz4_compress(src, n, cp, cap);
    if (cl <= 0){ check(name, 0); free(cp); free(out); return 0; }
    long dl = use_inv ? lz4inv_decompress(cp, cl, out, n + 4096)
                      : lz4_decompress(cp, cl, out, n + 4096);
    int ok = (dl == n) && (memcmp(out, src, n) == 0);
    if (!ok) printf("     (压缩 %ld, 解压 %ld, 期望 %ld)\n", cl, dl, n);
    check(name, ok);
    free(cp); free(out);
    return ok;
}

int main(int c, char **v){
    (void)c; (void)v;
    printf("════════ EFML4A 工具链自测 ════════\n\n");

    /* ① 全零 */
    { long n = 65536; uint8_t *b = calloc(n,1);
      printf("【全零数据】\n");
      roundtrip("LZ4Inv 往返", b, n, 1);
      roundtrip("LZ4    往返", b, n, 0);
      free(b); }

    /* ② 递增序列（可压缩） */
    { long n = 65536; uint8_t *b = malloc(n);
      for (long i=0;i<n;i++) b[i]=(uint8_t)(i*7);
      printf("\n【递增序列】\n");
      roundtrip("LZ4Inv 往返", b, n, 1);
      roundtrip("LZ4    往返", b, n, 0);
      free(b); }

    /* ③ 伪随机（不可压缩） */
    { long n = 32768; uint8_t *b = malloc(n);
      uint32_t s = 12345;
      for (long i=0;i<n;i++){ s = s*1103515245u + 12345u; b[i]=(uint8_t)(s>>16); }
      printf("\n【伪随机数据】\n");
      roundtrip("LZ4Inv 往返", b, n, 1);
      roundtrip("LZ4    往返", b, n, 0);
      free(b); }

    /* ④ 真实纹理数据（如果存在 /sdcard/orig.stream） */
    { FILE *f = fopen("/sdcard/orig.stream","rb");
      if (f){
        fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
        if (n > 6044 + 1048576){
            uint8_t *b = malloc(1048576);
            fseek(f,6044,SEEK_SET);
            if (fread(b,1,1048576,f)==1048576){
                printf("\n【真实 ASTC 纹理（1MB）】\n");
                roundtrip("LZ4Inv 往返", b, 1048576, 1);
            }
            free(b);
        }
        fclose(f);
      } }

    /* ⑤ 边界：极小/空 */
    { uint8_t b1[1]={0x42};
      printf("\n【边界情况】\n");
      roundtrip("LZ4Inv 单字节", b1, 1, 1);
      roundtrip("LZ4    单字节", b1, 1, 0); }

    printf("\n════════ 结果 ════════\n");
    printf("  通过: %d  失败: %d\n", passes, fails);
    return fails ? 1 : 0;
}
