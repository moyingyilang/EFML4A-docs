#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "vfs_key.h"
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
  if(c<2){fprintf(stderr,"用法: chunknames <in.blc>\n");return 2;}
  if(load_key()) return 1;
  FILE*f=fopen(v[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc(n);if(fread(b,1,n,f)!=(size_t)n)return 1;fclose(f);
  uint8_t nonce[12];memcpy(nonce,b,12);
  size_t bl=n-12;uint8_t*body=malloc(bl);memcpy(body,b+12,bl);
  cx(nonce,body,bl);
  /* @8-9 strLen, @10.. group, 然后 groupHash(8) fileInfoNum(4) chunksLen(8) blockType(1) chunkCount(4) */
  long p=4+4;
  uint16_t sl=*(uint16_t*)(body+p); p+=2+sl;
  p+=8+4+8+1;
  uint32_t cc=*(uint32_t*)(body+p); p+=4;
  printf("  chunkCount=%u  条目起始 @0x%lx\n",cc,p);
  for(uint32_t i=0;i<cc;i++){
    char hex[33];
    for(int k=0;k<16;k++) sprintf(hex+k*2,"%02x",body[p+k]);
    hex[32]=0;
    printf("    chunk%-3u md5Name=%s\n",i,hex);
    p+=16;   /* md5Name */
    /* 后续字段长度未知，但只需 name，退出 */
    if(i>=5) break;
  }
  return 0;
}
