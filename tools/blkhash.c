/* blkhash —— 对文件的每个固定大小块算指纹（用于 diff 找变化的块）
 *   用法: blkhash <file> [blk=262144]
 *   输出: "块号 偏移 指纹"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"用法: blkhash <file> [blk]\n"); return 2; }
    long blk = argc>2? strtol(argv[2],0,0) : 262144;
    FILE*f=fopen(argv[1],"rb"); if(!f){ perror(argv[1]); return 1; }
    uint8_t* b=malloc(blk);
    long off=0; int idx=0;
    while(1){
        size_t n=fread(b,1,blk,f);
        if(n==0) break;
        uint64_t h=1469598103934665603ULL;
        for(size_t i=0;i<n;i++){ h^=b[i]; h*=1099511628211ULL; }
        printf("%d %ld %016llx\n", idx, off, (unsigned long long)h);
        off+=blk; idx++;
        if(n<blk) break;
    }
    free(b); fclose(f);
    return 0;
}
