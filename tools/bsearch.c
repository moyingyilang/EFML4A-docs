/* bsearch —— 在文件里搜索二进制模式（纯 C，快速）
 *   用法: bsearch <haystack> <needle_file> [偏移] [最大命中数] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
int main(int argc,char**argv){
 if(argc<3){fprintf(stderr,"用法: bsearch <haystack> <needle_file> [offset] [maxhits]\n");return 2;}
 FILE*nf=fopen(argv[2],"rb"); if(!nf){perror(argv[2]);return 1;}
 fseek(nf,0,SEEK_END); long nn=ftell(nf); fseek(nf,0,SEEK_SET);
 uint8_t*nd=malloc(nn); if(fread(nd,1,nn,nf)!=(size_t)nn){fclose(nf);return 1;} fclose(nf);
 long start=argc>3?strtol(argv[3],0,0):0;
 int maxh=argc>4?atoi(argv[4]):20;
 FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
 fseek(f,0,SEEK_END); long n=ftell(f);
 fseek(f,start,SEEK_SET);   /* ★ 必须无条件回到 start（原 bug: start=0 时停在末尾） */
 /* 分块读，处理跨块匹配 */
 size_t CH=1<<22; uint8_t*buf=malloc(CH+nn);
 long pos=start; size_t keep=0; int hits=0;
 while(1){
   size_t got=fread(buf+keep,1,CH,f);
   if(keep==0 && got==0){ fprintf(stderr,"  [dbg] 首次 fread 返回 0, feof=%d ferror=%d errno=%d\n",feof(f),ferror(f),errno); }
   if(got==0) break;
   size_t tot=keep+got;
   for(size_t i=0;i+ (size_t)nn<=tot;i++){
     if(!memcmp(buf+i,nd,nn)){
       printf("  @%ld\n", pos-(long)keep+(long)i);
       if(++hits>=maxh){ printf("  ...(达上限)\n"); goto done; }
     }
   }
   keep=nn-1; if(tot<keep) keep=tot;
   memmove(buf,buf+tot-keep,keep);
   pos+=got;
 }
 done:
 if(!hits) printf("  （未找到）\n");
 fprintf(stderr,"  ⇒ %d 处命中, 扫描 %ld 字节\n",hits,pos-start);
 free(buf); free(nd); fclose(f); return 0;
}
