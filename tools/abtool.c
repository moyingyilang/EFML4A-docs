/* abtool —— 解析单个 .ab（Endfield VFS 容器）
 *   参考: AnimeStudio VFSFile.cs / VFSUtils.cs / LZ4.cs / LZ4Inv.cs
 *   用法: abtool <in.ab> [--dump]
 *
 * 流程:
 *   ① a/b 头校验（变体1 ArknightsEndfield）
 *   ② 头部字段解扰 → compBI/uncompBI/size/flags/encFlags
 *   ③ 块信息偏移 = (encFlags >= 7) ? 48 : 40
 *   ④ 若 (flags & 0x3F) != 0: DecryptBlock + LZ4；否则直接用
 *   ⑤ 解析块表 + 节点表
 *   ⑥ 数据块: flags=0(直接) / flags=5(DecryptBlock + LZ4Inv)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
int vfs_decrypt_block(uint8_t* buf, size_t len);

static uint32_t ror32(uint32_t x,int n){n&=31;return n?((x>>n)|(x<<(32-n))):x;}
static uint32_t rol32(uint32_t x,int n){n&=31;return n?((x<<n)|(x>>(32-n))):x;}
static uint16_t rol16(uint16_t x,int n){n&=15;return n?(uint16_t)((x<<n)|(x>>(16-n))):x;}
static uint64_t rol64(uint64_t x,int n){n&=63;return n?((x<<n)|(x>>(64-n))):x;}
static uint8_t*B; static long BN;
static uint32_t be32(long o){return ((uint32_t)B[o]<<24)|(B[o+1]<<16)|(B[o+2]<<8)|B[o+3];}
static uint16_t be16(long o){return (B[o]<<8)|B[o+1];}
static uint32_t le32(long o){return B[o]|(B[o+1]<<8)|(B[o+2]<<16)|((uint32_t)B[o+3]<<24);}

/* 标准 LZ4 */
static long lz4d(const uint8_t*in,long inLen,uint8_t*out,long cap,int inv){
    const uint8_t*ip=in,*iend=in+inLen; uint8_t*op=out,*oend=out+cap;
    while(ip<iend){
        uint8_t tk=*ip++; long ll,ml;
        if(inv){
            uint8_t lit=tk&0x33, enc=(tk&0xCC)>>2;
            ll=(lit&3)|(lit>>2); ml=(enc&3)|(enc>>2);
        } else { ll=tk>>4; ml=tk&0x0F; }
        if(ll==15){ uint8_t s; do{ if(ip>=iend) return -1; s=*ip++; ll+=s; }while(s==255); }
        if(ll>(iend-ip)||op+ll>oend) return -1;
        memcpy(op,ip,ll); ip+=ll; op+=ll;
        if(ip>=iend) break;
        if(ip+2>iend) return -1;
        long off = inv ? ((ip[0]<<8)|ip[1]) : (ip[0]|(ip[1]<<8));
        ip+=2;
        if(off==0||off>(op-out)) return -1;
        if(ml==15){ uint8_t s; do{ if(ip>=iend) return -1; s=*ip++; ml+=s; }while(s==255); }
        ml+=4;
        if(op+ml>oend) return -1;
        { const uint8_t*mp=op-off; for(long i=0;i<ml;i++) *op++=*mp++; }
    }
    return op-out;
}
int main(int c,char**v){
    if(c<2){ fprintf(stderr,"用法: abtool <in.ab> [--dump]\n"); return 2; }
    FILE*f=fopen(v[1],"rb"); if(!f){perror(v[1]);return 1;}
    fseek(f,0,SEEK_END); BN=ftell(f); fseek(f,0,SEEK_SET);
    B=malloc(BN); if(fread(B,1,BN,f)!=(size_t)BN) return 1; fclose(f);
    printf("  .ab: %ld 字节\n", BN);
    /* ① 头校验 */
    uint32_t a=be32(0), bh=be32(4);
    uint32_t hc1=(4*(a^0x4A92F0CD))&0xFFFF0000;
    uint32_t hc2=ror32(a^0x4A92F0CD,14);
    if(bh!=(hc1^hc2^0xD8B1E637)){ printf("  ✗ 头校验失败\n"); return 1; }
    printf("  ★ 头校验通过\n");
    /* ② 头部字段 */
    long p=8;
    uint16_t cBI2=be16(p); p+=2;
    uint32_t flags2=be32(p); p+=4;
    uint32_t encF=be32(p); p+=4;
    uint32_t size2=be32(p); p+=4;
    uint32_t flags1=be32(p); p+=4;
    uint16_t uBI1=be16(p); p+=2;
    p+=4;
    uint16_t uBI2=be16(p); p+=2;
    uint32_t size1=be32(p); p+=4;
    uint16_t cBI1=be16(p); p+=2;
    uint32_t compBI=((uint32_t)(cBI1^cBI2^0xA121)<<16)|cBI2; compBI=ror32(compBI,18)^0xF74324EE;
    uint32_t uncompBI=((uint32_t)(uBI1^uBI2^0xA121)<<16)|uBI2; uncompBI=ror32(uncompBI,18)^0xF74324EE;
    uint64_t sz=((uint64_t)(size1^size2^0xDAD76848)<<32)|size2; sz=rol64(sz,14)^0xA4F1A11747816520ULL;
    uint32_t fl=flags1^flags2^0xA7F49310;
    uint32_t enc=encF^flags2;
    printf("  compBI=%u uncompBI=%u size=%llu flags=0x%08x encFlags=0x%08x\n",
           compBI,uncompBI,(unsigned long long)sz,fl,enc);
    /* ③ 块信息偏移 */
    long bio;
    if(fl & 0x100){ bio = (long)(sz - compBI); }
    else bio = (enc>=7)? 48 : 40;
    printf("  块信息偏移 = %ld\n", bio);
    /* ④ 取块信息 */
    uint8_t*bi=malloc(compBI?compBI:1);
    memcpy(bi,B+bio,compBI);
    long biLen=compBI;
    if((fl & 0x3F)!=0){
        printf("  块信息被压缩 ⇒ DecryptBlock + LZ4\n");
        vfs_decrypt_block(bi,biLen);
        uint8_t*ub=malloc(uncompBI?uncompBI:1);
        long o=lz4d(bi,biLen,ub,uncompBI,0);
        printf("    LZ4 解出 %ld 字节（期望 %u）%s\n",o,uncompBI,o==(long)uncompBI?" ★":"");
        if(o>0){ free(bi); bi=ub; biLen=o; } else { free(ub); }
    }
    if(biLen<8){ printf("  ✗ 块信息太短\n"); return 1; }
    /* ⑤ 块表 */
    uint32_t raw=le32(0); /* placeholder */
    { uint8_t*t=bi; (void)t; }
    uint32_t e0=__builtin_bswap32((bi[0]|(bi[1]<<8)|(bi[2]<<16)|((uint32_t)bi[3]<<24)) ^ 0x8A7BF723);
    uint16_t l0=(uint16_t)e0,h0=(uint16_t)(e0>>16);
    uint32_t bc=ror32(((uint32_t)(l0^h0)<<16)|l0,18)^0x91CE0A4F;
    printf("  blocksCount=%u\n", bc);
    if(bc>128){ printf("  ✗ blocksCount 不合理\n"); return 1; }
    long q=4; long totalUn=0;
    uint32_t *bFlags=malloc(sizeof(uint32_t)*bc), *bUn=malloc(sizeof(uint32_t)*bc), *bCp=malloc(sizeof(uint32_t)*bc);
    for(uint32_t i=0;i<bc;i++){
        uint16_t A=be16((long)0),Bb,C,eF,D;
        /* 从 bi 读 */
        A=(bi[q]<<8)|bi[q+1]; Bb=(bi[q+2]<<8)|bi[q+3]; C=(bi[q+4]<<8)|bi[q+5];
        eF=(((bi[q+6]<<8)|bi[q+7])^0x9CD6); D=(bi[q+8]<<8)|bi[q+9];
        q+=10;
        uint8_t a0=eF&0xFF,a1=eF>>8;
        uint16_t f2=((uint16_t)(a0^a1)<<8)|a0;
        f2=(uint16_t)(C ^ rol16(f2,14) ^ 0x523F);
        uint32_t un=((uint32_t)(A^C^0xA121)<<16)|C; un=ror32(un,18)^0xF74324EE;
        uint32_t cp=((uint32_t)(Bb^D^0xA121)<<16)|D; cp=ror32(cp,18)^0xF74324EE;
        bFlags[i]=f2; bUn[i]=un; bCp[i]=cp; totalUn+=un;
        printf("    块%u: flags=%u uncomp=%u comp=%u %s\n",i,f2,un,cp,(f2==0||f2==5)?"★":"✗");
    }
    /* ⑥ 节点表 */
    uint32_t e1=__builtin_bswap32((bi[q]|(bi[q+1]<<8)|(bi[q+2]<<16)|((uint32_t)bi[q+3]<<24)) ^ 0x5DE50A6B);
    uint16_t l1=(uint16_t)e1,h1=(uint16_t)(e1>>16);
    uint32_t nc=ror32(((uint32_t)(l1^h1)<<16)|l1,18)^0xE4C1D9F2;
    printf("  nodesCount=%u (@bi+%ld)\n", nc, q);
    q+=4;
    if(nc>1024){ printf("  ✗ nodesCount 不合理\n"); return 1; }
    for(uint32_t i=0;i<nc;i++){
        uint32_t A=be32((long)0); (void)A;
        uint32_t a4=((uint32_t)bi[q]<<24)|(bi[q+1]<<16)|(bi[q+2]<<8)|bi[q+3]; q+=4;
        uint32_t b4=((uint32_t)bi[q]<<24)|(bi[q+1]<<16)|(bi[q+2]<<8)|bi[q+3]; q+=4;
        uint32_t c4=((uint32_t)bi[q]<<24)|(bi[q+1]<<16)|(bi[q+2]<<8)|bi[q+3]; q+=4;
        uint32_t d4=((uint32_t)bi[q]<<24)|(bi[q+1]<<16)|(bi[q+2]<<8)|bi[q+3]; q+=4;
        a4 ^= 0x8E06A9F8;
        char nm[68]; int k=0;
        while(q<biLen && bi[q]!=0 && k<64){ nm[k]=(char)(bi[q]^((k^0x97)&0xFF)); k++; q++; }
        nm[k]=0; if(q<biLen) q++;
        uint32_t e4=((uint32_t)bi[q]<<24)|(bi[q+1]<<16)|(bi[q+2]<<8)|bi[q+3]; q+=4;
        uint16_t a0=(uint16_t)a4,a1=(uint16_t)(a4>>16);
        uint32_t f2=((uint32_t)(a1^a0)<<16)|a0; f2=ror32(f2,18)^0xF13927C4^b4;
        uint64_t off=((uint64_t)(d4^c4^0xDAD76848)<<32)|c4; off=rol64(off,14)^0xA4F1A11747816520ULL;
        uint64_t s=((uint64_t)(b4^e4^0xDAD76848)<<32)|e4; s=rol64(s,14)^0xA4F1A11747816520ULL;
        printf("    节点%u: \"%s\" flags=0x%08x offset=%llu size=%llu\n",
               i,nm,f2,(unsigned long long)off,(unsigned long long)s);
    }
    printf("  块信息结束 @bi+%ld (共 %ld 字节)\n", q, biLen);
    /* ⑦ 数据区 */
    long dataOff = bio;
    if(!(fl & 0x100)){
        long t=compBI;
        if(fl & 0x200) t=(t+15)&~15L;
        dataOff = ((enc>=7)?48:40) + t;
    }
    printf("  数据区偏移 = %ld\n", dataOff);
    if(c>2 && !strcmp(v[2],"--dump") && dataOff<BN){
        uint8_t*out=malloc(totalUn?totalUn:1);
        long op=0;
        for(uint32_t i=0;i<bc;i++){
            const uint8_t*src=B+dataOff;
            if(bFlags[i]==0){
                memcpy(out+op,src,bUn[i]); op+=bUn[i];
                dataOff+=bUn[i];
            } else if(bFlags[i]==5){
                uint8_t*cp=malloc(bCp[i]);
                memcpy(cp,src,bCp[i]);
                vfs_decrypt_block(cp,bCp[i]);
                long o=lz4d(cp,bCp[i],out+op,bUn[i],1);
                printf("    数据块%u: LZ4Inv 解出 %ld / %u\n",i,o,bUn[i]);
                if(o>0) op+=o;
                free(cp);
                dataOff+=bCp[i];
            } else {
                printf("    数据块%u: 未知压缩类型 %u\n",i,bFlags[i]);
            }
        }
        printf("  解出数据共 %ld 字节\n", op);
        if(op>0){
            printf("  前 32 字节: ");
            for(long i=0;i<32&&i<op;i++) printf("%02x ", out[i]);
            printf("\n");
            if(memcmp(out,"UnityFS",7)==0) printf("  ★★★ 发现 UnityFS 魔数！\n");
            FILE*g=fopen("/sdcard/ab_dump.bin","wb"); fwrite(out,1,op,g); fclose(g);
            printf("  ✓ 已写出 /sdcard/ab_dump.bin\n");
        }
        free(out);
    }
    free(bi);
    return 0;
}
