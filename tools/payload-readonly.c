// EFML4A payload v4：**纯内存读取** VFS 密钥材料，不执行任何托管代码。
//
// 为什么必须这样：v2/v3 直接调用 GetCommonChachaKeyBs，在类未初始化时会走
// 运行时初始化分支（0xb37de44 -> 0x9a3fca0）并在 0x9a3fdd8 崩溃。本版改为只读。
//
// 指针链（全部由本机 v1.5.3 符号表 + 反汇编得出，见 docs/）：
//   b37dd20: ldr x8,[x22,#0xfb0]   ; x22=0x1a47e000
//   b37dd24: ldr x8,[x8,#0xb8]     ; Il2CppClass->static_fields
//   b37dd28: ldr x8,[x8,#0x38]     ; CHACHA_KEYS
// 故：class = *(void**)(base+0x1a47e000+0xfb0)
//     statics = *(void**)(class+0xb8)
//     CHACHA_KEY(0x30) / CHACHA_KEYS(0x38) / s_commomChachaKeyBs(0x40)
// Il2CppArray: len@+0x18 data@+0x20   Il2CppString: len@+0x10 UTF16@+0x14
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
#define OFF_CHACHAKEY 0x30ULL
#define OFF_CHACHAKEYS 0x38ULL
#define OFF_KEYBS     0x40ULL

#define LOG1 "/data/data/com.hypergryph.endfield/files/efml4a-read.log"
#define LOG2 "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a-read.log"
#define OUTF "/data/data/com.hypergryph.endfield/files/efml4a-vfsmaterial.txt"

static void Log(const char* fmt, ...){
    char line[1024]; va_list ap; va_start(ap,fmt);
    int m=vsnprintf(line,sizeof(line),fmt,ap); va_end(ap);
    const char* P[2]={LOG1,LOG2};
    for(int i=0;i<2;i++){
        int fd=open(P[i],O_WRONLY|O_CREAT|O_APPEND,0644); if(fd<0) continue;
        char b[1088]; int n=snprintf(b,sizeof(b),"[ro pid=%d] %.*s\n",(int)getpid(),m>0?m:0,line);
        if(n>0){ ssize_t w=write(fd,b,(size_t)n); (void)w; }
        close(fd);
    }
}
static void Out(const char* s){
    int fd=open(OUTF,O_WRONLY|O_CREAT|O_APPEND,0644); if(fd<0) return;
    ssize_t w=write(fd,s,strlen(s)); (void)w; close(fd);
}

/* ---------- 读保护：任何非法解引用都被 siglongjmp 接住，不崩进程 ---------- */
static sigjmp_buf JB;
static void OnSegv(int sig){ (void)sig; siglongjmp(JB,1); }
static int SafeRead(void* dst, const void* src, size_t n){
    if(sigsetjmp(JB,1)==0){ memcpy(dst,src,n); return 0; }
    return -1;
}
static void* SafeDeref(const void* p){
    void* v=NULL; if(!p) return NULL;
    if(SafeRead(&v,p,sizeof(v))!=0) return NULL;
    return v;
}
static unsigned SafeU32(const void* p, unsigned dflt){
    uint32_t v=0; if(!p) return dflt;
    if(SafeRead(&v,p,sizeof(v))!=0) return dflt;
    return v;
}

/* UTF-16LE → UTF-8（ASCII 足够；非 ASCII 用 '?'） */
static void U16toU8(const uint16_t* s, int len, char* out, int cap){
    int o=0;
    for(int i=0;i<len && o<cap-2;i++){
        uint16_t c=s[i];
        if(c<0x80) out[o++]=(char)c;
        else if(c<0x800 && o<cap-3){ out[o++]=(char)(0xC0|(c>>6)); out[o++]=(char)(0x80|(c&0x3F)); }
        else if(o<cap-4){ out[o++]=(char)(0xE0|(c>>12)); out[o++]=(char)(0x80|((c>>6)&0x3F)); out[o++]=(char)(0x80|(c&0x3F)); }
    }
    out[o]=0;
}
static void DumpManagedString(const char* label, void* strObj){
    if(!strObj){ Log("%s = NULL",label); return; }
    int len=(int)SafeU32((const char*)strObj+0x10, 0x7fffffff);
    if(len<0 || len>4096){ Log("%s 长度异常(%d)，跳过",label,len); return; }
    uint16_t* chars=(uint16_t*)((char*)strObj+0x14);
    char buf[8192]; U16toU8(chars,len,buf,sizeof(buf));
    char hex[256]; int n=0;
    for(int i=0;i<len && i<32;i++) n+=snprintf(hex+n,sizeof(hex)-n,"%04x ",(unsigned)chars[i]);
    Log("%s = \"%s\"   (len=%d, 前 32 个 UTF-16: %s)",label,buf,len,hex);
    char line[9000]; snprintf(line,sizeof(line),"%s = %s\n",label,buf); Out(line);
}

static int FindBase(struct dl_phdr_info* i, size_t s, void* d){
    (void)s;
    if(i->dlpi_name && strstr(i->dlpi_name,"libil2cpp.so")){
        *(unsigned long long*)d=(unsigned long long)i->dlpi_addr; return 1; }
    return 0;
}

static void* Worker(void* arg){
    (void)arg;
    unsigned long long base=0;
    for(int t=0;t<1200;t++){ base=0; dl_iterate_phdr(FindBase,&base); if(base) break; usleep(100000); }
    if(!base){ Log("未见 libil2cpp.so"); return NULL; }
    Log("libil2cpp.so 基址 = 0x%llx",base);

    volatile uint32_t* slot=(volatile uint32_t*)(base+RVA_CLASSSLOT+OFF_CLASSSLOT);
    void* klass=NULL; void* statics=NULL;
    for(int t=0;t<6000;t++){
        klass = SafeDeref((const void*)slot);
        if(klass){ statics = SafeDeref((const char*)klass+OFF_STATICS);
                   if(statics){ void* kk=SafeDeref((const char*)statics+OFF_CHACHAKEYS);
                                if(kk) break; } }   /* 只要 CHACHA_KEYS 就绪即立刻转储 */
        usleep(100000);
    }
    Log("class=%p statics=%p",klass,statics);
    if(!statics){ Log("静态字段区仍为空，放弃（不执行任何托管代码）"); return NULL; }

    char hdr[256];
    snprintf(hdr,sizeof(hdr),"\n===== pid=%d base=0x%llx class=%p statics=%p =====\n",(int)getpid(),base,klass,statics);
    Out(hdr);

    /* 1) master key 字符串（CHACHA_KEY, 0x30） */
    DumpManagedString("CHACHA_KEY", SafeDeref((const char*)statics+OFF_CHACHAKEY));


    /* 4) 派生函数使用的第三个参数（b37de0c: ldr x3,[x8,#0x1d0] 其中 x8=0x1a5d2000） */
    void* x3 = SafeDeref((const char*)(base + 0x1a5d2000ULL + 0x1d0ULL));
    Log("派生第三参数 *(0x1a5d2000+0x1d0) = %p", x3);
    if(x3){
        int l3=(int)SafeU32((const char*)x3+0x10, 0x7fffffff);
        Log("  其 +0x10 = %d（若像字符串长度则可按字符串解读）", l3);
        DumpManagedString("x3-as-string", x3);
    }
    void* x568 = SafeDeref((const char*)(base + 0x1a47e000ULL + 0x568ULL));
    Log("*(0x1a47e000+0x568) = %p", x568);
    if(x568) DumpManagedString("x568-as-string", x568);

    /* 2) CHACHA_KEYS 数组（0x38）：8 个 Il2CppString* */
    void* arr=SafeDeref((const char*)statics+OFF_CHACHAKEYS);
    if(!arr){ Log("CHACHA_KEYS = NULL"); }
    else {
        int n=(int)SafeU32((const char*)arr+0x18, -1);
        Log("CHACHA_KEYS 数组长度 = %d",n);
        if(n>0 && n<64){
            for(int i=0;i<n;i++){
                void* sp=SafeDeref((const char*)arr+0x20+i*8);
                char label[64]; snprintf(label,sizeof(label),"CHACHA_KEYS[%d]",i);
                DumpManagedString(label,sp);
            }
        }
    }

    /* 3) s_commomChachaKeyBs（0x40）：32 字节 */
    void* kb=NULL;
    Log("开始长轮询 s_commomChachaKeyBs（最多 600 秒）…");
    for(int t=0;t<6000;t++){ kb=SafeDeref((const char*)statics+OFF_KEYBS); if(kb) break; usleep(100000); }
    if(!kb){ Log("s_commomChachaKeyBs 600 秒内始终为 NULL（游戏未走该路径）"); }
    else {
        int n=(int)SafeU32((const char*)kb+0x18, -1);
        Log("s_commomChachaKeyBs 长度 = %d",n);
        if(n>0 && n<=256){
            Log("s_commomChachaKeyBs 已就绪（长度 %d）",n);
            unsigned char data[256]; 
            if(SafeRead(data,(const char*)kb+0x20,(size_t)n)==0){
                char hex[600]; int o=0;
                for(int i=0;i<n && o<(int)sizeof(hex)-3;i++) o+=snprintf(hex+o,sizeof(hex)-o,"%02x",data[i]);
                Log("★★★ s_commomChachaKeyBs = %s",hex);
                char line[700]; snprintf(line,sizeof(line),"KEYBS = %s\n",hex); Out(line);
            } else Log("读取字节数组失败");
        }
    }
    Log("=== 读取完成 ===");
    return NULL;
}

extern "C" __attribute__((visibility("default"))) int init(void){
    pthread_t t; pthread_create(&t,NULL,Worker,NULL); pthread_detach(t); return 0;
}
__attribute__((constructor)) static void Ctor(void){
    /* 安装 SIGSEGV 保护（独立信号栈） */
    static char altstk[65536];
    stack_t ss; ss.ss_sp=altstk; ss.ss_size=sizeof(altstk); ss.ss_flags=0;
    sigaltstack(&ss,NULL);
    struct sigaction sa; memset(&sa,0,sizeof(sa));
    sa.sa_handler=OnSegv; sa.sa_flags=SA_NODEFER|SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV,&sa,NULL); sigaction(SIGBUS,&sa,NULL);
    Log("=== readonly payload 构造函数执行 ===");
    pthread_t t; pthread_create(&t,NULL,Worker,NULL); pthread_detach(t);
}
