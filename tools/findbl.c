/* findbl —— 在 ELF 的代码段里找所有 bl 到指定地址的调用点（ARM64）
 * 用法: findbl <elf> <target_hex> [起] [止]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
int main(int c,char**v){
    if(c<3){ fprintf(stderr,"用法: findbl <elf> <target_hex> [起] [止]\n"); return 2; }
    const char*p=v[1];
    unsigned long long target=strtoull(v[2],0,16);
    unsigned long long lo=c>3?strtoull(v[3],0,16):0x9534000ULL;
    unsigned long long hi=c>4?strtoull(v[4],0,16):0x1ef58000ULL;
    FILE*f=fopen(p,"rb"); if(!f){perror(p);return 1;}
    fseek(f,0,SEEK_END); long long n=ftell(f);
    if(hi>n) hi=n;
    unsigned char*buf=malloc(1<<20);
    printf("  target=0x%llx  扫描 [0x%llx, 0x%llx)\n",target,lo,hi);
    int hits=0;
    for(unsigned long long base=lo; base<hi; base+=(1<<20)){
        size_t want=(size_t)((hi-base<(1<<20))?(hi-base):(1<<20));
        fseek(f,(long)base,SEEK_SET);
        size_t got=fread(buf,1,want,f);
        for(size_t i=0;i+4<=got;i+=4){
            uint32_t w; memcpy(&w,buf+i,4);
            if((w>>26)!=0x25) continue;            /* bl 的 opcode 高 6 位 = 100101 */
            int32_t imm=(int32_t)(w & 0x03FFFFFF);
            if(imm & 0x02000000) imm |= 0xFC000000; /* 符号扩展 */
            long long pc=(long long)(base+i);
            long long tgt=pc + ((long long)imm<<2);
            if((unsigned long long)tgt==target){
                printf("  ★ 调用点 @0x%llx\n",(unsigned long long)pc);
                if(++hits>=50) break;
            }
        }
        if(hits>=50) break;
    }
    printf("  ⇒ 共 %d 个调用点%s\n",hits,hits>=50?" (截断)":"");
    free(buf); fclose(f); return 0;
}
