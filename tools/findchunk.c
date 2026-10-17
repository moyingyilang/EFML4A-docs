/* findchunk —— 给定全局偏移，输出 chunk 名与本地偏移 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static const uint8_t KEY[32]={0xe9,0x5b,0x31,0x7a,0xc4,0xf8,0x28,0x56,0x9d,0x23,0xa8,0x6b,0xf2,0x71,0xdc,0xb5,0x3e,0x84,0x6f,0xa7,0x5c,0x92,0x4d,0x67,0x1d,0xba,0x8e,0x38,0xf4,0xca,0x52,0xe1};
static uint32_t rotl32(uint32_t x,int n){return (x<<n)|(x>>(32-n));}
static void qr(uint32_t*s,int a,int b,int c,int d){
  s[a]+=s[b];s[d]^=s[a];s[d]=rotl32(s[d],16);
  s[c]+=s[d];s[b]^=s[c];s[b]=rotl32(s[b],12);
  s[a]+=s[b];s[d]^=s[a];s[d]=rotl32(s[d],8);
  s[c]+=s[d];s[b]^=s[c];s[b]=rotl32(s[b],7);
}
static void cblk(const uint8_t k[32],const uint8_t n[12],uint32_t ctr,uint8_t o[64]){
  uint32_t st[16]={0x61707865,0x3320646e,0x79622d32,0x6b206574};
  for(int i=0;i<8;i++) st[4+i]=k[i*4]|(k[i*4+1]<<8)|(k[i*4+2]<<16)|((uint32_t)k[i*4+3]<<24);
  st[12]=ctr;
  for(int i=0;i<3;i++) st[13+i]=n[i*4]|(n[i*4+1]<<8)|(n[i*4+2]<<16)|((uint32_t)n[i*4+3]<<24);
  uint32_t w[16]; memcpy(w,st,sizeof w);
  for(int i=0;i<10;i++){
    qr(w,0,4,8,12);qr(w,1,5,9,13);qr(w,2,6,10,14);qr(w,3,7,11,15);
    qr(w,0,5,10,15);qr(w,1,6,11,12);qr(w,2,7,8,13);qr(w,3,4,9,14);
  }
  for(int i=0;i<16;i++){uint32_t v=w[i]+st[i];o[i*4]=v;o[i*4+1]=v>>8;o[i*4+2]=v>>16;o[i*4+3]=v>>24;}
}
static void cx(const uint8_t n[12],uint8_t*b,size_t len){
  uint8_t ks[64];uint32_t c=1;
  for(size_t off=0;off<len;off+=64){cblk(KEY,n,c++,ks);
    size_t l=(len-off<64)?(len-off):64;
    for(size_t i=0;i<l;i++)b[off+i]^=ks[i];}
}
int main(int c,char**v){
  FILE*f=fopen(v[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc(n);if(fread(b,1,n,f)!=(size_t)n)return 1;fclose(f);
  uint8_t nonce[12];memcpy(nonce,b,12);
  size_t bl=n-12;uint8_t*body=malloc(bl);memcpy(body,b+12,bl);
  cx(nonce,body,bl);
  long target=atol(v[2]);
  /* 解析: version(4) unk(4) strLen(2) group | groupHash(8) fileInfoNum(4) chunksLen(8) blockType(1) chunkCount(4) */
  long p=4+4;
  uint16_t sl=*(uint16_t*)(body+p); p+=2+sl;
  p+=8; p+=4; p+=8; p+=1;
  uint32_t cc=*(uint32_t*)(body+p); p+=4;
  unsigned long long acc=0;
  printf("  chunkCount=%u\n",cc);
  for(uint32_t i=0;i<cc;i++){
    uint8_t name[16]; memcpy(name,body+p,16); p+=16;
    uint8_t cm[16];   memcpy(cm,body+p,16);   p+=16;
    uint64_t off=*(uint64_t*)(body+p); p+=8;
    uint64_t len=*(uint64_t*)(body+p); p+=8;
    uint8_t bt=body[p++]; uint8_t enc=body[p++];
    uint64_t ivs=0; if(enc){ ivs=*(uint64_t*)(body+p); p+=8; }
    char hex[33]; for(int k=0;k<16;k++) sprintf(hex+k*2,"%02x",name[k]); hex[32]=0;
    if(target>=(long)acc && target<(long)(acc+len)){
      printf("  ★ 命中! chunk=%s\n",hex);
      printf("     全局范围 [%llu, %llu)\n",acc,acc+len);
      printf("     本地偏移 = %ld\n",target-(long)acc);
      printf("     blockType=%u enc=%u ivSeed=%llu\n",bt,enc,ivs);
      printf("     contentMD5(旧)=");for(int k=0;k<16;k++)printf("%02x",cm[k]);printf("\n");
    }
    if(i<3||i>cc-3) printf("     [%u] %s len=%llu 累计=%llu\n",i,hex,(unsigned long long)len,acc);
    acc+=len;
  }
  return 0;
}
