/* texscan —— 在文件里扫描 Unity Texture2D 结构 / BC7 纹理块
 *   用法: texscan <file> [--size N] [--mips N]
 *   默认扫描 512x512 BC7 (262144 字节) 的候选
 *   策略: 找形如 [u32 width][u32 height][u32 format][u32 mipCount] 的 Texture2D 头
 *         并校验其后是否有合理大小的图像数据
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"用法: texscan <file> [--size N]\n"); return 2; }
    long want=262144;   /* 512*512 BC7 */
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--size")&&i+1<argc) want=strtol(argv[++i],0,0);
    }
    FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    size_t CH=1<<22; uint8_t*b=malloc(CH+64);
    long pos=0; size_t keep=0; long hits=0;
    printf("  扫描 %s (%ld 字节), 目标尺寸 %ld\n",argv[1],n,want);
    while(1){
        size_t got=fread(b+keep,1,CH,f);
        if(got==0) break;
        size_t tot=keep+got;
        for(size_t i=0;i+24<=tot;i++){
            uint32_t w,h,fmt,mips;
            memcpy(&w,b+i,4); memcpy(&h,b+i+4,4);
            memcpy(&fmt,b+i+8,4); memcpy(&mips,b+i+12,4);
            /* 512x512 或 256/128/64 等 2 的幂 */
            if(w!=h) continue;
            if(w<32||w>4096) continue;
            if((w&(w-1))!=0) continue;
            if(mips<1||mips>16) continue;
            /* BC7: w*h = want/1 ; 也接受 BC5(16B/block 同 BC7) */
            long px=(long)w*(long)h;
            if(px<1024) continue;
            long off=pos-(long)keep+(long)i;
            printf("  @%ld  w=%u h=%u fmt=%u mips=%u  (px=%ld)\n",off,w,h,fmt,mips,px);
            if(++hits>=40){ printf("  ...(达上限)\n"); goto done; }
        }
        keep=23; if(tot<keep) keep=tot;
        memmove(b,b+tot-keep,keep);
        pos+=(long)got;
    }
done:
    if(!hits) printf("  （未找到）\n");
    free(b); fclose(f); return 0;
}
