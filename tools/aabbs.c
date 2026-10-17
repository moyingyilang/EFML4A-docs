/* aabbs —— 扫描文件里所有「6×float32 包围盒」模式，并与给定 extent 匹配
 * 用法: aabbs <file> <cx> <cy> <cz> <ex> <ey> <ez> [容差]
 * 判据: 6 个 float 合理、extent>0、center 与 extent 尺度合理
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
int main(int c,char**v){
    if(c<8){ fprintf(stderr,"用法: aabbs <file> <cx> <cy> <cz> <ex> <ey> <ez> [tol]\n"); return 2; }
    const char*path=v[1];
    float tc[3]={atof(v[2]),atof(v[3]),atof(v[4])};
    float te[3]={atof(v[5]),atof(v[6]),atof(v[7])};
    float tol = c>8?atof(v[8]):0.30f;
    FILE*f=fopen(path,"rb"); if(!f){perror(path);return 1;}
    fseek(f,0,SEEK_END); long long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*b=malloc(n); if(!b){fclose(f);return 1;}
    if(fread(b,1,n,f)!=(size_t)n){fclose(f);free(b);return 1;}
    fclose(f);
    printf("  目标 center=(%.4f,%.4f,%.4f) extent=(%.4f,%.4f,%.4f)  tol=%.3f\n",
        tc[0],tc[1],tc[2],te[0],te[1],te[2],tol);
    printf("  %12s  %-46s %s\n","off","AABB (center | extent)","相对误差");
    int hits=0;
    for(long long p=0;p+24<=n;p+=4){
        float v6[6]; memcpy(v6,b+p,24);
        if(!isfinite(v6[0])||!isfinite(v6[1])||!isfinite(v6[2])) continue;
        if(!isfinite(v6[3])||!isfinite(v6[4])||!isfinite(v6[5])) continue;
        if(v6[3]<=0||v6[4]<=0||v6[5]<=0) continue;
        if(v6[3]>1000||v6[4]>1000||v6[5]>1000) continue;
        if(fabsf(v6[0])>10000||fabsf(v6[1])>10000||fabsf(v6[2])>10000) continue;
        /* 相对误差（排序后比较，允许轴重排） */
        float ce[3]={v6[3],v6[4],v6[5]}, t2[3]={te[0],te[1],te[2]};
        for(int a=0;a<3;a++)for(int d=a+1;d<3;d++){ if(ce[a]>ce[d]){float t=ce[a];ce[a]=ce[d];ce[d]=t;} if(t2[a]>t2[d]){float t=t2[a];t2[a]=t2[d];t2[d]=t;} }
        float err=0; for(int a=0;a<3;a++) err += fabsf(ce[a]-t2[a])/(t2[a]>1e-6f?t2[a]:1e-6f);
        err/=3;
        if(err<tol){
            printf("  +%11lld  (%.4f,%.4f,%.4f | %.4f,%.4f,%.4f)  %.1f%%\n",
                p,v6[0],v6[1],v6[2],v6[3],v6[4],v6[5],err*100);
            if(++hits>=40) break;
        }
    }
    printf("  ⇒ 命中 %d 个\n",hits);
    free(b); return 0;
}
