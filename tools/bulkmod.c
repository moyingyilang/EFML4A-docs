/* bulkmod —— 在全量替换模式：扫描 .chk，把所有 256KB 高熵块替换成 mod 数据
 *   用法: bulkmod <chk> <modfile> [minent=7.2] [maxblocks=0=全部]
 *   输出: 替换的块数 + 偏移列表
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
static double ent(const uint8_t*p,size_t n){
    size_t c[256]={0}; for(size_t i=0;i<n;i++) c[p[i]]++;
    double e=0; for(int i=0;i<256;i++) if(c[i]){ double q=(double)c[i]/n; e-=q*log2(q);} return e;
}
int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,"用法: bulkmod <chk> <modfile> [minent=7.2] [maxblocks=0]\n"); return 2; }
    const char* chk=argv[1];
    double ment = argc>3? atof(argv[3]) : 7.2;
    long maxb = argc>4? strtol(argv[4],0,0) : 0;
    FILE*m=fopen(argv[2],"rb"); if(!m){perror(argv[2]);return 1;}
    fseek(m,0,SEEK_END); long mn=ftell(m); fseek(m,0,SEEK_SET);
    uint8_t* md=malloc(mn); if(fread(md,1,mn,m)!=(size_t)mn) return 1; fclose(m);
    FILE*f=fopen(chk,"r+b"); if(!f){perror(chk);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f);
    const long BLK=262144;
    uint8_t* buf=malloc(BLK);
    long cnt=0;
    for(long off=0; off+BLK<=n; off+=BLK){
        if(fseek(f,off,SEEK_SET)!=0) break;
        if(fread(buf,1,BLK,f)!=(size_t)BLK) break;
        if(ent(buf,BLK) < ment) continue;
        if(fseek(f,off,SEEK_SET)!=0) break;
        long w = mn<BLK? mn:BLK;
        if(fwrite(md,1,w,f)!=(size_t)w) break;
        printf("  @%ld\n",off); fflush(stdout);
        cnt++;
        if(maxb>0 && cnt>=maxb) break;
    }
    fflush(f); fclose(f); free(buf); free(md);
    fprintf(stderr,"  ⇒ 替换 %ld 个块（阈值 %.2f）\n",cnt,ment);
    return 0;
}
