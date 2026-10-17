// EFML4A payload v5：顺 VirtualFileSystem 单例取 m_fileName2FileInfo 的键/值数组。
//
// 依据（本机 v1.5.3 dump.cs + 已实测成功的静态区读取）：
//   class   = *(void**)(base + 0x1a47e000 + 0xfb0)     ← 已验证
//   statics = *(void**)(class + 0xb8)                  ← 已验证
//   s_instance(0x58) → VFS 单例
//   实例字段 m_fileName2FileInfo(0x20) = NativeParallelHashMap<Int64, FVFBlockFileInfo>
//     （struct 内联，其唯一字段 m_Buffer 即 UnsafeParallelHashMapData*）
//   UnsafeParallelHashMapData: { values; keys; next; buckets; keyCapacity; ... }
//
// ★ 全程只做内存解引用，不执行任何托管代码；加 SIGSEGV 保护。
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>
#include <setjmp.h>
#include <link.h>
#include <pthread.h>

#define RVA_CLASSSLOT 0x1a47e000ULL
#define OFF_CLASSSLOT 0xfb0ULL
#define OFF_STATICS   0xb8ULL
#define OFF_INSTANCE  0x58ULL
#define OFF_HMAP      0x20ULL

#define LOG1 "/data/data/com.hypergryph.endfield/files/efml4a-walk.log"
#define LOG2 "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a-walk.log"
#define OUTF "/data/data/com.hypergryph.endfield/files/efml4a-hashmap.txt"

static void Log(const char* fmt, ...){
    char line[1024]; va_list ap; va_start(ap,fmt);
    int m=vsnprintf(line,sizeof(line),fmt,ap); va_end(ap);
    const char* P[2]={LOG1,LOG2};
    for(int i=0;i<2;i++){
        int fd=open(P[i],O_WRONLY|O_CREAT|O_APPEND,0644); if(fd<0) continue;
        char b[1088]; int n=snprintf(b,sizeof(b),"[walk pid=%d] %.*s\n",(int)getpid(),m>0?m:0,line);
        if(n>0){ ssize_t w=write(fd,b,(size_t)n); (void)w; }
        close(fd);
    }
}
static void Out(const char* s){ int fd=open(OUTF,O_WRONLY|O_CREAT|O_APPEND,0644); if(fd<0)return; ssize_t w=write(fd,s,strlen(s)); (void)w; close(fd); }

static sigjmp_buf JB;
static void OnSegv(int s){ (void)s; siglongjmp(JB,1); }
static int SafeRead(void* d,const void* s,size_t n){ if(sigsetjmp(JB,1)==0){ memcpy(d,s,n); return 0;} return -1; }
static void* SD(const void* p){ void* v=NULL; if(!p) return NULL; if(SafeRead(&v,p,8)!=0) return NULL; return v; }
static uint32_t SU32(const void* p,uint32_t d){ uint32_t v=0; if(!p) return d; if(SafeRead(&v,p,4)!=0) return d; return v; }
static int64_t SI64(const void* p,int64_t d){ int64_t v=0; if(!p) return d; if(SafeRead(&v,p,8)!=0) return d; return v; }

static int FindBase(struct dl_phdr_info* i,size_t s,void* d){
    (void)s;
    if(i->dlpi_name && strstr(i->dlpi_name,"libil2cpp.so")){ *(unsigned long long*)d=(unsigned long long)i->dlpi_addr; return 1; }
    return 0;
}

static void* Worker(void* arg){
    (void)arg;
    unsigned long long base=0;
    for(int t=0;t<1200;t++){ base=0; dl_iterate_phdr(FindBase,&base); if(base) break; usleep(100000); }
    if(!base){ Log("未见 libil2cpp.so"); return NULL; }
    Log("base=0x%llx",base);

    volatile uint32_t* slot=(volatile uint32_t*)(base+RVA_CLASSSLOT+OFF_CLASSSLOT);
    void* klass=NULL,*statics=NULL;
    for(int t=0;t<3000;t++){
        klass=SD((const void*)slot);
        if(klass){ statics=SD((const char*)klass+OFF_STATICS);
                   if(statics && SD((const char*)statics+0x38)) break; }
        usleep(100000);
    }
    Log("class=%p statics=%p",klass,statics);
    if(!statics){ Log("静态区未就绪"); return NULL; }

    /* 等 s_instance 出现（VFS 单例是懒加载的） */
    void* inst=NULL;
    for(int t=0;t<7200;t++){ inst=SD((const char*)statics+OFF_INSTANCE); if(inst) break; if(t%600==0) Log("  等 s_instance…(%d 秒)",t/10); usleep(100000); }
    Log("s_instance(statics+0x58)=%p",inst);
    if(!inst){ Log("2 小时内 s_instance 始终为 NULL"); return NULL; }
    Out("\n===== VFS 单例实例字段 =====\n");

    /* 逐字段打印实例头部 */
    for(int off=0;off<0x80;off+=8){
        void* v=SD((const char*)inst+off);
        char b[128]; snprintf(b,sizeof(b),"inst+0x%02x = 0x%llx\n",off,(unsigned long long)v); Out(b);
        Log("inst+0x%02x = 0x%llx",off,(unsigned long long)v);
    }

    void* hmap=SD((const char*)inst+OFF_HMAP);
    Log("m_fileName2FileInfo(inst+0x20) = %p（struct 内联，此即 m_Buffer）",hmap);
    if(!hmap){ Log("哈希表缓冲为空"); return NULL; }

    /* UnsafeParallelHashMapData */
    Log("--- UnsafeParallelHashMapData @ %p ---",hmap);
    void* values =SD((const char*)hmap+0x00);
    void* keys   =SD((const char*)hmap+0x08);
    void* nxt    =SD((const char*)hmap+0x10);
    void* buckets=SD((const char*)hmap+0x18);
    uint32_t cap =SU32((const char*)hmap+0x20,0);
    uint32_t mask=SU32((const char*)hmap+0x24,0);
    uint32_t allocd=SU32((const char*)hmap+0x28,0);
    Log("values=%p keys=%p next=%p buckets=%p keyCapacity=%u bucketMask=%u allocated=%u",
        values,keys,nxt,buckets,cap,mask,allocd);
    { char b[512]; snprintf(b,sizeof(b),"values=%p keys=%p next=%p buckets=%p cap=%u mask=%u alloc=%u\n",
        values,keys,nxt,buckets,cap,mask,allocd); Out(b); }

    /* 打印前若干键与对应的值记录 */
    if(keys && cap>0 && cap<10000000){
        Out("--- 前 24 个键（Int64 哈希）与对应值记录 ---\n");
        for(uint32_t i=0;i<24 && i<cap;i++){
            int64_t k=SI64((const char*)keys+(size_t)i*8,0);
            char b[256];
            if(values){
                /* FVFBlockFileInfo: h(8) ci(4) iv(4) off(4) len(4) flags(4) */
                const char* vp=(const char*)values+(size_t)i*0x20;
                int64_t h=SI64(vp,0);
                uint32_t ci=SU32(vp+8,0), iv=SU32(vp+12,0), off=SU32(vp+16,0), len=SU32(vp+20,0), fl=SU32(vp+24,0);
                snprintf(b,sizeof(b),"key[%u]=0x%llx  val{h=0x%llx ci=%u iv=%u off=0x%x len=0x%x fl=0x%x}\n",
                         i,(unsigned long long)k,(unsigned long long)h,ci,iv,off,len,fl);
            } else snprintf(b,sizeof(b),"key[%u]=0x%llx\n",i,(unsigned long long)k);
            Out(b);
            if(i<8) Log("%s",b);
        }
    } else Log("keys 指针或容量不合理，无法枚举");
    Log("=== walk 完成 ===");
    return NULL;
}

extern "C" __attribute__((visibility("default"))) int init(void){ pthread_t t; pthread_create(&t,NULL,Worker,NULL); pthread_detach(t); return 0; }
__attribute__((constructor)) static void Ctor(void){
    static char altstk[65536];
    stack_t ss; ss.ss_sp=altstk; ss.ss_size=sizeof(altstk); ss.ss_flags=0; sigaltstack(&ss,NULL);
    struct sigaction sa; memset(&sa,0,sizeof(sa));
    sa.sa_handler=OnSegv; sa.sa_flags=SA_NODEFER|SA_ONSTACK; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV,&sa,NULL); sigaction(SIGBUS,&sa,NULL);
    Log("=== walk payload 构造执行 ===");
    pthread_t t; pthread_create(&t,NULL,Worker,NULL); pthread_detach(t);
}
