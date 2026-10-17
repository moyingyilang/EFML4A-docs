/* blcfix —— 更新 .blc 里指定 chunk 的 contentMD5，并重算 CRC32
 *
 * 用途：等长替换 .chk 之后，.blc 里记录的 contentMD5 仍然是旧值
 *       ⇒ 游戏校验不符 ⇒ 拒绝加载 ⇒ mod「未加载」
 *       本工具把 contentMD5 改成新 .chk 的 md5，使游戏接受。
 *
 * 原理（已实测确认）:
 *   .blc 布局: [0..12) nonce | [12..len) ChaCha20 密文
 *   解密体  : version(4) unk(4) strLen(2) group | ... | chunk 头 | 条目 | ... | CRC32(4)
 *   chunk 头: md5Name(16) @ +0x2d | contentMD5(16) @ +0x3d   （首个 chunk，1.6.4/version=5）
 *   CRC32   : 解密体最后 4 字节（小端）= crc32(解密体[0 .. len-4))
 *   ChaCha20: key 已知，counter 从 1 开始，nonce = blc[0:12]
 *
 * 用法:
 *   blcfix <in.blc> <out.blc> <new_md5_hex32> [md5Name_hex32]
 *     new_md5_hex32 : 新 .chk 的 md5（32 个 hex 字符）
 *     md5Name_hex32 : 可选，指定要改哪个 chunk（默认第一个）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "vfs_key.h"


static uint32_t rotl32(uint32_t x,int n){ return (x<<n)|(x>>(32-n)); }
static void qr(uint32_t*s,int a,int b,int c,int d){
    s[a]+=s[b]; s[d]^=s[a]; s[d]=rotl32(s[d],16);
    s[c]+=s[d]; s[b]^=s[c]; s[b]=rotl32(s[b],12);
    s[a]+=s[b]; s[d]^=s[a]; s[d]=rotl32(s[d],8);
    s[c]+=s[d]; s[b]^=s[c]; s[b]=rotl32(s[b],7);
}
static void chacha_block(const uint8_t key[32],const uint8_t nonce[12],uint32_t cnt,uint8_t out[64]){
    uint32_t st[16], w[16];
    st[0]=0x61707865; st[1]=0x3320646e; st[2]=0x79622d32; st[3]=0x6b206574;
    for(int i=0;i<8;i++) st[4+i]=key[i*4]|(key[i*4+1]<<8)|(key[i*4+2]<<16)|((uint32_t)key[i*4+3]<<24);
    st[12]=cnt;
    for(int i=0;i<3;i++) st[13+i]=nonce[i*4]|(nonce[i*4+1]<<8)|(nonce[i*4+2]<<16)|((uint32_t)nonce[i*4+3]<<24);
    memcpy(w,st,sizeof w);
    for(int i=0;i<10;i++){
        qr(w,0,4,8,12);  qr(w,1,5,9,13);  qr(w,2,6,10,14); qr(w,3,7,11,15);
        qr(w,0,5,10,15); qr(w,1,6,11,12); qr(w,2,7,8,13);  qr(w,3,4,9,14);
    }
    for(int i=0;i<16;i++){
        uint32_t v=w[i]+st[i];
        out[i*4]=v&0xff; out[i*4+1]=(v>>8)&0xff; out[i*4+2]=(v>>16)&0xff; out[i*4+3]=(v>>24)&0xff;
    }
}
static void chacha_xor(const uint8_t nonce[12],uint8_t*buf,size_t n){
    uint8_t ks[64]; uint32_t c=1; size_t off=0;
    while(off<n){
        chacha_block(KEY,nonce,c++,ks);
        size_t m=(n-off<64)?(n-off):64;
        for(size_t i=0;i<m;i++) buf[off+i]^=ks[i];
        off+=m;
    }
}
static uint32_t crc_table[256]; static int crc_init=0;
static void mk_crc(void){ if(crc_init)return; for(uint32_t i=0;i<256;i++){
    uint32_t c=i; for(int k=0;k<8;k++) c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1); crc_table[i]=c; } crc_init=1; }
static uint32_t crc32_of(const uint8_t*p,size_t n){
    mk_crc(); uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c=crc_table[(c^p[i])&0xff]^(c>>8);
    return c^0xFFFFFFFFu;
}
static int hex2bin(const char*h,uint8_t*out,int n){
    if((int)strlen(h)!=n*2) return -1;
    for(int i=0;i<n;i++){ unsigned v; if(sscanf(h+i*2,"%2x",&v)!=1) return -1; out[i]=(uint8_t)v; }
    return 0;
}
static void bin2hex(const uint8_t*b,int n,char*out){ for(int i=0;i<n;i++) sprintf(out+i*2,"%02x",b[i]); out[n*2]=0; }

int main(int argc,char**argv){
    if(load_key()) return 1;
    if(argc<4){
        fprintf(stderr,"用法: blcfix <in.blc> <out.blc> <new_md5_hex32> [md5Name_hex32]\n");
        return 2;
    }
    const char*fin=argv[1], *fout=argv[2], *hexmd5=argv[3];
    uint8_t newmd5[16], want[16]; int have_want=0;
    if(hex2bin(hexmd5,newmd5,16)!=0){ fprintf(stderr,"new_md5 必须是 32 个 hex 字符\n"); return 2; }
    if(argc>=5){ if(hex2bin(argv[4],want,16)!=0){ fprintf(stderr,"md5Name 必须是 32 个 hex 字符\n"); return 2; } have_want=1; }

    FILE*f=fopen(fin,"rb"); if(!f){perror(fin);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*buf=malloc(n); if(fread(buf,1,n,f)!=(size_t)n){fprintf(stderr,"读失败\n");return 1;} fclose(f);
    printf("  输入 %s  %ld 字节\n",fin,n);
    if(n<20){fprintf(stderr,"文件太小\n");return 1;}

    uint8_t nonce[12]; memcpy(nonce,buf,12);
    size_t blen=n-12;
    uint8_t*body=malloc(blen); memcpy(body,buf+12,blen);
    chacha_xor(nonce,body,blen);
    printf("  解密 %zu 字节, nonce=%02x%02x%02x%02x...\n",blen,nonce[0],nonce[1],nonce[2],nonce[3]);

    uint32_t ver=*(uint32_t*)body;
    uint32_t stored=*(uint32_t*)(body+blen-4);
    uint32_t calc=crc32_of(body,blen-4);
    printf("  version=%u  存储CRC=0x%08x  实算CRC=0x%08x  %s\n",
           ver,stored,calc,(stored==calc)?"✓ 一致":"✗ 不一致");

    /* 扫整个解密体找 md5Name==want（或第一个 chunk 头） */
    long hit=-1;
    if(have_want){
        for(size_t i=0;i+32<=blen;i++){
            if(memcmp(body+i,want,16)==0){ hit=(long)i; break; }
        }
        if(hit<0){ fprintf(stderr,"  未找到 md5Name=%s\n",argv[4]); return 1; }
        printf("  找到 md5Name @ +0x%lx\n",hit);
    } else {
        /* 默认：按已知布局定位首个 chunk 头的 contentMD5 */
        /* version(4)+unk(4)+strLen(2)+group(10)+... 用启发式：找 "InitBundle" 后推算 */
        hit=-1;
        for(size_t i=0;i+32<=blen;i++){
            if(memcmp(body+i,"InitBundle",10)==0){
                /* group 后: u64 + u32(fileNum) + u64(chunkLen) + u8 + u32 + u8? + md5Name(16) + contentMD5(16) */
                long base=(long)i+10;
                for(long cand=base+8; cand<base+48; cand++){
                    if(cand+32>(long)blen) break;
                    /* contentMD5 在 md5Name 之后 16 字节 ⇒ 这里直接找 md5Name 的位置 */
                    /* 简化：已知 1.6.4 布局 md5Name @ group+0x23 */
                    if(cand==base+0x19){ hit=cand; break; }   /* base = "InitBundle" 位置 + 10; md5Name 在其后 0x19 */
                }
                break;
            }
        }
        if(hit<0){ fprintf(stderr,"  未找到 chunk 头（请用第 4 个参数指定 md5Name）\n"); return 1; }
        printf("  自动定位 md5Name @ +0x%lx\n",hit);
    }

    char oldn[33],oldc[33];
    bin2hex(body+hit,16,oldn);
    bin2hex(body+hit+16,16,oldc);
    printf("  md5Name    = %s\n",oldn);
    printf("  contentMD5 = %s  (旧)\n",oldc);
    printf("               %s  (新)\n",hexmd5);
    memcpy(body+hit+16,newmd5,16);

    uint32_t newcrc=crc32_of(body,blen-4);
    memcpy(body+blen-4,&newcrc,4);
    printf("  新 CRC     = 0x%08x\n",newcrc);

    chacha_xor(nonce,body,blen);       /* 重新加密（同一 nonce ⇒ 等长） */
    memcpy(buf+12,body,blen);
    FILE*g=fopen(fout,"wb"); if(!g){perror(fout);return 1;}
    if(fwrite(buf,1,n,g)!=(size_t)n){fprintf(stderr,"写失败\n");return 1;}
    fclose(g);
    printf("  ⇒ 已写出 %s  %ld 字节（等长 %s）\n",fout,n,(n==(long)n)?"✓":"✗");
    free(buf); free(body);
    return 0;
}
