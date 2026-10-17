#include <stdio.h>
#include <stdlib.h>
#include <math.h>
int main(int c,char**v){
    FILE*f=fopen(v[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char*b=malloc(n);if(fread(b,1,n,f)!=(size_t)n)return 1;fclose(f);
    long cnt[256]={0};for(long i=0;i<n;i++)cnt[b[i]]++;
    double H=0;for(int i=0;i<256;i++)if(cnt[i]){double p=(double)cnt[i]/n;H-=p*log2(p);}
    printf("  %-18s %8ld 字节  熵=%.3f bit/byte  %s\n",v[1],n,H,
        H>7.9?"≈随机/加密":H>6.0?"≈压缩":H>3.0?"≈半结构化":"≈文本/结构");
    return 0;
}
