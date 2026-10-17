// EFML4A 最终 payload：进程内直接调用 GetCommonChachaKeyBs 取 VFS 密钥。
//
// RVA 来源：本机 v1.5.3 符号表（zygisk_il2cppdumper 产出的 dump.cs）
//   public static Span`1 GetCommonChachaKeyBs(Span`1 keyBuffer) { }   // RVA: 0xb37dcd0
// ABI（由反汇编确认）：入参 x0=Span.ptr, x1=Span.len；返回 x0=key.ptr, x1=key.len。
//
// 关键教训（此前 payload 全部"无声失败"的真正原因）：
//   /data/local/tmp 对 untrusted_app 不可写（SELinux），且早期阶段
//   /proc/self/cmdline 仍是 zygote ⇒ 用 cmdline 推路径会失败。
//   故此处一律使用**硬编码**的、经验证可写的路径。
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <link.h>
#include <pthread.h>
#include <dlfcn.h>

#define RVA_GET_KEY     0xb37dcd0ULL
#define LOG1 "/data/data/com.hypergryph.endfield/files/efml4a-callkey.log"
#define LOG2 "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a-callkey.log"
#define KEYF "/data/data/com.hypergryph.endfield/files/efml4a-vfskey.txt"

static void Log(const char* fmt, ...){
    char line[768]; va_list ap; va_start(ap,fmt);
    int m=vsnprintf(line,sizeof(line),fmt,ap); va_end(ap);
    const char* P[2]={LOG1,LOG2};
    for(int i=0;i<2;i++){
        int fd=open(P[i],O_WRONLY|O_CREAT|O_APPEND,0644); if(fd<0) continue;
        char b[832]; int n=snprintf(b,sizeof(b),"[callkey pid=%d] %.*s\n",(int)getpid(),m>0?m:0,line);
        if(n>0){ ssize_t w=write(fd,b,(size_t)n); (void)w; }
        close(fd);
    }
}
static void SaveKey(const char* hex){
    int fd=open(KEYF,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd<0) return;
    ssize_t w=write(fd,hex,strlen(hex)); (void)w; w=write(fd,"\n",1); (void)w; close(fd);
}

typedef struct { unsigned long long ptr, len; } Span;
typedef Span (*fn_t)(Span);

static int FindBase(struct dl_phdr_info* i, size_t s, void* d){
    (void)s;
    if(i->dlpi_name && strstr(i->dlpi_name,"libil2cpp.so")){ *(unsigned long long*)d=(unsigned long long)i->dlpi_addr; return 1; }
    return 0;
}

static void* Worker(void* arg){
    (void)arg;
    unsigned long long base=0;
    /* 等 libil2cpp.so 出现（早期阶段它还没加载） */
    for(int t=0;t<600;t++){
        base=0; dl_iterate_phdr(FindBase,&base);
        if(base) break;
        usleep(100000);
    }
    if(!base){ Log("60 秒内未见 libil2cpp.so，放弃"); return NULL; }
    Log("libil2cpp.so 基址=0x%llx",base);
    fn_t fn=(fn_t)(base+RVA_GET_KEY);
    Log("GetCommonChachaKeyBs @0x%llx，准备调用",(unsigned long long)fn);

    static unsigned char buf[512];
    Span in={ (unsigned long long)buf, sizeof(buf) };
    Span out=fn(in);
    Log("调用返回: ptr=0x%llx len=%llu",out.ptr,out.len);
    if(out.ptr && out.len>0 && out.len<=256){
        const unsigned char* k=(const unsigned char*)out.ptr;
        char hex[520]; int n=0;
        for(unsigned long long i=0;i<out.len && n<(int)sizeof(hex)-3;i++)
            n+=snprintf(hex+n,sizeof(hex)-n,"%02x",k[i]);
        Log("★★★ VFS 密钥(%llu 字节) = %s",out.len,hex);
        SaveKey(hex);
    } else {
        Log("返回值不合法；尝试再调一次（可能首次触发类初始化）");
        out=fn(in);
        Log("第二次: ptr=0x%llx len=%llu",out.ptr,out.len);
        if(out.ptr && out.len>0 && out.len<=256){
            const unsigned char* k=(const unsigned char*)out.ptr;
            char hex[520]; int n=0;
            for(unsigned long long i=0;i<out.len && n<(int)sizeof(hex)-3;i++)
                n+=snprintf(hex+n,sizeof(hex)-n,"%02x",k[i]);
            Log("★★★ VFS 密钥(%llu 字节) = %s",out.len,hex);
            SaveKey(hex);
        }
    }
    return NULL;
}

extern "C" __attribute__((visibility("default"))) int init(void){
    Log("init() 被显式调用");
    pthread_t t; pthread_create(&t,NULL,Worker,NULL); pthread_detach(t);
    return 0;
}

__attribute__((constructor)) static void Ctor(void){
    Log("=== EFML4A callkey payload 构造函数执行（注入成功） ===");
    pthread_t t; pthread_create(&t,NULL,Worker,NULL); pthread_detach(t);
}
