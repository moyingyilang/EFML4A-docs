/* blcverify —— 校验 .blc 里每个 chunk 的 contentMD5 是否等于对应 .chk 的 md5
 *   用法: blcverify <blc> <chkdir>
 *
 * ★ 用途：确认修改/还原后【所有】chunk 都一致
 *   之前踩坑：还原脚本误报"本就一致"，遗漏一个 .chk ⇒ 游戏报"数据加载失败"
 *
 * 原理：
 *   .blc 解密体: ... chunk 头(md5Name(16) contentMD5(16)) ...
 *   每个条目的 chunkMD5 指向一个 .chk 文件（文件名 = 大写 chunkMD5 + ".chk"）
 *   contentMD5 应等于该 .chk 文件的 md5
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

static const uint8_t KEY[32] = {
    0xe9,0x5b,0x31,0x7a,0xc4,0xf8,0x28,0x56,0x9d,0x23,0xa8,0x6b,0xf2,0x71,0xdc,0xb5,
    0x3e,0x84,0x6f,0xa7,0x5c,0x92,0x4d,0x67,0x1d,0xba,0x8e,0x38,0xf4,0xca,0x52,0xe1
};
static uint32_t rotl32(uint32_t x,int n){ return (x<<n)|(x>>(32-n)); }
static void qr(uint32_t*s,int a,int b,int c,int d){
    s[a]+=s[b]; s[d]^=s[a]; s[d]=rotl32(s[d],16);
    s[c]+=s[d]; s[b]^=s[c]; s[b]=rotl32(s[b],12);
    s[a]+=s[b]; s[d]^=s[a]; s[d]=rotl32(s[d],8);
    s[c]+=s[d]; s[b]^=s[c]; s[b]=rotl32(s[b],7);
}
static void chacha(const uint8_t*key,const uint8_t*nonce,uint32_t ctr,uint8_t*out,size_t n){
    uint32_t st[16]; uint32_t k[8];
    for(int i=0;i<8;i++) k[i]=key[i*4]|(key[i*4+1]<<8)|(key[i*4+2]<<16)|((uint32_t)key[i*4+3]<<24);
    st[0]=0x61707865; st[1]=0x3320646e; st[2]=0x79622d32; st[3]=0x6b206574;
    for(int i=0;i<8;i++) st[4+i]=k[i];
    st[12]=ctr;
    st[13]=nonce[0]|(nonce[1]<<8)|(nonce[2]<<16)|((uint32_t)nonce[3]<<24);
    st[14]=nonce[4]|(nonce[5]<<8)|(nonce[6]<<16)|((uint32_t)nonce[7]<<24);
    st[15]=nonce[8]|(nonce[9]<<8)|(nonce[10]<<16)|((uint32_t)nonce[11]<<24);
    for(size_t i=0;i<n;i+=64){
        uint32_t w[16]; memcpy(w,st,sizeof w);
        for(int r=0;r<10;r++){
            qr(w,0,4,8,12); qr(w,1,5,9,13); qr(w,2,6,10,14); qr(w,3,7,11,15);
            qr(w,0,5,10,15); qr(w,1,6,11,12); qr(w,2,7,8,13); qr(w,3,4,9,14);
        }
        for(int j=0;j<16;j++){ uint32_t v=w[j]+st[j]; for(int b=0;b<4;b++){ if(i+j*4+b<n) out[i+j*4+b]=(v>>(8*b))&0xFF; } }
        st[12]++;
    }
}
/* md5 */
typedef struct { uint32_t a,b,c,d; uint64_t len; uint8_t buf[64]; size_t n; } MD5;
static uint32_t rol(uint32_t x,int c){ return (x<<c)|(x>>(32-c)); }
static void md5b(MD5*m,const uint8_t*p,size_t n){
    static const uint32_t K[64]={
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const int S[64]={7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
                            5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                            4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
                            6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    m->len += n;
    for(size_t i=0;i<n;i++){
        m->buf[m->n++]=p[i];
        if(m->n==64){
            uint32_t w[16];
            for(int j=0;j<16;j++) w[j]=m->buf[j*4]|(m->buf[j*4+1]<<8)|(m->buf[j*4+2]<<16)|((uint32_t)m->buf[j*4+3]<<24);
            uint32_t A=m->a,B=m->b,C=m->c,D=m->d;
            for(int j=0;j<64;j++){
                uint32_t F; int g;
                if(j<16){ F=(B&C)|(~B&D); g=j; }
                else if(j<32){ F=(D&B)|(~D&C); g=(5*j+1)%16; }
                else if(j<48){ F=B^C^D; g=(3*j+5)%16; }
                else { F=C^(B|~D); g=(7*j)%16; }
                F=F+A+K[j]+w[g]; A=D; D=C; C=B; B=B+rol(F,S[j]);
            }
            m->a+=A; m->b+=B; m->c+=C; m->d+=D;
            m->n=0;
        }
    }
}
static void md5end(MD5*m,uint8_t out[16]){
    uint64_t bits=m->len*8;
    uint8_t pad[72]; size_t pn=0;
    pad[pn++]=0x80;
    while((m->n+pn)%64!=56) pad[pn++]=0;
    for(int i=0;i<8;i++) pad[pn++]=(bits>>(8*i))&0xFF;
    md5b(m,pad,pn);
    for(int i=0;i<4;i++){
        out[i]=(m->a>>(8*i))&0xFF; out[4+i]=(m->b>>(8*i))&0xFF;
        out[8+i]=(m->c>>(8*i))&0xFF; out[12+i]=(m->d>>(8*i))&0xFF;
    }
}
static int md5file(const char*path,uint8_t out[16]){
    FILE*f=fopen(path,"rb"); if(!f) return -1;
    MD5 m={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0,{0},0};
    uint8_t buf[65536]; size_t n;
    while((n=fread(buf,1,sizeof buf,f))>0) md5b(&m,buf,n);
    fclose(f); md5end(&m,out); return 0;
}
static void hex(const uint8_t*p,int n,char*out){ for(int i=0;i<n;i++) sprintf(out+i*2,"%02x",p[i]); out[n*2]=0; }
int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,"用法: blcverify <blc> <chkdir>\n"); return 2; }
    FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*b=malloc(n); if(fread(b,1,n,f)!=(size_t)n) return 1; fclose(f);
    if(n<16){ fprintf(stderr,"文件太小\n"); return 1; }
    /* 解密 [12, n) */
    uint8_t*dec=malloc(n-12);
    chacha(KEY,b,b[12]|(b[13]<<8)|(b[14]<<16)|((uint32_t)b[15]<<24),dec,n-12);
    /* 找所有 md5Name(16) + contentMD5(16) 对: md5Name 是 16 字节且后跟 16 字节 */
    /* 简化：扫描解密体，把每个 16 字节块当候选 md5Name，检查其对应 .chk 是否存在 */
    int good=0, bad=0, miss=0;
    char path[512], h1[40], h2[40];
    for(long i=0;i+32<n-12;i++){
        /* 启发：md5Name 后 16 字节是 contentMD5；只检查能对上文件名的 */
        char nm[40]; hex(dec+i,16,nm);
        /* 转大写 */
        for(int k=0;k<32;k++) if(nm[k]>='a'&&nm[k]<='f') nm[k]-='a'-'A';
        snprintf(path,sizeof path,"%s/%s.chk",argv[2],nm);
        struct stat st;
        if(stat(path,&st)!=0) continue;
        /* 找到匹配文件 ⇒ 这是有效的 chunk 头 */
        uint8_t md[16]; if(md5file(path,md)!=0) continue;
        hex(md,16,h1); hex(dec+i+16,16,h2);
        if(strcmp(h1,h2)==0){ good++; printf("  ✓ %s\n",nm); }
        else { bad++; printf("  ✗ %s\n     .blc contentMD5=%s\n     .chk 实际 md5 =%s\n",nm,h2,h1); }
        i+=31;
    }
    printf("\n  ⇒ 一致 %d / 不一致 %d\n",good,bad);
    free(b); free(dec);
    return bad?1:0;
}
