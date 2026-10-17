/* abverify —— 读回 .ab，输出完整流，可比对目标区 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
extern int vfs_decrypt_block(uint8_t*,size_t);
extern long lz4_decompress(const uint8_t*,long,uint8_t*,long);
extern long lz4inv_decompress(const uint8_t*,long,uint8_t*,long);
static uint32_t ror32(uint32_t x,int n){return (x>>n)|(x<<(32-n));}
static uint16_t rol16(uint16_t x,int n){return (uint16_t)((x<<n)|(x>>(16-n)));}
static uint32_t bs32(uint32_t x){return (x>>24)|((x>>8)&0xFF00)|((x<<8)&0xFF0000)|(x<<24);}
static uint32_t le32(const uint8_t*p){return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t be16(const uint8_t*p){return (p[0]<<8)|p[1];}
int main(int c,char**v){
    if(c<2){fprintf(stderr,"用法: abverify <in.ab> [out.stream] [target] [expect.bin]\n");return 2;}
    FILE*f=fopen(v[1],"rb"); if(!f){perror(v[1]);return 1;}
    fseek(f,0,SEEK_END); long rn=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*raw=malloc(rn); if(!raw||fread(raw,1,rn,f)!=(size_t)rn)return 1; fclose(f);
    uint16_t c2=be16(raw+8), u1=be16(raw+26), u2=be16(raw+32), c1=be16(raw+38);
    uint32_t compBI=ror32(((uint32_t)(c1^c2^0xA121)<<16)|c2,18)^0xF74324EE;
    uint32_t uncompBI=ror32(((uint32_t)(u1^u2^0xA121)<<16)|u2,18)^0xF74324EE;
    printf("  compBI=%u uncompBI=%u\n",compBI,uncompBI);
    if(compBI==0||compBI>rn){fprintf(stderr,"  ✗ compBI 越界\n");return 1;}
    uint8_t*bi=malloc(compBI); memcpy(bi,raw+48,compBI);
    vfs_decrypt_block(bi,compBI);
    uint8_t*bp=malloc(uncompBI+4096);
    long bl=lz4_decompress(bi,compBI,bp,uncompBI+4096);
    printf("  块信息明文=%ld (期望 %u)\n",bl,uncompBI);
    if(bl<=0){fprintf(stderr,"  ✗ 块信息解压失败\n");return 1;}
    uint32_t bcr=bs32(le32(bp)^0x8A7BF723);
    uint16_t blo=bcr&0xFFFF, bhi=(bcr>>16)&0xFFFF;
    uint32_t bc=ror32(((uint32_t)(blo^bhi)<<16)|blo,18)^0x91CE0A4F;
    printf("  blocksCount=%u\n",bc);
    if(bc==0||bc>1000){fprintf(stderr,"  ✗ blocksCount 不合理\n");return 1;}
    long p=4;
    long *co=malloc(sizeof(long)*bc), *un=malloc(sizeof(long)*bc);
    for(uint32_t i=0;i<bc;i++){
        uint16_t a=be16(bp+p),b2=be16(bp+p+2),cc=be16(bp+p+4),ef=be16(bp+p+6)^0x9CD6,d=be16(bp+p+8);
        p+=10;
        uint8_t a0=ef&0xFF,a1=ef>>8;
        uint16_t f2=((uint16_t)(a0^a1)<<8)|a0; f2=(uint16_t)(cc^rol16(f2,14)^0x523F);
        uint32_t u=((uint32_t)(a^cc^0xA121)<<16)|cc; u=ror32(u,18)^0xF74324EE;
        uint32_t cp=((uint32_t)(b2^d^0xA121)<<16)|d; cp=ror32(cp,18)^0xF74324EE;
        co[i]=cp; un[i]=u;
        if(i<3||i==29) printf("    块%u: comp=%u uncomp=%u flags=%u\n",i,cp,u,f2);
    }
    printf("  块表结束 @%ld, dataStart=%ld\n",p,48+((compBI+15)/16*16));
    long total=0; for(uint32_t i=0;i<bc;i++) total+=un[i];
    uint8_t*st=malloc(total); if(!st)return 1;
    long so=0,dso=48+((compBI+15)/16*16);
    for(uint32_t i=0;i<bc;i++){
        uint8_t*cp2=malloc(co[i]); memcpy(cp2,raw+dso,co[i]);
        vfs_decrypt_block(cp2,co[i]);
        long o=lz4inv_decompress(cp2,co[i],st+so,un[i]);
        if(o!=un[i]){fprintf(stderr,"  ✗ 块%u 解压 %ld != %ld\n",i,o,un[i]);return 1;}
        so+=o; dso+=co[i]; free(cp2);
    }
    printf("  完整流=%ld\n",so);
    if(c>=3){FILE*o=fopen(v[2],"wb");fwrite(st,1,so,o);fclose(o);printf("  ✓ 写出流 %s\n",v[2]);}
    if(c>=5){
        long t=atol(v[3]);
        FILE*e=fopen(v[4],"rb"); fseek(e,0,SEEK_END); long en=ftell(e); fseek(e,0,SEEK_SET);
        uint8_t*ed=malloc(en); if(fread(ed,1,en,e)!=(size_t)en)return 1; fclose(e);
        int ok=(t>=0)&&(t+en<=so)&&(memcmp(st+t,ed,en)==0);
        printf("  ★ 目标区(%ld,%ld) 匹配=%s\n",t,en,ok?"TRUE ✓":"FALSE ✗");
        if(!ok&&t+en<=so) for(long i=0;i<en;i++) if(st[t+i]!=ed[i]){
            printf("    首个差异 @%ld: 流=%02X 期望=%02X\n",i,st[t+i],ed[i]); break; }
    }
    return 0;
}
