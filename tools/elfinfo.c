/* elfinfo —— 读 ELF 机器类型（替代脚本里的 python struct 用法）
 *   用法: elfinfo <file>...
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static const char* mach_name(uint16_t m){
    switch(m){
        case 0x03: return "x86";
        case 0x3E: return "x86-64";
        case 0x28: return "ARM";
        case 0xB7: return "AArch64";
        case 0xF3: return "RISC-V";
        case 0x08: return "MIPS";
        case 0x14: return "PowerPC";
        default:   return 0;
    }
}
int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"用法: elfinfo <file>...\n"); return 2; }
    for(int i=1;i<argc;i++){
        FILE*f=fopen(argv[i],"rb");
        if(!f){ printf("  %s: 打不开\n",argv[i]); continue; }
        uint8_t h[20]; size_t n=fread(h,1,20,f); fclose(f);
        const char* base=strrchr(argv[i],'/'); base=base?base+1:argv[i];
        if(n<20 || memcmp(h,"\x7f""ELF",4)){
            printf("  %-40s : 非 ELF (%zu 字节)\n",base,n);
            continue;
        }
        uint16_t m = h[18] | (h[19]<<8);
        const char* nm = mach_name(m);
        char buf[32];
        if(!nm){ snprintf(buf,sizeof buf,"0x%x",m); nm=buf; }
        printf("  %-40s : %s  (class=%s endian=%s)\n", base, nm,
               h[4]==2?"64":"32", h[5]==1?"LE":"BE");
    }
    return 0;
}
