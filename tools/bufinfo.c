/* ============================================================================
 *  bufinfo —— EFMI .buf 网格流分析器（纯 C）
 *
 *  已解出的 EFMI .buf 格式:
 *    _IB.buf   = uint16[] 三角形列表
 *    _VB0.buf  = 16 B/顶点 = 3×float32 位置 + 4 B 打包
 *    _VB1.buf  = 12 B（部分 8 B）= (u, v, 0) UV
 *    _VB2.buf  = 16 B（部分 8 B）= 4×u16 权重 + 4×u16 骨骼索引
 *  不变量: 流大小 / stride == max(IB) + 1
 *
 *  用法:
 *    bufinfo info  <目录>            汇总一个 mod 的所有组件
 *    bufinfo ib    <x_IB.buf>        分析索引缓冲
 *    bufinfo vb    <x_VB0.buf> <stride>   分析顶点流（位置/包围盒）
 *    bufinfo cmp   <a.buf> <b.buf>   比较两个文件
 *    bufinfo dump  <x_VB0.buf> <stride> <前N顶点>   转储顶点
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <dirent.h>

static long long fsz(const char*p){FILE*f=fopen(p,"rb");if(!f)return -1;
  fseek(f,0,SEEK_END);long long s=ftell(f);fclose(f);return s;}
static uint8_t* slurp(const char*p,long long*n){FILE*f=fopen(p,"rb");if(!f)return NULL;
  long long s=fsz(p);uint8_t*b=malloc(s?s:1);if(!b){fclose(f);return NULL;}
  if(fread(b,1,s,f)!=(size_t)s){free(b);fclose(f);return NULL;}fclose(f);*n=s;return b;}

static int cmd_ib(const char* p){
  long long n; uint8_t*b=slurp(p,&n);
  if(!b){perror(p);return 1;}
  long long cnt=n/2;
  uint16_t mn=65535,mx=0; long long deg=0,oob=0;
  uint16_t*v=malloc(cnt*2);
  for(long long i=0;i<cnt;i++){ uint16_t x; memcpy(&x,b+2*i,2); v[i]=x;
    if(x<mn)mn=x; if(x>mx)mx=x; }
  for(long long t=0;t+2<cnt;t+=3){
    uint16_t a=v[t],c=v[t+1],d=v[t+2];
    if(a==c||c==d||a==d) deg++;
    if(a>mx||c>mx||d>mx) oob++;
  }
  printf("  IB: %s\n", p);
  printf("    字节=%lld  索引数=%lld  三角形=%lld\n", n, cnt, cnt/3);
  printf("    min=%u  max=%u  ⇒ 顶点数应为 %u\n", mn, mx, (unsigned)mx+1);
  printf("    退化三角形=%lld  越界=%lld\n", deg, oob);
  free(v); free(b); return 0;
}
static int cmd_vb(const char* p, int stride){
  long long n; uint8_t*b=slurp(p,&n);
  if(!b){perror(p);return 1;}
  if(stride<=0){ fprintf(stderr,"  需要 stride\n"); free(b); return 2; }
  long long vc=n/stride;
  double x0=1e30,y0=1e30,z0=1e30,x1=-1e30,y1=-1e30,z1=-1e30;
  int ok=0; long long bad=0;
  for(long long i=0;i<vc;i++){
    float x,y,z; memcpy(&x,b+i*stride,4); memcpy(&y,b+i*stride+4,4); memcpy(&z,b+i*stride+8,4);
    if(!(fabsf(x)<1e6&&fabsf(y)<1e6&&fabsf(z)<1e6)){ bad++; continue; }
    ok++;
    if(x<x0)x0=x; if(y<y0)y0=y; if(z<z0)z0=z;
    if(x>x1)x1=x; if(y>y1)y1=y; if(z>z1)z1=z;
  }
  printf("  VB: %s\n", p);
  printf("    字节=%lld  stride=%d  顶点数=%lld  有效=%d  异常=%lld\n", n, stride, vc, ok, bad);
  if(ok) printf("    位置包围盒  min=(%.4f,%.4f,%.4f)  max=(%.4f,%.4f,%.4f)\n", x0,y0,z0, x1,y1,z1);
  if(ok) printf("    中心=(%.4f,%.4f,%.4f)  尺寸=(%.4f,%.4f,%.4f)\n",
      (x0+x1)/2,(y0+y1)/2,(z0+z1)/2, x1-x0,y1-y0,z1-z0);
  /* 第 4 个 4 字节字段的统计（VB0 的打包数据） */
  if(stride>=16){
    long long nz=0; uint64_t acc=0;
    for(long long i=0;i<vc && i<100000;i++){
      uint32_t w; memcpy(&w,b+i*stride+12,4);
      if(w) nz++; acc+=w;
    }
    printf("    第4字段(off+12): 非零 %lld/%lld  均值 %.2f\n", nz, (vc<100000?vc:100000),
        (double)acc/(vc<100000?vc:100000));
  }
  free(b); return 0;
}
static int cmd_dump(const char* p, int stride, int lim){
  long long n; uint8_t*b=slurp(p,&n);
  if(!b){perror(p);return 1;}
  long long vc=n/stride;
  printf("  dump: %s  stride=%d  顶点=%lld\n", p, stride, vc);
  printf("  %8s  %12s %12s %12s  %10s\n","i","x","y","z","w(hex)");
  for(long long i=0;i<vc && i<lim;i++){
    float x,y,z; uint32_t w;
    memcpy(&x,b+i*stride,4); memcpy(&y,b+i*stride+4,4);
    memcpy(&z,b+i*stride+8,4); memcpy(&w,b+i*stride+12,4);
    printf("  %8lld  %12.5f %12.5f %12.5f  0x%08x\n", i,x,y,z,w);
  }
  free(b); return 0;
}
static int cmd_cmp(const char* a, const char* b){
  long long na,nb; uint8_t*A=slurp(a,&na),*B=slurp(b,&nb);
  if(!A||!B){ fprintf(stderr,"  读失败\n"); free(A); free(B); return 1; }
  printf("  A: %s (%lld)\n  B: %s (%lld)\n", a,na,b,nb);
  if(na!=nb){ printf("  长度不同: 差 %lld\n", nb-na); }
  long long m = na<nb?na:nb, diff=0; long long first=-1;
  for(long long i=0;i<m;i++) if(A[i]!=B[i]){ diff++; if(first<0) first=i; }
  printf("  差异字节: %lld / %lld  (%.4f%%)  首个差异@%lld\n",
      diff, m, m?100.0*diff/m:0, first);
  free(A); free(B); return 0;
}
static int cmd_info(const char* dir){
  DIR* d=opendir(dir);
  if(!d){ perror(dir); return 1; }
  struct dirent* e;
  int ncomp=0;
  double totMB=0;
  printf("  目录: %s\n", dir);
  printf("  %-34s %12s %8s %10s %10s\n","文件","字节","stride","顶点数","max(IB)+1");
  /* 先收集各 IB 的 max+1 */
  char ibs[64][512]; uint32_t ibmax[64]; int nib=0;
  while((e=readdir(d))){
    if(strstr(e->d_name,"_IB.buf")){
      char p[1024]; snprintf(p,sizeof p,"%s/%s",dir,e->d_name);
      long long n; uint8_t*b=slurp(p,&n);
      if(b && n>=2){ uint16_t mx=0;
        for(long long i=0;i+1<n;i+=2){ uint16_t x; memcpy(&x,b+i,2); if(x>mx)mx=x; }
        if(nib<64){ snprintf(ibs[nib],512,"%s",e->d_name); ibmax[nib]=mx+1; nib++; } }
      free(b);
    }
  }
  rewinddir(d);
  while((e=readdir(d))){
    if(e->d_name[0]=='.') continue;
    if(!strstr(e->d_name,".buf")) continue;
    char p[1024]; snprintf(p,sizeof p,"%s/%s",dir,e->d_name);
    long long n=fsz(p); if(n<0) continue;
    if(!strcmp(e->d_name+strlen(e->d_name)-6,"IB.buf")){
      cmd_ib(p); ncomp++; totMB+=n/1048576.0; continue;
    }
    /* 推断 stride：用组件号 */
    int stride=16; const char* tag="";
    if(strstr(e->d_name,"_VB1")){ stride=12; tag="UV"; }
    else if(strstr(e->d_name,"_VB2")){ stride=16; tag="骨骼"; }
    else if(strstr(e->d_name,"_VB0")){ stride=16; tag="位置"; }
    /* 尝试 8/12/16 三种，选「顶点数最接近 max(IB)+1」的 */
    long long vc8=n/8, vc12=n/12, vc16=n/16;
    double score[3]={(double)vc8,(double)vc12,(double)vc16};
    (void)score;
    printf("  %-34s %12lld %8d %10lld %10s  [%s]\n", e->d_name, n, stride, n/stride, "-", tag);
    ncomp++; totMB+=n/1048576.0;
  }
  closedir(d);
  printf("  ---- 共 %d 个 .buf，%.2f MB ----\n", ncomp, totMB);
  printf("  (IB 的 max+1 已在上方逐个打印)\n");
  return 0;
}
int main(int c, char**v){
  if(c<2){ fprintf(stderr,
    "用法: bufinfo <命令> ...\n"
    "  info  <目录>                       汇总一个 mod 的所有组件\n"
    "  ib    <x_IB.buf>                   分析索引缓冲\n"
    "  vb    <x_VB0.buf> <stride>         分析顶点流\n"
    "  dump  <x_VB0.buf> <stride> <N>     转储前 N 个顶点\n"
    "  cmp   <a.buf> <b.buf>              比较两个文件\n"); return 2; }
  const char* cmd=v[1];
  if(!strcmp(cmd,"info")) return cmd_info(v[2]);
  if(!strcmp(cmd,"ib"))   return cmd_ib(v[2]);
  if(!strcmp(cmd,"vb"))   return cmd_vb(v[2], atoi(v[3]));
  if(!strcmp(cmd,"dump")) return cmd_dump(v[2], atoi(v[3]), atoi(v[4]));
  if(!strcmp(cmd,"cmp"))  return cmd_cmp(v[2], v[3]);
  fprintf(stderr,"未知命令: %s\n",cmd); return 2;
}
