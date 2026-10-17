/* prologue —— 检查一批 RVA 的函数序言是否适合 inline hook
 *   用法: prologue <elf> <rva_hex> [rva_hex ...]
 *   输出: 每条指令 + 是否分支 + trampoline 安全性判定
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static const char* br_kind(uint32_t w){
    uint32_t o26=w>>26,o24=w>>24;
    if(o26==0x05)return "B"; if(o26==0x25)return "BL";
    if(o24==0x54)return "B.cond";
    if(o24==0x34||o24==0x35||o24==0xb4||o24==0xb5)return "CBZ/CBNZ";
    if(o24==0x36||o24==0x37||o24==0xb6||o24==0xb7)return "TBZ/TBNZ";
    return 0;
}
int main(int argc,char**argv){
    if(argc<3){fprintf(stderr,"用法: prologue <elf> <rva_hex> [rva_hex ...]\n");return 2;}
    FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f);
    for(int a=2;a<argc;a++){
        unsigned long rva=strtoul(argv[a],0,16);
        if((long)rva+16>n){ printf("  RVA 0x%lx: 超出文件\n",rva); continue; }
        fseek(f,rva,SEEK_SET);
        uint32_t w[4]; if(fread(w,4,4,f)!=4){ printf("  RVA 0x%lx: 读失败\n",rva); continue; }
        int nz=0; for(int k=0;k<16;k++) if(((uint8_t*)w)[k]) nz++;
        printf("  ── RVA 0x%lx (nz=%d/16) ──\n",rva,nz);
        int safe=1;
        for(int k=0;k<4;k++){
            const char*bk=br_kind(w[k]);
            printf("    [%d] 0x%08x  %s\n",k,w[k],bk?bk:"ok");
            if(bk) safe=0;
        }
        printf("    ⇒ %s\n", safe? "★ 安全（可 hook）" : "✗ 含分支（trampoline 不安全）");
    }
    fclose(f);
    return 0;
}
