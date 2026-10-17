/* strtab —— 解析 [u32 len][bytes] 字符串表，输出条目及其上下文
 *   用法: strtab <file> [偏移] [长度] [过滤正则(子串)]
 *   输出: 偏移 索引 字符串
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"用法: strtab <file> [off] [len] [filter]\n"); return 2; }
    long off = argc>2? strtol(argv[2],0,0):0;
    long len = argc>3? strtol(argv[3],0,0):0;
    const char* filt = argc>4? argv[4]:0;
    FILE*f=fopen(argv[1],"rb"); if(!f){ perror(argv[1]); return 1; }
    fseek(f,0,SEEK_END); long n=ftell(f);
    if(len<=0 || off+len>n) len=n-off;
    if(off<0){ off=0; len=n; }
    fseek(f,off,SEEK_SET);
    uint8_t* b=malloc(len);
    if(fread(b,1,len,f)!=(size_t)len){ fprintf(stderr,"读失败\n"); return 1; }
    fclose(f);
    long p=0; int idx=0; int cnt=0;
    char prev[512]="";
    while(p+4<=len){
        uint32_t l; memcpy(&l,b+p,4);
        if(l==0 || l>4096 || p+4+l>len){ p++; continue; }
        /* 校验全可打印 */
        int ok=1;
        for(uint32_t i=0;i<l;i++){ uint8_t c=b[p+4+i]; if(c<32||c>126){ ok=0; break; } }
        if(!ok){ p++; continue; }
        char s[4096]; memcpy(s,b+p+4,l); s[l]=0;
        if(!filt || strstr(s,filt)){
            printf("%-10ld %-5d %s\n", off+p, idx, s);
            if(strlen(prev)) printf("%-10s %-5s   (前一条: %s)\n","","",prev);
            cnt++;
        }
        if(l<512) strncpy(prev,s,sizeof prev-1);
        p += 4 + l; idx++;
    }
    fprintf(stderr,"  ⇒ 解析 %d 条字符串（%ld 条匹配）\n",idx,cnt);
    free(b);
    return 0;
}
