/* ── 密钥材料不随仓库分发 ────────────────────────────────────────────
 * `.blc` 用 ChaCha20 加密，密钥属于游戏运行时材料，本项目【不提供】。
 * 若要复现解析流程，请自行通过环境变量 VFS_KEY 传入 64 位十六进制。
 *
 * 用法: 在 main() 中先调用 load_key()，失败时直接返回。
 */
#ifndef EFML_VFS_KEY_H
#define EFML_VFS_KEY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t KEY[32];

static int key_hexv(int c){
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int load_key(void){
    const char *h = getenv("VFS_KEY");
    if(!h || strlen(h) != 64){
        fprintf(stderr, "缺少环境变量 VFS_KEY（64 位十六进制）\n");
        return 1;
    }
    for(int i = 0; i < 32; i++){
        int a = key_hexv((unsigned char)h[i * 2]);
        int b = key_hexv((unsigned char)h[i * 2 + 1]);
        if(a < 0 || b < 0){
            fprintf(stderr, "VFS_KEY 含非法字符\n");
            return 1;
        }
        KEY[i] = (uint8_t)((a << 4) | b);
    }
    return 0;
}

#endif /* EFML_VFS_KEY_H */
