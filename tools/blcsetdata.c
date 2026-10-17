/* blcsetdata —— 更新 .blc 里某个【文件条目】的 dataMd5，并重算 CRC32
 *   用法: blcsetdata <in.blc> <out.blc> <filename> <new_datamd5_hex32>
 *
 * 条目布局（实测）:
 *   nameLen(u16) + name(bytes) | nameHash(8) | chunkName(16) | dataMd5(16)
 *   | offset(i64) | len(i64) | blockType(u8) | encrypt(u8) | [ivSeed(i64)]
 */
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
static uint32_t crc32_of(const uint8_t*p,size_t n){
  uint32_t c=0xFFFFFFFF;
  for(size_t i=0;i<n;i++){c^=p[i];for(int k=0;k<8;k++)c=(c>>1)^(0xEDB88320&(-(int32_t)(c&1)));}
  return ~c;
}
static int hex2bin(const char*h,uint8_t*o,int n){
  for(int i=0;i<n;i++){unsigned x=0;if(sscanf(h+i*2,"%2x",&x)!=1)return -1;o[i]=(uint8_t)x;}
  return 0;
}
int main(int c,char**v){
  if(load_key()) return 1;
  if(c<5){fprintf(stderr,"用法: blcsetdata <in.blc> <out.blc> <filename> <new_datamd5_hex32>\n");return 2;}
  FILE*f=fopen(v[1],"rb");if(!f){perror(v[1]);return 1;}fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc(n);if(fread(b,1,n,f)!=(size_t)n)return 1;fclose(f);
  uint8_t nonce[12];memcpy(nonce,b,12);
  size_t bl=n-12;uint8_t*body=malloc(bl);memcpy(body,b+12,bl);
  cx(nonce,body,bl);
  uint8_t nm[16]; if(hex2bin(v[4],nm,16)!=0){fprintf(stderr,"md5 须 32 hex\n");return 2;}
  const char*t=v[3];size_t tl=strlen(t);
  long hit=-1;
  for(size_t i=0;i+tl<=bl;i++) if(memcmp(body+i,t,tl)==0){hit=(long)i;break;}
  if(hit<0){fprintf(stderr,"  未找到文件名 %s\n",t);return 1;}
  size_t p=(size_t)hit+tl;   /* 名字后 */
  printf("  找到 @+0x%lx，名字后布局:\n",hit);
  /* char o[33]; 移除未使用变量 */
  printf("    nameHash  = ");for(int k=0;k<8;k++)printf("%02X",body[p+k]);printf("\n");
  printf("    chunkName = ");for(int k=8;k<24;k++)printf("%02X",body[p+k]);printf("\n");
  printf("    dataMd5   = ");for(int k=24;k<40;k++)printf("%02X",body[p+k]);printf("  (旧)\n");
  printf("                ");for(int k=0;k<16;k++)printf("%02X",nm[k]);printf("  (新)\n");
  memcpy(body+p+24,nm,16);
  uint32_t nc=crc32_of(body,bl-4);
  memcpy(body+bl-4,&nc,4);
  printf("    新 CRC    = 0x%08x\n",nc);
  cx(nonce,body,bl);
  FILE*o2=fopen(v[2],"wb");if(!o2){perror(v[2]);return 1;}if(fwrite(nonce,1,12,o2)!=12){fprintf(stderr,"写 nonce 失败\n");return 1;}if(fwrite(body,1,bl,o2)!=bl){fprintf(stderr,"写 body 失败\n");return 1;}fclose(o2);
  printf("  ⇒ 已写出 %s (%ld 字节)\n",v[2],n);
  return 0;
}
