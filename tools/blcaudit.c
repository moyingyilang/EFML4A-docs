/* blcaudit —— 校验 .blc 里每个文件的 dataMd5 是否与实际内容一致
 *   用法: blcaudit <group.blc> <group_dir>
 *
 * 需要 MD5 实现（内置）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- 简易 MD5 ---- */
typedef struct { uint32_t s[4]; uint64_t len; uint8_t buf[64]; size_t n; } MD5;
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
static uint32_t rotl(uint32_t x,int n){return (x<<n)|(x>>(32-n));}
static void md5_blocks(MD5*m,const uint8_t*p,size_t nb){
    for(size_t b=0;b<nb;b++,p+=64){
        uint32_t w[16]; for(int i=0;i<16;i++) w[i]=p[i*4]|(p[i*4+1]<<8)|(p[i*4+2]<<16)|((uint32_t)p[i*4+3]<<24);
        uint32_t a=m->s[0],bb=m->s[1],c=m->s[2],d=m->s[3];
        for(int i=0;i<64;i++){
            uint32_t f; int g;
            if(i<16){ f=(bb&c)|(~bb&d); g=i; }
            else if(i<32){ f=(d&bb)|(~d&c); g=(5*i+1)%16; }
            else if(i<48){ f=bb^c^d; g=(3*i+5)%16; }
            else { f=c^(bb|~d); g=(7*i)%16; }
            uint32_t tmp=d; d=c; c=bb;
            bb=bb+rotl(a+f+K[i]+w[g],S[i]);
            a=tmp;
        }
        m->s[0]+=a;m->s[1]+=bb;m->s[2]+=c;m->s[3]+=d;
    }
}
static void md5_init(MD5*m){m->s[0]=0x67452301;m->s[1]=0xefcdab89;m->s[2]=0x98badcfe;m->s[3]=0x10325476;m->len=0;m->n=0;}
static void md5_upd(MD5*m,const uint8_t*p,size_t l){
    m->len+=l*8;
    if(m->n){ size_t need=64-m->n; size_t take=l<need?l:need; memcpy(m->buf+m->n,p,take); m->n+=take; p+=take; l-=take;
        if(m->n==64){ md5_blocks(m,m->buf,1); m->n=0; } }
    if(l>=64){ size_t nb=l/64; md5_blocks(m,p,nb); p+=nb*64; l-=nb*64; }
    if(l){ memcpy(m->buf,p,l); m->n=l; }
}
static void md5_fin(MD5*m,uint8_t out[16]){
    uint64_t bits=m->len;
    uint8_t pad[72]; size_t pl=0; pad[pl++]=0x80;
    while((m->n+pl)%64!=56) pad[pl++]=0;
    md5_upd(m,pad,pl);
    uint8_t lenb[8]; for(int i=0;i<8;i++) lenb[i]=(uint8_t)(bits>>(8*i));
    md5_upd(m,lenb,8);
    for(int i=0;i<4;i++){ out[i*4]=(uint8_t)m->s[i]; out[i*4+1]=(uint8_t)(m->s[i]>>8); out[i*4+2]=(uint8_t)(m->s[i]>>16); out[i*4+3]=(uint8_t)(m->s[i]>>24); }
}

/* ---- ChaCha20（同 VFS） ---- */
static const uint8_t VK[32]={0xe9,0x5b,0x31,0x7a,0xc4,0xf8,0x28,0x56,0x9d,0x23,0xa8,0x6b,0xf2,0x71,0xdc,0xb5,0x3e,0x84,0x6f,0xa7,0x5c,0x92,0x4d,0x67,0x1d,0xba,0x8e,0x38,0xf4,0xca,0x52,0xe1};
static void qr(uint32_t*s,int a,int b,int c,int d){
  s[a]+=s[b];s[d]^=s[a];s[d]=rotl(s[d],16);
  s[c]+=s[d];s[b]^=s[c];s[b]=rotl(s[b],12);
  s[a]+=s[b];s[d]^=s[a];s[d]=rotl(s[d],8);
  s[c]+=s[d];s[b]^=s[c];s[b]=rotl(s[b],7);
}
static void cbk(const uint8_t n[12],uint32_t ctr,uint8_t o[64]){
  uint32_t st[16]={0x61707865,0x3320646e,0x79622d32,0x6b206574};
  for(int i=0;i<8;i++) st[4+i]=VK[i*4]|(VK[i*4+1]<<8)|(VK[i*4+2]<<16)|((uint32_t)VK[i*4+3]<<24);
  st[12]=ctr;
  for(int i=0;i<3;i++) st[13+i]=n[i*4]|(n[i*4+1]<<8)|(n[i*4+2]<<16)|((uint32_t)n[i*4+3]<<24);
  uint32_t w[16];memcpy(w,st,sizeof w);
  for(int i=0;i<10;i++){qr(w,0,4,8,12);qr(w,1,5,9,13);qr(w,2,6,10,14);qr(w,3,7,11,15);
    qr(w,0,5,10,15);qr(w,1,6,11,12);qr(w,2,7,8,13);qr(w,3,4,9,14);}
  for(int i=0;i<16;i++){uint32_t v=w[i]+st[i];o[i*4]=v;o[i*4+1]=v>>8;o[i*4+2]=v>>16;o[i*4+3]=v>>24;}
}
static void cck(const uint8_t n[12],uint8_t*b,size_t len){
  uint8_t ks[64];uint32_t c=1;
  for(size_t off=0;off<len;off+=64){cbk(n,c++,ks);
    size_t l=(len-off<64)?(len-off):64;
    for(size_t i=0;i<l;i++)b[off+i]^=ks[i];}
}
int main(int c,char**v){
    if(c<3){fprintf(stderr,"用法: blcaudit <group.blc> <group_dir>\n");return 2;}
    FILE*f=fopen(v[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t*b=malloc(n);if(fread(b,1,n,f)!=(size_t)n)return 1;fclose(f);
    uint8_t nonce[12];memcpy(nonce,b,12);
    size_t bl=n-12;uint8_t*body=malloc(bl);memcpy(body,b+12,bl);
    cck(nonce,body,bl);
    long p=4+4;
    uint16_t sl=*(uint16_t*)(body+p); p+=2+sl;
    p+=8+4+8+1;
    uint32_t cc=*(uint32_t*)(body+p); p+=4;
    printf("  chunkCount=%u\n",cc);
    /* 跳 chunk 条目：md5Name(16)+contentMD5(16)+off(8)+len(8)+bt(1)+enc(1) = 50 */
    for(uint32_t i=0;i<cc;i++) p+=50;
    uint32_t fn=*(uint32_t*)(body+p); p+=4;
    printf("  fileInfoNum=%u\n\n",fn);
    int bad=0,checked=0;
    for(uint32_t i=0;i<fn;i++){
        uint16_t nl=*(uint16_t*)(body+p); p+=2;
        char nm[512]={0}; memcpy(nm,body+p,nl<511?nl:511); p+=nl;
        p+=8;                       /* nameHash */
        const uint8_t*chunkName=body+p; p+=16;
        const uint8_t*wantMd5=body+p; p+=16;
        uint64_t off=*(uint64_t*)(body+p); p+=8;
        uint64_t len=*(uint64_t*)(body+p); p+=8;
        uint8_t bt=body[p++]; uint8_t enc=body[p++];
        if(enc) p+=8;
        /* 构造路径 */
        const char*base=strrchr(nm,'/'); base=base?base+1:nm;
        /* 找 .chk 文件 */
        char chk[600]; snprintf(chk,sizeof chk,"%s/%s",v[2],nm);
        /* 只处理 .ab 文件（在 .chk 里） */
        char path[600]; snprintf(path,sizeof path,"%s/%s",v[2],nm);
        FILE*g=fopen(path,"rb");
        if(!g){
            /* 尝试直接按 base 名 */
            char p2[600]; snprintf(p2,sizeof p2,"%s/%.32s.ab",v[2],base);
            g=fopen(p2,"rb");
            if(!g) continue;
        }
        uint8_t*data=malloc(len);
        if(fread(data,1,len,g)!=(size_t)len){ free(data); fclose(g); continue; }
        fclose(g);
        MD5 m; md5_init(&m); md5_upd(&m,data,len);
        uint8_t got[16]; md5_fin(&m,got);
        checked++;
        if(memcmp(got,wantMd5,16)!=0){
            bad++;
            if(bad<=10){
                printf("  ✗ %.50s\n      期望 ",nm);
                for(int k=0;k<16;k++)printf("%02X",wantMd5[k]);
                printf("\n      实际 ");
                for(int k=0;k<16;k++)printf("%02X",got[k]);
                printf("\n");
            }
        }
        free(data);
    }
    printf("\n  检查 %d 个文件，不一致 %d 个\n",checked,bad);
    return bad?1:0;
}
