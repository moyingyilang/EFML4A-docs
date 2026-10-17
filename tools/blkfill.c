/* blkfill —— 把文件的指定区间按块替换成给定数据（等长，原地覆盖）
 *   用法: blkfill <file> <blk> <start> <end> <datafile>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int main(int argc,char**argv){
    if(argc<6){ fprintf(stderr,"用法: blkfill <file> <blk> <start> <end> <datafile>\n"); return 2; }
    const char* path=argv[1];
    long blk=strtol(argv[2],0,0), start=strtol(argv[3],0,0), end=strtol(argv[4],0,0);
    FILE*d=fopen(argv[5],"rb"); if(!d){perror(argv[5]);return 1;}
    fseek(d,0,SEEK_END); long dn=ftell(d); fseek(d,0,SEEK_SET);
    uint8_t* db=malloc(dn);
    if(fread(db,1,dn,d)!=(size_t)dn){ fclose(d); return 1; }
    fclose(d);
    if(dn>blk) dn=blk;
    FILE*f=fopen(path,"r+b"); if(!f){perror(path);return 1;}
    fseek(f,0,SEEK_END); long fn=ftell(f);
    if(end>fn) end=fn;
    long cnt=0;
    for(long off=start; off+blk<=end; off+=blk){
        fseek(f,off,SEEK_SET);
        if(fwrite(db,1,dn,f)!=(size_t)dn){ perror("write"); break; }
        cnt++;
    }
    fflush(f); fclose(f); free(db);
    printf("  ⇒ 替换 %ld 块（每块 %ld 字节，数据 %ld 字节），区间 [%ld, %ld)\n",cnt,blk,dn,start,end);
    return 0;
}
