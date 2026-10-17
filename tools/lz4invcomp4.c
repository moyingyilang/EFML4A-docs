/* lz4invcomp4 —— 强化版 LZ4Inv 压缩器
 *   改进: HBITS 20（1M 桶）· CHAIN 64 · lazy matching（延迟一步）
 *   用法: lz4invcomp4 <in> <out>
 *
 * LZ4Inv token 布局（与 lz4inv.c 的解压器严格对应）:
 *   token = (litC & 3) | ((mlC & 3) << 2) | (((litC>>2)&3) << 4) | (((mlC>>2)&3) << 6)
 *   litC/mlC == 15 时追加 255 扩展字节
 *   offset 大端（2 字节）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef HBITS
#define HBITS  20
#endif
#define HSIZE  (1u << HBITS)
#ifndef CHAIN
#define CHAIN  1024   /* 可 -DCHAIN=256 换取速度 */
#endif
#define MAXOFF 65535

static int32_t (*htab)[CHAIN];
static int32_t *hcnt;
static uint32_t rd32(const uint8_t *p){ return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24); }
static uint32_t hash4(uint32_t v){ return (v * 2654435761u) >> (32 - HBITS); }

static void hins(uint32_t h, int32_t pos){
    if (hcnt[h] < CHAIN) htab[h][hcnt[h]++] = pos;
    else { memmove(&htab[h][0], &htab[h][1], sizeof(int32_t)*(CHAIN-1)); htab[h][CHAIN-1] = pos; }
}

/* 在位置 ip 找最佳匹配；返回长度，outOff 写偏移 */
static long find(const uint8_t *src, long ip, long slen, long *outOff){
    uint32_t h = hash4(rd32(src + ip));
    long bestLen = 0, bestOff = 0;
    int n = hcnt[h];
    long maxL = slen - ip;
    for (int k = n - 1; k >= 0; k--){          /* 最近的优先 */
        int32_t ref = htab[h][k];
        long off = ip - ref;
        if (off <= 0 || off > MAXOFF) continue;
        if (src[ref + bestLen] != src[ip + bestLen]) continue;  /* 快速排除 */
        long ml = 0;
        while (ml < maxL && src[ref + ml] == src[ip + ml]) ml++;
        if (ml > bestLen){ bestLen = ml; bestOff = off; if (ml >= 256) break; }
    }
    *outOff = bestOff;
    return bestLen;
}

int main(int c, char **v){
    if (c < 3){ fprintf(stderr, "用法: lz4invcomp4 <in> <out>\n"); return 2; }
    FILE *f = fopen(v[1], "rb"); if (!f){ perror(v[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *src = malloc(n);
    if (fread(src, 1, n, f) != (size_t)n) return 1;
    fclose(f);

    htab = malloc(sizeof(int32_t) * (size_t)HSIZE * CHAIN);
    hcnt = calloc(HSIZE, sizeof(int32_t));
    if (!htab || !hcnt){ fprintf(stderr, "内存不足\n"); return 1; }

    long cap = n * 2 + 65536;
    uint8_t *dst = malloc(cap);
    long ip = 0, op = 0, anchor = 0;

    while (ip + 12 < n){
        long off1 = 0, len1 = find(src, ip, n, &off1);
        hins(hash4(rd32(src + ip)), (int32_t)ip);

        /* lazy matching: 如果下一个位置有更长的匹配，放弃当前匹配 */
        if (len1 >= 4 && ip + 1 + 12 < n){
            long off2 = 0, len2 = find(src, ip + 1, n, &off2);
            if (len2 > len1 + 1){
                ip++;
                continue;
            }
        }
        if (len1 < 4){ ip++; continue; }

        long litLen = ip - anchor;
        long mlCode = len1 - 4;
        long litC = litLen < 15 ? litLen : 15;
        long mlC  = mlCode < 15 ? mlCode : 15;

        if (op + 1 > cap) return 1;
        dst[op++] = (uint8_t)((litC & 3) | ((mlC & 3) << 2) |
                              (((litC >> 2) & 3) << 4) | (((mlC >> 2) & 3) << 6));
        if (litC == 15){ long r = litLen - 15; while (r >= 255){ if (op+1>cap) return 1; dst[op++]=255; r-=255; } if (op+1>cap) return 1; dst[op++]=(uint8_t)r; }
        if (op + litLen > cap) return 1;
        memcpy(dst + op, src + anchor, litLen); op += litLen;
        if (op + 2 > cap) return 1;
        dst[op++] = (uint8_t)(off1 >> 8);
        dst[op++] = (uint8_t)(off1 & 0xFF);
        if (mlC == 15){ long r = mlCode - 15; while (r >= 255){ if (op+1>cap) return 1; dst[op++]=255; r-=255; } if (op+1>cap) return 1; dst[op++]=(uint8_t)r; }

        long end = ip + len1;
        for (long t = ip + 1; t < end && t + 12 < n; t++)
            hins(hash4(rd32(src + t)), (int32_t)t);
        ip = end; anchor = ip;
    }
    /* 尾部字面量 */
    {
        long litLen = n - anchor;
        long litC = litLen < 15 ? litLen : 15;
        if (op + 1 > cap) return 1;
        dst[op++] = (uint8_t)((litC & 3) | (((litC >> 2) & 3) << 4));
        if (litC == 15){ long r = litLen - 15; while (r >= 255){ if (op+1>cap) return 1; dst[op++]=255; r-=255; } if (op+1>cap) return 1; dst[op++]=(uint8_t)r; }
        if (op + litLen > cap) return 1;
        memcpy(dst + op, src + anchor, litLen); op += litLen;
    }
    FILE *o = fopen(v[2], "wb");
    fwrite(dst, 1, op, o); fclose(o);
    printf("  v4: %ld → %ld (%.2f%%)\n", n, op, 100.0 * op / n);
    return 0;
}
