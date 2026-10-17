/* ============================================================================
 *  floatscan —— 在文件里找「密集 float32 段」（纯 C，快）
 *
 *  判据: 连续 N 个 stride 字节的块，其第 0/4/8 字节都是合理 float32
 *
 *  用法: floatscan <file> [stride] [最少块数] [范围下限] [范围上限]
 *        默认 stride=16  最少块数=200  [-100,100]
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"用法: floatscan <file> [stride] [minBlocks] [lo] [hi]\n"); return 2; }
    const char* path=argv[1];
    int stride = argc>2?atoi(argv[2]):16;
    int minB   = argc>3?atoi(argv[3]):200;
    double lo  = argc>4?atof(argv[4]):-100.0;
    double hi  = argc>5?atof(argv[5]): 100.0;
    if(stride<4) stride=16;

    FILE* f=fopen(path,"rb");
    if(!f){ perror(path); return 1; }
    fseek(f,0,SEEK_END); long long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t* b=malloc(n);
    if(!b){ fclose(f); return 1; }
    if(fread(b,1,n,f)!=(size_t)n){ fclose(f); free(b); return 1; }
    fclose(f);

    printf("  file=%s  size=%lld  stride=%d  minBlocks=%d  range=[%.1f,%.1f]\n",
           path,n,stride,minB,lo,hi);
    printf("  %12s %10s %12s  %s\n","off","blocks","bytes","(16进制)");

    long long p=0; int found=0;
    while(p+(long long)stride*minB < n){
        long long c=0;
        while(1){
            long long q=p+c*stride;
            if(q+12>n) break;
            float x,y,z; memcpy(&x,b+q,4); memcpy(&y,b+q+4,4); memcpy(&z,b+q+8,4);
            if(!(x>lo&&x<hi&&y>lo&&y<hi&&z>lo&&z<hi)) break;
            if(!isfinite(x)||!isfinite(y)||!isfinite(z)) break;
            /* ★ 排除 denormal / 过小值：真实坐标不会三个分量都 ≈0 */
            if(fabsf(x)<1e-6f && fabsf(y)<1e-6f && fabsf(z)<1e-6f) break;
            c++;
        }
        if(c>=(long long)minB){
            printf("  +%11lld %10lld %12lld  0x%llx\n",p,c,c*(long long)stride,(unsigned long long)p);
            if(++found>=80) break;
        }
        p += (c>0? c*(long long)stride : (long long)stride);
    }
    if(found==0) printf("  （无 ≥%d 块的密集 float 段）\n",minB);
    else printf("  ⇒ 共 %d 段\n",found);
    free(b);
    return 0;
}
