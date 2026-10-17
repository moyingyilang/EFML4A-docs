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
  const char*t=v[2];size_t tl=strlen(t);
  for(size_t i=0;i+tl<=bl;i++){
    if(memcmp(body+i,t,tl)==0){
      size_t p=i+tl;
      printf("  名字 @+0x%zx (len=%zu)\n",i,tl);
      printf("  名字后 72 字节:\n");
      for(int k=0;k<72;k++){
        printf("%02X ",body[p+k]);
        if((k+1)%16==0)printf("\n");
      }
      printf("\n  按布局解析:\n");
      printf("    nameHash  = ");for(int k=0;k<8;k++)printf("%02X",body[p+k]);printf("\n");
      printf("    chunkMd5  = ");for(int k=8;k<24;k++)printf("%02X",body[p+k]);printf("\n");
      printf("    dataMd5   = ");for(int k=24;k<40;k++)printf("%02X",body[p+k]);printf("\n");
      printf("    offset    = ");for(int k=40;k<48;k++)printf("%02X",body[p+k]);printf("\n");
      printf("    len       = ");for(int k=48;k<56;k++)printf("%02X",body[p+k]);printf("\n");
      break;
    }
  }
  return 0;
}
