/* abpack —— 纯 C 的 .ab 重建器（自包含：解析→改→重压→重建，无 subprocess）
 *   用法: abpack <in.ab> <out.ab> <streamOffset> <newdata.bin>
 *   依赖: deps.c（LZ4/LZ4Inv 双向）, vfsaes.c（DecryptBlock）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern int vfs_decrypt_block(uint8_t* buf, size_t len);
extern long lz4inv_decompress(const uint8_t*in,long inLen,uint8_t*out,long cap);
extern long lz4inv_compress(const uint8_t*src,long slen,uint8_t*dst,long dcap);
extern long lz4_decompress(const uint8_t*in,long inLen,uint8_t*out,long cap);
extern long lz4_compress(const uint8_t*src,long slen,uint8_t*dst,long dcap);

static uint32_t rol32(uint32_t x,int n){return (x<<n)|(x>>(32-n));}
static uint32_t ror32(uint32_t x,int n){return (x>>n)|(x<<(32-n));}
static uint16_t rol16(uint16_t x,int n){return (uint16_t)((x<<n)|(x>>(16-n)));}
static uint16_t ror16(uint16_t x,int n){return (uint16_t)((x>>n)|(x<<(16-n)));}
static uint32_t bs32(uint32_t x){return (x>>24)|((x>>8)&0xFF00)|((x<<8)&0xFF0000)|(x<<24);}
static uint32_t le32(const uint8_t*p){return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t be16(const uint8_t*p){return (p[0]<<8)|p[1];}
static void w16be(uint8_t*p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)(v&0xFF);}

/* 通用 32 位混淆写入：x = rol32(v^K,18); l=x&0xFFFF; h=((x>>16)&0xFFFF)^l[^0xA121]; 小端写 */
static void put32(uint8_t*out,uint32_t v,uint32_t K,int useA121){
    uint32_t x=rol32(v^K,18);
    uint16_t l=(uint16_t)(x&0xFFFF);
    uint16_t h=(uint16_t)(((x>>16)&0xFFFF)^l);
    if(useA121) h=(uint16_t)(h^0xA121);
        uint32_t e=((uint32_t)h<<16)|l;   /* ★ encCount = (h<<16)|l  */
    uint32_t rw=bs32(e)^K;   /* 注意：K 的第二次 XOR 用的是 0x8A7BF723 / 0x5DE50A6B，见调用处 */
    (void)rw;
    out[0]=(uint8_t)(e&0xFF); (void)out;
}

int main(int c,char**v){
    if(c<5){fprintf(stderr,"用法: abpack <in.ab> <out.ab> <streamOffset> <newdata.bin>\n");return 2;}
    FILE*f=fopen(v[1],"rb"); if(!f){perror(v[1]);return 1;}
    fseek(f,0,SEEK_END); long rn=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*raw=malloc(rn); if(!raw||fread(raw,1,rn,f)!=(size_t)rn)return 1; fclose(f);
    long target=atol(v[3]);
    FILE*g=fopen(v[4],"rb"); fseek(g,0,SEEK_END); long nn=ftell(g); fseek(g,0,SEEK_SET);
    uint8_t*nd=malloc(nn); if(!nd||fread(nd,1,nn,g)!=(size_t)nn)return 1; fclose(g);
    printf("  .ab=%ld  目标=%ld  新数据=%ld\n",rn,target,nn);

    /* ---- 头部字段 ---- */
    uint16_t cBI2=be16(raw+8), uBI1=be16(raw+26), uBI2=be16(raw+32), cBI1=be16(raw+38);
    uint32_t compBI  =ror32(((uint32_t)(cBI1^cBI2^0xA121)<<16)|cBI2,18)^0xF74324EE;
    uint32_t uncompBI=ror32(((uint32_t)(uBI1^uBI2^0xA121)<<16)|uBI2,18)^0xF74324EE;
    printf("  compBI=%u uncompBI=%u\n",compBI,uncompBI);

    /* ---- 解块信息 ---- */
    uint8_t*bi=malloc(compBI); memcpy(bi,raw+48,compBI);
    vfs_decrypt_block(bi,compBI);
    uint8_t*biPlain=malloc(uncompBI+4096);
    long biLen=lz4_decompress(bi,compBI,biPlain,uncompBI+4096);
    printf("  块信息明文=%ld (期望 %u)\n",biLen,uncompBI);
    if(biLen<=0){fprintf(stderr,"  ✗ 块信息解压失败\n");return 1;}

    /* ---- 解析块表（混淆） ---- */
    uint32_t bcr=bs32(le32(biPlain)^0x8A7BF723);
    uint16_t blo=(uint16_t)(bcr&0xFFFF), bhi=(uint16_t)(bcr>>16);
    uint32_t bc=ror32(((uint32_t)(blo^bhi)<<16)|blo,18)^0x91CE0A4F;
    printf("  blocksCount=%u\n",bc);
    if(bc==0||bc>1000){fprintf(stderr,"  ✗ blocksCount 不合理\n");return 1;}
    long p=4;
    long *co=malloc(sizeof(long)*bc), *un=malloc(sizeof(long)*bc);
    for(uint32_t i=0;i<bc;i++){
        uint16_t a=be16(biPlain+p),bb=be16(biPlain+p+2),cc=be16(biPlain+p+4);
        uint16_t ef=(uint16_t)(be16(biPlain+p+6)^0x9CD6), d=be16(biPlain+p+8);
        p+=10;
        uint8_t a0=(uint8_t)(ef&0xFF), a1=(uint8_t)(ef>>8);
        uint16_t f2=(uint16_t)(((uint16_t)(a0^a1)<<8)|a0); f2=(uint16_t)(cc^rol16(f2,14)^0x523F);
        uint32_t u=((uint32_t)(a^cc^0xA121)<<16)|cc; u=ror32(u,18)^0xF74324EE;
        uint32_t cp=((uint32_t)(bb^d^0xA121)<<16)|d; cp=ror32(cp,18)^0xF74324EE;
        co[i]=cp; un[i]=u;
        if(i<3) printf("    块%u: comp=%u uncomp=%u flags=%u\n",i,cp,u,f2);
    }
    long nodesOff=p;
    printf("  块表结束 @%ld  dataStart=%ld\n",nodesOff,48+((compBI+15)/16*16));

    /* ---- 解压所有块 → 完整流 ---- */
    long total=0; for(uint32_t i=0;i<bc;i++) total+=un[i];
    uint8_t*stream=malloc(total);
    long srcOff=48+((compBI+15)/16*16), strOff=0;
    for(uint32_t i=0;i<bc;i++){
        uint8_t*cp=malloc(co[i]); memcpy(cp,raw+srcOff,co[i]);
        vfs_decrypt_block(cp,co[i]);
        long o=lz4inv_decompress(cp,co[i],stream+strOff,un[i]);
        if(o!=un[i]){fprintf(stderr,"  ✗ 块%u 解压 %ld != %ld\n",i,o,un[i]);return 1;}
        strOff+=o; srcOff+=co[i]; free(cp);
    }
    printf("  完整流=%ld (期望 %ld)\n",strOff,total);

    /* ---- 修改 ---- */
    if(target<0||target+nn>total){fprintf(stderr,"  ✗ 目标越界\n");return 1;}
    memcpy(stream+target,nd,nn);

    /* ---- 重压块 ----
     * 第 5 个参数 = "min" 时启用【最小改动模式】：
     *   只重压与目标区重叠的块，其余【原样复制】原始压缩字节
     *   ⇒ 总长变化极小，可零填充到原长（.blc 无需改 off/len）
     */
    int minimal = (c>=6 && (strcmp(v[5],"min")==0 || strcmp(v[5],"pad")==0));
    long origDataStart = 48+((compBI+15)/16*16);
    long origTotal = rn - origDataStart;
    uint8_t**newBlk=malloc(sizeof(uint8_t*)*bc);
    long *newLen=malloc(sizeof(long)*bc);
    long newTotal=0, bOff=0, srcp=origDataStart;
    int *copied=calloc(bc,sizeof(int));
    for(uint32_t i=0;i<bc;i++){
        long off=bOff;
        int touched = (target < off+(long)un[i]) && (target+nn > off);
        if(minimal && !touched){
            /* 原样复制原始压缩字节（★ 注意：这些字节【已经是加密态】） */
            newLen[i]=co[i];
            newBlk[i]=malloc(co[i]);
            memcpy(newBlk[i],raw+srcp,co[i]);
            copied[i]=1;
        } else {
            long cap=un[i]*2+65536;
            newBlk[i]=malloc(cap);
            newLen[i]=lz4inv_compress(stream+off,un[i],newBlk[i],cap);
            if(newLen[i]<=0){fprintf(stderr,"  ✗ 块%u 压缩失败\n",i);return 1;}
        }
        newTotal+=newLen[i]; bOff+=un[i]; srcp+=co[i];
    }
    printf("  重压后总 comp=%ld (原 %ld)%s\n",newTotal,origTotal,minimal?"  [最小改动]":"");
    if(newTotal>origTotal){
        fprintf(stderr,"  ✗ 新数据超出原长 %ld 字节，无法零填充\n",newTotal-origTotal);
        return 1;
    }

    /* ---- 重建块信息（混淆） ---- */
    uint8_t*nbi=malloc(uncompBI+4096);
    long np=0;
    {   /* ① blocksCount: x=rol32(bc^0x91CE0A4F,18); l=x&0xFFFF; h=((x>>16)&0xFFFF)^l  ★无 0xA121 */
        uint32_t x=rol32(bc^0x91CE0A4F,18);
        uint16_t l=(uint16_t)(x&0xFFFF), h=(uint16_t)(((x>>16)&0xFFFF)^l);
        uint32_t e=((uint32_t)h<<16)|l;   /* ★ encCount = (h<<16)|l  */
        uint32_t rw=bs32(e)^0x8A7BF723;
        nbi[np++]=(uint8_t)(rw&0xFF);       nbi[np++]=(uint8_t)((rw>>8)&0xFF);
        nbi[np++]=(uint8_t)((rw>>16)&0xFF); nbi[np++]=(uint8_t)((rw>>24)&0xFF);
        fprintf(stderr,"  [dbg] bc=%u x=%08X l=%04X h=%04X e=%08X rw=%08X\\n",bc,x,l,h,e,rw);
    }
    for(uint32_t i=0;i<bc;i++){
        uint32_t u=un[i], cp=(uint32_t)newLen[i];
        uint16_t fl=5;
        uint32_t xu=rol32(u^0xF74324EE,18);
        uint16_t cc=(uint16_t)(xu&0xFFFF), a=(uint16_t)(((xu>>16)&0xFFFF)^cc^0xA121);
        uint32_t xc=rol32(cp^0xF74324EE,18);
        uint16_t d=(uint16_t)(xc&0xFFFF), bb=(uint16_t)(((xc>>16)&0xFFFF)^d^0xA121);
        uint16_t xf=ror16((uint16_t)(fl^cc^0x523F),14);
        uint8_t p0=(uint8_t)(xf&0xFF), a1=(uint8_t)(((xf>>8)&0xFF)^p0);
        uint16_t ef=(uint16_t)(((uint16_t)((a1<<8)|p0))^0x9CD6);
        w16be(nbi+np,a); w16be(nbi+np+2,bb); w16be(nbi+np+4,cc);
        w16be(nbi+np+6,ef); w16be(nbi+np+8,d);
        np+=10;
    }
    /* ③ 节点表：原样复制 */
    long nodeBytes=biLen-nodesOff;
    memcpy(nbi+np,biPlain+nodesOff,nodeBytes); np+=nodeBytes;
    printf("  新块信息混淆明文=%ld (原 %u)\n",np,uncompBI);

    /* ---- 压缩 + 加密 ---- */
    uint8_t*nbiC=malloc(np*2+65536);
    long nbiCLen=lz4_compress(nbi,np,nbiC,np*2+65536);
    printf("  块信息压缩=%ld (原 %u)\n",nbiCLen,compBI);
    /* ★ pad 模式：给【未压缩】块信息追加填充，直到压缩后恰好 = 原 compBI
     *   原理：不能对压缩流零填充（0x00 是非法 token：offset=0）
     *   改为在【未压缩】数据末尾追加垃圾（节点表之后会被解析器忽略）
     *   ⇒ 压缩后长度可精确命中原 compBI ⇒ dataStart 完全不变 */
    int padmode = (c>=6 && strcmp(v[5],"pad")==0);
    long compBIOut = nbiCLen;
    long npOut = np;                       /* ★ 头部 uncompBI 用【填充后】的大小 */
    if(padmode && nbiCLen < (long)compBI){
        uint8_t*big=malloc(np+65536);
        memcpy(big,nbi,np);
        for(long extra=1; extra<=64000; extra++){
            big[np+extra-1]=0x00;
            long l=lz4_compress(big,np+extra,nbiC,np*2+131072);
            if(l==(long)compBI){
                nbiCLen=l; compBIOut=l; npOut=np+extra;
                printf("  [pad] 未压缩 %ld → %ld（+%ld）⇒ 压缩后 = 原 compBI %u，"
                       "dataStart 不变，uncompBI 写 %ld\n",
                       np,np+extra,extra,compBI,npOut);
                break;
            }
            if(l>(long)compBI) break;
        }
        free(big);
    }
    vfs_decrypt_block(nbiC,(size_t)nbiCLen);
    long nbiStored=(nbiCLen+15)/16*16;

    /* ---- 组装 ---- */
    long outLen=48+nbiStored+newTotal;
    /* 最小改动模式：零填充到【原长】，使 .blc 的 off/len 无需改动 */
    long finalLen = (minimal && outLen<rn) ? rn : outLen;
    uint8_t*out=calloc(1,finalLen);
    memcpy(out,raw,48);
    memcpy(out+48,nbiC,nbiCLen);
    memset(out+48+nbiCLen,0,nbiStored-nbiCLen);
    long wp=48+nbiStored;
    for(uint32_t i=0;i<bc;i++){
        /* ★ 关键：只有【新压缩】的块需要加密；原样复制的块已是加密态 */
        if(!copied[i]) vfs_decrypt_block(newBlk[i],(size_t)newLen[i]);
        memcpy(out+wp,newBlk[i],newLen[i]);
        wp+=newLen[i];
    }
    if(finalLen>outLen) printf("  零填充 %ld 字节 → 总长 %ld（与原长一致）\n",finalLen-outLen,finalLen);

    /* ---- 更新头部 compBI / uncompBI ---- */
    {   /* compBI → cBI2(@8) 与 cBI1(@38)，用 0xA121 */
        uint32_t x=rol32((uint32_t)nbiCLen^0xF74324EE,18);
        uint16_t l=(uint16_t)(x&0xFFFF), h=(uint16_t)(((x>>16)&0xFFFF)^l^0xA121);
        w16be(out+8,l); w16be(out+38,h);
    }
    {   /* uncompBI → uBI1(@26) 与 uBI2(@32) */
        uint32_t x=rol32((uint32_t)npOut^0xF74324EE,18);
        uint16_t l=(uint16_t)(x&0xFFFF), h=(uint16_t)(((x>>16)&0xFFFF)^l^0xA121);
        w16be(out+26,h); w16be(out+32,l);
    }
    FILE*of=fopen(v[2],"wb"); fwrite(out,1,finalLen,of); fclose(of);
    printf("  ✓ 写出 %s: %ld 字节 (原 %ld)%s\n",v[2],finalLen,rn,finalLen==rn?"  ★ 总长不变":"");
    return 0;
}
