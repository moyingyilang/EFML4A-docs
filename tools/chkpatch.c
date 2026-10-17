/* ============================================================================
 *  chkpatch —— 终末地 VFS .chk 的【等长】内容替换工具
 *
 *  为什么要「等长」：
 *    本项目实测结论（docs/26 §28.2）——
 *      同长度改内容 + .blc 不动          → ✅ 游戏确实采用（已铁证）
 *      改长度  + 重写 .blc(CRC/重加密)   → ❌ 失败
 *    因为 .blc 里记着每个文件的 {offset,len}，改长度就得重写索引，
 *    引入的变量太多。等长替换只动数据，索引与 CRC32 全都天然保持有效。
 *
 *  用法:
 *    chkpatch flip <x.chk> <offset> <len> [step] [xor]
 *        把 [offset, offset+len) 区间内每 step 字节异或 xor（默认 step=7 xor=0xA5）
 *        —— 用于验证「游戏是否读取该文件」：画面/音频会明显花掉
 *
 *    chkpatch splice <dst.chk> <dstOff> <src.chk> <srcOff> <len>
 *        把 src 的 [srcOff, srcOff+len) 抄到 dst 的 [dstOff, dstOff+len)（等长）
 *        —— 用于把 A 视频换成 B 视频（必须等长）
 *
 *    chkpatch hex <x.chk> <offset> <hexbytes>
 *        把一段十六进制字节写到 offset（等长，按 hex 长度）
 *
 *    chkpatch restore <x.chk>
 *        从 <x.chk>.orig 恢复
 *
 *  安全:
 *    · 每次写入前自动备份到 <x.chk>.orig（若已存在则不覆盖，保留最初版本）
 *    · 全部操作【等长】，文件总长度不变
 *    · 不改 .blc —— 索引/CRC32 天然继续有效
 *
 *  注意:
 *    ⚠️ 若要在设备上跑，请优先在 Termux 内生成结果再 cp 过去
 *       （Android 的 su -c root shell 里没有 python3/perl 等工具）
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

static long long fsize(const char* p){
    struct stat st; return stat(p,&st)==0 ? (long long)st.st_size : -1;
}
static unsigned char* slurp(const char* p, long long* n){
    FILE* f=fopen(p,"rb"); if(!f) return NULL;
    long long s=fsize(p); if(s<0){fclose(f);return NULL;}
    unsigned char* b=malloc(s?s:1); if(!b){fclose(f);return NULL;}
    if(fread(b,1,s,f)!=(size_t)s){free(b);fclose(f);return NULL;}
    fclose(f); *n=s; return b;
}
static int backup_once(const char* p){
    char bak[4096]; snprintf(bak,sizeof(bak),"%s.orig",p);
    if(access(bak,F_OK)==0){ printf("  备份已存在: %s（保留最初版本）\n",bak); return 0; }
    FILE* in=fopen(p,"rb"); if(!in){perror("open");return -1;}
    FILE* out=fopen(bak,"wb"); if(!out){fclose(in);perror("bak");return -1;}
    unsigned char buf[1<<20]; size_t n;
    while((n=fread(buf,1,sizeof(buf),in))>0) fwrite(buf,1,n,out);
    fclose(in); fclose(out);
    printf("  已备份: %s\n",bak);
    return 0;
}
static int wrange(const char* p, long long off, const unsigned char* d, long long n){
    FILE* f=fopen(p,"r+b"); if(!f){perror("r+b");return -1;}
    if(fseek(f,off,SEEK_SET)!=0){fclose(f);return -1;}
    if(fwrite(d,1,n,f)!=(size_t)n){fclose(f);perror("write");return -1;}
    fclose(f); return 0;
}
static int hexval(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }

int main(int argc, char** argv){
    if(argc<2){ fprintf(stderr,"用法见 chkpatch.c 头部注释\n"); return 2; }
    const char* cmd=argv[1];

    if(!strcmp(cmd,"flip")){
        if(argc<5){ fprintf(stderr,"用法: chkpatch flip <x.chk> <offset> <len> [step] [xor]\n"); return 2; }
        const char* p=argv[2];
        long long off=atoll(argv[3]), len=atoll(argv[4]);
        long long step=(argc>5)?atoll(argv[5]):7;
        unsigned xv=(argc>6)?(unsigned)strtoul(argv[6],NULL,0):0xA5;
        if(step<1) step=1;
        long long sz=fsize(p); if(sz<0){perror("stat");return 1;}
        if(off<0||len<=0||off+len>sz){ fprintf(stderr,"  区间越界 (文件 %lld, 请求 [%lld,%lld))\n",sz,off,off+len); return 1; }
        if(backup_once(p)) return 1;
        unsigned char* b=slurp(p,&sz); if(!b) return 1;
        long long n=0;
        for(long long i=off;i<off+len;i+=step){ b[i]^=(unsigned char)xv; n++; }
        int r=wrange(p,off,b+off,len);
        free(b);
        if(r) return 1;
        printf("  ✓ flip 完成: %s [%lld,%lld) step=%lld xor=0x%02X  异或 %lld 字节\n",p,off,off+len,step,xv,n);
        printf("    文件长度未变: %lld\n",fsize(p));
        return 0;
    }

    if(!strcmp(cmd,"splice")){
        if(argc<7){ fprintf(stderr,"用法: chkpatch splice <dst.chk> <dstOff> <src.chk> <srcOff> <len>\n"); return 2; }
        const char* dp=argv[2]; long long doff=atoll(argv[3]);
        const char* sp=argv[4]; long long soff=atoll(argv[5]);
        long long len=atoll(argv[6]);
        long long dsz=fsize(dp), ssz=fsize(sp);
        if(doff+len>dsz || soff+len>ssz){ fprintf(stderr,"  越界 (dst %lld, src %lld)\n",dsz,ssz); return 1; }
        if(backup_once(dp)) return 1;
        long long sn; unsigned char* sb=slurp(sp,&sn); if(!sb) return 1;
        int r=wrange(dp,doff,sb+soff,len);
        free(sb);
        if(r) return 1;
        printf("  ✓ splice 完成: %s[%lld,+%lld) <= %s[%lld,+%lld)  【等长】\n",dp,doff,len,sp,soff,len);
        printf("    dst 长度未变: %lld\n",fsize(dp));
        return 0;
    }

    if(!strcmp(cmd,"hex")){
        if(argc<5){ fprintf(stderr,"用法: chkpatch hex <x.chk> <offset> <hexbytes>\n"); return 2; }
        const char* p=argv[2]; long long off=atoll(argv[3]);
        const char* h=argv[4];
        size_t hl=strlen(h); if(hl%2){ fprintf(stderr,"  hex 长度须为偶数\n"); return 2; }
        long long n=hl/2, sz=fsize(p);
        if(off+n>sz){ fprintf(stderr,"  越界\n"); return 1; }
        unsigned char* d=malloc(n);
        for(long long i=0;i<n;i++){ int a=hexval(h[2*i]),b=hexval(h[2*i+1]);
            if(a<0||b<0){ fprintf(stderr,"  非法 hex\n"); free(d); return 2; } d[i]=(a<<4)|b; }
        if(backup_once(p)){ free(d); return 1; }
        int r=wrange(p,off,d,n); free(d);
        if(r) return 1;
        printf("  ✓ hex 写入完成: %s @%lld 共 %lld 字节（等长）\n",p,off,n);
        return 0;
    }

    if(!strcmp(cmd,"restore")){
        if(argc<3){ fprintf(stderr,"用法: chkpatch restore <x.chk>\n"); return 2; }
        char bak[4096]; snprintf(bak,sizeof(bak),"%s.orig",argv[2]);
        if(access(bak,F_OK)!=0){ fprintf(stderr,"  找不到备份 %s\n",bak); return 1; }
        FILE* in=fopen(bak,"rb"); if(!in){perror("bak");return 1;}
        FILE* out=fopen(argv[2],"wb"); if(!out){fclose(in);perror("out");return 1;}
        unsigned char buf[1<<20]; size_t n;
        while((n=fread(buf,1,sizeof(buf),in))>0) fwrite(buf,1,n,out);
        fclose(in); fclose(out);
        printf("  ✓ 已从 %s 恢复\n",bak);
        return 0;
    }

    fprintf(stderr,"未知命令: %s（可选 flip / splice / hex / restore）\n",cmd);
    return 2;
}
