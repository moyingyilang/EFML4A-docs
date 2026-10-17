/* fextract —— 按 "off len name" 列表并行提取（pread，极快）
 *   用法: fextract <src> <listfile> <outdir> [threads]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
typedef struct { long long off,len; char name[128]; } E;
static E *ents; static int n=0, next_i=0;
static pthread_mutex_t ql=PTHREAD_MUTEX_INITIALIZER;
static int g_fd; static char g_out[256];
static void*worker(void*a){
    (void)a;
    char buf[1<<16];
    for(;;){
        pthread_mutex_lock(&ql);
        if(next_i>=n){pthread_mutex_unlock(&ql);break;}
        int i=next_i++; pthread_mutex_unlock(&ql);
        char p[512]; snprintf(p,sizeof p,"%s/%s",g_out,ents[i].name);
        int o=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(o<0) continue;
        long long rem=ents[i].len, off=ents[i].off;
        while(rem>0){
            size_t chunk=(rem>(long long)sizeof buf)?sizeof buf:(size_t)rem;
            ssize_t r=pread(g_fd,buf,chunk,off);
            if(r<=0) break;
            if(write(o,buf,(size_t)r)!=(ssize_t)r) break;
            off+=r; rem-=r;
        }
        close(o);
    }
    return NULL;
}
int main(int c,char**v){
    if(c<4){fprintf(stderr,"用法: fextract <src> <list> <outdir> [threads]\n");return 2;}
    int nt=(c>=5)?atoi(v[4]):16; if(nt<1)nt=1; if(nt>64)nt=64;
    snprintf(g_out,sizeof g_out,"%s",v[3]); mkdir(g_out,0755);
    FILE*l=fopen(v[2],"r"); if(!l){perror(v[2]);return 1;}
    ents=malloc(sizeof(E)*400000);
    while(fscanf(l,"%lld %lld %127s",&ents[n].off,&ents[n].len,ents[n].name)==3) n++;
    fclose(l);
    printf("  待提取 %d 个（%d 线程）\n",n,nt);
    g_fd=open(v[1],O_RDONLY); if(g_fd<0){perror(v[1]);return 1;}
    pthread_t th[64];
    for(int i=0;i<nt;i++) pthread_create(&th[i],NULL,worker,NULL);
    for(int i=0;i<nt;i++) pthread_join(th[i],NULL);
    close(g_fd);
    printf("  ✓ 完成\n");
    return 0;
}
