/* mkpattern —— 生成鲜明的测试图案（棋盘 + 对角渐变 + 中心十字）
 *   用法: mkpattern <out.tga> <w> <h>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int main(int c,char**v){
    int w=atoi(v[2]),h=atoi(v[3]);
    FILE*f=fopen(v[1],"wb");
    unsigned char hdr[18]; memset(hdr,0,18);
    hdr[2]=2;                                  /* uncompressed true-color */
    hdr[12]=w&0xFF; hdr[13]=(w>>8)&0xFF;
    hdr[14]=h&0xFF; hdr[15]=(h>>8)&0xFF;
    hdr[16]=24;
    fwrite(hdr,1,18,f);
    unsigned char*row=malloc((size_t)w*3);
    for(int y=0;y<h;y++){
        for(int x=0;x<w;x++){
            int bx=(x/32)&1, by=(y/32)&1;
            unsigned char R,G,B;
            if(bx^by){ R=255; G=0; B=0; }        /* 红 */
            else     { R=0; G=0; B=255; }        /* 蓝 */
            /* 对角渐变叠加（让纹理有明显方向性） */
            int g=(x*255)/w;
            R=(unsigned char)((R*(255-g)+255*g)/255);
            G=(unsigned char)((G*(255-g)+255*g)/255);
            B=(unsigned char)((B*(255-g)+0*g)/255);
            /* 中心十字（白色，便于定位） */
            int cx=w/2, cy=h/2;
            if((abs(x-cx)<4 && abs(y-cy)<h/4) || (abs(y-cy)<4 && abs(x-cx)<w/4)){
                R=G=B=255;
            }
            row[x*3+0]=B;  /* TGA 是 BGR */
            row[x*3+1]=G;
            row[x*3+2]=R;
        }
        fwrite(row,1,(size_t)w*3,f);
    }
    fclose(f); free(row);
    printf("  写出 %s (%dx%d 棋盘+渐变+十字)\n",v[1],w,h);
    return 0;
}
