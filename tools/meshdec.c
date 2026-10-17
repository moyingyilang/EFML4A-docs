/* ============================================================================
 *  meshdec —— Unity CompressedMesh 的 PackedFloatVector 解码器（纯 C）
 *
 *  格式（见 docs/40）:
 *    PackedFloatVector {
 *      u32  m_NumItems;
 *      f32  m_Range;     // = 1/(max-min)
 *      f32  m_Start;     // = min
 *      u32  m_DataSize;  // Unity Array 的 u32 长度
 *      u8   m_Data[m_DataSize];
 *    }
 *    解量化: value = (q / ((1<<bits)-1)) / range + start
 *            bits  = dataSize*8 / numItems
 *
 *  用法:
 *    meshdec scan  <file> [起] [止]        扫描并列出候选（含解码后的包围盒）
 *    meshdec dec   <file> <偏移>           解码指定偏移的候选
 *    meshdec try   <file> <cx> <cy> <cz> <ex> <ey> <ez>
 *                                          自动找「解码后能重现该包围盒」的候选
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static long long fsz(const char*p){FILE*f=fopen(p,"rb");if(!f)return -1;
  fseek(f,0,SEEK_END);long long s=ftell(f);fclose(f);return s;}
static uint8_t* slurp(const char*p,long long*n){FILE*f=fopen(p,"rb");if(!f)return NULL;
  long long s=fsz(p);uint8_t*b=malloc(s?s:1);if(!b){fclose(f);return NULL;}
  if(fread(b,1,s,f)!=(size_t)s){free(b);fclose(f);return NULL;}fclose(f);*n=s;return b;}

typedef struct { long long pos; uint32_t ni; float rg, st; uint32_t ds; double bits; } Cand;

/* 解码 PackedFloatVector，输出包围盒；返回 1 成功 */
static int decode_at(const uint8_t*b, long long n, long long p, Cand*c,
                     double*x0,double*y0,double*z0,double*x1,double*y1,double*z1){
    memcpy(&c->ni,b+p,4);
    memcpy(&c->rg,b+p+4,4);
    memcpy(&c->st,b+p+8,4);
    memcpy(&c->ds,b+p+12,4);
    c->pos=p;
    if(c->ni==0||c->ni>4000000) return 0;
    if(!(c->rg>0)||!isfinite(c->rg)) return 0;
    if(!isfinite(c->st)) return 0;
    if(c->ds==0||c->ds>40000000) return 0;
    if(p+16+(long long)c->ds>n) return 0;
    /* ★ Unity 用【整数除法】: bitsPerItem = (dataSize*8) / numItems */
    int bits = (int)(((uint64_t)c->ds * 8u) / c->ni);
    if(bits<1||bits>32) return 0;
    c->bits = bits;

    const uint8_t*D=b+p+16;
    uint32_t qmax=(bits>=32)?0xFFFFFFFFu:((1u<<bits)-1);
    double invq = (double)qmax;
    double xa=1e300,ya=1e300,za=1e300,xb=-1e300,yb=-1e300,zb=-1e300;
    uint32_t N=c->ni;
    /* 逐元素读 bits 位（小端位流） */
    for(uint32_t i=0;i<N;i++){
        uint64_t byteIdx = ((uint64_t)i*bits)>>3;
        int bitIdx = (int)(((uint64_t)i*bits)&7);
        uint64_t acc=0; int got=0;
        while(got<bits){
            if(byteIdx>=c->ds) return 0;
            acc |= ((uint64_t)D[byteIdx]) << got;
            got+=8; byteIdx++;
        }
        acc >>= bitIdx;
        uint32_t q = (uint32_t)(acc & (qmax?qmax:0xFFFFFFFFu));
        double v = ((double)q / invq) / (double)c->rg + (double)c->st;
        switch(i%3){
            case 0: if(v<xa)xa=v; if(v>xb)xb=v; break;
            case 1: if(v<ya)ya=v; if(v>yb)yb=v; break;
            case 2: if(v<za)za=v; if(v>zb)zb=v; break;
        }
    }
    *x0=xa;*y0=ya;*z0=za;*x1=xb;*y1=yb;*z1=zb;
    return 1;
}

static int cmd_scan(const char*p, long long a, long long b){
    long long n; uint8_t*B=slurp(p,&n);
    if(!B){perror(p);return 1;}
    if(a<0)a=0; if(b<=0||b>n)b=n;
    printf("  scan %s  [%lld,%lld)\n",p,a,b);
    printf("  %10s %10s %12s %10s %10s %4s  %-26s %s\n",
        "off","numItems","range","start","dataSize","bits","解码包围盒(min..max)","判定");
    int hits=0;
    for(long long i=a;i+20<b;i++){
        Cand c; double x0,y0,z0,x1,y1,z1;
        if(!decode_at(B,n,i,&c,&x0,&y0,&z0,&x1,&y1,&z1)) continue;
        /* 过滤：包围盒要有限且不过分巨大 */
        double dx=x1-x0, dy=y1-y0, dz=z1-z0;
        if(!(dx>0&&dy>0&&dz>0)) continue;
        if(dx>1e4||dy>1e4||dz>1e4) continue;
        printf("  +%9lld %10u %12.6f %10.4f %10u %4d  (%.4f,%.4f,%.4f)..(%.4f,%.4f,%.4f)\n",
            i,c.ni,c.rg,c.st,c.ds,(int)c.bits,x0,y0,z0,x1,y1,z1);
        if(++hits>=60) break;
    }
    printf("  ⇒ 命中 %d 个\n",hits);
    free(B); return 0;
}
static int cmd_try(const char*p, double cx,double cy,double cz, double ex,double ey,double ez){
    long long n; uint8_t*B=slurp(p,&n);
    if(!B){perror(p);return 1;}
    double mn[3]={cx-ex,cy-ey,cz-ez}, mx[3]={cx+ex,cy+ey,cz+ez};
    printf("  目标包围盒 min=(%.4f,%.4f,%.4f) max=(%.4f,%.4f,%.4f)\n",mn[0],mn[1],mn[2],mx[0],mx[1],mx[2]);
    printf("  %10s %10s %12s %10s %4s  %-40s %s\n","off","numItems","range","start","bits","解码包围盒","误差");
    int hits=0, best=-1; double bestErr=1e300; long long bestOff=0;
    for(long long i=0;i+20<n;i++){
        Cand c; double x0,y0,z0,x1,y1,z1;
        if(!decode_at(B,n,i,&c,&x0,&y0,&z0,&x1,&y1,&z1)) continue;
        double dx=x1-x0,dy=y1-y0,dz=z1-z0;
        if(!(dx>0&&dy>0&&dz>0)) continue;
        if(dx>1e4||dy>1e4||dz>1e4) continue;
        /* 误差：中心 + 尺寸，排序后比较（允许轴重排） */
        double dc[3]={(x0+x1)/2,(y0+y1)/2,(z0+z1)/2}, de[3]={dx/2,dy/2,dz/2};
        double tc[3]={cx,cy,cz}, te[3]={ex,ey,ez};
        /* 排序 */
        for(int a=0;a<3;a++)for(int b=a+1;b<3;b++){
            if(dc[a]>dc[b]){double t=dc[a];dc[a]=dc[b];dc[b]=t;}
            if(de[a]>de[b]){double t=de[a];de[a]=de[b];de[b]=t;}
            if(tc[a]>tc[b]){double t=tc[a];tc[a]=tc[b];tc[b]=t;}
            if(te[a]>te[b]){double t=te[a];te[a]=te[b];te[b]=t;}
        }
        double err=0; for(int a=0;a<3;a++) err += fabs(dc[a]-tc[a])+fabs(de[a]-te[a]);
        if(err<bestErr){bestErr=err;best=(int)i;bestOff=i;}
        if(err<0.02){
            printf("  +%9lld %10u %12.6f %10.4f %4d  (%.4f,%.4f,%.4f)..(%.4f,%.4f,%.4f) err=%.5f ★\n",
                i,c.ni,c.rg,c.st,(int)c.bits,x0,y0,z0,x1,y1,z1,err);
            hits++;
            if(hits>=20) break;
        }
    }
    if(hits==0){
        printf("  ⇒ 无 err<0.02 的候选\n");
        if(best>=0){
            Cand c; double x0,y0,z0,x1,y1,z1;
            decode_at(B,n,best,&c,&x0,&y0,&z0,&x1,&y1,&z1);
            printf("  最佳候选 @+%lld: numItems=%u range=%.6f start=%.4f bits=%d\n",
                bestOff,c.ni,c.rg,c.st,(int)c.bits);
            printf("    解码包围盒 (%.4f,%.4f,%.4f)..(%.4f,%.4f,%.4f)  err=%.5f\n",
                x0,y0,z0,x1,y1,z1,bestErr);
        }
    } else printf("  ⇒ 命中 %d 个 ★\n",hits);
    free(B); return 0;
}
int main(int c,char**v){
    if(c<3){ fprintf(stderr,
      "用法: meshdec scan <file> [起] [止]\n"
      "      meshdec try  <file> <cx> <cy> <cz> <ex> <ey> <ez>\n"); return 2; }
    if(!strcmp(v[1],"scan")) return cmd_scan(v[2], c>3?atoll(v[3]):0, c>4?atoll(v[4]):0);
    if(!strcmp(v[1],"try"))  {
        if(c<9){ fprintf(stderr,"  需要 6 个包围盒参数\n"); return 2; }
        return cmd_try(v[2],atof(v[3]),atof(v[4]),atof(v[5]),atof(v[6]),atof(v[7]),atof(v[8]));
    }
    fprintf(stderr,"未知命令\n"); return 2;
}
