/* EFML4A gleshook：hook vkCmdCopyBufferToImage（BC7 纹理上传必经）
 *   签名: void vkCmdCopyBufferToImage(GLenum target, GLint level, GLenum internalformat,
 *                                    GLsizei width, GLsizei height, GLint border,
 *                                    GLsizei imageSize, const void *data)
 *   ARM64 寄存器: w0=target w1=level w2=internalformat w3=width w4=height
 *                 w5=border w6=imageSize x7=data
 *   ⚠️ 先只读探测（记录尺寸），确认命中再替换
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
#define SYMNAME "_ZN11qglinternal22vkCmdCopyBufferToImageEP17VkCommandBuffer_TP10VkBuffer_TP9VkImage_T13VkImageLayoutjPK17VkBufferImageCopy"
#define LOGF "/sdcard/Android/data/com.hypergryph.endfield/files/efml4a_vk.log"

static unsigned char  g_saved[16];
static unsigned char* g_target=0;
static int g_logfd=-1;
static volatile long long g_calls=0;
static int g_hooked=0;
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
static int is_mapped(unsigned long addr){
  unsigned long pg=(unsigned long)sysconf(_SC_PAGESIZE);
  unsigned long p=addr & ~(pg-1);
  unsigned char vec=0;
  if(mincore((void*)p,pg,&vec)!=0) return 0;
  return 1;
}
static const char* br_kind(uint32_t w){
  uint32_t o26=w>>26,o24=w>>24;
  if(o26==0x05)return "B"; if(o26==0x25)return "BL";
  if(o24==0x54)return "B.cond";
  if(o24==0x34||o24==0x35||o24==0xb4||o24==0xb5)return "CBZ";
  if(o24==0x36||o24==0x37||o24==0xb6||o24==0xb7)return "TBZ";
  if((w&0xfffffbff)==0xd503233f)return "PAC(paciasp)";   /* ★ PAC 序言：改 SP 会破坏签名验证 */
  if(w==0xd503237f)return "PAC(pacibsp)";
  if(w==0xd503245f)return "BTI";
  return 0;
}
extern void hook_entry(void);
static unsigned char g_tramp[64] __attribute__((aligned(16)));
void* g_tramp_ptr=0;

/* vkCmdCopyBufferToImage(cmd, srcBuffer, dstImage, dstLayout, regionCount, pRegions) */
__attribute__((used,visibility("default"))) long hook_impl(long a0,long a1,long a2,
                                                           long a3,long a4,long a5,
                                                           long a6,long a7){
  long long n=++g_calls;
  int rc=(int)a4;
  const unsigned char* pr=(const unsigned char*)a5;
  if(n<=40 && pr && rc>0 && rc<64){
    /* VkBufferImageCopy: bufferOffset(8) bufferRowLength(4) bufferImageHeight(4)
       imageSubresource(8) imageOffset(12) imageExtent(12) */
      unsigned int bw = *(const unsigned int*)(pr+8);
      unsigned int bh = *(const unsigned int*)(pr+12);
      unsigned int ew = *(const unsigned int*)(pr+44);
      unsigned int eh = *(const unsigned int*)(pr+48);
      unsigned int ed = *(const unsigned int*)(pr+52);
      unsigned long long bo = *(const unsigned long long*)(pr+0);
      lgf("[vk] CopyBufferToImage #%lld cmd=%p buf=%p img=%p regions=%d"
          " bufRowLen=%u bufImgH=%u ext=%ux%ux%u bufOffset=%llu\n",
          n,(void*)a0,(void*)a1,(void*)a2,rc,bw,bh,ew,eh,ed,bo);
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

/* 在 /proc/self/maps 里找库基址 */
static unsigned long find_lib(const char* name){
  FILE*f=fopen("/proc/self/maps","r"); if(!f) return 0;
  char line[512]; unsigned long base=0;
  while(fgets(line,sizeof line,f)){
    if(!strstr(line,name)) continue;
    unsigned long a,b,off; char perms[8];
    if(sscanf(line,"%lx-%lx %7s %lx",&a,&b,perms,&off)!=4) continue;
    if(perms[2]=='x'){ base=a-off; break; }   /* 可执行段反推基址 */
  }
  fclose(f); return base;
}
/* 在 ELF 的 .dynsym 里找符号值（stripped 也能用） */
static unsigned long find_sym(unsigned long base,const char* name){
  FILE*f=fopen("/proc/self/maps","r"); if(!f) return 0;
  char line[512]; char path[256]={0}; unsigned long foff=0;
  while(fgets(line,sizeof line,f)){
    if(strstr(line,LIBNAME)){ 
      unsigned long a,b,off; char perms[8];
      if(sscanf(line,"%lx-%lx %7s %lx",&a,&b,perms,&off)==4 && perms[0]=='r'){
        char*p=strchr(line,'/'); if(p){ sscanf(p,"%255s",path); foff=off; break; }
      }
    }
  }
  fclose(f);
  if(!path[0]) return 0;
  FILE*g=fopen(path,"rb"); if(!g) return 0;
  /* 读 ELF 头 -> 节头 -> 找 .dynsym/.dynstr（简化：直接扫文件里的符号名） */
  fseek(g,0,SEEK_END); long n=ftell(g); fseek(g,0,SEEK_SET);
  uint8_t* d=malloc(n); if(fread(d,1,n,g)!=(size_t)n){ fclose(g); free(d); return 0; } fclose(g);
  unsigned long shoff=*(uint64_t*)(d+0x28);
  uint16_t shentsize=*(uint16_t*)(d+0x3a), shnum=*(uint16_t*)(d+0x3c);
  uint16_t shstrndx=*(uint16_t*)(d+0x3e);
  unsigned long dynsym=0,dynstr=0,dynsymsz=0;
  const char*shstr=(const char*)(d+*(uint64_t*)(d+shoff+shstrndx*shentsize+0x18));
  for(int i=0;i<shnum;i++){
    uint8_t*sh=d+shoff+i*shentsize;
    uint32_t tn=*(uint32_t*)(sh+0);
    const char*nm=shstr+tn;
    if(!strcmp(nm,".dynsym")){ dynsym=*(uint64_t*)(sh+0x18); dynsymsz=*(uint64_t*)(sh+0x20); }
    else if(!strcmp(nm,".dynstr")) dynstr=*(uint64_t*)(sh+0x18);
  }
  unsigned long res=0;
  if(dynsym&&dynstr){
    for(unsigned long off=0; off<dynsymsz; off+=24){
      uint32_t nmoff=*(uint32_t*)(d+dynsym+off);
      if(!nmoff) continue;
      if(!strcmp((const char*)(d+dynstr+nmoff),name)){
        res=*(uint64_t*)(d+dynsym+off+8);
        break;
      }
    }
  }
  free(d);
  return res? base+res : 0;
}
static int install_once(void){
  if(g_hooked) return 1;
  unsigned long base=find_lib(LIBNAME);
  if(!base){ lgf("[vk] %s 基址未找到\n",LIBNAME); return 0; }
  unsigned long sym=find_sym(base,SYMNAME);
  if(!sym){ lgf("[vk] %s 符号未找到\n",SYMNAME); return 0; }
  g_target=(unsigned char*)sym;
  lgf("[vk] base=0x%lx %s=0x%lx\n",base,SYMNAME,sym);
  if(!is_mapped(sym)){ lg("[vk] 目标未映射\n"); return 0; }
  mprotect_rwx(g_target,16);
  memcpy(g_saved,g_target,16);
  { int nz=0; for(int i=0;i<16;i++) if(g_saved[i]) nz++;
    lgf("[vk] bytes %02x%02x%02x%02x nz=%d\n",g_saved[0],g_saved[1],g_saved[2],g_saved[3],nz);
    if(nz<8){ lg("[vk] EMPTY\n"); return 0; }
    for(int k=0;k<4;k++){ uint32_t w; memcpy(&w,g_saved+k*4,4);
      const char*bk=br_kind(w); if(bk){ lgf("[vk] insn[%d] %s 不安全\n",k,bk); return 0; } } }
  memcpy(g_tramp,g_saved,16);
  { uint32_t j[4]; j[0]=0x58000050u; j[1]=0xd61f0200u;
    void* cont=(void*)(g_target+16); memcpy(&j[2],&cont,8); memcpy(g_tramp+16,j,16); }
  mprotect_rwx(g_tramp,sizeof g_tramp); flush_icache(g_tramp,sizeof g_tramp);
  g_tramp_ptr=(void*)g_tramp;
  mprotect_rwx(g_target,16);
  { uint32_t c[4]; c[0]=0x58000050u; c[1]=0xd61f0200u;
    void* d=(void*)hook_entry; memcpy(&c[2],&d,8); memcpy(g_target,c,16); }
  flush_icache(g_target,16);
  g_hooked=1;
  lg("[vk] vkCmdCopyBufferToImage HOOKED");
  return 1;
}
static void* waiter(void* a){
  (void)a; lgf("[vk] waiter pid=%d tid=%d\n",getpid(),gettid_());
  for(long long tick=0; tick<9000; tick++){
    if(!g_hooked){ install_once(); }
    else {
      uint32_t cur0; memcpy(&cur0,g_target,4);
      if(cur0!=0x58000050u){
        g_hooked=0; install_once();
        lgf("[vk] 检测到还原，已重装 #%lld\n",tick);
      }
    }
    usleep(200000);
  }
  return 0;
}
static void do_install(void){
  static int done=0; if(done)return; done=1;
  g_logfd=open(LOGF,O_WRONLY|O_CREAT|O_APPEND,0644);
  lgf("=== efml4a vkhook pid=%d tid=%d ===\n",getpid(),gettid_());
  pthread_t t; if(pthread_create(&t,0,waiter,0)!=0){ lg("[vk] pthread fail"); return; }
  pthread_detach(t);
}
__attribute__((used,visibility("default"))) void init(void);
__attribute__((constructor)) static void ctor(void){ do_install(); }
__attribute__((used,visibility("default"))) void init(void){ do_install(); }
