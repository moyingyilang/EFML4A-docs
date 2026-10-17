/* vfslist —— 完整解析 .blc 的所有文件条目（纯 C，取代 blcparse 的单条 bug）
 *   用法: vfslist <in.blc> [--filter 子串] [--tsv 输出]
 *   输出: nameLen name offset len blockType useEncrypt ivSeed chunkMD5
 *
 *   布局（1.6.4 / version=5，实测）:
 *     header: version(4) unk(4) strLen(2) group(strLen) id(8) fileNum(4)
 *             chunkLen(8) blockType(1) chunkNum(4)
 *     chunk : [chunkMD5(16) contentMD5(16) ...]
 *     entry : nameLen(2) name(nameLen) fields(66)
 *             fields: +0 chunkMD5(16) +16 ?(8) +24 len(8) +32 blockType(1)
 *                     +33 useEncrypt(1) +34 ivSeed(4) ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const uint8_t KEY[32]={
 0xe9,0x5b,0x31,0x7a,0xc4,0xf8,0x28,0x56,0x9d,0x23,0xa8,0x6b,0xf2,0x71,0xdc,0xb5,
 0x3e,0x84,0x6f,0xa7,0x5c,0x92,0x4d,0x67,0x1d,0xba,0x8e,0x38,0xf4,0xca,0x52,0xe1};
static uint32_t rotl32(uint32_t x,int n){return (x<<n)|(x>>(32-n));}
static void qr(uint32_t*s,int a,int b,int c,int d){
 s[a]+=s[b];s[d]^=s[a];s[d]=rotl32(s[d],16);s[c]+=s[d];s[b]^=s[c];s[b]=rotl32(s[b],12);
 s[a]+=s[b];s[d]^=s[a];s[d]=rotl32(s[d],8);s[c]+=s[d];s[b]^=s[c];s[b]=rotl32(s[b],7);}
static void cblk(const uint8_t k[32],const uint8_t n[12],uint32_t c,uint8_t o[64]){
 uint32_t st[16],w[16];st[0]=0x61707865;st[1]=0x3320646e;st[2]=0x79622d32;st[3]=0x6b206574;
 for(int i=0;i<8;i++)st[4+i]=k[i*4]|(k[i*4+1]<<8)|(k[i*4+2]<<16)|((uint32_t)k[i*4+3]<<24);
 st[12]=c;for(int i=0;i<3;i++)st[13+i]=n[i*4]|(n[i*4+1]<<8)|(n[i*4+2]<<16)|((uint32_t)n[i*4+3]<<24);
 memcpy(w,st,sizeof w);
 for(int i=0;i<10;i++){qr(w,0,4,8,12);qr(w,1,5,9,13);qr(w,2,6,10,14);qr(w,3,7,11,15);
  qr(w,0,5,10,15);qr(w,1,6,11,12);qr(w,2,7,8,13);qr(w,3,4,9,14);}
 for(int i=0;i<16;i++){uint32_t v=w[i]+st[i];o[i*4]=v&0xff;o[i*4+1]=(v>>8)&0xff;o[i*4+2]=(v>>16)&0xff;o[i*4+3]=(v>>24)&0xff;}}
static int isp(uint8_t c){return c>=32&&c<127;}
static void hexs(const uint8_t*b,int n,char*o){for(int i=0;i<n;i++)sprintf(o+i*2,"%02x",b[i]);o[n*2]=0;}

int main(int argc,char**argv){
    if(argc<2){fprintf(stderr,"用法: vfslist <in.blc> [--filter 子串]\n");return 2;}
    const char* fin=argv[1]; const char* filt=0;
    for(int i=2;i<argc;i++) if(!strcmp(argv[i],"--filter")&&i+1<argc) filt=argv[++i];
    FILE*f=fopen(fin,"rb"); if(!f){perror(fin);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t* raw=malloc(n); if(fread(raw,1,n,f)!=(size_t)n){fclose(f);return 1;} fclose(f);
    uint8_t nonce[12]; memcpy(nonce,raw,12);
    size_t bl=n-12; uint8_t* b=malloc(bl); memcpy(b,raw+12,bl); free(raw);
    {uint8_t ks[64];uint32_t c=1;size_t off=0;
     while(off<bl){cblk(KEY,nonce,c++,ks);size_t m=(bl-off<64)?(bl-off):64;
      for(size_t i=0;i<m;i++)b[off+i]^=ks[i];off+=m;}}
    /* 头部 */
    uint32_t ver=*(uint32_t*)b; uint16_t sl=*(uint16_t*)(b+8);
    char grp[128]={0}; if(sl<sizeof grp) memcpy(grp,b+10,sl);
    size_t p=10+sl;
    uint64_t id=*(uint64_t*)(b+p); p+=8;
    uint32_t fn=*(uint32_t*)(b+p); p+=4;
    uint64_t cl=*(uint64_t*)(b+p); p+=8;
    uint8_t bt=b[p]; p+=1;
    uint32_t cn=*(uint32_t*)(b+p); p+=4;
    fprintf(stderr,"  version=%u group=\"%s\" fileNum=%u chunkLen=%llu blockType=%u chunkNum=%u\n",
            ver,grp,fn,(unsigned long long)cl,bt,cn);
    /* 扫描所有 "Data/" 起点，向前找 nameLen，向后解析字段 */
    long found=0;
    for(size_t i=p;i+4<bl;i++){
        if(memcmp(b+i,"Data/",5)) continue;
        /* 名字长度应在 i-2 */
        if(i<2) continue;
        uint16_t nl=*(uint16_t*)(b+i-2);
        if(nl<5||nl>512) continue;
        if(i+nl>bl) continue;
        /* 校验名字全可打印 */
        int ok=1; for(size_t k=0;k<nl;k++) if(!isp(b[i+k])){ok=0;break;}
        if(!ok) continue;
        char name[520]; memcpy(name,b+i,nl); name[nl]=0;
        if(filt && !strstr(name,filt)) continue;
        size_t fe=i+nl;               /* 字段起点 */
        if(fe+66>bl) continue;
        char cmd5[33]; hexs(b+fe+8,16,cmd5);
        long long foff=*(long long*)(b+fe+40);
        long long flen=*(long long*)(b+fe+48);
        uint8_t fbt=b[fe+56], enc=b[fe+57];
        uint32_t iv=*(uint32_t*)(b+fe+58);
        printf("%s\t%d\t%lld\t%lld\t%u\t%u\t%u\t%s\n",name,nl,foff,flen,fbt,enc,iv,cmd5);
        found++;
    }
    fprintf(stderr,"  ⇒ 解析出 %ld 条\n",found);
    free(b); return 0;
}
