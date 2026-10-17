/* EFML4A 路线C：hook libc 的文件读取，记录 (fd, offset, count)
 *   目的：绕过 libil2cpp 加壳，直接看游戏是否读了 .chk 的某个偏移
 *   ⚠️ 纯只读日志
 *   ★ hook 的是系统 libc（无壳）
 *   构建：clang -O2 -fPIC -shared -target aarch64-linux-android24 \
 *           -Wl,-rpath,/system/lib64 -o libefml4a_libcprobe.so payload-libcprobe.c \
 *           -llog -ldl -lm -lpthread
 *         然后 patchelf --remove-rpath
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <stdarg.h>
#include <dlfcn.h>

#define LOGF "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a_libc.log"
/* 关注的偏移窗口（我们改的 .ab 条目在 .chk 里的绝对偏移） */
#define WATCH_LO 50000000LL
#define WATCH_HI 56000000LL

static int  g_logfd=-1;
static volatile long long g_n=0, g_hits=0;
static int gettid_(void){ return (int)syscall(SYS_gettid); }
static void lg(const char*s){ if(g_logfd>=0) write(g_logfd,s,strlen(s)); }
static void lgf(const char*f,...){ char b[384]; va_list ap; va_start(ap,f);
    int n=vsnprintf(b,sizeof b,f,ap); va_end(ap); if(g_logfd>=0&&n>0) write(g_logfd,b,n); }

/* 通过 /proc/self/fd/N 反查路径 */
static void fdpath(int fd,char*out,size_t n){
    char p[64]; snprintf(p,sizeof p,"/proc/self/fd/%d",fd);
    ssize_t r=readlink(p,out,n-1); if(r<0){ out[0]=0; return; } out[r]=0;
}

static void flush_icache(void*p,size_t n){ __builtin___clear_cache((char*)p,(char*)p+n); }
static int mprotect_rwx(void*p,size_t n){
    unsigned long pg=(unsigned long)sysconf(_SC_PAGESIZE);
    void*s=(void*)((unsigned long)p&~(pg-1));
    size_t l=n+((unsigned long)p-(unsigned long)s);
    return mprotect(s,l,PROT_READ|PROT_WRITE|PROT_EXEC);
}

/* ---- 原始 pread64 的 trampoline ---- */
typedef ssize_t (*pread64_t)(int,void*,size_t,off64_t);
static pread64_t real_pread64=0;
static unsigned char g_tramp[64] __attribute__((aligned(16)));
extern void hook_entry(void);
void* g_tramp_ptr=0;

/* pread64(fd, buf, count, offset) */
__attribute__((used,visibility("default"))) long hook_impl(long a0,long a1,long a2,long a3){
    long long n=++g_n;
    off64_t off=(off64_t)a3;
    if(off>=WATCH_LO && off<WATCH_HI){
        long long h=++g_hits;
        char path[256]; fdpath((int)a0,path,sizeof path);
        if(h<=200) lgf("[libc] #%lld fd=%ld off=%lld cnt=%ld path=%s\n",
                       h,(long)a0,(long long)off,(long)a2,path);
    }
    return (long)real_pread64((int)a0,(void*)a1,(size_t)a2,(off64_t)a3);
}
__asm__(
".text\n.global hook_entry\n.type hook_entry, %function\n"
"hook_entry:\n"
"  stp x29, x30, [sp, #-16]!\n  mov x29, sp\n"
"  stp x0, x1, [sp, #-16]!\n  stp x2, x3, [sp, #-16]!\n"
"  stp x4, x5, [sp, #-16]!\n  stp x6, x7, [sp, #-16]!\n"
"  bl  hook_impl\n"
"  ldp x6, x7, [sp], #16\n  ldp x4, x5, [sp], #16\n"
"  ldp x2, x3, [sp], #16\n  ldp x0, x1, [sp], #16\n"
"  ldp x29, x30, [sp], #16\n"
"  adrp x16, :got:g_tramp_ptr\n"
"  ldr  x16, [x16, :got_lo12:g_tramp_ptr]\n"
"  ldr  x16, [x16]\n  br x16\n"
".size hook_entry, .-hook_entry\n");

static unsigned long find_lib(const char* name){
    FILE* f=fopen("/proc/self/maps","r"); if(!f) return 0;
    char line[512]; unsigned long base=0;
    while(fgets(line,sizeof line,f)){
        if(!strstr(line,name)) continue;
        unsigned long a,b,off; char perms[8];
        if(sscanf(line,"%lx-%lx %7s %lx",&a,&b,perms,&off)!=4) continue;
        if(off==0){ base=a; break; }
    }
    fclose(f); return base;
}
static const char* br_kind(uint32_t w){
    uint32_t o26=w>>26,o24=w>>24;
    if(o26==0x05) return "B"; if(o26==0x25) return "BL";
    if(o24==0x54) return "B.cond";
    if(o24==0x34||o24==0x35||o24==0xb4||o24==0xb5) return "CBZ";
    if(o24==0x36||o24==0x37||o24==0xb6||o24==0xb7) return "TBZ";
    return 0;
}
static void install(void){
    void* p=dlsym(RTLD_DEFAULT,"pread64");
    if(!p){ lg("[libc] dlsym pread64 FAILED"); return; }
    lgf("[libc] pread64 @%p\n",p);
    real_pread64=(pread64_t)p;
    unsigned char* t=(unsigned char*)p;
    unsigned char saved[16]; memcpy(saved,t,16);
    int nz=0; for(int i=0;i<16;i++) if(saved[i]) nz++;
    lgf("[libc] bytes %02x%02x%02x%02x nz=%d\n",saved[0],saved[1],saved[2],saved[3],nz);
    if(nz<8){ lg("[libc] EMPTY, skip"); return; }
    for(int k=0;k<4;k++){ uint32_t w; memcpy(&w,saved+k*4,4);
        const char* bk=br_kind(w);
        if(bk){ lgf("[libc] insn[%d] is %s -> skip\n",k,bk); return; } }
    memcpy(g_tramp,saved,16);
    { uint32_t j[4]; j[0]=0x58000050u; j[1]=0xd61f0200u;
      void* cont=(void*)(t+16); memcpy(&j[2],&cont,8); memcpy(g_tramp+16,j,16); }
    mprotect_rwx(g_tramp,sizeof g_tramp); flush_icache(g_tramp,sizeof g_tramp);
    g_tramp_ptr=(void*)g_tramp;
    mprotect_rwx(t,16);
    { uint32_t c[4]; c[0]=0x58000050u; c[1]=0xd61f0200u;
      void* d=(void*)hook_entry; memcpy(&c[2],&d,8); memcpy(t,c,16); }
    flush_icache(t,16);
    lg("[libc] pread64 HOOKED");
}
static void* waiter(void* arg){
    (void)arg;
    lgf("[libc] waiter pid=%d tid=%d\n",getpid(),gettid_());
    for(int i=0;i<900;i++){ unsigned long b=find_lib("libc.so"); if(b){ lgf("[libc] libc @0x%lx\n",b); install(); return 0; } usleep(100000); }
    lg("[libc] timeout");
    return 0;
}
static void do_install(void){
    static int done=0; if(done) return; done=1;
    g_logfd=open(LOGF,O_WRONLY|O_CREAT|O_APPEND,0644);
    lgf("=== efml4a libcprobe pid=%d tid=%d ===\n",getpid(),gettid_());
    pthread_t t; if(pthread_create(&t,0,waiter,0)!=0){ lg("[libc] pthread fail"); return; }
    pthread_detach(t);
}
__attribute__((used,visibility("default"))) void init(void);
__attribute__((constructor)) static void ctor(void){ do_install(); }
__attribute__((used,visibility("default"))) void init(void){ do_install(); }
