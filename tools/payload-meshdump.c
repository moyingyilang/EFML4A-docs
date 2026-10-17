/* EFML4A 只读 hook：Mesh.get_vertices() -> Vector3[]  @ 0x1a4abe64
 *   ⚠️ 纯只读：只记录，不改任何数据/返回值
 *   ★ 运行时自检：若目标函数前 4 条含分支 ⇒ 拒绝安装（避免 trampoline 重定位错误导致崩溃）
 *   ★ 延迟安装（等 libil2cpp.so）
 *   ★ 构建：必须清 RUNPATH + 声明 DT_NEEDED（见 BUILD.md）
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

/* ★ 多目标：全部已验证「前 4 条无分支」⇒ trampoline 安全 */
static const struct { unsigned long rva; const char* name; int nargs; } g_targets[] = {
    { 0x1a4ab408UL, "get_bounds",        1 },  /* ★ 视锥剔除每帧调用 ⇒ 最热 */
    { 0x1a4ab378UL, "get_subMeshCount",  1 },
    { 0x1a4abe64UL, "get_vertices",      1 },
    { 0x1a4ad29cUL, "SetUVs",            6 },
};
#define NTARGETS ((int)(sizeof(g_targets)/sizeof(g_targets[0])))
#define PATCH_LEN  16
#define LOGF "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a_meshdump.log"

static unsigned char  g_saved[NTARGETS][PATCH_LEN];
static unsigned char* g_target[NTARGETS];
static int  g_logfd = -1;
static volatile long long g_calls = 0;

static int gettid_(void){ return (int)syscall(SYS_gettid); }
static void lg(const char* s){ if(g_logfd>=0) write(g_logfd,s,strlen(s)); }
static void lgf(const char* f,...){ char b[320]; va_list ap; va_start(ap,f);
    int n=vsnprintf(b,sizeof b,f,ap); va_end(ap); if(g_logfd>=0&&n>0) write(g_logfd,b,n); }

static void flush_icache(void* p,size_t n){ __builtin___clear_cache((char*)p,(char*)p+n); }
static int mprotect_rwx(void* p,size_t n){
    unsigned long pg=(unsigned long)sysconf(_SC_PAGESIZE);
    void* s=(void*)((unsigned long)p & ~(pg-1));
    size_t l=n+((unsigned long)p-(unsigned long)s);
    return mprotect(s,l,PROT_READ|PROT_WRITE|PROT_EXEC);
}
static void write_jump(void* t,void* d){
    uint32_t c[4]; c[0]=0x58000050u; c[1]=0xd61f0200u; memcpy(&c[2],&d,8);
    mprotect_rwx(t,16); memcpy(t,c,16); flush_icache(t,16);
}

/* ★ 分支检测（ARM64 正确编码） */
static const char* br_kind(uint32_t w){
    uint32_t o26=w>>26, o24=w>>24;
    if(o26==0x05) return "B";
    if(o26==0x25) return "BL";
    if(o24==0x54) return "B.cond";
    if(o24==0x34||o24==0x35||o24==0xb4||o24==0xb5) return "CBZ/CBNZ";
    if(o24==0x36||o24==0x37||o24==0xb6||o24==0xb7) return "TBZ/TBNZ";
    return 0;
}

extern void hook_entry0(void);
extern void hook_entry1(void);
extern void hook_entry2(void);
static void* const g_entries[3] = { (void*)hook_entry0, (void*)hook_entry1, (void*)hook_entry2 };
void* g_tramp_ptr = 0;
static unsigned char g_tramp[NTARGETS][64] __attribute__((aligned(16)));

/* get_vertices(self) -> Vector3[] */
__attribute__((used,visibility("default"))) long hook_impl(long a0,long a1,long a2,long a3,long id){
    long long n = ++g_calls;
    if(n<=500 && id>=0 && id<NTARGETS)
        lgf("[mesh] %s #%lld a0=%p a1=%ld a2=%ld a3=%ld\n",
            g_targets[id].name, n, (void*)a0, a1, a2, a3);
    return 0;
}
__attribute__((used,visibility("default"))) void* g_tramp_addr(void){ return (void*)g_tramp; }
__asm__(
".text\n"
".macro HOOKSTUB id\n"
".global hook_entry\\id\n"
".type hook_entry\\id, %function\n"
"hook_entry\\id:\n"
"  stp x29, x30, [sp, #-16]!\n"
"  mov x29, sp\n"
"  stp x0, x1, [sp, #-16]!\n"
"  stp x2, x3, [sp, #-16]!\n"
"  stp x4, x5, [sp, #-16]!\n"
"  stp x6, x7, [sp, #-16]!\n"
"  mov  w17, #\\id\n"
"  bl  hook_impl\n"
"  ldp x6, x7, [sp], #16\n"
"  ldp x4, x5, [sp], #16\n"
"  ldp x2, x3, [sp], #16\n"
"  ldp x0, x1, [sp], #16\n"
"  ldp x29, x30, [sp], #16\n"
"  adrp x16, :got:g_tramp\n"
"  add  x16, x16, :lo12:g_tramp\n"
"  add  x16, x16, #(\\id * 64)\n"
"  br   x16\n"
".size hook_entry\\id, .-hook_entry\\id\n"
".endm\n"
"HOOKSTUB 0\n"
"HOOKSTUB 1\n"
"HOOKSTUB 2\n");

/* ★★★ 检查地址是否已在某个【可读】映射内（防止写入未映射页导致 SIGSEGV） */
/* ★★★ 用 mincore 检查地址是否已映射（比解析 maps 可靠） */
static int is_mapped(unsigned long addr){
  unsigned long pg=(unsigned long)sysconf(_SC_PAGESIZE);
  unsigned long p=addr & ~(pg-1);
  unsigned char vec=0;
  if(mincore((void*)p,pg,&vec)!=0) return 0;
  return 1;
}
static unsigned long find_base(void){
    /* ★★★ 正确做法：从【可执行段】反推加载基址
     *   base = exec_start - exec_offset
     *   （直接取第一行 offset==0 是错的：可能有多个 offset=0 的映射） */
    FILE* f=fopen("/proc/self/maps","r"); if(!f) return 0;
    char line[512]; unsigned long base=0, any0=0;
    while(fgets(line,sizeof line,f)){
        if(!strstr(line,"libil2cpp.so")) continue;
        unsigned long a,b,off; char perms[8];
        if(sscanf(line,"%lx-%lx %7s %lx",&a,&b,perms,&off)!=4) continue;
        if(perms[2]=='x'){ base=a-off; break; }      /* ★ 可执行段优先 */
        if(off==0 && !any0) any0=a;                  /* 后备 */
    }
    fclose(f);
    return base?base:any0;
}
/* ★ 目标可执行段范围检查：用 offset==0 基址时 VA 恒等映射
 *   代码段文件范围 0x9534000..0x1ef58000；目标必须在此区间 */
static int range_ok(unsigned long va){
    return (va>=0x9534000UL && va<0x1ef58000UL);
}
static void install_hook(unsigned long base){
    for(int t=0;t<NTARGETS;t++){
        unsigned long rva=g_targets[t].rva;
        if(rva<0x9534000UL||rva>=0x1ef58000UL){ lgf("[mesh] %s: RVA out of range\n",g_targets[t].name); continue; }
        unsigned char* tgt=(unsigned char*)(base+rva);
        mprotect_rwx(tgt,PATCH_LEN);
        memcpy(g_saved[t],tgt,PATCH_LEN);
        int nz=0; for(int k=0;k<PATCH_LEN;k++) if(g_saved[t][k]) nz++;
        lgf("[mesh] %s @0x%lx bytes %02x%02x%02x%02x nz=%d\n",
            g_targets[t].name,(unsigned long)tgt,g_saved[t][0],g_saved[t][1],g_saved[t][2],g_saved[t][3],nz);
        if(nz<8){ lgf("[mesh] %s: EMPTY, skip\n",g_targets[t].name); continue; }
        int ok=1;
        for(int k=0;k<4;k++){
            uint32_t w; memcpy(&w,g_saved[t]+k*4,4);
            const char* bk=br_kind(w);
            if(bk){ lgf("[mesh] %s: insn[%d] is %s -> skip\n",g_targets[t].name,k,bk); ok=0; break; }
        }
        if(!ok) continue;
        memcpy(g_tramp[t],g_saved[t],16);
        { uint32_t j[4]; j[0]=0x58000050u; j[1]=0xd61f0200u;
          void* cont=(void*)(tgt+16); memcpy(&j[2],&cont,8);
          memcpy(g_tramp[t]+16,j,16); }
        mprotect_rwx(g_tramp[t],sizeof g_tramp[t]);
        flush_icache(g_tramp[t],sizeof g_tramp[t]);
        g_target[t]=tgt;
        write_jump(tgt,g_entries[t]);
        lgf("[mesh] %s: HOOKED\n",g_targets[t].name);
    }
}
static void* waiter(void* arg){
    (void)arg;
    lgf("[mesh] waiter pid=%d tid=%d\n", getpid(), gettid_());
    for(int i=0;i<900;i++){
        unsigned long base=find_base();
        if(base){ lgf("[mesh] il2cpp @0x%lx (iter=%d)\n",base,i);
                  install_hook(base); return 0; }
        usleep(100000);
    }
    lg("[mesh] timeout: il2cpp never appeared");
    return 0;
}
static void do_install(void){
    static int done=0; if(done) return; done=1;
    g_logfd=open(LOGF,O_WRONLY|O_CREAT|O_APPEND,0644);
    lgf("=== efml4a meshdump pid=%d tid=%d ===\n", getpid(), gettid_());
    pthread_t t;
    if(pthread_create(&t,0,waiter,0)!=0){ lg("[mesh] pthread_create failed"); return; }
    pthread_detach(t);
}
__attribute__((used,visibility("default"))) void init(void);
__attribute__((constructor)) static void ctor(void){ do_install(); }
__attribute__((used,visibility("default"))) void init(void){ do_install(); }
