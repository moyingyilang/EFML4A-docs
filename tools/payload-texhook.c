/* EFML4A keydump：hook XXE1.KeySetup(ReadOnlySpan<byte> key) @ RVA 0xc64e2f4
 *   参数: x0=this, x1=key.ptr, x2=key.len
 *   ⇒ 把 key 前后各若干字节 dump 到日志 ⇒ 拿到 VFS 内容 ChaCha20 密钥
 *   ⚠️ 纯只读
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

#define TARGET_RVA 0x1a4b2a4cUL   /* Texture2D.LoadRawTextureDataImpl(IntPtr data, UInt64 size) */
#define PATCH_LEN 16
#define LOGF "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a_tex.log"

static unsigned char  g_saved[PATCH_LEN];
static unsigned char* g_target=0;
static int g_logfd=-1;
static volatile long long g_calls=0;
static int gettid_(void){ return (int)syscall(SYS_gettid); }
static void lg(const char*s){ if(g_logfd>=0) write(g_logfd,s,strlen(s)); }
static void lgf(const char*f,...){ char b[512]; va_list ap; va_start(ap,f);
  int n=vsnprintf(b,sizeof b,f,ap); va_end(ap); if(g_logfd>=0&&n>0) write(g_logfd,b,n); }
static void hexdump(const char*tag,const uint8_t*p,int n){
  if(!p||n<=0) return;
  char line[200]; int off=0;
  for(int i=0;i<n && i<128;i++){
    off+=snprintf(line+off,sizeof line-off,"%02x",p[i]);
    if(off>180) break;
  }
  lgf("%s len=%d [%s]\n",tag,n,line);
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
  if(o26==0x05)return "B"; if(o26==0x25)return "BL";
  if(o24==0x54)return "B.cond";
  if(o24==0x34||o24==0x35||o24==0xb4||o24==0xb5)return "CBZ";
  if(o24==0x36||o24==0x37||o24==0xb6||o24==0xb7)return "TBZ";
  return 0;
}
extern void hook_entry(void);
static unsigned char g_tramp[64] __attribute__((aligned(16)));
void* g_tramp_ptr=0;
/* LoadRawTextureDataImpl(this, data, size) */
__attribute__((used,visibility("default"))) long hook_impl(long a0,long a1,long a2,long a3){
  long long n=++g_calls;
  if(n<=300){
    lgf("[tex] LoadRaw #%lld this=%p data=%p size=%ld\n",
        n,(void*)a0,(void*)a1,a2);
  }
  return 0;
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
"  adrp x16, :got:g_tramp_ptr\n  ldr x16, [x16, :got_lo12:g_tramp_ptr]\n  ldr x16, [x16]\n  br x16\n"
".size hook_entry, .-hook_entry\n");
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
  FILE*f=fopen("/proc/self/maps","r"); if(!f) return 0;
  char line[512]; unsigned long base=0, any0=0;
  while(fgets(line,sizeof line,f)){
    if(!strstr(line,"libil2cpp.so")) continue;
    unsigned long a,b,off; char perms[8];
    if(sscanf(line,"%lx-%lx %7s %lx",&a,&b,perms,&off)!=4) continue;
    if(perms[2]=='x'){ base=a-off; break; }
    if(off==0 && !any0) any0=a;
  }
  fclose(f); return base?base:any0;
}
static void install(unsigned long base){
  g_target=(unsigned char*)(base+TARGET_RVA);
  lgf("[tex] base=0x%lx target=0x%lx\n",base,(unsigned long)g_target);
  if(!is_mapped((unsigned long)g_target)){ lgf("[hook] 目标 0x%lx 未映射，稍后重试\n",(unsigned long)g_target); return; }
  mprotect_rwx(g_target,PATCH_LEN);
  /* ★★★ 只在第一次保存原始字节（重装时不能把我们的跳转当成原始字节存下来） */
  static int saved_once=0;
  if(!saved_once){
    memcpy(g_saved,g_target,PATCH_LEN);
    saved_once=1;
    int nz=0; for(int i=0;i<PATCH_LEN;i++) if(g_saved[i]) nz++;
    lgf("[tex] bytes %02x%02x%02x%02x nz=%d\n",g_saved[0],g_saved[1],g_saved[2],g_saved[3],nz);
    if(nz<8){ lg("[tex] EMPTY skip"); return; }
    for(int k=0;k<4;k++){ uint32_t w; memcpy(&w,g_saved+k*4,4);
      const char*bk=br_kind(w); if(bk){ lgf("[tex] insn[%d] %s skip\n",k,bk); return; } }
    memcpy(g_tramp,g_saved,16);
    { uint32_t j[4]; j[0]=0x58000050u; j[1]=0xd61f0200u;
      void* cont=(void*)(g_target+16); memcpy(&j[2],&cont,8); memcpy(g_tramp+16,j,16); }
    mprotect_rwx(g_tramp,sizeof g_tramp); flush_icache(g_tramp,sizeof g_tramp);
    g_tramp_ptr=(void*)g_tramp;
  }
  /* ★ 检查当前是否已是我们的跳转；不是才写 */
  {
    uint32_t cur0; memcpy(&cur0,g_target,4);
    if(cur0==0x58000050u) return;   /* 已装好，无需重复 */
  }
  if(!is_mapped((unsigned long)g_target)){ lgf("[hook] 目标 0x%lx 未映射，稍后重试\n",(unsigned long)g_target); return; }
  mprotect_rwx(g_target,PATCH_LEN);
  { uint32_t c[4]; c[0]=0x58000050u; c[1]=0xd61f0200u;
    void* d=(void*)hook_entry; memcpy(&c[2],&d,8); memcpy(g_target,c,16); }
  flush_icache(g_target,16);
  lg("[tex] ctor HOOKED");
}
static void* waiter(void* a){
  (void)a; lgf("[tex] waiter pid=%d tid=%d\n",getpid(),gettid_());
  unsigned long base=0;
  for(int i=0;i<900;i++){
    base=find_base();
    if(base){ lgf("[tex] il2cpp @0x%lx (iter=%d)\n",base,i); break; }
    usleep(100000);
  }
  if(!base){ lg("[tex] timeout"); return 0; }
  /* ★★★ 看门狗：游戏有反篡改还原 ⇒ 必须【周期性重装】 */
  for(long long tick=0; tick<9000; tick++){   /* 9000 × 200ms = 30 分钟 */
    if(tick==0){ install(base); }
    else {
      /* 只在被还原时才重装，避免无谓日志 */
      uint32_t cur0; memcpy(&cur0,g_target,4);
      if(cur0!=0x58000050u){
        install(base);
        lgf("[tex] ★ 检测到还原，已重装 #%lld (tick=%lld)\n",tick,tick);
      }
    }
    usleep(200000);
  }
  lg("[tex] watchdog exit");
  return 0;
}
static void do_install(void){
  static int done=0; if(done)return; done=1;
  g_logfd=open(LOGF,O_WRONLY|O_CREAT|O_APPEND,0644);
  lgf("=== efml4a texhook pid=%d tid=%d ===\n",getpid(),gettid_());
  pthread_t t; if(pthread_create(&t,0,waiter,0)!=0){ lg("[tex] pthread fail"); return; }
  pthread_detach(t);
}
__attribute__((used,visibility("default"))) void init(void);
__attribute__((constructor)) static void ctor(void){ do_install(); }
__attribute__((used,visibility("default"))) void init(void){ do_install(); }
