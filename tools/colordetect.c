/* colordetect —— 检测 5 种 mod 纯色 + 输出主要饱和色 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
int main(int c,char**v){
    FILE*f=fopen(v[1],"rb"); if(!f){perror(v[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*b=malloc(n); if(fread(b,1,n,f)!=(size_t)n)return 1; fclose(f);
    long p=8; uint32_t W=0,H=0; uint8_t ct=0;
    uint8_t*idat=NULL; size_t idlen=0;
    while(p+8<n){
        uint32_t len=(b[p]<<24)|(b[p+1]<<16)|(b[p+2]<<8)|b[p+3];
        char type[5]={b[p+4],b[p+5],b[p+6],b[p+7],0};
        uint8_t*data=b+p+8;
        if(!strcmp(type,"IHDR")){ W=(data[0]<<24)|(data[1]<<16)|(data[2]<<8)|data[3];
            H=(data[4]<<24)|(data[5]<<16)|(data[6]<<8)|data[7]; ct=data[9]; }
        else if(!strcmp(type,"IDAT")){ idat=realloc(idat,idlen+len); memcpy(idat+idlen,data,len); idlen+=len; }
        else if(!strcmp(type,"IEND")) break;
        p+=12+len;
    }
    int ch=(ct==6)?4:(ct==2)?3:1;
    size_t rawLen=(size_t)H*(1+(size_t)W*ch);
    uint8_t*raw=malloc(rawLen); uLongf dl=rawLen;
    if(uncompress(raw,&dl,idat,idlen)!=Z_OK)return 1;
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
                case 0: val=cur[x]; break; case 1: val=cur[x]+a; break;
                case 2: val=cur[x]+bb; break; case 3: val=cur[x]+((a+bb)>>1); break;
                case 4: { int pp=a+bb-cc; int pa=abs(pp-a),pb=abs(pp-bb),pc=abs(pp-cc);
                          int pr=(pa<=pb&&pa<=pc)?a:(pb<=pc?bb:cc); val=cur[x]+pr; } break;
                default: val=cur[x];
            }
            out[x]=(uint8_t)val;
        }
    }
    /* 检测 5 种纯色（容差 ±40） */
    const char*names[]={"品红(FF00FF)","绿(00FF00)","黄(FFFF00)","青(00FFFF)","橙(FF8000)"};
    int tr[]={255,0,255, 0,255,0, 255,255,0, 0,255,255, 255,128,0};
    long cnt[5]={0};
    for(uint32_t y=0;y<H;y++) for(uint32_t x=0;x<W;x++){
        size_t o=(size_t)y*stride+(size_t)x*ch;
        int R=img[o],G=img[o+1],B=img[o+2];
        for(int k=0;k<5;k++){
            if(abs(R-tr[k*3])<=40 && abs(G-tr[k*3+1])<=40 && abs(B-tr[k*3+2])<=40){ cnt[k]++; break; }
        }
    }
    printf("  %ux%u:",W,H);
    int any=0;
    for(int k=0;k<5;k++) if(cnt[k]>500){ printf(" %s=%ld",names[k],cnt[k]); any=1; }
    if(!any) printf(" (无 mod 纯色)");
    printf("\n");
    return any?0:1;
}
