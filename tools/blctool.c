/* blctool —— .blc 全能工具（纯 C）
 *   取代此前所有 Python 内联操作
 *
 * 用法:
 *   blctool dump  <in.blc> [起始] [长度]     十六进制 + ASCII 转储解密体
 *   blctool find  <in.blc> <字符串>          在解密体里搜 ASCII 字符串（打印偏移）
 *   blctool xfind <in.blc> <hexbytes>        在解密体里搜十六进制字节串
 *   blctool head  <in.blc>                   解析并打印头部字段
 *   blctool strs  <in.blc> [最短长度]        列出解密体里的可读字符串
 *   blctool paths <in.blc>                   列出所有形如 *.ab / *.png 的路径
 *   blctool crc   <in.blc>                   校验 CRC32
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

static const uint8_t KEY[32] = {
    0xe9,0x5b,0x31,0x7a,0xc4,0xf8,0x28,0x56,0x9d,0x23,0xa8,0x6b,0xf2,0x71,0xdc,0xb5,
    0x3e,0x84,0x6f,0xa7,0x5c,0x92,0x4d,0x67,0x1d,0xba,0x8e,0x38,0xf4,0xca,0x52,0xe1
};
static uint32_t rotl32(uint32_t x,int n){ return (x<<n)|(x>>(32-n)); }
static void qr(uint32_t*s,int a,int b,int c,int d){
    s[a]+=s[b]; s[d]^=s[a]; s[d]=rotl32(s[d],16);
    s[c]+=s[d]; s[b]^=s[c]; s[b]=rotl32(s[b],12);
    s[a]+=s[b]; s[d]^=s[a]; s[d]=rotl32(s[d],8);
    s[c]+=s[d]; s[b]^=s[c]; s[b]=rotl32(s[b],7);
}
static void chacha_block(const uint8_t key[32],const uint8_t nonce[12],uint32_t cnt,uint8_t out[64]){
    uint32_t st[16],w[16];
    st[0]=0x61707865; st[1]=0x3320646e; st[2]=0x79622d32; st[3]=0x6b206574;
    for(int i=0;i<8;i++) st[4+i]=key[i*4]|(key[i*4+1]<<8)|(key[i*4+2]<<16)|((uint32_t)key[i*4+3]<<24);
    st[12]=cnt;
    for(int i=0;i<3;i++) st[13+i]=nonce[i*4]|(nonce[i*4+1]<<8)|(nonce[i*4+2]<<16)|((uint32_t)nonce[i*4+3]<<24);
    memcpy(w,st,sizeof w);
    for(int i=0;i<10;i++){
        qr(w,0,4,8,12);  qr(w,1,5,9,13);  qr(w,2,6,10,14); qr(w,3,7,11,15);
        qr(w,0,5,10,15); qr(w,1,6,11,12); qr(w,2,7,8,13);  qr(w,3,4,9,14);
    }
    for(int i=0;i<16;i++){ uint32_t v=w[i]+st[i];
        out[i*4]=v&0xff; out[i*4+1]=(v>>8)&0xff; out[i*4+2]=(v>>16)&0xff; out[i*4+3]=(v>>24)&0xff; }
}
static uint32_t crc_tab[256]; static int crc_ok=0;
static void mk_crc(void){ if(crc_ok)return; for(uint32_t i=0;i<256;i++){
    uint32_t c=i; for(int k=0;k<8;k++) c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1); crc_tab[i]=c; } crc_ok=1; }
static uint32_t crc32_of(const uint8_t*p,size_t n){
    mk_crc(); uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c=crc_tab[(c^p[i])&0xff]^(c>>8);
    return c^0xFFFFFFFFu;
}
static uint8_t* load_dec(const char* path, size_t* plen, uint8_t nonce[12]){
    FILE* f=fopen(path,"rb"); if(!f){ perror(path); return 0; }
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    if(n<20){ fprintf(stderr,"文件太小\n"); fclose(f); return 0; }
    uint8_t* raw=malloc(n); if(fread(raw,1,n,f)!=(size_t)n){ fclose(f); free(raw); return 0; } fclose(f);
    memcpy(nonce,raw,12);
    size_t bl=n-12; uint8_t* body=malloc(bl); memcpy(body,raw+12,bl);
    /* ChaCha20 */
    uint8_t ks[64]; uint32_t c=1; size_t off=0;
    while(off<bl){ chacha_block(KEY,nonce,c++,ks);
        size_t m=(bl-off<64)?(bl-off):64;
        for(size_t i=0;i<m;i++) body[off+i]^=ks[i]; off+=m; }
    free(raw); *plen=bl; return body;
}
static int is_print(uint8_t c){ return c>=32 && c<127; }

int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,
        "用法:\n"
        "  blctool dump  <in.blc> [起始] [长度]\n"
        "  blctool find  <in.blc> <字符串>\n"
        "  blctool xfind <in.blc> <hexbytes>\n"
        "  blctool head  <in.blc>\n"
        "  blctool strs  <in.blc> [最短]\n"
        "  blctool paths <in.blc>\n"
        "  blctool crc   <in.blc>\n"); return 2; }
    const char* cmd=argv[1];
    size_t len=0; uint8_t nonce[12]={0};
    uint8_t* b=load_dec(argv[2],&len,nonce);
    if(!b) return 1;
    printf("  解密 %zu 字节, nonce=%02x%02x%02x%02x\n",len,nonce[0],nonce[1],nonce[2],nonce[3]);

    if(!strcmp(cmd,"head")){
        uint32_t ver=*(uint32_t*)b;
        uint32_t unk=*(uint32_t*)(b+4);
        uint16_t sl=*(uint16_t*)(b+8);
        char grp[128]={0}; if(sl<sizeof grp){ memcpy(grp,b+10,sl); }
        uint64_t id=*(uint64_t*)(b+10+sl);
        uint32_t fn=*(uint32_t*)(b+18+sl);
        uint64_t cl=*(uint64_t*)(b+22+sl);
        uint8_t  bt=b[30+sl];
        uint32_t cn=*(uint32_t*)(b+31+sl);
        printf("  version=%u  unk=0x%08x\n",ver,unk);
        printf("  strLen=%u  group=\"%s\"\n",sl,grp);
        printf("  块标识=0x%llx  fileNum=%u  chunkLen=%llu\n",
               (unsigned long long)id,fn,(unsigned long long)cl);
        printf("  blockType=%u  chunkNum=%u\n",bt,cn);
        printf("  ⇒ 条目区起点 ≈ %u\n", 35+sl);
    }
    else if(!strcmp(cmd,"crc")){
        uint32_t stored=*(uint32_t*)(b+len-4);
        uint32_t calc=crc32_of(b,len-4);
        printf("  存储=0x%08x 实算=0x%08x %s\n",stored,calc,stored==calc?"✓":"✗");
    }
    else if(!strcmp(cmd,"dump")){
        size_t st = argc>3? strtoul(argv[3],0,0):0;
        size_t n  = argc>4? strtoul(argv[4],0,0):256;
        if(st>len) st=0;
        if(st+n>len) n=len-st;
        for(size_t i=0;i<n;i+=16){
            printf("  +%06zx  ",st+i);
            for(size_t k=0;k<16;k++){ if(i+k<n) printf("%02x ",b[st+i+k]); else printf("   "); }
            printf(" |");
            for(size_t k=0;k<16 && i+k<n;k++){ uint8_t c=b[st+i+k]; putchar(is_print(c)?c:'.'); }
            printf("|\n");
        }
    }
    else if(!strcmp(cmd,"find")){
        if(argc<4){ fprintf(stderr,"需要字符串\n"); return 2; }
        const char* pat=argv[3]; size_t pl=strlen(pat);
        size_t cnt=0;
        for(size_t i=0;i+pl<=len;i++){
            if(!memcmp(b+i,pat,pl)){ printf("  @0x%zx\n",i); if(++cnt>=200){printf("  ...(截断)\n");break;} i+=pl-1; }
        }
        printf("  ⇒ 共 %zu 处\n",cnt);
    }
    else if(!strcmp(cmd,"xfind")){
        if(argc<4){ fprintf(stderr,"需要 hex\n"); return 2; }
        const char* h=argv[3]; size_t hl=strlen(h)/2;
        uint8_t* pat=malloc(hl);
        for(size_t i=0;i<hl;i++){ unsigned v; sscanf(h+i*2,"%2x",&v); pat[i]=v; }
        size_t cnt=0;
        for(size_t i=0;i+hl<=len;i++){
            if(!memcmp(b+i,pat,hl)){ printf("  @0x%zx\n",i); if(++cnt>=200){printf("  ...(截断)\n");break;} }
        }
        printf("  ⇒ 共 %zu 处\n",cnt); free(pat);
    }
    else if(!strcmp(cmd,"strs")){
        size_t minl = argc>3? strtoul(argv[3],0,0):8;
        size_t i=0,cnt=0;
        while(i<len){
            if(is_print(b[i])){
                size_t j=i; while(j<len && is_print(b[j])) j++;
                if(j-i>=minl){ fwrite(b+i,1,j-i,stdout); printf("\n"); if(++cnt>=2000) break; }
                i=j;
            } else i++;
        }
        fprintf(stderr,"  ⇒ %zu 条\n",cnt);
    }
    else if(!strcmp(cmd,"paths")){
        /* 提取形如 xxx.ab / xxx.png / xxx.dds / Data/... 的路径 */
        size_t i=0,cnt=0;
        while(i<len){
            if(is_print(b[i])){
                size_t j=i; while(j<len && is_print(b[j])) j++;
                size_t L=j-i;
                if(L>=5){
                    char* s=(char*)(b+i);
                    int ok=0;
                    for(size_t k=0;k+3<L;k++){
                        if(!memcmp(s+k,".ab",3)||!memcmp(s+k,".png",4)||
                           !memcmp(s+k,".dds",4)||!memcmp(s+k,".txt",4)){ ok=1; break; }
                    }
                    if(ok){ fwrite(b+i,1,L,stdout); printf("\n"); if(++cnt>=3000) break; }
                }
                i=j;
            } else i++;
        }
        fprintf(stderr,"  ⇒ %zu 条路径\n",cnt);
    }
    else fprintf(stderr,"未知命令: %s\n",cmd);
    free(b);
    return 0;
}
