// 最小验证 v2：排除「日志路径不可写」这个变量。
// 同时导出 init()（zygisk-loader 可能只显式调用 init，而不依赖构造函数）。
#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static void try_write(const char* path, const char* tag){
    int fd = open(path, O_WRONLY|O_CREAT|O_APPEND, 0644);
    if(fd < 0) return;
    char b[256];
    int n = snprintf(b,sizeof(b),"[%s] pid=%d uid=%d\n", tag, (int)getpid(), (int)getuid());
    if(n>0){ ssize_t w=write(fd,b,(size_t)n); (void)w; }
    close(fd);
}

extern "C" void efml4a_go(const char* tag);
static void go(const char* tag){
    try_write("/data/data/com.hypergryph.endfield/files/efml4a-t1.log", tag);
    try_write("/data/user/0/com.hypergryph.endfield/files/efml4a-t2.log", tag);
    try_write("/data/local/tmp/efml4a-t3.log", tag);
    try_write("/sdcard/Android/data/com.hypergryph.endfield/files/efml4a-t4.log", tag);
}

/* 两条路都铺上 */
__attribute__((constructor)) static void Ctor(void){ go("ctor"); }
extern "C" __attribute__((visibility("default"))) int init(void){ go("init"); return 0; }
extern "C" __attribute__((visibility("default"))) int zygisk_module_entry(void* a, void* b){ (void)a;(void)b; return 0; }
