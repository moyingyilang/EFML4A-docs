/* texguard —— 守护进程：只替换【确认是 BC7 纹理】的 256KB 块
 *   用法: texguard <dir> <modfile> [interval_ms=200] [minent=7.0]
 *
 * ★ 判据（基于实测校准，见 docs/47）:
 *   ① 熵 > minent                 —— 排除明文/结构化数据
 *   ② BC7 合法 mode 率 >= 50%     —— 每 16 字节块的首字节须是合法 mode
 *   ③ 高质量模式集中              —— (m6+m7)/total > 0.40
 *   ④ 低质量模式稀少              —— (m0+m1)/total < 0.05
 *
 * 实测校准:
 *   真 BC7 纹理 (mod):  m0=0 m1=0 m2=0 m3=0 m4=0 m5=622 m6=4905 m7=3433
 *                       ⇒ hi=0.509 lo=0.000  ⇒ ★ 判定为纹理
 *   浮点几何数据:       m0=547 m1=3658 m2=1630 ... m6=228 m7=110
 *                       ⇒ hi=0.021 lo=0.257  ⇒ 判定为非纹理
 *   ⇒ 判据③④能可靠区分【真 BC7 纹理】与【浮点/随机数据】
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#define BLK 262144

static uint8_t* g_mod=0; static long g_modn=0;

static double ent(const uint8_t*p,size_t n){
    size_t c[256]={0}; for(size_t i=0;i<n;i++) c[p[i]]++;
    double e=0; for(int i=0;i<256;i++) if(c[i]){ double q=(double)c[i]/n; e-=q*log2(q);} return e;
}
/* ★ 核心判据：像真 BC7 纹理吗？ */
static int is_bc7_texture(const uint8_t*p, long n, double* hi_out, double* lo_out){
    long blks=n/16; if(blks<=0) return 0;
    long m[8]={0}; long valid=0;
    for(long i=0;i<blks;i++){
        uint8_t b=p[i*16]; int md=-1;
        if(b==0x00) md=0;
        else if((b&0x03)==0x01) md=1;
        else if((b&0x07)==0x02) md=2;
        else if((b&0x0F)==0x04) md=3;
        else if((b&0x1F)==0x08) md=4;
        else if((b&0x3F)==0x10) md=5;
        else if((b&0x7F)==0x20) md=6;
        else if(b==0x40) md=7;
        if(md>=0){ valid++; m[md]++; }
    }
    double hi=(double)(m[6]+m[7])/blks;
    double lo=(double)(m[0]+m[1])/blks;
    if(hi_out)*hi_out=hi;
    if(lo_out)*lo_out=lo;
    if(valid < blks/2) return 0;          /* 判据② */
    return (hi > 0.40 && lo < 0.05) ? 1 : 0;   /* 判据③④ */
}
static int hack_file(const char* path, double ment){
    FILE*f=fopen(path,"r+b"); if(!f) return 0;
    fseek(f,0,SEEK_END); long n=ftell(f);
    if(n < 8L*1024*1024){ fclose(f); return 0; }
    uint8_t* buf=malloc(BLK);
    long hits=0;
    for(long off=0; off+BLK<=n; off+=BLK){
        if(fseek(f,off,SEEK_SET)!=0) break;
        if(fread(buf,1,BLK,f)!=(size_t)BLK) break;
        double e=ent(buf,BLK);
        if(e < ment) continue;                          /* 判据① */
        double hi=0, lo=0;
        if(!is_bc7_texture(buf,BLK,&hi,&lo)) continue;  /* 判据②③④ */
        if(fseek(f,off,SEEK_SET)!=0) break;
        long w = g_modn<BLK? g_modn:BLK;
        if(fwrite(g_mod,1,w,f)!=(size_t)w) break;
        printf("    @%ld 熵=%.3f hi=%.3f lo=%.3f ★替换\n",off,e,hi,lo); fflush(stdout);
        hits++;
    }
    fflush(f); fclose(f); free(buf);
    return (int)hits;
}
int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,"用法: texguard <dir> <modfile> [interval_ms=200] [minent=7.0]\n"); return 2; }
    const char* dir=argv[1];
    int interval = argc>3? atoi(argv[3]) : 200;
    double ment = argc>4? atof(argv[4]) : 7.0;
    FILE*m=fopen(argv[2],"rb"); if(!m){perror(argv[2]);return 1;}
    fseek(m,0,SEEK_END); g_modn=ftell(m); fseek(m,0,SEEK_SET);
    g_mod=malloc(g_modn);
    if(fread(g_mod,1,g_modn,m)!=(size_t)g_modn){ fclose(m); return 1; }
    fclose(m);
    printf("=== texguard: dir=%s mod=%ld interval=%dms minent=%.2f ===\n",dir,g_modn,interval,ment);
    double hi=0,lo=0;
    int ok=is_bc7_texture(g_mod,g_modn,&hi,&lo);
    printf("  mod 基准: 熵=%.3f hi(m6+m7)=%.3f lo(m0+m1)=%.3f ⇒ %s\n",
           ent(g_mod,g_modn), hi, lo, ok? "★ 是 BC7 纹理":"✗ 判定失败!");
    fflush(stdout);
    if(!ok){ fprintf(stderr,"  ✗ mod 文件未通过判据，退出\n"); return 1; }
    long round=0, total=0;
    for(;;){
        round++;
        DIR*d=opendir(dir);
        if(d){
            struct dirent*e;
            while((e=readdir(d))){
                if(e->d_name[0]=='.') continue;
                char p[1024];
                snprintf(p,sizeof p,"%s/%s",dir,e->d_name);
                struct stat st;
                if(stat(p,&st)!=0) continue;
                if(!S_ISREG(st.st_mode)) continue;
                if(st.st_size < 8L*1024*1024) continue;
                int h=hack_file(p,ment);
                if(h>0){ total+=h; printf("  [%ld] %s: 替换 %d 块\n",round,e->d_name,h); fflush(stdout); }
            }
            closedir(d);
        }
        if(round%100==0){ printf("  ...轮 %ld, 累计 %ld 块\n",round,total); fflush(stdout); }
        usleep(interval*1000);
    }
    return 0;
}
