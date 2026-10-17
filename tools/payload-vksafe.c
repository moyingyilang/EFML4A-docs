/* EFML4A vksafe：★ 绝对安全的只读探针
 *   只 hook vkCmdCopyBufferToImage，只读 pRegions 里的【数值字段】
 *   ❌ 不解析 host、不解引用任何指针、不调用任何 Vulkan API、不写入
 *   ⇒ 唯一的内存访问是 pRegions（驱动传入的有效参数）
 *
 *   VkBufferImageCopy 布局（已验证）:
 *     +0  bufferOffset(8)  +8 bufferRowLength(4)  +12 bufferImageHeight(4)
 *     +16 imageSubresource(16)  +32 imageOffset(12)  +44 imageExtent(12)
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

#define LIBNAME "vulkan.adreno.so"
#define SYMBOL  "_ZN11qglinternal22vkCmdCopyBufferToImageEP17VkCommandBuffer_TP10VkBuffer_TP9VkImage_T13VkImageLayoutjPK17VkBufferImageCopy"
#define SYMBOL_BIND "_ZN11qglinternal18vkBindBufferMemoryEP10VkDevice_TP10VkBuffer_TP16VkDeviceMemory_Tm"
#define LOGF "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a_vksafe.log"

static unsigned char  g_saved[16];
static unsigned char* g_target=0;
static unsigned char  g_tramp[64] __attribute__((aligned(16)));
static void*          g_trampp=0;
static volatile int   g_hooked=0;
static int g_logfd=-1;
static volatile long long g_calls=0;
static int gettid_(void){ return (int)syscall(SYS_gettid); }
static void lg(const char*s){ if(g_logfd>=0) write(g_logfd,s,strlen(s)); }
static void lgf(const char*f,...){ char b[256]; va_list ap; va_start(ap,f);
  int n=vsnprintf(b,sizeof b,f,ap); va_end(ap); if(g_logfd>=0&&n>0) write(g_logfd,b,n); }
static void flush_icache(void*p,size_t n){ __builtin___clear_cache((char*)p,(char*)p+n); }
static int mprotect_rwx(void*p,size_t n){
  unsigned long pg=(unsigned long)sysconf(_SC_PAGESIZE);
  void*s=(void*)((unsigned long)p&~(pg-1));
  size_t l=n+((unsigned long)p-(unsigned long)s);
  return mprotect(s,l,PROT_READ|PROT_WRITE|PROT_EXEC);
}
static int is_mapped(unsigned long addr);   /* 前置声明 */
/* ★★★ 严格 host 校验：排除 GPU 句柄域、低地址、未映射 */
static int safe_host(unsigned long a){
  if(a < 0x100000000UL) return 0;                 /* 太低 */
  if((a >> 32) == 0xb4000077UL) return 0;          /* GPU 句柄域 0xb4000077... */
  if((a >> 40) == 0xb400UL) return 0;              /* 同上，宽范围 */
  if(a > 0x0000ffffffffffffUL) return 0;           /* 太高（tagged） */
  return is_mapped(a);
}
static int is_mapped(unsigned long addr){
  unsigned long pg=(unsigned long)sysconf(_SC_PAGESIZE);
  unsigned long p=addr & ~(pg-1); unsigned char vec=0;
  return mincore((void*)p,pg,&vec)==0;
}
static const char* br_kind(uint32_t w){
  uint32_t o26=w>>26,o24=w>>24;
  if(o26==0x05)return "B"; if(o26==0x25)return "BL";
  if(o24==0x54)return "B.cond";
  if(o24==0x34||o24==0x35||o24==0xb4||o24==0xb5)return "CBZ";
  if(o24==0x36||o24==0x37||o24==0xb6||o24==0xb7)return "TBZ";
  return 0;
}
extern void stub0(void);
void* g_trampp0=0;
__attribute__((used,visibility("default"))) long impl0(long a0,long a1,long a2,
                                                        long a3,long a4,long a5,long a6){
  long long n=++g_calls;
  int rc=(int)a4;
  const unsigned char* pr=(const unsigned char*)a5;
  /* ★ pRegions 是驱动传入的有效参数 ⇒ 只读它的数值字段，安全 */
  if(pr && rc>0 && rc<64 && is_mapped((unsigned long)pr)){
    unsigned int ew=*(const unsigned int*)(pr+44);
    unsigned int eh=*(const unsigned int*)(pr+48);
    unsigned int ed=*(const unsigned int*)(pr+52);
    unsigned long long bo=*(const unsigned long long*)(pr+0);
    /* ★ 只记录 512x512（目标）或前 80 条 */
    if((ew==512 && eh==512) || n<=80){
      lgf("[safe] #%lld %ux%ux%u bo=%llu regions=%d\n",n,ew,eh,ed,bo,rc);
    }
    if(ew==512 && eh==512){
      lgf("[safe] *** 512x512 出现！第 %lld 次调用\n",n);
    }
  }
  return 0;
}
__asm__(
".text\n.global stub0\n.type stub0, %function\n"
"stub0:\n"
"  stp x29,x30,[sp,#-16]!\n  mov x29,sp\n"
"  stp x0,x1,[sp,#-16]!\n  stp x2,x3,[sp,#-16]!\n"
"  stp x4,x5,[sp,#-16]!\n  stp x6,x7,[sp,#-16]!\n"
"  bl  impl0\n"
"  ldp x6,x7,[sp],#16\n  ldp x4,x5,[sp],#16\n"
"  ldp x2,x3,[sp],#16\n  ldp x0,x1,[sp],#16\n"
"  ldp x29,x30,[sp],#16\n"
"  adrp x16, :got:g_trampp0\n  ldr x16,[x16,:got_lo12:g_trampp0]\n  ldr x16,[x16]\n  br x16\n"
".size stub0, .-stub0\n");
static unsigned long find_lib(const char* name){
  FILE*f=fopen("/proc/self/maps","r"); if(!f) return 0;
  char line[512]; unsigned long base=0;
  while(fgets(line,sizeof line,f)){
    if(!strstr(line,name)) continue;
    unsigned long a,b,off; char perms[8];
    if(sscanf(line,"%lx-%lx %7s %lx",&a,&b,perms,&off)!=4) continue;
    if(perms[2]=='x'){ base=a-off; break; }
  }
  fclose(f); return base;
}
static unsigned long find_sym(unsigned long base,const char* name){
  FILE*f=fopen("/proc/self/maps","r"); if(!f) return 0;
  char line[512]; char path[256]={0};
  while(fgets(line,sizeof line,f)) if(strstr(line,LIBNAME)){
    char*p=strchr(line,'/'); if(p){ sscanf(p,"%255s",path); break; } }
  fclose(f);
  if(!path[0]) return 0;
  FILE*g=fopen(path,"rb"); if(!g) return 0;
  fseek(g,0,SEEK_END); long n=ftell(g); fseek(g,0,SEEK_SET);
  uint8_t*d=malloc(n); if(fread(d,1,n,g)!=(size_t)n){fclose(g);free(d);return 0;} fclose(g);
  unsigned long shoff=*(uint64_t*)(d+0x28);
  uint16_t shent=*(uint16_t*)(d+0x3a), shnum=*(uint16_t*)(d+0x3c), shstr=*(uint16_t*)(d+0x3e);
  unsigned long dynsym=0,dynstr=0,dynsymsz=0;
  const char*ss=(const char*)(d+*(uint64_t*)(d+shoff+shstr*shent+0x18));
  for(int i=0;i<shnum;i++){ uint8_t*sh=d+shoff+i*shent; const char*nm=ss+*(uint32_t*)sh;
    if(!strcmp(nm,".dynsym")){ dynsym=*(uint64_t*)(sh+0x18); dynsymsz=*(uint64_t*)(sh+0x20); }
    else if(!strcmp(nm,".dynstr")) dynstr=*(uint64_t*)(sh+0x18); }
  unsigned long res=0;
  if(dynsym&&dynstr) for(unsigned long o=0;o<dynsymsz;o+=24){
    uint32_t no=*(uint32_t*)(d+dynsym+o); if(!no) continue;
    if(!strcmp((const char*)(d+dynstr+no),name)){ res=*(uint64_t*)(d+dynsym+o+8); break; } }
  free(d); return res?base+res:0;
}
static int install_once(void){
  if(g_hooked) return 1;
  unsigned long base=find_lib(LIBNAME); if(!base) return 0;
  unsigned long s=find_sym(base,SYMBOL); if(!s) return 0;
  if(!is_mapped(s)) return 0;
  g_target=(unsigned char*)s;
  mprotect_rwx(g_target,16);
  memcpy(g_saved,g_target,16);
  int nz=0; for(int i=0;i<16;i++) if(g_saved[i]) nz++;
  if(nz<8){ lg("[safe] EMPTY\n"); return 0; }
  for(int k=0;k<4;k++){ uint32_t w; memcpy(&w,g_saved+k*4,4);
    const char*bk=br_kind(w);
    if(bk){ lgf("[safe] insn[%d]=%s 不安全\n",k,bk); return 0; }
    /* ★ 额外检查：前 16 字节内是否修改 SP（会破坏 PAC 帧链） */
    if((w&0xffc003ff)==0x910003ff || (w&0xff8003ff)==0xd10003ff ||
       (w&0xffc003ff)==0x910003ff || w==0xd106c3ff){
      lgf("[safe] insn[%d]=0x%08x 修改 SP（警告：本 stub 会在跳走前完整恢复 SP，故允许）\n",k,w); }
  }
  memcpy(g_tramp,g_saved,16);
  { uint32_t j[4]; j[0]=0x58000050u; j[1]=0xd61f0200u;
    void* cont=(void*)(g_target+16); memcpy(&j[2],&cont,8); memcpy(g_tramp+16,j,16); }
  mprotect_rwx(g_tramp,sizeof g_tramp); flush_icache(g_tramp,sizeof g_tramp);
  g_trampp0=(void*)g_tramp;
  mprotect_rwx(g_target,16);
  { uint32_t c[4]; c[0]=0x58000050u; c[1]=0xd61f0200u;
    void* d=(void*)stub0; memcpy(&c[2],&d,8); memcpy(g_target,c,16); }
  flush_icache(g_target,16);
  g_hooked=1;
  lgf("[safe] HOOKED @0x%lx（零解引用只读模式）\n",s);
  return 1;
}
static void* waiter(void* a){
  (void)a; lgf("[safe] waiter pid=%d\n",getpid());
  for(long long t=0;t<40;t++){
    if(!g_hooked) install_once();
    else { uint32_t c0; memcpy(&c0,g_target,4); if(c0!=0x58000050u) g_hooked=0; }
    usleep(200000);
  }
  lg("[safe] ★ 重装窗口已关闭（不再触碰代码）\n");
  return 0;
}
static void do_install(void){
  static int done=0; if(done)return; done=1;
  g_logfd=open(LOGF,O_WRONLY|O_CREAT|O_APPEND,0644);
  lgf("=== efml4a vksafe pid=%d（零解引用只读） ===\n",getpid());
  pthread_t t; if(pthread_create(&t,0,waiter,0)!=0){ lg("[safe] pthread fail"); return; }
  pthread_detach(t);
}
__attribute__((used,visibility("default"))) void init(void);
__attribute__((constructor)) static void ctor(void){ do_install(); }
__attribute__((used,visibility("default"))) void init(void){ do_install(); }
