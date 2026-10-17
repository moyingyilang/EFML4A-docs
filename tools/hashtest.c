/* hashtest —— 对文件试多种 32 位哈希，寻找匹配目标值
 *   用法: hashtest <file> <target_hex> [maxlen]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint32_t fnv1a32(const uint8_t*p,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;i++){h^=p[i];h*=16777619u;}return h;}
static uint32_t fnv1_32(const uint8_t*p,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;i++){h*=16777619u;h^=p[i];}return h;}
static uint32_t djb2(const uint8_t*p,size_t n){uint32_t h=5381u;for(size_t i=0;i<n;i++)h=((h<<5)+h)+p[i];return h;}
static uint32_t djb2x(const uint8_t*p,size_t n){uint32_t h=5381u;for(size_t i=0;i<n;i++)h=((h<<5)+h)^p[i];return h;}
static uint32_t sdbm(const uint8_t*p,size_t n){uint32_t h=0;for(size_t i=0;i<n;i++)h=p[i]+(h<<6)+(h<<16)-h;return h;}
static uint32_t adler32(const uint8_t*p,size_t n){uint32_t a=1,b=0;for(size_t i=0;i<n;i++){a=(a+p[i])%65521;b=(b+a)%65521;}return (b<<16)|a;}
static uint32_t crct[256];static int ci=0;
static void mkcrc(void){if(ci)return;for(uint32_t i=0;i<256;i++){uint32_t c=i;for(int k=0;k<8;k++)c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1);crct[i]=c;}ci=1;}
static uint32_t crc32f(const uint8_t*p,size_t n){mkcrc();uint32_t c=0xFFFFFFFFu;for(size_t i=0;i<n;i++)c=crct[(c^p[i])&0xff]^(c>>8);return c^0xFFFFFFFFu;}
static uint32_t mm3(const uint8_t*data,size_t len,uint32_t seed){
 uint32_t h=seed,c1=0xcc9e2d51,c2=0x1b873593;
 size_t nblk=len/4;
 for(size_t i=0;i<nblk;i++){
  uint32_t k; memcpy(&k,data+i*4,4);
  k*=c1; k=(k<<15)|(k>>17); k*=c2;
  h^=k; h=(h<<13)|(h>>19); h=h*5+0xe6546b64; }
 const uint8_t*tail=data+nblk*4; uint32_t k=0;
 switch(len&3){case 3:k^=tail[2]<<16;case 2:k^=tail[1]<<8;case 1:k^=tail[0];
  k*=c1;k=(k<<15)|(k>>17);k*=c2;h^=k;}
 h^=(uint32_t)len; h^=h>>16; h*=0x85ebca6b; h^=h>>13; h*=0xc2b2ae35; h^=h>>16;
 return h;
}
int main(int argc,char**argv){
 if(argc<3){fprintf(stderr,"用法: hashtest <file> <target_hex> [maxlen]\n");return 2;}
 uint32_t tgt=(uint32_t)strtoul(argv[2],0,16);
 FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
 fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
 uint8_t*b=malloc(n); if(fread(b,1,n,f)!=(size_t)n){fclose(f);return 1;} fclose(f);
 long maxlen=argc>3?strtol(argv[3],0,0):n;
 printf("  文件 %ld 字节, 目标 0x%08x, maxlen=%ld\n",n,tgt,maxlen);
 /* 试多种长度 */
 long lens[8]={n, 16524L*2, 16524L*4, maxlen, n/2, 4096, 1024, 256};
 const char*names[7]={"fnv1a32","fnv1_32","djb2","djb2x","sdbm","adler32","crc32"};
 uint32_t(*fns[7])(const uint8_t*,size_t)={fnv1a32,fnv1_32,djb2,djb2x,sdbm,adler32,crc32f};
 printf("\n  %-9s %-10s %s\n","哈希","长度","值");
 int hit=0;
 for(int L=0;L<8;L++){
   long ln=lens[L]; if(ln<=0||ln>n) continue;
   for(int k=0;k<7;k++){
     uint32_t v=fns[k](b,ln);
     int m=(v==tgt);
     printf("  %-9s %-10ld 0x%08x%s\n",names[k],ln,v,m?"  ★★★ 匹配！":"");
     if(m) hit=1;
   }
   uint32_t v3=mm3(b,ln,0); if(v3==tgt){printf("  %-9s %-10ld 0x%08x  ★★★ 匹配！\n","murmur3",ln,v3);hit=1;}
 }
 if(!hit) printf("\n  ⇒ 未匹配（可能需要别的算法/长度/种子）\n");
 free(b); return 0;
}
