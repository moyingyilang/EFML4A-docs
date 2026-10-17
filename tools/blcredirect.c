/* blcredirect —— 重定向 .blc 里的文件条目（指向另一个 .ab 的数据）
 *   用法: blcredirect <in.blc> <out.blc> <目标文件名> <chunkName_hex32> <offset> <length>
 *
 * 原理: 修改 file entry 的 chunkName/offset/length
 *       ⇒ 游戏按目标路径加载时，实际读到的是另一个 .ab 的数据
 *       ⇒ 两个 .ab 都是原始的 ⇒ 结构校验天然通过
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const uint8_t VK[32]={0xe9,0x5b,0x31,0x7a,0xc4,0xf8,0x28,0x56,0x9d,0x23,0xa8,0x6b,0xf2,0x71,0xdc,0xb5,0x3e,0x84,0x6f,0xa7,0x5c,0x92,0x4d,0x67,0x1d,0xba,0x8e,0x38,0xf4,0xca,0x52,0xe1};
static uint32_t rotl32(uint32_t x,int n){return (x<<n)|(x>>(32-n));}
static void qr(uint32_t*s,int a,int b,int c,int d){
  s[a]+=s[b];s[d]^=s[a];s[d]=rotl32(s[d],16);
  s[c]+=s[d];s[b]^=s[c];s[b]=rotl32(s[b],12);
  s[a]+=s[b];s[d]^=s[a];s[d]=rotl32(s[d],8);
  s[c]+=s[d];s[b]^=s[c];s[b]=rotl32(s[b],7);
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
static uint32_t crc32_of(const uint8_t*p,size_t n){
  uint32_t c=0xFFFFFFFF;
  for(size_t i=0;i<n;i++){c^=p[i];for(int k=0;k<8;k++)c=(c>>1)^(0xEDB88320&(-(int32_t)(c&1)));}
  return ~c;
}
static int hex2bin(const char*h,uint8_t*o,int n){
  for(int i=0;i<n;i++){unsigned x=0;if(sscanf(h+i*2,"%2x",&x)!=1)return -1;o[i]=(uint8_t)x;}
  return 0;
}
int main(int argc,char**argv){
  if(argc<7){
    fprintf(stderr,"用法: blcredirect <in.blc> <out.blc> <目标文件名> <chunkName_hex32> <offset> <length>\n");
    return 2;
  }
  FILE*f=fopen(argv[1],"rb");
  if(!f){perror(argv[1]);return 1;}
  fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
  uint8_t*b=malloc(n);
  if(!b){fclose(f);return 1;}
  if(fread(b,1,n,f)!=(size_t)n){fclose(f);return 1;}
  fclose(f);

  uint8_t nonce[12];memcpy(nonce,b,12);
  size_t bl=n-12;
  uint8_t*body=malloc(bl);
  if(!body){free(b);return 1;}
  memcpy(body,b+12,bl);
  cck(nonce,body,bl);

  uint8_t cn[16];
  if(hex2bin(argv[4],cn,16)!=0){fprintf(stderr,"chunkName 须 32 hex\n");return 2;}
  uint64_t noff=strtoull(argv[5],0,10);
  uint64_t nlen=strtoull(argv[6],0,10);

  const char*t=argv[3];
  size_t tl=strlen(t);
  long hit=-1;
  for(size_t i=0;i+tl<=bl;i++) if(memcmp(body+i,t,tl)==0){hit=(long)i;break;}
  if(hit<0){fprintf(stderr,"  未找到文件名 %s\n",t);return 1;}

  /* 名字后布局: nameHash(8) chunkName(16) dataMd5(16) off(8) len(8) bt(1) enc(1) */
  size_t p=(size_t)hit+tl;
  printf("  找到 @+0x%lx\n",hit);
  printf("    旧 chunkName = ");for(int k=8;k<24;k++)printf("%02X",body[p+k]);printf("\n");
  printf("    新 chunkName = ");for(int k=0;k<16;k++)printf("%02X",cn[k]);printf("\n");
  printf("    旧 off/len   = %llu / %llu\n",
    (unsigned long long)(*(uint64_t*)(body+p+40)),
    (unsigned long long)(*(uint64_t*)(body+p+48)));
  printf("    新 off/len   = %llu / %llu\n",
    (unsigned long long)noff,(unsigned long long)nlen);

  memcpy(body+p+8, cn, 16);
  memcpy(body+p+40, &noff, 8);
  memcpy(body+p+48, &nlen, 8);

  uint32_t nc=crc32_of(body,bl-4);
  memcpy(body+bl-4,&nc,4);
  printf("    新 CRC = 0x%08x\n",nc);

  cck(nonce,body,bl);
  FILE*g=fopen(argv[2],"wb");
  if(!g){perror(argv[2]);free(b);free(body);return 1;}
  int ok=1;
  if(fwrite(nonce,1,12,g)!=12)ok=0;
  if(ok && fwrite(body,1,bl,g)!=bl)ok=0;
  fclose(g);
  if(!ok){fprintf(stderr,"写失败\n");free(b);free(body);return 1;}
  printf("  ⇒ 已写出 %s (%ld 字节)\n",argv[2],n);
  free(b);free(body);
  return 0;
}
