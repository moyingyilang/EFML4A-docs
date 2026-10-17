/* dds2c —— 把 DDS 的图像数据（跳过头部）转成 C 数组 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int main(int argc,char**argv){
  if(argc<3){fprintf(stderr,"用法: dds2c <in.dds> <out.h> [符号名]\n");return 2;}
  FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
  fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  uint8_t*d=malloc(n); if(fread(d,1,n,f)!=(size_t)n){fclose(f);return 1;} fclose(f);
  long hdr=128;
  if(n>88 && !memcmp(d+84,"DX10",4)) hdr=148;
  long dn=n-hdr;
  const char* sym = argc>3? argv[3] : "g_modData";
  FILE*o=fopen(argv[2],"w"); if(!o){perror(argv[2]);return 1;}
  fprintf(o,"/* 自动生成：%s 去掉 %ld 字节头 ⇒ %ld 字节图像数据 */\n",argv[1],hdr,dn);
  fprintf(o,"#define MOD_DATA_SIZE %ld\n",dn);
  fprintf(o,"static const unsigned char %s[MOD_DATA_SIZE] = {\n",sym);
  for(long i=0;i<dn;i++){
    if(i%16==0) fprintf(o,"  ");
    fprintf(o,"0x%02x,",d[hdr+i]);
    if(i%16==15) fprintf(o,"\n");
  }
  fprintf(o,"\n};\n");
  fclose(o); free(d);
  printf("  ⇒ %s: %ld 字节图像数据（头 %ld）\n",argv[2],dn,hdr);
  return 0;
}
