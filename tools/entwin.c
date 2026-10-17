/* entwin —— 在文件里滑窗找高熵区（找纹理数据）
 *   用法: entwin <file> [win=262144] [step=65536] [minent=7.0] [threads] [maxcand=20]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
static uint8_t* g_buf=0; static long g_n=0,g_win=262144,g_step=65536; static double g_ment=7.0;
static pthread_mutex_t g_mu=PTHREAD_MUTEX_INITIALIZER; static int g_found=0,g_maxc=20;
static double ent(const uint8_t*p,size_t n){
    size_t c[256]={0}; for(size_t i=0;i<n;i++) c[p[i]]++;
    double e=0; for(int i=0;i<256;i++) if(c[i]){ double q=(double)c[i]/n; e-=q*log2(q);} return e;
}
typedef struct { long s,e; } R;
static void* worker(void* a){
    R* r=(R*)a;
    for(long i=r->s; i+g_win<=r->e; i+=g_step){
        double e=ent(g_buf+i,g_win);
        if(e>=g_ment){
            pthread_mutex_lock(&g_mu);
            printf("  @%ld  熵=%.4f  ★★ 高熵区\n",i,e); fflush(stdout);
            int f=++g_found; pthread_mutex_unlock(&g_mu);
            if(f>=g_maxc) return 0;
        }
    }
    return 0;
}
int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"用法: entwin <file> [win] [step] [minent] [threads] [maxcand]\n"); return 2; }
    if(argc>2) g_win=strtol(argv[2],0,0);
    if(argc>3) g_step=strtol(argv[3],0,0);
    if(argc>4) g_ment=atof(argv[4]);
    int nt=argc>5?atoi(argv[5]):0;
    if(argc>6) g_maxc=atoi(argv[6]);
    if(nt<=0){ nt=(int)sysconf(_SC_NPROCESSORS_ONLN); if(nt<1)nt=1; if(nt>16)nt=16; }
    FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
    fseek(f,0,SEEK_END); g_n=ftell(f); fseek(f,0,SEEK_SET);
    g_buf=malloc(g_n);
    if(fread(g_buf,1,g_n,f)!=(size_t)g_n){ fprintf(stderr,"读失败\n"); return 1; }
    fclose(f);
    printf("  文件 %ld, 窗 %ld, 步 %ld, 熵阈值 %.2f, 线程 %d\n",g_n,g_win,g_step,g_ment,nt);
    long span=g_n/nt+1;
    pthread_t* th=malloc(sizeof(pthread_t)*nt); R* rs=malloc(sizeof(R)*nt);
    for(int k=0;k<nt;k++){
        rs[k].s=(long)k*span; rs[k].e=(k==nt-1)?g_n:(long)(k+1)*span;
        pthread_create(&th[k],0,worker,&rs[k]);
    }
    for(int k=0;k<nt;k++) pthread_join(th[k],0);
    if(!g_found) printf("  （没有熵 >= %.2f 的窗口）\n",g_ment);
    free(th); free(rs); free(g_buf); return 0;
}
