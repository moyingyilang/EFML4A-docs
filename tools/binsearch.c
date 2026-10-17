/* binsearch —— 在大文件里搜二进制模式
 *   用法: binsearch <file> <hexpattern> [maxHits]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int main(int c,char**v){
    if(c<3){fprintf(stderr,"用法: binsearch <file> <hexpattern> [maxHits]\n");return 2;}
    size_t plen=strlen(v[2])/2;
    if(plen==0){fprintf(stderr,"模式非法\n");return 2;}
    uint8_t*pat=malloc(plen);
    for(size_t i=0;i<plen;i++){unsigned x;if(sscanf(v[2]+i*2,"%2x",&x)!=1)return 2;pat[i]=(uint8_t)x;}
    int maxhits=(c>=4)?atoi(v[3]):8;
    FILE*f=fopen(v[1],"rb");
    if(!f){perror(v[1]);return 1;}
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    const size_t CH=1<<20;
    uint8_t*buf=malloc(CH+plen);
    long off=0,hits=0;
    size_t keep=0;
    while(off<n && hits<maxhits){
        size_t want=CH;
        if(off+(long)want>n) want=(size_t)(n-off);
        if(fread(buf+keep,1,want,f)!=want) break;
        size_t total=keep+want;
        for(size_t i=0;i+plen<=total;i++){
            if(memcmp(buf+i,pat,plen)==0){
                printf("  HIT @ 0x%lx (%ld)\n",(long)(off-(long)keep+(long)i),(long)(off-(long)keep+(long)i));
                if(++hits>=maxhits) break;
            }
        }
        keep=plen-1;
        if(total>=keep) memmove(buf,buf+total-keep,keep);
        off+=want;
    }
    printf("  搜索完成: 命中 %d 处 (文件 %ld 字节)\n",hits,n);
    free(buf);free(pat);fclose(f);
    return hits?0:1;
}
