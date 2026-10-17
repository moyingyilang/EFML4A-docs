// EFML4A 方案B：内联 hook VirtualFileSystem::GetAssetStream(ref FVFBlockFileInfo)
//   目标 RVA: 0xb385bc0   (libil2cpp.so, 1.5.3)
//   目的: 记录调用（先验证 hook 生效），后续可扩展为读取重定向
//   注入: zygisk-loader (memfd + android_dlopen_ext)
// ⚠️ 纯日志版：只记录，不改变行为 —— 用来确认 hook 是否命中
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdarg.h>

#define TARGET_RVA 0xb385bc0UL
#define PATCH_LEN  16
#define LOGF "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a_hookb.log"

static unsigned char g_saved[PATCH_LEN];
static unsigned char* g_target = NULL;
static int g_logfd = -1;
static volatile long long g_calls = 0;

static void lg(const char* s){
    if(g_logfd < 0) return;
    write(g_logfd, s, strlen(s));
    write(g_logfd, "\n", 1);
}
static void lgf(const char* fmt, ...){
    char b[256]; va_list ap; va_start(ap,fmt);
    int n=vsnprintf(b,sizeof(b),fmt,ap); va_end(ap);
    if(g_logfd>=0 && n>0) write(g_logfd,b,n);
}

/* ---------- 通用内联 hook ---------- */
static void flush_icache(void* p, size_t n){
    __builtin___clear_cache((char*)p,(char*)p+n);
}
static int mprotect_rwx(void* p, size_t n){
    unsigned long pg=(unsigned long)sysconf(_SC_PAGESIZE);
    void* s=(void*)((unsigned long)p & ~(pg-1));
    size_t  l=n+((unsigned long)p-(unsigned long)s);
    return mprotect(s,l,PROT_READ|PROT_WRITE|PROT_EXEC);
}
/* 在 target 处写 16 字节绝对跳转: ldr x16,#8 ; br x16 ; .quad dest */
static void write_jump(void* target, void* dest){
    uint32_t code[4];
    code[0]=0x58000050u;          /* ldr x16, #8 */
    code[1]=0xd61f0200u;          /* br  x16    */
    memcpy(&code[2], &dest, 8);   /* dest       */
    mprotect_rwx(target,16);
    memcpy(target,code,16);
    flush_icache(target,16);
}

/* ---------- hook 处理 ---------- */
/* 由于是纯日志版：记录后执行被覆盖的 4 条原指令，再跳回 target+16 */
extern void hook_entry(void);
static unsigned char g_tramp[64] __attribute__((aligned(16)));
static void* g_back = NULL;

/* 打印参数字段名（FVFBlockFileInfo.offset/len 在 +0x10/+0x14） */
__attribute__((used,visibility("default"))) void* hook_impl(void* a0, void* a1){
    g_calls++;
    if(g_calls <= 5 || (g_calls % 500)==0){
        unsigned off=0,len=0;
        if(a1){ memcpy(&off,(char*)a1+0x10,4); memcpy(&len,(char*)a1+0x14,4); }
        char b[256];
        int n=snprintf(b,sizeof(b),"[hookB] call#%lld a0=%p a1=%p off=%u len=%u\n",
                       g_calls,a0,a1,off,len);
        if(g_logfd>=0&&n>0) write(g_logfd,b,n);
    }
    /* 执行原指令 + 跳回 */
    typedef void* (*fn_t)(void*,void*);
    fn_t back=(fn_t)g_back;      /* g_back 指向 target+16 的续体 */
    return back(a0,a1);
}
__asm__(
".text\n"
".global hook_entry\n"
".type hook_entry, %function\n"
"hook_entry:\n"
"  stp x29, x30, [sp, #-0x10]!\n"
"  stp x0, x1, [sp, #-0x10]!\n"
"  bl  hook_impl\n"
"  mov x16, x0\n"
"  ldp x0, x1, [sp], #0x10\n"
"  ldp x29, x30, [sp], #0x10\n"
"  br  x16\n"
);

__attribute__((constructor)) static void init(void){
    g_logfd = open(LOGF, O_WRONLY|O_CREAT|O_APPEND, 0644);
    if(g_logfd<0) g_logfd = open("/data/local/tmp/efml4a_hookb.log", O_WRONLY|O_CREAT|O_APPEND, 0644);
    lg("[hookB] payload ctor 进入");

    /* 找 libil2cpp.so 基址 */
    FILE* f=fopen("/proc/self/maps","r");
    if(!f){ lg("[hookB] 打不开 maps"); return; }
    char line[512]; unsigned long base=0;
    while(fgets(line,sizeof(line),f)){
        if(strstr(line,"libil2cpp.so")){
            unsigned long a=0; sscanf(line,"%lx",&a);
            if(a && (base==0 || a<base)) base=a;
        }
    }
    fclose(f);
    if(!base){ lgf("[hookB] 未找到 libil2cpp.so base=%p\n",(void*)base); return; }
    lgf("[hookB] libil2cpp base = %p\n",(void*)base);

    g_target=(unsigned char*)(base+TARGET_RVA);
    memcpy(g_saved,g_target,PATCH_LEN);
    lgf("[hookB] target = %p\n",(void*)g_target);
    lg("[hookB] 原 16 字节已保存");

    /* 构造 trampoline: 原 4 条指令 + 跳回 target+16 */
    g_back = g_target+PATCH_LEN;
    memcpy(g_tramp, g_saved, PATCH_LEN);
    write_jump(g_tramp+PATCH_LEN, g_back);
    mprotect_rwx(g_tramp,sizeof(g_tramp)); flush_icache(g_tramp,sizeof(g_tramp));

    write_jump(g_target, (void*)hook_entry);
    lg("[hookB] hook 已安装 ✓");
}
