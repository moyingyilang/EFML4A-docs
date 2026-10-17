/* EFML4A vktex：Vulkan 运行时纹理替换
 *   目标1 vkMapMemory        (device, memory, offset, size, flags, ppData)
 *        ⇒ 记录 memory → 主机指针
 *   目标2 vkBindBufferMemory (device, buffer, memory, memoryOffset)
 *        ⇒ 记录 buffer → (memory, offset)
 *   目标3 vkCmdCopyBufferToImage (cmd, buffer, image, layout, regionCount, pRegions)
 *        ⇒ 查表解析可写地址；命中目标尺寸则覆写 mod 数据
 *   ⚠️ 默认【只观测】，把 ENABLE_REPLACE 设为 1 才替换
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

#include "moddata.h"   /* g_modBC7[262144], MOD_DATA_SIZE */

#define ENABLE_REPLACE 0        /* ★★★ 纯只读：不做任何写入 */
#define TARGET_W 512
#define TARGET_H 512

#define LIBNAME "vulkan.adreno.so"
#define LOGF "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a_vktex.log"

/* 完整 mangled 名 */
static const char* SYM_MAP  = "_ZN11qglinternal11vkMapMemoryEP10VkDevice_TP16VkDeviceMemory_TmmjPPv";
static const char* SYM_BIND = "_ZN11qglinternal18vkBindBufferMemoryEP10VkDevice_TP10VkBuffer_TP16VkDeviceMemory_Tm";
static const char* SYM_COPY = "_ZN11qglinternal22vkCmdCopyBufferToImageEP17VkCommandBuffer_TP10VkBuffer_TP9VkImage_T13VkImageLayoutjPK17VkBufferImageCopy";

#define NMEM 8192
#define NBUF 16384
static struct { void* mem; void* host; } g_mem[NMEM]; static volatile int g_memn=0;
static struct { void* buf; void* mem; unsigned long off; } g_buf[NBUF]; static volatile int g_bufn=0;

static unsigned char  g_saved[3][16];
static unsigned char* g_tgt[3];
static unsigned char  g_tramp[3][64] __attribute__((aligned(16)));
static void*          g_trampp[3];
static volatile int   g_hooked[3];
static int g_logfd=-1;
static void* g_device=0;              /* 从 vkBindBufferMemory 的 x0 拿到 */
static void* (*g_fnMapMemory)(void*,void*,unsigned long,unsigned long,unsigned int,void**)=0;
static volatile long long g_calls[3];
static int gettid_(void){ return (int)syscall(SYS_gettid); }
static void lg(const char*s){ if(g_logfd>=0) write(g_logfd,s,strlen(s)); }
static void lgf(const char*f,...){ char b[320]; va_list ap; va_start(ap,f);
  int n=vsnprintf(b,sizeof b,f,ap); va_end(ap); if(g_logfd>=0&&n>0) write(g_logfd,b,n); }
static void flush_icache(void*p,size_t n){ __builtin___clear_cache((char*)p,(char*)p+n); }
static int mprotect_rwx(void*p,size_t n){
  unsigned long pg=(unsigned long)sysconf(_SC_PAGESIZE);
  void*s=(void*)((unsigned long)p&~(pg-1));
  size_t l=n+((unsigned long)p-(unsigned long)s);
  return mprotect(s,l,PROT_READ|PROT_WRITE|PROT_EXEC);
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
  if((w&0xfffffbff)==0xd503233f){ /* ★ 警告：paciasp；实测若其后不改 SP 则可安全 hook */ return 0; }
  if(w==0xd503237f) return 0;
  if(w==0xd503245f) return 0;
  return 0;
}
extern void stub0(void), stub1(void), stub2(void);
void* g_trampp0=0; void* g_trampp1=0; void* g_trampp2=0;

/* ---- 目标1: vkMapMemory ---- */
__attribute__((used,visibility("default"))) long impl0(long a0,long a1,long a2,
                                                        long a3,long a4,long a5,long a6){
  long long n=++g_calls[0];
  /* 原函数已通过 trampoline 执行吗？不 —— 我们在调用前记录入参，
     真正的 ppData 要等原函数写完。所以先把 (memory, ppData槽) 记下，
     下一层再解析。简化：此处不记录，改用 vkMapMemory 的返回值不可靠，
     ⇒ 改为在 CopyBufferToImage 时直接读 VkDeviceMemory 的映射（方案B）。
     实际采用：记录 (memory → ppData 所指的槽地址)，稍后读取 *槽。 */
  if(a5 && g_memn<NMEM){
    /* a5 = ppData (void**)，原函数返回后 *a5 才是主机指针 */
    g_mem[g_memn].mem=(void*)a1; g_mem[g_memn].host=(void*)a5; g_memn++;
    if(n<=5) lgf("[vktex] MapMemory #%lld mem=%p ppData=%p size=%ld\n",n,(void*)a1,(void*)a5,a3);
  }
  return 0;
}
/* ---- 目标2: vkBindBufferMemory ---- */
__attribute__((used,visibility("default"))) long impl1(long a0,long a1,long a2,
                                                        long a3,long a4,long a5,long a6){
  long long n=++g_calls[1];
  if(a0) g_device=(void*)a0;
  if(g_bufn<NBUF && a1){
    g_buf[g_bufn].buf=(void*)a1; g_buf[g_bufn].mem=(void*)a2; g_buf[g_bufn].off=(unsigned long)a3;
    g_bufn++;
    if(n<=5) lgf("[vktex] BindBufferMemory #%lld buf=%p mem=%p off=%lu\n",n,(void*)a1,(void*)a2,(unsigned long)a3);
  }
  return 0;
}
/* ---- 目标3: vkCmdCopyBufferToImage ---- */
__attribute__((used,visibility("default"))) long impl2(long a0,long a1,long a2,
                                                        long a3,long a4,long a5,long a6){
  long long n=++g_calls[2];
  int rc=(int)a4; const unsigned char* pr=(const unsigned char*)a5;
  if(!pr||rc<=0||rc>64) return 0;
  unsigned int ew=*(const unsigned int*)(pr+44), eh=*(const unsigned int*)(pr+48);
  unsigned long long bo=*(const unsigned long long*)(pr+0);
  /* 解析 buffer → 主机地址 */
  void* host=0;
  for(int i=0;i<g_bufn;i++) if(g_buf[i].buf==(void*)a1){
    void* mem=g_buf[i].mem; unsigned long moff=g_buf[i].off;
    for(int k=0;k<g_memn;k++) if(g_mem[k].mem==mem){
      host=(char*)(*(void**)g_mem[k].host)+moff; break;
    }
    break;
  }
  if(!host && g_device && g_fnMapMemory && g_bufn<NBUF){
    for(int i=0;i<g_bufn;i++) if(g_buf[i].buf==(void*)a1){
      void* p=0;
      if(g_fnMapMemory(g_device,g_buf[i].mem,0,~0UL,0,&p)==0 && p) host=(char*)p+g_buf[i].off;
      break;
    }
  }
  /* ★ 512x512 详细特征（只读） */
  if(ew==512 && eh==512 && host && is_mapped((unsigned long)host)){
    unsigned int s0=0; unsigned long long sum=0;
    for(int k=0;k<64;k++){ unsigned char c=((unsigned char*)host)[k]; s0=s0*31+c; sum+=c; }
    lgf("[vktex] *** 512x512 特征 host=%p bo=%llu sum64=%llu s0=%08x 前16=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
        host,bo,sum,s0,
        ((unsigned char*)host)[0],((unsigned char*)host)[1],((unsigned char*)host)[2],((unsigned char*)host)[3],
        ((unsigned char*)host)[4],((unsigned char*)host)[5],((unsigned char*)host)[6],((unsigned char*)host)[7],
        ((unsigned char*)host)[8],((unsigned char*)host)[9],((unsigned char*)host)[10],((unsigned char*)host)[11],
        ((unsigned char*)host)[12],((unsigned char*)host)[13],((unsigned char*)host)[14],((unsigned char*)host)[15]);
  }
  if(n<=60){
    /* ★ 只读验证：读回 host 处前 8 字节，确认指针真的指向纹理数据 */
    unsigned int d0=0,d1=0;
    if(host && is_mapped((unsigned long)host)){
      d0=*(unsigned int*)host;
      d1=*(unsigned int*)((char*)host+4);
    }
    lgf("[vktex] Copy #%lld ext=%ux%u bo=%llu host=%p data=%08x %08x\n",
        n,ew,eh,bo,host,d0,d1);
  }
  /* ★ 尺寸匹配替换：512x512 用完整 mod；256x256 用前 65536 字节（显眼测试） */
  {
    unsigned long want = (unsigned long)ew * (unsigned long)eh;   /* BC7 字节数 = w*h */
    int is_target = (ew==TARGET_W && eh==TARGET_H);
    int is_test   = 0;   /* ★ 已禁用：把 BC7 写进非 BC7 纹理会崩驱动 */
    if(ENABLE_REPLACE && host && (is_target || is_test)){
      unsigned long n2 = want;
      if(n2 > MOD_DATA_SIZE) n2 = MOD_DATA_SIZE;
      char*dst=(char*)host+bo;
      if(is_mapped((unsigned long)dst) && is_mapped((unsigned long)(dst+n2-1))){
        memcpy(dst,g_modBC7,n2);
        { unsigned int v0=*(unsigned int*)dst, v1=*(unsigned int*)(dst+4);
          lgf("[vktex] 写后读回 %p = %08x %08x (mod 前 8 字节 = %08x %08x)\n",
              dst,v0,v1,*(const unsigned int*)g_modBC7,*(const unsigned int*)(g_modBC7+4)); }
        lgf("[vktex] ★★★ 已替换 %ux%u @%p（%lu 字节）%s\n",ew,eh,dst,n2,
            is_target?"[目标512x512]":"[测试256x256]");
      } else {
        lgf("[vktex] 目标 %p 未映射，跳过 (%lux%lu)\n",dst,n2,0UL);
      }
    }
  }
  return 0;
}
/* ---- 三个独立 stub ---- */
#define STUB(N,IMPL,TRAMP) \
__asm__(".text\n.global stub"#N"\n.type stub"#N", %function\n" \
"stub"#N":\n" \
"  stp x29,x30,[sp,#-16]!\n  mov x29,sp\n" \
"  stp x0,x1,[sp,#-16]!\n  stp x2,x3,[sp,#-16]!\n" \
"  stp x4,x5,[sp,#-16]!\n  stp x6,x7,[sp,#-16]!\n" \
"  bl  "IMPL"\n" \
"  ldp x6,x7,[sp],#16\n  ldp x4,x5,[sp],#16\n" \
"  ldp x2,x3,[sp],#16\n  ldp x0,x1,[sp],#16\n" \
"  ldp x29,x30,[sp],#16\n" \
"  adrp x16, :got:"TRAMP"\n  ldr x16,[x16,:got_lo12:"TRAMP"]\n  ldr x16,[x16]\n  br x16\n" \
".size stub"#N", .-stub"#N"\n");
STUB(0, "impl0", "g_trampp0")
STUB(1, "impl1", "g_trampp1")
STUB(2, "impl2", "g_trampp2")

static void* g_stub[3];
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
static int hook_one(int idx, const char* sym, void* stub){
  if(g_hooked[idx]) return 1;
  unsigned long base=find_lib(LIBNAME); if(!base) return 0;
  unsigned long s=find_sym(base,sym); if(!s) return 0;
  if(!g_fnMapMemory){ unsigned long m=find_sym(base,"_ZN11qglinternal11vkMapMemoryEP10VkDevice_TP16VkDeviceMemory_TmmjPPv"); if(m) g_fnMapMemory=(void*)m; }
  if(!is_mapped(s)) return 0;
  g_tgt[idx]=(unsigned char*)s;
  mprotect_rwx(g_tgt[idx],16);
  memcpy(g_saved[idx],g_tgt[idx],16);
  int nz=0; for(int i=0;i<16;i++) if(g_saved[idx][i]) nz++;
  if(nz<8){ lgf("[vktex] %s EMPTY\n",sym); return 0; }
  for(int k=0;k<4;k++){ uint32_t w; memcpy(&w,g_saved[idx]+k*4,4);
    const char*bk=br_kind(w); if(bk){ if((g_calls[idx]++ % 50)==0) lgf("[vktex] %s insn[%d]=%s 不安全(节流)\n",sym,k,bk); return 0; } }
  memcpy(g_tramp[idx],g_saved[idx],16);
  { uint32_t j[4]; j[0]=0x58000050u; j[1]=0xd61f0200u;
    void* cont=(void*)(g_tgt[idx]+16); memcpy(&j[2],&cont,8); memcpy(g_tramp[idx]+16,j,16); }
  mprotect_rwx(g_tramp[idx],sizeof g_tramp[idx]); flush_icache(g_tramp[idx],sizeof g_tramp[idx]);
  if(idx==0) g_trampp0=(void*)g_tramp[idx];
  if(idx==1) g_trampp1=(void*)g_tramp[idx];
  if(idx==2) g_trampp2=(void*)g_tramp[idx];
  mprotect_rwx(g_tgt[idx],16);
  { uint32_t c[4]; c[0]=0x58000050u; c[1]=0xd61f0200u;
    memcpy(&c[2],&stub,8); memcpy(g_tgt[idx],c,16); }
  flush_icache(g_tgt[idx],16);
  g_hooked[idx]=1;
  lgf("[vktex] %s HOOKED @0x%lx\n",sym,s);
  return 1;
}
static void* waiter(void* a){
  (void)a; lgf("[vktex] waiter pid=%d\n",getpid());
  for(long long t=0;t<40;t++){   /* ★★★ 只在启动早期重装 40×200ms=8s，之后【停手】⇒ 消除对高频函数的长期竞态 */
    if(!g_hooked[0]) hook_one(0,SYM_MAP,g_stub[0]);
    if(!g_hooked[1]) hook_one(1,SYM_BIND,g_stub[1]);
    if(!g_hooked[2]) hook_one(2,SYM_COPY,g_stub[2]);
    for(int i=0;i<3;i++) if(g_hooked[i]){
      uint32_t c0; memcpy(&c0,g_tgt[i],4);
      if(c0!=0x58000050u){ g_hooked[i]=0; }
    }
    usleep(200000);
  }
  lg("[vktex] ★ 重装窗口已关闭（不再触碰代码，消除竞态）");
  return 0;
}
static void do_install(void){
  static int done=0; if(done)return; done=1;
  g_stub[0]=(void*)stub0; g_stub[1]=(void*)stub1; g_stub[2]=(void*)stub2;
  g_logfd=open(LOGF,O_WRONLY|O_CREAT|O_APPEND,0644);
  lgf("=== efml4a vktex pid=%d replace=%d ===\n",getpid(),ENABLE_REPLACE);
  pthread_t t; if(pthread_create(&t,0,waiter,0)!=0){ lg("[vktex] pthread fail"); return; }
  pthread_detach(t);
}
__attribute__((used,visibility("default"))) void init(void);
__attribute__((constructor)) static void ctor(void){ do_install(); }
__attribute__((used,visibility("default"))) void init(void){ do_install(); }
