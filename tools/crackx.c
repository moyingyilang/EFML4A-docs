/* crack —— 尝试破解 index/pref 的混淆 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static double entropy(const uint8_t *b, long n){
    long cnt[256]={0}; for(long i=0;i<n;i++) cnt[b[i]]++;
    double H=0; for(int i=0;i<256;i++) if(cnt[i]){ double p=(double)cnt[i]/n; H-=p*log2(p);} return H;
}
int main(int c,char**v){
    FILE*f=fopen(v[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*b=malloc(n); if(fread(b,1,n,f)!=(size_t)n)return 1; fclose(f);
    printf("  输入: %ld 字节  熵=%.3f\n", n, entropy(b,n));

    /* ① 单字节 XOR */
    printf("\n  [单字节 XOR]\n");
    int best=-1; double bestH=99;
    for(int k=0;k<256;k++){
        uint8_t*t=malloc(n); for(long i=0;i<n;i++) t[i]=b[i]^k;
        double H=entropy(t,n);
        if(H<bestH){ bestH=H; best=k; }
        /* 检查是否出现 "{" / "\"" */
        int brace=0,quote=0;
        for(long i=0;i<n&&i<4096;i++){ if(t[i]=='{'||t[i]=='}')brace++; if(t[i]=='"')quote++; }
        if(brace>=2 && quote>=4){
            printf("    ★ key=0x%02X 熵=%.3f 出现 JSON 特征 ({}==%d \"==%d)\n",k,H,brace,quote);
            printf("      前 80: "); for(long i=0;i<80&&i<n;i++){char ch=t[i];printf("%c",(ch>=32&&ch<127)?ch:'.');} printf("\n");
        }
        free(t);
    }
    printf("    最低熵 key=0x%02X 熵=%.3f（若无 JSON 特征则不是单字节 XOR）\n",best,bestH);

    /* ② 固定 13 字节头，后面对 body 试单字节 XOR */
    printf("\n  [13B 头 + body 单字节 XOR]\n");
    if(n>13){
        for(int k=0;k<256;k++){
            int brace=0,quote=0;
            for(long i=13;i<n&&i<13+4096;i++){ uint8_t t=b[i]^k; if(t=='{'||t=='}')brace++; if(t=='"')quote++; }
            if(brace>=2 && quote>=4){
                printf("    ★ key=0x%02X 出现 JSON 特征 ({}==%d \"==%d)\n",k,brace,quote);
                printf("      前 80: "); for(long i=13;i<93&&i<n;i++){uint8_t t=b[i]^k;printf("%c",(t>=32&&t<127)?t:'.');} printf("\n");
            }
        }
    }

    /* ③ 试 zlib（在多种偏移/异或后） */
    printf("\n  [试 zlib 解压（原始及 XOR 后）]\n");
    printf("    前 4 字节: %02X %02X %02X %02X\n", b[0],b[1],b[2],b[3]);
    printf("    （zlib 需 78 9C/01/DA；当前不是）\n");
    return 0;
}
