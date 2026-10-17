#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
int main(int c,char**v){
    FILE*f=fopen(v[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t*b=malloc(n);if(fread(b,1,n,f)!=(size_t)n)return 1;fclose(f);
    long p=8;uint32_t W=0,H=0;uint8_t ct=0;uint8_t*idat=NULL;size_t idlen=0;
    while(p+8<n){uint32_t len=(b[p]<<24)|(b[p+1]<<16)|(b[p+2]<<8)|b[p+3];
        char t[5]={b[p+4],b[p+5],b[p+6],b[p+7],0};uint8_t*d=b+p+8;
        if(!strcmp(t,"IHDR")){W=(d[0]<<24)|(d[1]<<16)|(d[2]<<8)|d[3];H=(d[4]<<24)|(d[5]<<16)|(d[6]<<8)|d[7];ct=d[9];}
        else if(!strcmp(t,"IDAT")){idat=realloc(idat,idlen+len);memcpy(idat+idlen,d,len);idlen+=len;}
        else if(!strcmp(t,"IEND"))break;p+=12+len;}
    int ch=(ct==6)?4:(ct==2)?3:1;
    size_t rawLen=(size_t)H*(1+(size_t)W*ch);uint8_t*raw=malloc(rawLen);uLongf dl=rawLen;
    if(uncompress(raw,&dl,idat,idlen)!=Z_OK)return 1;
    uint8_t*img=malloc((size_t)W*ch*(size_t)H);size_t stride=(size_t)W*ch;
    for(uint32_t y=0;y<H;y++){uint8_t ft=raw[(size_t)y*(stride+1)];uint8_t*cur=raw+(size_t)y*(stride+1)+1;
        uint8_t*out=img+(size_t)y*stride;uint8_t*prev=(y>0)?img+(size_t)(y-1)*stride:NULL;
        for(size_t x=0;x<stride;x++){uint8_t a=(x>=(size_t)ch)?out[x-ch]:0;uint8_t bb=prev?prev[x]:0;
            uint8_t cc=(prev&&x>=(size_t)ch)?prev[x-ch]:0;int val;
            switch(ft){case 0:val=cur[x];break;case 1:val=cur[x]+a;break;case 2:val=cur[x]+bb;break;
                case 3:val=cur[x]+((a+bb)>>1);break;
                case 4:{int pp=a+bb-cc;int pa=abs(pp-a),pb=abs(pp-bb),pc=abs(pp-cc);
                    int pr=(pa<=pb&&pa<=pc)?a:(pb<=pc?bb:cc);val=cur[x]+pr;}break;default:val=cur[x];}
            out[x]=(uint8_t)val;}}
    long minx=W,maxx=-1,miny=H,maxy=-1,cnt=0;
    for(uint32_t y=0;y<H;y++)for(uint32_t x=0;x<W;x++){
        size_t o=(size_t)y*stride+(size_t)x*ch;int R=img[o],G=img[o+1],B=img[o+2];
        if(R>200&&G<60&&B<60){cnt++;
            if((long)x<minx)minx=x;if((long)x>maxx)maxx=x;if((long)y<miny)miny=y;if((long)y>maxy)maxy=y;}}
    printf("  纯红 %ld  范围 x[%ld..%ld] y[%ld..%ld]  尺寸 %ldx%ld\n",
        cnt,minx,maxx,miny,maxy,maxx-minx+1,maxy-miny+1);
    return 0;
}
