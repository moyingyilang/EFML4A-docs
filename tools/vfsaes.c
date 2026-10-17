/* vfsaes.c —— Endfield VFS 自定义 AES（参考 AnimeStudio VFSUtils.cs）
 *   特点: 标准 AES-128 结构，但 SBox 被替换 + CBC-like 模式
 *         keystream = AES_encrypt(previous)，IV 派生用 SBox+XORKey
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const uint8_t VFSAESKey[16] = {
    0x3A,0xF1,0x8C,0x47,0xB2,0x09,0x6D,0xEE,0x51,0x24,0x90,0x7C,0x18,0xD3,0xA4,0x62
};
static const uint8_t VFSAESIV[16] = {
    0xC7,0x12,0x5E,0xA9,0x04,0xDB,0x33,0x88,0xF2,0x0E,0x77,0x49,0x65,0xBA,0x1C,0x93
};
#define VFS_XOR_KEY 0xF19AB7752CDD0196ULL
static const uint8_t VFSAESSBox[256] = {
0xE4,0xB9,0x45,0x07,0x92,0x82,0x2F,0x43,0xF5,0xC9,0x22,0x25,0xA9,0x4F,0x46,0x6D,
0x4A,0x71,0x8B,0x6C,0x8C,0xEB,0xB2,0xAC,0xCF,0x0C,0x9E,0x01,0x38,0x32,0xD3,0x93,
0x98,0x63,0xDA,0x96,0xE5,0xC4,0xC3,0x6B,0x7F,0x26,0x72,0xD7,0x97,0xD5,0x80,0xBC,
0x5D,0xBB,0x55,0x67,0x10,0x73,0xB3,0x8D,0xE2,0x35,0x29,0x47,0xA8,0x60,0x3F,0xC5,
0xEF,0x68,0xEC,0xBE,0xAB,0xC6,0xB8,0x5C,0xD8,0x15,0x09,0x54,0xF3,0x7A,0x40,0xA2,
0x30,0x0A,0xDC,0x53,0xFA,0xDB,0xF1,0x78,0xDE,0xAD,0xF0,0xB5,0xC1,0x81,0x9F,0x3E,
0x83,0x90,0x31,0xF2,0xFB,0x21,0x28,0x85,0x06,0xCA,0xCD,0x1E,0xD4,0x3C,0xA0,0xC8,
0x23,0x16,0x6E,0x89,0x1D,0xE7,0xEE,0x5E,0x42,0xBD,0xCB,0x13,0x50,0xA6,0x4E,0x49,
0x58,0xDF,0x2C,0x84,0x87,0xB6,0x91,0x52,0xDD,0x19,0xF9,0x2B,0x4D,0x77,0xBA,0x04,
0xA5,0x41,0xCE,0x94,0x3D,0x5F,0xFC,0x9B,0x79,0x9A,0x7E,0x65,0x5A,0xB1,0x66,0x34,
0x56,0xA7,0x1A,0xBF,0xEA,0x7D,0x27,0x0B,0x59,0x2E,0xAE,0x14,0x33,0xC0,0x51,0x39,
0xC7,0x3A,0x2A,0x9D,0xF4,0x7C,0xCC,0xD1,0xD6,0x70,0x37,0x0E,0x75,0x02,0x1B,0xE3,
0xE9,0x48,0x0D,0x24,0x2D,0xF7,0xD2,0xB7,0xAF,0xA3,0xA1,0x64,0x7B,0xED,0xF8,0x05,
0x95,0x3B,0x74,0xFD,0x62,0xD0,0x0F,0xFF,0x4B,0xAA,0x88,0x5B,0x03,0xB4,0xE8,0x9C,
0xB0,0x17,0x1C,0x76,0x57,0xE0,0xA4,0x44,0x20,0xD9,0x8E,0x11,0x86,0x69,0x36,0xFE,
0x4C,0x6F,0x61,0x6A,0x8F,0xE1,0x18,0x8A,0x12,0x99,0xE6,0x1F,0x00,0x08,0xF6,0xC2,
};

/* ---- AES 基础 ---- */
static uint8_t xtime(uint8_t a){ return (a&0x80)? (uint8_t)((a<<1)^0x1B) : (uint8_t)(a<<1); }

/* 列混淆（参考实现的 MixSingleColumn） */
static void mix_col(uint8_t*a){
    uint8_t t = a[0]^a[1]^a[2]^a[3];
    uint8_t u = a[0];
    a[0] ^= (uint8_t)(t ^ xtime((uint8_t)(a[0]^a[1])));
    a[1] ^= (uint8_t)(t ^ xtime((uint8_t)(a[1]^a[2])));
    a[2] ^= (uint8_t)(t ^ xtime((uint8_t)(a[2]^a[3])));
    a[3] ^= (uint8_t)(t ^ xtime((uint8_t)(a[3]^u)));
}
/* 密钥扩展: 标准 AES-128，用自定义 SBox */
static void expand_key(uint8_t rk[11][16]){
    static const uint8_t rcon[11]={0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36};
    uint8_t w[44][4];
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) w[i][j]=VFSAESKey[i*4+j];
    for(int i=4;i<44;i++){
        uint8_t t[4]; memcpy(t,w[i-1],4);
        if(i%4==0){
            uint8_t f=t[0]; t[0]=t[1]; t[1]=t[2]; t[2]=t[3]; t[3]=f;
            for(int k=0;k<4;k++) t[k]=VFSAESSBox[t[k]];
            t[0]^=rcon[i/4];
        }
        for(int k=0;k<4;k++) w[i][k]=w[i-4][k]^t[k];
    }
    for(int r=0;r<11;r++) for(int c=0;c<4;c++) for(int j=0;j<4;j++) rk[r][c*4+j]=w[r*4+c][j];
}
/* AES 加密一个块（用自定义 SBox） */
static void aes_encrypt_block(const uint8_t rk[11][16], const uint8_t in[16], uint8_t out[16]){
    uint8_t s[16]; memcpy(s,in,16);
    for(int i=0;i<16;i++) s[i]^=rk[0][i];
    for(int r=1;r<10;r++){
        for(int i=0;i<16;i++) s[i]=VFSAESSBox[s[i]];          /* SubBytes */
        /* ShiftRows（按参考实现的列置换） */
        uint8_t t[16]; memcpy(t,s,16);
        /* 参考: s[col][row] 视为 4x4 列主序
           列1: rows 轮转; 列2: 轮转2; 列3: 轮转3 */
        for(int c=0;c<4;c++){
            for(int r=0;r<4;r++){
                int src=(r+c)%4;
                s[r*4+c] = t[src*4+c];
            }
        }
        for(int c=0;c<4;c++) mix_col(s+c*4);                   /* MixColumns */
        for(int i=0;i<16;i++) s[i]^=rk[r][i];                  /* AddRoundKey */
    }
    for(int i=0;i<16;i++) s[i]=VFSAESSBox[s[i]];
    {
        uint8_t t[16]; memcpy(t,s,16);
        for(int r=0;r<4;r++) for(int c=0;c<4;c++){ int src=(r+c)%4; s[r*4+c]=t[src*4+c]; }
    }
    for(int i=0;i<16;i++) s[i]^=rk[10][i];
    memcpy(out,s,16);
}
/* VFS 解密（CBC-like） */
int vfs_decrypt(uint8_t* buf, size_t n, int verbose){
    uint8_t rk[11][16]; expand_key(rk);
    uint8_t prev[16]; memcpy(prev,VFSAESIV,16);
    size_t blocks=(n+15)/16;
    uint8_t ks[16];
    for(size_t bi=0;bi<blocks;bi++){
        aes_encrypt_block(rk, prev, ks);          /* keystream = E(prev) */
        size_t off=bi*16, len=(n-off<16)?(n-off):16;
        for(size_t i=0;i<len;i++) buf[off+i]^=ks[i];
        /* 派生下一个 IV */
        uint8_t nxt[16]; int count=0;
        for(int i=0;i<16;i++){
            uint64_t shiftSrc = VFS_XOR_KEY >> (count & 0x38);
            uint8_t temp = (uint8_t)(ks[i] ^ (31*i) ^ (uint8_t)shiftSrc);
            count += 8;
            temp = (uint8_t)(((temp>>5)|(8*temp)) & 0xFF);
            nxt[i] = VFSAESSBox[temp];
        }
        memcpy(prev,nxt,16);
    }
    if(verbose) printf("    解密 %zu 字节\n", n);
    return 0;
}

/* ---- 精确的 VFS DecryptBlock（参考实现: 稀疏解密） ----
 * buffer <= 256: 整体解密
 * buffer >  256: 每个 16 字节块取前 step 字节 → 256 字节 → 解密 → 写回
 */
int vfs_decrypt_block(uint8_t* buf, size_t len){
    uint8_t rk[11][16]; expand_key(rk);
    if(len <= 256){
        /* 整体解密（单块或小缓冲） */
        uint8_t prev[16]; memcpy(prev,VFSAESIV,16);
        uint8_t ks[16];
        size_t blocks_=(len+15)/16;
        for(size_t bi=0;bi<blocks_;bi++){
            aes_encrypt_block(rk,prev,ks);
            size_t off=bi*16, l=(len-off<16)?(len-off):16;
            for(size_t i=0;i<l;i++) buf[off+i]^=ks[i];
            uint8_t nxt[16]; int count=0;
            for(int i=0;i<16;i++){
                uint64_t sh=VFS_XOR_KEY>>(count&0x38);
                uint8_t t=(uint8_t)(ks[i]^(31*i)^(uint8_t)sh);
                count+=8; t=(uint8_t)(((t>>5)|(8*t))&0xFF);
                nxt[i]=VFSAESSBox[t];
            }
            memcpy(prev,nxt,16);
        }
        return 0;
    }
    size_t numBlocksFloor = len/16;
    size_t step = 256/numBlocksFloor;
    if(numBlocksFloor > 256) step = 1;
    uint8_t dec[256];
    size_t lim = numBlocksFloor<256? numBlocksFloor : 256;
    for(size_t i=0;i<lim;i++)
        for(size_t k=0;k<step;k++) dec[i*step+k]=buf[i*16+k];
    /* 对这 256 字节整体解密 */
    {
        uint8_t prev[16]; memcpy(prev,VFSAESIV,16);
        uint8_t ks[16];
        for(size_t bi=0;bi<16;bi++){
            aes_encrypt_block(rk,prev,ks);
            size_t off=bi*16;
            for(size_t i=0;i<16 && off+i<256;i++) dec[off+i]^=ks[i];
            uint8_t nxt[16]; int count=0;
            for(int i=0;i<16;i++){
                uint64_t sh=VFS_XOR_KEY>>(count&0x38);
                uint8_t t=(uint8_t)(ks[i]^(31*i)^(uint8_t)sh);
                count+=8; t=(uint8_t)(((t>>5)|(8*t))&0xFF);
                nxt[i]=VFSAESSBox[t];
            }
            memcpy(prev,nxt,16);
        }
    }
    for(size_t i=0;i<lim;i++)
        for(size_t k=0;k<step;k++) buf[i*16+k]=dec[i*step+k];
    return 0;
}

/* 调试用：打印 E(IV) */
void vfsaes_dump_first_ks(void){
    uint8_t rk[11][16]; expand_key(rk);
    uint8_t ks[16];
    aes_encrypt_block(rk, VFSAESIV, ks);
    printf("  C  E(IV) = ");
    for(int i=0;i<16;i++) printf("%02X ", ks[i]);
    printf("\n");
}

/* 调试用：打印轮密钥 */
void vfsaes_dump_rk(void){
    uint8_t rk[11][16]; expand_key(rk);
    for(int r=0;r<3;r++){
        printf("  C  rk[%d] = ", r);
        for(int i=0;i<16;i++) printf("%02X ", rk[r][i]);
        printf("\n");
    }
}
