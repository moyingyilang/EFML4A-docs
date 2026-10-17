/* texfind —— 多线程找「int32 长度标记 + 高熵数据」的纹理候选
 *   用法: texfind <file> [size=262144] [minent=7.0] [threads=auto] [maxcand=40]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

typedef struct { const uint8_t* b; long n; long want; double ment; long start; long end; int id; } Job;
static uint8_t* g_buf=0; static long g_n=0, g_want=262144; static double g_ment=7.0;
static pthread_mutex_t g_mu=PTHREAD_MUTEX_INITIALIZER;
static int g_found=0, g_maxc=40;
static FILE* g_out=0;

static double ent(const uint8_t*p,size_t n){
    size_t c[256]={0};
    for(size_t i=0;i<n;i++) c[p[i]]++;
    double e=0; for(int i=0;i<256;i++) if(c[i]){ double q=(double)c[i]/n; e-=q*log2(q);} return e;
}
static void* worker(void* arg){
    Job* j=(Job*)arg;
    long i=j->start;
    for(; i+4+j->want <= j->end; i++){
        uint32_t v; memcpy(&v,g_buf+i,4);
        if(v!=(uint32_t)g_want) continue;
        double e=ent(g_buf+i+4,j->want);
        if(e>=g_ment){
            pthread_mutex_lock(&g_mu);
            fprintf(g_out,"  @%ld  size=%ld  熵=%.4f  ★★ 强候选\n",i,j->want,e);
            fflush(g_out);
            int f=++g_found;
            pthread_mutex_unlock(&g_mu);
            if(f>=g_maxc) return 0;
        }
    }
    return 0;
}
int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"用法: texfind <file> [size] [minent] [threads] [maxcand]\n"); return 2; }
    if(argc>2) g_want=strtol(argv[2],0,0);
    if(argc>3) g_ment=atof(argv[3]);
    int nt = argc>4? atoi(argv[4]) : 0;
    if(argc>5) g_maxc=atoi(argv[5]);
    if(nt<=0){ nt=(int)sysconf(_SC_NPROCESSORS_ONLN); if(nt<1)nt=1; if(nt>16)nt=16; }
    FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
    fseek(f,0,SEEK_END); g_n=ftell(f); fseek(f,0,SEEK_SET);
    g_buf=malloc(g_n);
    if(fread(g_buf,1,g_n,f)!=(size_t)g_n){ fprintf(stderr,"读失败\n"); return 1; }
    fclose(f);
    g_out=stdout;
    printf("  文件 %ld 字节, 目标 %ld, 熵阈值 %.2f, 线程 %d\n",g_n,g_want,g_ment,nt);
    pthread_t* th=malloc(sizeof(pthread_t)*nt);
    Job* jobs=malloc(sizeof(Job)*nt);
    long chunk=(g_n/(long)nt)+1;
    for(int k=0;k<nt;k++){
        jobs[k].start=(long)k*chunk;
        jobs[k].end=(k==nt-1)? g_n : (long)(k+1)*chunk;
        jobs[k].id=k;
        pthread_create(&th[k],0,worker,&jobs[k]);
    }
    for(int k=0;k<nt;k++) pthread_join(th[k],0);
    if(!g_found) printf("  （没有熵 >= %.2f 的候选）\n",g_ment);
    free(th); free(jobs); free(g_buf);
    return 0;
}
