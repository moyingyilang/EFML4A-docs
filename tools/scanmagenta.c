/* scanmagenta —— 全图扫描品红像素（R>180, G<80, B>180） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
int main(int c,char**v){
    FILE*f=fopen(v[1],"rb"); if(!f){perror(v[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*b=malloc(n); if(fread(b,1,n,f)!=(size_t)n)return 1; fclose(f);
    if(n<8||memcmp(b,"\x89PNG\r\n\x1a\n",8)){fprintf(stderr,"非 PNG\n");return 1;}
    long p=8; uint32_t W=0,H=0; uint8_t bd=0,ct=0;
    uint8_t*idat=NULL; size_t idlen=0;
    while(p+8<n){
        uint32_t len=(b[p]<<24)|(b[p+1]<<16)|(b[p+2]<<8)|b[p+3];
        char type[5]={b[p+4],b[p+5],b[p+6],b[p+7],0};
        uint8_t*data=b+p+8;
        if(!strcmp(type,"IHDR")){ W=(data[0]<<24)|(data[1]<<16)|(data[2]<<8)|data[3];
            H=(data[4]<<24)|(data[5]<<16)|(data[6]<<8)|data[7]; bd=data[8]; ct=data[9]; }
        else if(!strcmp(type,"IDAT")){ idat=realloc(idat,idlen+len); memcpy(idat+idlen,data,len); idlen+=len; }
        else if(!strcmp(type,"IEND")) break;
        p+=12+len;
    }
    int ch=(ct==6)?4:(ct==2)?3:1;
    size_t rawLen=(size_t)H*(1+(size_t)W*ch);
    uint8_t*raw=malloc(rawLen); uLongf dl=rawLen;
    if(uncompress(raw,&dl,idat,idlen)!=Z_OK){fprintf(stderr,"解压失败\n");return 1;}
    uint8_t*img=malloc((size_t)W*ch*(size_t)H);
    size_t stride=(size_t)W*ch;
    for(uint32_t y=0;y<H;y++){
        uint8_t ft=raw[(size_t)y*(stride+1)];
        uint8_t*cur=raw+(size_t)y*(stride+1)+1;
        uint8_t*out=img+(size_t)y*stride;
        uint8_t*prev=(y>0)?img+(size_t)(y-1)*stride:NULL;
        for(size_t x=0;x<stride;x++){
            uint8_t a=(x>=(size_t)ch)?out[x-ch]:0;
            uint8_t bb=prev?prev[x]:0;
            uint8_t cc=(prev&&x>=(size_t)ch)?prev[x-ch]:0;
            int val;
            switch(ft){
                case 0: val=cur[x]; break;
                case 1: val=cur[x]+a; break;
                case 2: val=cur[x]+bb; break;
                case 3: val=cur[x]+((a+bb)>>1); break;
                case 4: { int pp=a+bb-cc; int pa=abs(pp-a),pb=abs(pp-bb),pc=abs(pp-cc);
                          int pr=(pa<=pb&&pa<=pc)?a:(pb<=pc?bb:cc); val=cur[x]+pr; } break;
                default: val=cur[x];
            }
            out[x]=(uint8_t)val;
        }
    }
    /* 全图扫描品红 */
    long mag=0; long first_x=-1,first_y=-1;
    long minx=W,maxx=-1,miny=H,maxy=-1;
    for(uint32_t y=0;y<H;y++){
        for(uint32_t x=0;x<W;x++){
            size_t o=(size_t)y*stride+(size_t)x*ch;
            uint8_t R=img[o],G=img[o+1],B=img[o+2];
            if(R>180 && G<80 && B>180){
                mag++;
                if(first_x<0){first_x=x;first_y=y;}
                if((long)x<minx)minx=x; if((long)x>maxx)maxx=x;
                if((long)y<miny)miny=y; if((long)y>maxy)maxy=y;
            }
        }
    }
    printf("  PNG %ux%u  总像素=%ld\n",W,H,(long)W*H);
    printf("  品红像素: %ld (%.4f%%)",mag,100.0*mag/((double)W*H));
    if(mag>0) printf("  首处(%ld,%ld)  范围 x[%ld..%ld] y[%ld..%ld]",first_x,first_y,minx,maxx,miny,maxy);
    printf("\n");
    if(mag>1000) printf("  ★★★ 品红可见！\n");
    return mag>1000?0:1;
}
