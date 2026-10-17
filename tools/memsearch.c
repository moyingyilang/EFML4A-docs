/* ms —— 多线程 /proc/<pid>/mem 字节模式搜索
 *   用法: ms <pid> <hexpat> [max_hits] [threads]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

typedef struct { unsigned long s, e; } M;
static M *maps; static int nmaps=0, next_map=0;
static pthread_mutex_t qlock=PTHREAD_MUTEX_INITIALIZER;
static uint8_t *pat; static size_t plen;
static int maxhit=64, hits=0;
static pthread_mutex_t hlock=PTHREAD_MUTEX_INITIALIZER;
static long pid; static unsigned long long scanned=0;

static void scan(const uint8_t*b,size_t n,unsigned long base){
    if(n<plen) return;
    const uint8_t*p=b,*end=b+n-plen, first=pat[0];
    while(p<=end){
        const uint8_t*q=memchr(p,first,(size_t)(end-p+1));
        if(!q) break;
        if(plen==1||memcmp(q,pat,plen)==0){
            pthread_mutex_lock(&hlock);
            if(hits<maxhit){
                printf("  HIT addr=0x%lx (base=0x%lx +0x%lx)\n",
                    base+(unsigned long)(q-b),base,(unsigned long)(q-b));
                fflush(stdout); hits++;
            }
            pthread_mutex_unlock(&hlock);
        }
        p=q+1;
    }
}
static void*worker(void*a){
    (void)a; char path[64];
    snprintf(path,sizeof path,"/proc/%ld/mem",pid);
    FILE*mem=fopen(path,"rb"); if(!mem) return NULL;
    for(;;){
        pthread_mutex_lock(&qlock);
        if(next_map>=nmaps){pthread_mutex_unlock(&qlock);break;}
        int i=next_map++; pthread_mutex_unlock(&qlock);
        unsigned long s=maps[i].s,e=maps[i].e,len=e-s;
        uint8_t*buf=malloc(len); if(!buf) continue;
        if(fseek(mem,(long)s,SEEK_SET)!=0){free(buf);continue;}
        size_t got=fread(buf,1,len,mem);
        if(got>=plen){ scan(buf,got,s);
            pthread_mutex_lock(&hlock); scanned+=got; pthread_mutex_unlock(&hlock); }
        free(buf);
    }
    fclose(mem); return NULL;
}
int main(int c,char**v){
    if(c<3){fprintf(stderr,"用法: ms <pid> <hexpat> [max=64] [threads=8]\n");return 2;}
    pid=atol(v[1]); const char*h=v[2]; size_t hl=strlen(h);
    if(!hl||hl%2){fprintf(stderr,"hex 须偶数长度\n");return 2;}
    plen=hl/2; pat=malloc(plen);
    for(size_t i=0;i<plen;i++){unsigned x=0;sscanf(h+i*2,"%2x",&x);pat[i]=(uint8_t)x;}
    if(c>=4) maxhit=atoi(v[3]);
    int nt=(c>=5)?atoi(v[4]):8; if(nt<1)nt=1; if(nt>64)nt=64;
    char path[64]; snprintf(path,sizeof path,"/proc/%ld/maps",pid);
    FILE*m=fopen(path,"r"); if(!m){perror(path);return 1;}
    maps=malloc(sizeof(M)*65536); char line[512]; unsigned long long tot=0;
    while(fgets(line,sizeof line,m)&&nmaps<65536){
        unsigned long s=0,e=0; char perm[8]={0};
        if(sscanf(line,"%lx-%lx %7s",&s,&e,perm)!=3) continue;
        if(perm[0]!='r') continue;
        unsigned long len=e-s; if(!len) continue;
        if(len>(1UL<<30)) continue;
        maps[nmaps].s=s; maps[nmaps].e=e; nmaps++; tot+=len;
    }
    fclose(m);
    printf("  PID=%ld maps=%d total=%.1fMB pat=%zuB threads=%d\n",
        pid,nmaps,tot/1048576.0,plen,nt); fflush(stdout);
    pthread_t th[64];
    for(int i=0;i<nt;i++) pthread_create(&th[i],NULL,worker,NULL);
    for(int i=0;i<nt;i++) pthread_join(th[i],NULL);
    printf("  scanned %.1fMB hits=%d%s\n",scanned/1048576.0,hits,hits?"":" (not found)");
    return hits>0?0:1;
}
