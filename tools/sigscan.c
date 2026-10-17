// 用「条目头部签名」扫描 .chk，并与映射交叉验证。
//
// 已实测确认的 InitBundle 条目头签名（1052/1052 = 100%）：
//   u16@+0x08 == 0x0CDD           （常量）
//   u16@+0x0A == u16@+0x0E        （同值重复）
//
// 本工具：32 线程全文件扫描；命中点与映射中的 offset 集合比对。
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#define NTH 32
static const char* PATH;
static unsigned long long SZ;
static long long* MAP; static long NMAP=0;         /* 映射中的 offset（已排序） */
static volatile long long NHIT=0;
static long long* HITS; static long long HCAP=0;
static pthread_mutex_t MU=PTHREAD_MUTEX_INITIALIZER;

static int cmpll(const void*a,const void*b){ long long x=*(const long long*)a,y=*(const long long*)b; return x<y?-1:(x>y?1:0); }

static void* worker(void* arg){
    long tid=(long)arg;
    int fd=open(PATH,O_RDONLY); if(fd<0){perror("open");return NULL;}
    const long long CHUNK = 1<<22;
    long long start = (SZ/NTH)*tid;
    long long end   = (tid==NTH-1)? SZ : (SZ/NTH)*(tid+1);
    /* 每线程多读 16 字节以覆盖跨块 */
    long long len = end-start+16; if(start+len>SZ) len=SZ-start;
    uint8_t* buf=malloc(len); if(!buf) return NULL;
    if(pread(fd,buf,len,start)!=(ssize_t)len){ free(buf); close(fd); return NULL; }
    for(long long i=0;i+16<=len;i++){
        if(buf[i+8]!=0xDD || buf[i+9]!=0x0C) continue;
        uint16_t u10=buf[i+10]|(buf[i+11]<<8);
        uint16_t u14=buf[i+14]|(buf[i+15]<<8);
        if(u10==u14){
            pthread_mutex_lock(&MU);
            if(NHIT<HCAP) HITS[NHIT]=start+i;
            NHIT++;
            pthread_mutex_unlock(&MU);
        }
    }
    free(buf); close(fd); return NULL;
}

int main(int argc,char**argv){
    PATH=argv[1];
    /* 读映射 offset（第 5 列） */
    FILE* f=fopen(argv[2],"r");
    HCAP=20000000;
    MAP=malloc(sizeof(long long)*HCAP);
    HITS=malloc(sizeof(long long)*HCAP);
    if(!f||!MAP||!HITS){ fprintf(stderr,"alloc/map fail\n"); return 1; }
    char line[1024];
    while(fgets(line,sizeof(line),f)){
        char* p=line; int col=0; char* save=line;
        /* 简易制表符切分 */
        char* fields[8]={0}; int nf=0;
        char* tok=line;
        fields[nf++]=tok;
        for(char* q=line; *q; q++){ if(*q=='\t'){ *q=0; if(nf<8) fields[nf++]=q+1; } else if(*q=='\n'){ *q=0; } }
        if(nf<7) continue;
        if(strcmp(fields[0],"InitBundle")!=0) continue;
        MAP[NMAP++]=strtoll(fields[4],NULL,10);
    }
    fclose(f);
    qsort(MAP,NMAP,sizeof(long long),cmpll);
    printf("  映射中 InitBundle 条目起点: %ld 个\n",NMAP);

    int fd=open(PATH,O_RDONLY); SZ=lseek(fd,0,SEEK_END); close(fd);
    printf("  扫描文件 %s (%llu 字节), %d 线程\n",PATH,SZ,NTH);
    pthread_t th[NTH];
    for(long i=0;i<NTH;i++) pthread_create(&th[i],NULL,worker,(void*)i);
    for(int i=0;i<NTH;i++) pthread_join(th[i],NULL);
    printf("  签名命中总数: %lld\n",NHIT);
    qsort(HITS,NHIT<HCAP?NHIT:HCAP,sizeof(long long),cmpll);
    long long inter=0, onlysig=0, onlymap=0;
    for(long long i=0;i<NHIT && i<HCAP;i++){
        if(bsearch(&HITS[i],MAP,NMAP,sizeof(long long),cmpll)) inter++; else onlysig++;
    }
    for(long i=0;i<NMAP;i++) if(!bsearch(&MAP[i],HITS,NHIT<HCAP?NHIT:HCAP,sizeof(long long),cmpll)) onlymap++;
    printf("  与映射交集: %lld\n",inter);
    printf("  仅签名命中: %lld\n",onlysig);
    printf("  仅映射有  : %lld\n",onlymap);
    printf("  前 8 个签名命中: "); for(long long i=0;i<8&&i<NHIT;i++) printf("0x%llx ",(unsigned long long)HITS[i]); printf("\n");
    if(NMAP>0){ printf("  前 8 个映射起点: "); for(long i=0;i<8&&i<NMAP;i++) printf("0x%llx ",(unsigned long long)MAP[i]); printf("\n"); }
    return 0;
}
