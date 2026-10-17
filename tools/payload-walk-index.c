// EFML4A payload v6：完整遍历 VFS 索引，导出 FVFBlockFileInfo 映射。
//
// 链路（本机 v1.5.3 dump.cs + 已实测成功的 walk payload）：
//   class   = *(void**)(base + 0x1a47e000 + 0xfb0)
//   statics = *(void**)(class + 0xb8)
//   s_instance = *(void**)(statics + 0x58)                 ← VFS 单例
//   m_allVFBlock = *(void**)(inst + 0x10)                  ← 托管 Dictionary
//
// 托管 Dictionary 布局：
//   +0x10 buckets(int[])  +0x18 entries(Entry[])  +0x20 count
//   Entry = { int hashCode; int next; TKey key; <pad>; TValue value; }
//   （TKey=int, TValue=VFBlockMainInfo* ⇒ Entry 24 字节）
//
// VFBlockMainInfo：
//   +0x18 groupCfgName(String*)  +0x28 groupFileInfoNum(int)
//   +0x40 allChunks(FVFBlockChunkInfo[])
// FVFBlockChunkInfo（struct，0x38）：
//   +0x00 md5Name(UInt128)  +0x20 length(Int64)  +0x28 files(NativeArray)
// FVFBlockFileInfo（非托管）：
//   +0x00 fileNameHash(Int64) +0x08 chunkIdx(Int32) +0x0C ivSeed(Int32)
//   +0x10 offset(UInt32) +0x14 len(UInt32) +0x18 flags(UInt32)
//
// ★ 全程只读；加 SIGSEGV 保护。
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
#define OFF_ALLVFBLOCK 0x10ULL

#define LOG1 "/data/data/com.hypergryph.endfield/files/efml4a-walk2.log"
#define LOG2 "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a-walk2.log"
#define OUTF "/data/data/com.hypergryph.endfield/files/efml4a-map.tsv"

static void Log(const char* fmt, ...){
    char line[1024]; va_list ap; va_start(ap,fmt);
    int m=vsnprintf(line,sizeof(line),fmt,ap); va_end(ap);
    const char* P[2]={LOG1,LOG2};
    for(int i=0;i<2;i++){
        int fd=open(P[i],O_WRONLY|O_CREAT|O_APPEND,0644); if(fd<0) continue;
        char b[1088]; int n=snprintf(b,sizeof(b),"[w2 pid=%d] %.*s\n",(int)getpid(),m>0?m:0,line);
        if(n>0){ ssize_t w=write(fd,b,(size_t)n); (void)w; }
        close(fd);
    }
}
static FILE* OF=NULL;
static void Out(const char* s){ if(OF){ fputs(s,OF); } }

static sigjmp_buf JB;
static void OnSegv(int s){ (void)s; siglongjmp(JB,1); }
static int SR(void* d,const void* s,size_t n){ if(sigsetjmp(JB,1)==0){ memcpy(d,s,n); return 0;} return -1; }
static void* SD(const void* p){ void* v=NULL; if(!p) return NULL; if(SR(&v,p,8)!=0) return NULL; return v; }
static uint32_t SU32(const void* p,uint32_t d){ uint32_t v=0; if(!p) return d; if(SR(&v,p,4)!=0) return d; return v; }
static int64_t SI64(const void* p,int64_t d){ int64_t v=0; if(!p) return d; if(SR(&v,p,8)!=0) return d; return v; }

static int FindBase(struct dl_phdr_info* i,size_t s,void* d){
    (void)s;
    if(i->dlpi_name && strstr(i->dlpi_name,"libil2cpp.so")){ *(unsigned long long*)d=(unsigned long long)i->dlpi_addr; return 1; }
    return 0;
}
/* UTF-16 → UTF-8 */
static void U16(const uint16_t* s,int len,char* o,int cap){
    int k=0; for(int i=0;i<len&&k<cap-2;i++){ uint16_t c=s[i];
        if(c<0x80) o[k++]=(char)c;
        else if(c<0x800&&k<cap-3){ o[k++]=(char)(0xC0|(c>>6)); o[k++]=(char)(0x80|(c&0x3F)); }
        else if(k<cap-4){ o[k++]=(char)(0xE0|(c>>12)); o[k++]=(char)(0x80|((c>>6)&0x3F)); o[k++]=(char)(0x80|(c&0x3F)); } }
    o[k]=0;
}
static void StrOf(const char* label, void* so, char* out, int cap){
    out[0]=0; if(!so) return;
    int32_t L=SU32((const char*)so+0x10,0x7fffffff);
    if(L<0||L>512) return;
    U16((const uint16_t*)((const char*)so+0x14),L,out,cap);
    if(label) Log("%s = \"%s\"",label,out);
}

extern "C" __attribute__((visibility("default"))) int init(void);
static void* Worker(void* arg);

__attribute__((constructor)) static void Ctor(void){
    static char altstk[65536];
    stack_t ss; ss.ss_sp=altstk; ss.ss_size=sizeof(altstk); ss.ss_flags=0; sigaltstack(&ss,NULL);
    struct sigaction sa; memset(&sa,0,sizeof(sa));
    sa.sa_handler=OnSegv; sa.sa_flags=SA_NODEFER|SA_ONSTACK; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV,&sa,NULL); sigaction(SIGBUS,&sa,NULL);
    Log("=== walk2 payload 构造执行 ===");
    pthread_t t; pthread_create(&t,NULL,Worker,NULL); pthread_detach(t);
}
extern "C" __attribute__((visibility("default"))) int init(void){
    pthread_t t; pthread_create(&t,NULL,Worker,NULL); pthread_detach(t); return 0;
}

static int64_t g_records=0, g_chunks=0, g_blocks=0;

static void WalkFiles(void* filesPtr, const char* blcName){
    /* NativeArray<FVFBlockFileInfo>: { void* buffer; int32 len; int32 allocator; } */
    void* buf=SD(filesPtr);
    int32_t n=SU32((const char*)filesPtr+8,0);
    if(!buf || n<=0 || n>2000000) return;
    g_chunks++;
    for(int32_t i=0;i<n;i++){
        const char* p=(const char*)buf+(size_t)i*0x20;
        int64_t h=SI64(p,0);
        uint32_t ci=SU32(p+8,0), iv=SU32(p+12,0), off=SU32(p+16,0), len=SU32(p+20,0), fl=SU32(p+24,0);
        if(OF){ char b[256];
            snprintf(b,sizeof(b),"%s\t%lld\t%u\t%u\t%u\t%u\t%u\n",
                     blcName,(unsigned long long)h,i,iv,off,len,fl);
            Out(b); }
        g_records++;
    }
}

static void WalkBlock(void* blk){
    if(!blk) return;
    g_blocks++;
    char blc[256];
    StrOf(NULL, SD((const char*)blk+0x18), blc, sizeof(blc));   /* groupCfgName */
    uint32_t num=SU32((const char*)blk+0x28,0);
    void* chunks=SD((const char*)blk+0x40);
    int32_t cn=0;
    if(chunks) cn=SU32((const char*)chunks+0x18,0);
    Log("VFBlockMainInfo @%p name=\"%s\" groupFileInfoNum=%u allChunks=%p n=%d",
        blk,blc,num,chunks,cn);
    if(!chunks || cn<=0 || cn>100000) return;
    const char* data=(const char*)chunks+0x20;
    for(int32_t i=0;i<cn && i<20000;i++){
        const char* el=data+(size_t)i*0x40;          /* FVFBlockChunkInfo（含 UInt128，对齐到 0x40） */
        uint32_t L32=SU32(el+0x20+4,0);
        int64_t  LEN=SI64(el+0x20,0);
        void*    files=(void*)(el+0x28);
        if(i<3){
            char md[40]; int k=0;
            for(int j=0;j<16;j++){ char t[4]; snprintf(t,sizeof(t),"%02x",(uint8_t)SU32(el+j,0)); md[k++]=t[0]; md[k++]=t[1]; }
            md[32]=0;
            Log("  chunk[%d] md5=%s len=%lld files=%p n=%d",i,md,(long long)LEN,SD(files),SU32((const char*)files+8,0));
        }
        (void)L32;
        WalkFiles(files, blc);
    }
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
    if(!statics){ Log("静态区未就绪"); return NULL; }

    void* inst=NULL;
    for(int t=0;t<7200;t++){ inst=SD((const char*)statics+OFF_INSTANCE); if(inst) break; usleep(100000); }
    Log("s_instance=%p",inst);
    if(!inst){ Log("s_instance 始终为 NULL"); return NULL; }

    /* 等 m_allVFBlock 就绪 */
    void* dict=NULL; void* entries=NULL; uint32_t count=0;
    for(int t=0;t<7200;t++){                            /* 最多等 12 分钟直到索引装载完成 */
        dict=SD((const char*)inst+OFF_ALLVFBLOCK);
        if(dict){
            entries=SD((const char*)dict+0x18);
            count=SU32((const char*)dict+0x20,0);
            if(entries && count>0) break;               /* ★ 必须等到有实际条目 */
        }
        if(t%150==0) Log("  等索引装载…(%d 秒) dict=%p entries=%p count=%u",t/10,dict,entries,count);
        usleep(100000);
    }
    Log("m_allVFBlock(Dictionary)=%p entries=%p count=%u",dict,entries,count);
    if(!dict || !entries || count==0){ Log("索引始终未装载"); return NULL; }
    int32_t en=0; if(entries) en=SU32((const char*)entries+0x18,0);
    Log("entries=%p 数组长度=%d count=%u",entries,en,count);

    OF=fopen(OUTF,"w");
    if(OF) Out("blc\tfileNameHash\tidx\tivSeed\toffset\tlen\tflags\n");

    const char* edata=entries?(const char*)entries+0x20:NULL;
    /* 先探测 Entry 步长：用 count 与数组长度反推（常见 16/24/32） */
    int strides[3]={24,32,16};
    int stride=24;
    if(en>0 && count>0){
        /* 用「第一个非零 value 必须是指针」来挑选步长 */
        for(int si=0;si<3;si++){
            int st=strides[si]; int okcnt=0;
            for(uint32_t i=0;i<count && i<64;i++){
                void* v=SD(edata+(size_t)i*st+16);       /* value 通常在 +16 */
                if(v && ((uintptr_t)v>>40)==0x6e || ((uintptr_t)v>>40)==0x6d) okcnt++;
            }
            if(okcnt>0){ stride=st; Log("探测到 Entry 步长=%d（%d 个指针有效）",st,okcnt); break; }
        }
    }
    for(uint32_t i=0;i<count && i<4096;i++){
        const char* e=edata+(size_t)i*stride;
        void* v=SD(e+16);
        if(!v) continue;
        WalkBlock(v);
    }
    if(OF) fclose(OF);
    Log("=== walk2 完成：block=%lld chunk=%lld record=%lld ===",g_blocks,g_chunks,g_records);
    return NULL;
}
