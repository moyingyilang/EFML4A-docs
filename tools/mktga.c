/* mktga —— 生成纯色 TGA（未压缩 24bit） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int c,char**v){
    int w=atoi(v[2]),h=atoi(v[3]);
    unsigned r=strtoul(v[4],0,16),g=strtoul(v[5],0,16),b=strtoul(v[6],0,16);
    FILE*f=fopen(v[1],"wb");
    unsigned char hdr[18]; memset(hdr,0,18);
    hdr[2]=2;                       /* uncompressed true-color */
    hdr[12]=w&0xFF; hdr[13]=(w>>8)&0xFF;
    hdr[14]=h&0xFF; hdr[15]=(h>>8)&0xFF;
    hdr[16]=24; hdr[17]=0;          /* 24bpp, top-left origin */
    fwrite(hdr,1,18,f);
    unsigned char*row=malloc(w*3);
    for(int x=0;x<w;x++){row[x*3]=b;row[x*3+1]=g;row[x*3+2]=r;}  /* TGA 是 BGR */
    for(int y=0;y<h;y++) fwrite(row,1,w*3,f);
    fclose(f); free(row);
    printf("  写出 %s (%dx%d BGR %02X%02X%02X)\n",v[1],w,h,r,g,b);
    return 0;
}
