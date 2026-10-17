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
static void chacha_block(const uint8_t key[32],const uint8_t nonce[12],uint32_t ctr,uint8_t out[64]){
  uint32_t st[16]={0x61707865,0x3320646e,0x79622d32,0x6b206574};
  for(int i=0;i<8;i++) st[4+i]=key[i*4]|(key[i*4+1]<<8)|(key[i*4+2]<<16)|((uint32_t)key[i*4+3]<<24);
  st[12]=ctr;
  for(int i=0;i<3;i++) st[13+i]=nonce[i*4]|(nonce[i*4+1]<<8)|(nonce[i*4+2]<<16)|((uint32_t)nonce[i*4+3]<<24);
  uint32_t w[16]; memcpy(w,st,sizeof w);
  for(int i=0;i<10;i++){
    qr(w,0,4,8,12);qr(w,1,5,9,13);qr(w,2,6,10,14);qr(w,3,7,11,15);
    qr(w,0,5,10,15);qr(w,1,6,11,12);qr(w,2,7,8,13);qr(w,3,4,9,14);
  }
  for(int i=0;i<16;i++){uint32_t v=w[i]+st[i];out[i*4]=v;out[i*4+1]=v>>8;out[i*4+2]=v>>16;out[i*4+3]=v>>24;}
}
static void chacha_xor(const uint8_t nonce[12],uint8_t*buf,size_t n){
  uint8_t ks[64]; uint32_t ctr=1;
  for(size_t off=0;off<n;off+=64){
    chacha_block(KEY,nonce,ctr++,ks);
    size_t l=(n-off<64)?(n-off):64;
    for(size_t i=0;i<l;i++) buf[off+i]^=ks[i];
  }
}
int main(int c,char**v){
  FILE*f=fopen(v[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc(n);if(fread(b,1,n,f)!=(size_t)n)return 1;fclose(f);
  uint8_t nonce[12]; memcpy(nonce,b,12);
  size_t blen=n-12; uint8_t*body=malloc(blen); memcpy(body,b+12,blen);
  chacha_xor(nonce,body,blen);
  printf("  version=%u\n",*(uint32_t*)body);
  printf("  前 64 字节:\n    ");
  for(int i=0;i<64;i++){printf("%02X ",body[i]); if((i+1)%16==0)printf("\n    ");}
  printf("\n  ASCII @16: ");
  for(int i=16;i<48;i++){char ch=body[i]; printf("%c",(ch>=32&&ch<127)?ch:'.');}
  printf("\n");
  return 0;
}
