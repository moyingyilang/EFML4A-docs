/* ============================================================================
 *  blcparse —— 终末地 VFS .blc 解密 + 解析（纯 C，双布局支持）
 *
 *  支持:
 *    version = 4  → 1.5.3 布局（chunk 头 fileCount 在偏移 41..48）
 *    version = 5  → 1.6.4 布局（chunk 头多 4 字节，fileCount 在偏移 45..52）
 *
 *  用法:
 *    blcparse <x.blc> [out.tsv]          解析为 TSV（fileName/offset/len/...）
 *    blcparse -i <x.blc>                 只打印摘要（group/fileNum/blockType）
 *    blcparse -v <x.blc>                 只做解密 + CRC 校验
 *
 *  输出 TSV 列:
 *    fileName  chunkMD5  offset  len  blockType  useEncrypt  ivSeed  group
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define KEY_HEX "e95b317ac4f828569d23a86bf271dcb53e846fa75c924d671dba8e38f4ca52e1"

static uint32_t rotl(uint32_t x,int n){return (x<<n)|(x>>(32-n));}
static void qr(uint32_t*s,int a,int b,int c,int d){
    s[a]+=s[b];s[d]^=s[a];s[d]=rotl(s[d],16);s[c]+=s[d];s[b]^=s[c];s[b]=rotl(s[b],12);
    s[a]+=s[b];s[d]^=s[a];s[d]=rotl(s[d],8);s[c]+=s[d];s[b]^=s[c];s[b]=rotl(s[b],7);}
static void chacha(const uint8_t*k,const uint8_t*nn,uint32_t ct,uint8_t*o){
    uint32_t st[16]={0x61707865,0x3320646e,0x79622d32,0x6b206574};
    for(int i=0;i<8;i++){uint32_t v;memcpy(&v,k+4*i,4);st[4+i]=v;}
    st[12]=ct;for(int i=0;i<3;i++){uint32_t v;memcpy(&v,nn+4*i,4);st[13+i]=v;}
    uint32_t w[16];memcpy(w,st,sizeof(w));
    for(int i=0;i<10;i++){qr(w,0,4,8,12);qr(w,1,5,9,13);qr(w,2,6,10,14);qr(w,3,7,11,15);
        qr(w,0,5,10,15);qr(w,1,6,11,12);qr(w,2,7,8,13);qr(w,3,4,9,14);}
    for(int i=0;i<16;i++){uint32_t v=w[i]+st[i];memcpy(o+4*i,&v,4);}}
static uint32_t crc32f(const uint8_t*d,long long n){uint32_t c=0xFFFFFFFFu;
    for(long long i=0;i<n;i++){c^=d[i];for(int k=0;k<8;k++)c=(c>>1)^(0xEDB88320u&(~((c&1)-1)));}return c^0xFFFFFFFFu;}

static long long fsize(const char*p){FILE*f=fopen(p,"rb");if(!f)return -1;
    fseek(f,0,SEEK_END);long long s=ftell(f);fclose(f);return s;}
static uint8_t* slurp(const char*p,long long*n){FILE*f=fopen(p,"rb");if(!f)return NULL;
    long long s=fsize(p);uint8_t*b=malloc(s?s:1);if(!b){fclose(f);return NULL;}
    if(fread(b,1,s,f)!=(size_t)s){free(b);fclose(f);return NULL;}fclose(f);*n=s;return b;}
static int hex2key(const char*h,uint8_t*k){for(int i=0;i<32;i++){unsigned v;
    if(sscanf(h+2*i,"%2x",&v)!=1)return -1;k[i]=v;}return 0;}

/* 解密 + 校验，返回 0 成功 */
static int decrypt(uint8_t*b,long long n,const uint8_t*k,uint32_t*verOut){
    if(n<16) return -1;
    uint8_t nn[12]; memcpy(nn,b,12);
    uint8_t ks[64]; uint32_t ct=1;
    for(long long o=12;o<n;o+=64){chacha(k,nn,ct++,ks);long long m=(n-o<64)?(n-o):64;
        for(long long i=0;i<m;i++) b[o+i]^=ks[i];}
    uint32_t tail; memcpy(&tail,b+n-4,4);
    uint32_t cc=crc32f(b+12,n-4-12);
    if(tail!=cc) return -2;
    uint32_t ver; memcpy(&ver,b+12,4); if(verOut)*verOut=ver;
    return 0;
}

/* 在 chunk 头里探测 fileCount：返回 1 成功，填 fpos/nf 与首条目名 */
static int probe(uint8_t*b,long long n,long long p,int lo,int hi,long long*fpos,uint32_t*nf,char*fname,long long*fnlen){
    uint32_t bestNf=0; long long bestPos=0, bestL=0; char bestName[600]={0};
    for(int off=lo; off<=hi; off++){
        uint32_t t; memcpy(&t,b+p+off,4);
        if(t==0||t>1000000) continue;
        long long q=p+off+4;
        if(q+2>=n) continue;
        uint16_t L; memcpy(&L,b+q,2);
        if(L<4||L>512||q+2+L>n) continue;
        int ok=1;
        for(int i=0;i<L;i++){uint8_t c=b[q+2+i]; if(c<32||c>126){ok=0;break;}}
        if(!ok) continue;
        /* 只验证前 3 条（避免因个别条目异常而整体否决） */
        long long r=q; uint32_t k=0, room = (t<3?t:3);
        for(;k<room;k++){
            if(r+2>n){ok=0;break;}
            uint16_t l2; memcpy(&l2,b+r,2);
            if(l2<4||l2>512||r+2+l2>n){ok=0;break;}
            int o2=1; for(int i=0;i<l2;i++){uint8_t c=b[r+2+i]; if(c<32||c>126){o2=0;break;}}
            if(!o2){ok=0;break;}
            r = r+2+l2 + 16+16+8+8+1+1+4;
        }
        if(ok && t>bestNf){ bestNf=t; bestPos=p+off; bestL=L; memcpy(bestName,b+q+2,L); bestName[L]=0; }
    }
    if(!bestNf) return 0;
    *fpos=bestPos; *nf=bestNf; *fnlen=bestL; memcpy(fname,bestName,bestL+1); return 1;
}

int main(int argc,char**argv){
    int mode=0; /* 0=tsv 1=info 2=verify */
    const char* path=NULL; const char* outp=NULL;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"-i")) mode=1;
        else if(!strcmp(argv[i],"-v")) mode=2;
        else if(!path) path=argv[i];
        else outp=argv[i];
    }
    if(!path){ fprintf(stderr,"用法: blcparse [-i|-v] <x.blc> [out.tsv]\n"); return 2; }
    long long n; uint8_t*b=slurp(path,&n);
    if(!b){ perror("read"); return 1; }
    uint8_t key[32]; hex2key(KEY_HEX,key);
    uint32_t ver=0;
    int r=decrypt(b,n,key,&ver);
    if(r==-2){ fprintf(stderr,"  错误: CRC 不匹配（不是本方案的 .blc，或已解密过）\n"); free(b); return 1; }
    if(r<0){ fprintf(stderr,"  错误: 文件太小\n"); free(b); return 1; }
    if(mode==2){ printf("  CRC OK  version=%u\n",ver); free(b); return 0; }

    long long p=20;                 /* 12 = version(4) + unk(4) → 20 起是 groupName */
    uint16_t gl; memcpy(&gl,b+p,2); p+=2;
    if(gl>500){ fprintf(stderr,"  错误: groupName 长度异常 (%u) — 偏移对齐有误\n",gl); free(b); return 1; }
    char gn[512]; memcpy(gn,b+p,gl); gn[gl]=0; p+=gl;
    uint64_t gh; memcpy(&gh,b+p,8); p+=8;
    uint32_t fn; memcpy(&fn,b+p,4); p+=4;
    uint64_t cl; memcpy(&cl,b+p,8); p+=8;
    uint8_t bt=b[p++]; uint32_t nc; memcpy(&nc,b+p,4); p+=4;

    if(mode==1){
        printf("  version    : %u%s\n",ver, ver==4?" (1.5.3 布局)":(ver==5?" (1.6.4 布局)":" (未知)"));
        printf("  groupName  : \"%s\"\n",gn);
        printf("  groupHash  : %016llx\n",(unsigned long long)gh);
        printf("  fileNum    : %u\n",fn);
        printf("  chunkLen   : %llu\n",(unsigned long long)cl);
        printf("  blockType  : %u\n",bt);
        printf("  chunkNum   : %u\n",nc);
        printf("  chunk头起点: %lld\n",p);
        free(b); return 0;
    }

    /* ── 遍历所有 chunk ── */
    FILE* o = outp? fopen(outp,"w") : stdout;
    if(outp) fprintf(o,"fileName\tchunkMD5\toffset\tlen\tblockType\tuseEncrypt\tivSeed\tgroup\n");
    long long total=0, cp=p;
    for(uint32_t ci=0; ci<nc; ci++){
        /* ★ 按 version 选择搜索窗口
         *   1.5.3 (ver<=4): chunk 头 fileCount@+45, nameLen@+49, name@+51
         *   1.6.4 (ver>=5): chunk 头 +45 处多 4 字节
         *                   ⇒ fileCount@+49, nameLen@+53, name@+55
         * 同时两套都试（自适应），取最大合法值 */
        int lo = (ver>=5)?45:41, hi = (ver>=5)?56:52;
        uint32_t bestNf=0; long long bestPos=0;
        for(int off=lo; off<=hi; off++){
            uint32_t t; memcpy(&t,b+cp+off,4);
            if(t==0||t>1000000) continue;
            long long q=cp+off+4;
            if(q+2>n) continue;
            uint16_t L; memcpy(&L,b+q,2);
            if(L<4||L>512||q+2+L>n) continue;
            int ok=1;
            for(int k=0;k<L;k++){uint8_t c=b[q+2+k]; if(c<32||c>126){ok=0;break;}}
            if(!ok) continue;
            /* 再验证：首个条目名应以 "Data/" 或 "Assets/" 开头（终末地的资产名特征） */
            if(L>=5){
                const char* nm=(const char*)(b+q+2);
                int looks = (L>=5 && !memcmp(nm,"Data/",5)) || (L>=7 && !memcmp(nm,"Assets/",7));
                if(!looks && bestNf>0) continue;   /* 已有更好候选时跳过可疑的 */
            }
            if(t>bestNf){ bestNf=t; bestPos=cp+off; }
        }
        if(!bestNf){ break; }
        long long q=bestPos+4;
        for(uint32_t fi=0; fi<bestNf; fi++){
            if(q+2>n) break;
            uint16_t L; memcpy(&L,b+q,2);
            if(L<4||L>512||q+2+L>n) break;
            char nm[600]; memcpy(nm,b+q+2,L); nm[L]=0;
            long long at=q+2+L;
            uint64_t off; memcpy(&off,b+at+8+16+16,8);
            uint64_t len; memcpy(&len,b+at+8+16+16+8,8);
            uint8_t fbt=b[at+8+16+16+8+8];
            uint8_t en =b[at+8+16+16+8+8+1];
            uint32_t iv;  memcpy(&iv,b+at+8+16+16+8+8+2,4);
            fprintf(o,"%s\t-\t%llu\t%llu\t%u\t%u\t%u\t%s\n",nm,
                (unsigned long long)off,(unsigned long long)len,fbt,en,iv,gn);
            q = at+8+16+16+8+8+1+1+4;
            total++;
        }
        /* 自适应前进：从 q 起向前扫，找下一个能通过 probe 的位置 */
        {
            long long nc2 = -1;
            for(long long t=q; t<=q+64; t++){
                uint32_t tt=0; long long bp=0;
                for(int off=41; off<=56; off++){
                    if(t+off+4>(long long)n) break;
                    uint32_t v; memcpy(&v,b+t+off,4);
                    if(v==0||v>1000000) continue;
                    long long qq=t+off+4;
                    if(qq+2>(long long)n) continue;
                    uint16_t LL; memcpy(&LL,b+qq,2);
                    if(LL<4||LL>512||qq+2+LL>(long long)n) continue;
                    int okk=1;
                    for(int k=0;k<LL;k++){uint8_t c=b[qq+2+k]; if(c<32||c>126){okk=0;break;}}
                    if(okk){ tt=v; bp=t+off; break; }
                }
                if(tt){ nc2=t; break; }
            }
            cp = (nc2>=0)? nc2 : q;
        }
    }
    if(outp) fclose(o);
    if(outp) printf("  version=%u group=\"%s\" fileNum=%u chunkNum=%u  已导出 %lld 条 -> %s\n",
                    ver,gn,fn,nc,total,outp);
    else     printf("  version=%u group=\"%s\" fileNum=%u chunkNum=%u  共 %lld 条\n",
                    ver,gn,fn,nc,total);
    free(b); return 0;
}
