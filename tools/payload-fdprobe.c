/* EFML4A fd 探针：hook libc 的 open / mmap / pread64
 *   目的：找出游戏【究竟怎么读 .chk】（read 还是 mmap）
 *   ⚠️ 纯只读
 *   构建：clang -O2 -fPIC -shared -target aarch64-linux-android24 \
 *           -Wl,-rpath,/system/lib64 -o libefml4a_fdprobe.so payload-fdprobe.c \
 *           -llog -ldl -lm -lpthread ; patchelf --remove-rpath ...
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

#define LOGF "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a_fd.log"

static int g_logfd=-1;
static volatile long long g_open_n=0, g_mmap_n=0, g_pread_n=0;
static int gettid_(void){ return (int)syscall(SYS_gettid); }
static void lg(const char*s){ if(g_logfd>=0) write(g_logfd,s,strlen(s)); }
static void lgf(const char*f,...){ char b[384]; va_list ap; va_start(ap,f);
    int n=vsnprintf(b,sizeof b,f,ap); va_end(ap); if(g_logfd>=0&&n>0) write(g_logfd,b,n); }
static int interesting(const char*p){
    return p && (strstr(p,".chk")||strstr(p,".blc")||strstr(p,"VFS")||strstr(p,"endfield"));
}
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
static const char* br_kind(uint32_t w){
    uint32_t o26=w>>26,o24=w>>24;
    if(o26==0x05) return "B"; if(o26==0x25) return "BL";
    if(o24==0x54) return "B.cond";
    if(o24==0x34||o24==0x35||o24==0xb4||o24==0xb5) return "CBZ";
    if(o24==0x36||o24==0x37||o24==0xb6||o24==0xb7) return "TBZ";
    return 0;
}
/* 通用安装：把 hook 装到 dlsym(sym) 上，trampoline 指针写到 *pp */
static int install_one(const char* sym, void* hook, unsigned char* tramp, void** pp, void** realfp){
    void* tgt=dlsym(RTLD_DEFAULT,sym);
    if(!tgt){ lgf("[fd] dlsym %s FAILED\n",sym); return -1; }
    *realfp=tgt;
    unsigned char sv[16]; memcpy(sv,tgt,16);
    int nz=0; for(int i=0;i<16;i++) if(sv[i]) nz++;
    lgf("[fd] %s @%p bytes %02x%02x%02x%02x nz=%d\n",sym,tgt,sv[0],sv[1],sv[2],sv[3],nz);
    if(nz<8){ lgf("[fd] %s EMPTY skip\n",sym); return -1; }
    for(int k=0;k<4;k++){ uint32_t w; memcpy(&w,sv+k*4,4);
        const char* bk=br_kind(w); if(bk){ lgf("[fd] %s insn[%d] %s skip\n",sym,k,bk); return -1; } }
    memcpy(tramp,sv,16);
    { uint32_t j[4]; j[0]=0x58000050u; j[1]=0xd61f0200u;
      void* cont=(void*)((unsigned char*)tgt+16); memcpy(&j[2],&cont,8); memcpy(tramp+16,j,16); }
    mprotect_rwx(tramp,64); flush_icache(tramp,64);
    *pp=(void*)tramp;
    mprotect_rwx(tgt,16);
    { uint32_t c[4]; c[0]=0x58000050u; c[1]=0xd61f0200u;
      memcpy(&c[2],&hook,8); memcpy(tgt,c,16); }
    flush_icache(tgt,16);
    lgf("[fd] %s HOOKED\n",sym);
    return 0;
}

/* ===== open ===== */
typedef int (*open_t)(const char*,int,...);
static open_t real_open=0;
static unsigned char g_otramp[64] __attribute__((aligned(16)));
void* g_otramp_ptr=0;
extern void hook_entry_open(void);
__attribute__((used,visibility("default"))) long hook_open(long a0,long a1,long a2){
    if(interesting((const char*)a0)){ long long n=++g_open_n;
        if(n<=500) lgf("[open] #%lld %s\n",n,(const char*)a0); }
    return (long)real_open((const char*)a0,(int)a1,(int)a2);
}
__asm__(
".text\n.global hook_entry_open\n.type hook_entry_open, %function\n"
"hook_entry_open:\n"
"  stp x29, x30, [sp, #-16]!\n  mov x29, sp\n"
"  stp x0, x1, [sp, #-16]!\n  stp x2, x3, [sp, #-16]!\n"
"  stp x4, x5, [sp, #-16]!\n  stp x6, x7, [sp, #-16]!\n"
"  bl  hook_open\n"
"  ldp x6, x7, [sp], #16\n  ldp x4, x5, [sp], #16\n"
"  ldp x2, x3, [sp], #16\n  ldp x0, x1, [sp], #16\n"
"  ldp x29, x30, [sp], #16\n"
"  adrp x16, :got:g_otramp_ptr\n  ldr x16, [x16, :got_lo12:g_otramp_ptr]\n  ldr x16, [x16]\n  br x16\n"
".size hook_entry_open, .-hook_entry_open\n");

/* ===== mmap ===== */
typedef void* (*mmap_t)(void*,size_t,int,int,int,off_t);
static mmap_t real_mmap=0;
static unsigned char g_mtramp[64] __attribute__((aligned(16)));
void* g_mtramp_ptr=0;
extern void hook_entry_mmap(void);
__attribute__((used,visibility("default"))) long hook_mmap(long a0,long a1,long a2,long a3,long a4,long a5){
    if(a4>=0){ char path[256]; fdpath((int)a4,path,sizeof path);
        if(interesting(path)){ long long n=++g_mmap_n;
            if(n<=500) lgf("[mmap] #%lld len=%ld prot=%ld off=%ld path=%s\n",n,a1,a2,a5,path); } }
    return (long)real_mmap((void*)a0,(size_t)a1,(int)a2,(int)a3,(int)a4,(off_t)a5);
}
__asm__(
".text\n.global hook_entry_mmap\n.type hook_entry_mmap, %function\n"
"hook_entry_mmap:\n"
"  stp x29, x30, [sp, #-16]!\n  mov x29, sp\n"
"  stp x0, x1, [sp, #-16]!\n  stp x2, x3, [sp, #-16]!\n"
"  stp x4, x5, [sp, #-16]!\n  stp x6, x7, [sp, #-16]!\n"
"  bl  hook_mmap\n"
"  ldp x6, x7, [sp], #16\n  ldp x4, x5, [sp], #16\n"
"  ldp x2, x3, [sp], #16\n  ldp x0, x1, [sp], #16\n"
"  ldp x29, x30, [sp], #16\n"
"  adrp x16, :got:g_mtramp_ptr\n  ldr x16, [x16, :got_lo12:g_mtramp_ptr]\n  ldr x16, [x16]\n  br x16\n"
".size hook_entry_mmap, .-hook_entry_mmap\n");

/* ===== pread64 ===== */
typedef ssize_t (*pread64_t)(int,void*,size_t,off64_t);
static pread64_t real_pread64=0;
static unsigned char g_ptramp[64] __attribute__((aligned(16)));
void* g_ptramp_ptr=0;
extern void hook_entry_pread(void);
__attribute__((used,visibility("default"))) long hook_pread(long a0,long a1,long a2,long a3){
    if(a3>=40000000LL && a3<60000000LL){
        char path[256]; fdpath((int)a0,path,sizeof path);
        if(interesting(path)){ long long n=++g_pread_n;
            if(n<=500) lgf("[pread] #%lld fd=%ld off=%ld cnt=%ld path=%s\n",n,a0,a3,a2,path); } }
    return (long)real_pread64((int)a0,(void*)a1,(size_t)a2,(off64_t)a3);
}
__asm__(
".text\n.global hook_entry_pread\n.type hook_entry_pread, %function\n"
"hook_entry_pread:\n"
"  stp x29, x30, [sp, #-16]!\n  mov x29, sp\n"
"  stp x0, x1, [sp, #-16]!\n  stp x2, x3, [sp, #-16]!\n"
"  stp x4, x5, [sp, #-16]!\n  stp x6, x7, [sp, #-16]!\n"
"  bl  hook_pread\n"
"  ldp x6, x7, [sp], #16\n  ldp x4, x5, [sp], #16\n"
"  ldp x2, x3, [sp], #16\n  ldp x0, x1, [sp], #16\n"
"  ldp x29, x30, [sp], #16\n"
"  adrp x16, :got:g_ptramp_ptr\n  ldr x16, [x16, :got_lo12:g_ptramp_ptr]\n  ldr x16, [x16]\n  br x16\n"
".size hook_entry_pread, .-hook_entry_pread\n");

static void* waiter(void* arg){
    (void)arg;
    lgf("[fd] waiter pid=%d tid=%d\n",getpid(),gettid_());
    install_one("open",  (void*)hook_entry_open,  g_otramp, &g_otramp_ptr, (void**)&real_open);
    install_one("mmap",  (void*)hook_entry_mmap,  g_mtramp, &g_mtramp_ptr, (void**)&real_mmap);
    install_one("pread64",(void*)hook_entry_pread,g_ptramp, &g_ptramp_ptr,(void**)&real_pread64);
    return 0;
}
static void do_install(void){
    static int done=0; if(done) return; done=1;
    g_logfd=open(LOGF,O_WRONLY|O_CREAT|O_APPEND,0644);
    lgf("=== efml4a fdprobe pid=%d tid=%d ===\n",getpid(),gettid_());
    pthread_t t; if(pthread_create(&t,0,waiter,0)!=0){ lg("[fd] pthread fail"); return; }
    pthread_detach(t);
}
__attribute__((used,visibility("default"))) void init(void);
__attribute__((constructor)) static void ctor(void){ do_install(); }
__attribute__((used,visibility("default"))) void init(void){ do_install(); }
