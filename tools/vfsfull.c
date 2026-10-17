/* vfsfull —— 严格实现 AnimeStudio VfsParser（含 codeVersion>3 的 tag）
 *   用法: vfsfull <in.blc>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>

static const uint8_t KEY[32]={0xe9,0x5b,0x31,0x7a,0xc4,0xf8,0x28,0x56,0x9d,0x23,0xa8,0x6b,0xf2,0x71,0xdc,0xb5,
                              0x3e,0x84,0x6f,0xa7,0x5c,0x92,0x4d,0x67,0x1d,0xba,0x8e,0x38,0xf4,0xca,0x52,0xe1};
static uint32_t rotl32(uint32_t x,int n){return (x<<n)|(x>>(32-n));}
static void qr(uint32_t*s,int a,int b,int c,int d){
    s[a]+=s[b];s[d]^=s[a];s[d]=rotl32(s[d],16);
    s[c]+=s[d];s[b]^=s[c];s[b]=rotl32(s[b],12);
    s[a]+=s[b];s[d]^=s[a];s[d]=rotl32(s[d],8);
    s[c]+=s[d];s[b]^=s[c];s[b]=rotl32(s[b],7);
}
static void chacha_block(const uint8_t key[32],const uint8_t nonce[12],uint32_t cnt,uint8_t out[64]){
    uint32_t st[16],w[16];
    st[0]=0x61707865;st[1]=0x3320646e;st[2]=0x79622d32;st[3]=0x6b206574;
    for(int i=0;i<8;i++) st[4+i]=key[i*4]|(key[i*4+1]<<8)|(key[i*4+2]<<16)|((uint32_t)key[i*4+3]<<24);
    st[12]=cnt;
    for(int i=0;i<3;i++) st[13+i]=nonce[i*4]|(nonce[i*4+1]<<8)|(nonce[i*4+2]<<16)|((uint32_t)nonce[i*4+3]<<24);
    memcpy(w,st,sizeof w);
    for(int i=0;i<10;i++){
        qr(w,0,4,8,12);qr(w,1,5,9,13);qr(w,2,6,10,14);qr(w,3,7,11,15);
        qr(w,0,5,10,15);qr(w,1,6,11,12);qr(w,2,7,8,13);qr(w,3,4,9,14);
    }
    for(int i=0;i<16;i++){uint32_t v=w[i]+st[i];out[i*4]=v&0xff;out[i*4+1]=(v>>8)&0xff;out[i*4+2]=(v>>16)&0xff;out[i*4+3]=(v>>24)&0xff;}
}
static void chacha_xor(const uint8_t nonce[12],uint8_t*buf,size_t n){
    uint8_t ks[64];uint32_t c=1;size_t off=0;
    while(off<n){
        chacha_block(KEY,nonce,c++,ks);
        size_t m=(n-off<64)?(n-off):64;
        for(size_t i=0;i<m;i++) buf[off+i]^=ks[i];
        off+=m;
    }
}
static uint8_t*D; static long DN,DP;
static void ck(long need){ if(DP+need>DN){ fprintf(stderr,"  OOB @%ld+%ld>%ld\n",DP,need,DN); exit(1);} }
static int32_t rI32(void){ck(4);int32_t v=D[DP]|(D[DP+1]<<8)|(D[DP+2]<<16)|((uint32_t)D[DP+3]<<24);DP+=4;return v;}
static int64_t rI64(void){uint64_t lo=(uint32_t)rI32();uint64_t hi=(uint32_t)rI32();return (int64_t)((hi<<32)|lo);}
static uint16_t rU16(void){ck(2);uint16_t v=D[DP]|(D[DP+1]<<8);DP+=2;return v;}
static uint8_t rU8(void){ck(1);return D[DP++];}
static void rU128(uint8_t*o){ck(16);memcpy(o,D+DP,16);DP+=16;}
int main(int c,char**v){
    if(c<2){ fprintf(stderr,"用法: vfsfull <in.blc>\n"); return 2; }
    FILE*f=fopen(v[1],"rb"); if(!f){perror(v[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*raw=malloc(n); if(fread(raw,1,n,f)!=(size_t)n) return 1; fclose(f);
    printf("  .blc: %ld 字节\n", n);
    DN=n-12; D=malloc(DN); memcpy(D,raw+12,DN);
    chacha_xor(raw,D,DN);
    printf("  解密后前 16 字节: ");
    for(int i=0;i<16;i++) printf("%02x ",D[i]);
    printf("\n");
    uint32_t stored=D[DN-4]|(D[DN-3]<<8)|(D[DN-2]<<16)|((uint32_t)D[DN-1]<<24);
    uint32_t calc=crc32(0,D,DN-4);
    printf("  CRC32: 存储=0x%08x 实算=0x%08x %s\n",stored,calc,stored==calc?"✓":"✗");
    DP=0;
    int rawVersion=rI32(); int codeVersion,version;
    if(rawVersion<11){ codeVersion=rawVersion; version=rI32(); }
    else { codeVersion=3; version=rawVersion; }
    printf("  version=%d codeVersion=%d\n",version,codeVersion);
    int glen=rU16();
    char gname[512]; ck(glen); if(glen<512){memcpy(gname,D+DP,glen);gname[glen]=0;} else gname[0]=0;
    DP+=glen;
    int64_t ghash=rI64();
    int fileInfoNum=rI32();
    int64_t chunksLen=rI64();
    uint8_t blockType=rU8();
    int chunkCount=rI32();
    printf("  group=\"%s\" hash=0x%llx fileInfoNum=%d chunksLen=%lld blockType=%u chunkCount=%d\n",
           gname,(unsigned long long)ghash,fileInfoNum,(long long)chunksLen,blockType,chunkCount);
    if(chunkCount<0||chunkCount>1000){ printf("  ✗ chunkCount 不合理\n"); return 1; }
    long totalFiles=0;
    for(int ci=0;ci<chunkCount;ci++){
        uint8_t md5[16],cmd5[16];
        rU128(md5); rU128(cmd5);
        int64_t clen=rI64();
        uint8_t cbt=rU8();
        if(codeVersion>3){ (void)rI32(); (void)rI32(); }
        int fileCount=rI32();
        printf("  chunk%d: len=%lld bt=%u files=%d\n",ci,(long long)clen,cbt,fileCount);
        totalFiles+=fileCount;
        if(fileCount<0||fileCount>2000000){ printf("    ✗ fileCount 不合理\n"); return 1; }
        for(int fi=0;fi<fileCount;fi++){
            int nl=rU16();
            char nm[600]; ck(nl); if(nl>0&&nl<600){memcpy(nm,D+DP,nl);nm[nl]=0;} else nm[0]=0;
            DP+=nl;
            (void)rI64();
            uint8_t a[16],b2[16]; rU128(a); rU128(b2);
            int64_t off=rI64(); int64_t len=rI64();
            uint8_t fbt=rU8(); uint8_t enc=rU8();
            int64_t iv=0; if(enc) iv=rI64();
            if(codeVersion>3){ (void)rI32(); (void)rI32(); }
            if(fi<2000000) printf("    file%d: \"%s\" off=%lld len=%lld bt=%u enc=%u iv=%lld\n",
                            fi,nm,(long long)off,(long long)len,fbt,enc,(long long)iv);
        }
    }
    printf("\n  ⇒ 总共 %ld 个文件；解析结束 @%ld / %ld\n", totalFiles, DP, DN);
    return 0;
}
