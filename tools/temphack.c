/* temphack —— 循环扫描 Temp 目录，把高熵纹理块替换成 mod 数据
 *   用法: temphack <tempdir> <modfile> [minMB] [interval_ms]
 *   原理:
 *     · 游戏启动时把 .chk 解压到 Temp/<uuid>，然后 mmap（MAP_SHARED）
 *     · 文件写入 ⇒ 页缓存更新 ⇒ 游戏映射内存立即反映
 *     · 本工具循环: 找大文件 → 逐 256KB 块算熵 → 熵>7.0 的块替换成 mod 数据
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#define BLK 262144
static uint8_t* g_mod=0; static long g_modn=0;

static double ent(const uint8_t*p,size_t n){
    size_t c[256]={0}; for(size_t i=0;i<n;i++) c[p[i]]++;
    double e=0; for(int i=0;i<256;i++) if(c[i]){ double q=(double)c[i]/n; e-=q*log2(q);} return e;
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
        if(ent(buf,BLK) < ment) continue;
        if(fseek(f,off,SEEK_SET)!=0) break;
        if(fwrite(g_mod,1,g_modn,f)!=(size_t)g_modn) break;
        hits++;
    }
    fflush(f); fclose(f); free(buf);
    return (int)hits;
}
int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,"用法: temphack <tempdir> <modfile> [minMB=8] [interval_ms=200]\n"); return 2; }
    const char* dir=argv[1];
    FILE*m=fopen(argv[2],"rb"); if(!m){perror(argv[2]);return 1;}
    fseek(m,0,SEEK_END); g_modn=ftell(m); fseek(m,0,SEEK_SET);
    g_mod=malloc(g_modn);
    if(fread(g_mod,1,g_modn,m)!=(size_t)g_modn){ fclose(m); return 1; }
    fclose(m);
    int interval = argc>4? atoi(argv[4]) : 200;
    printf("=== temphack: dir=%s mod=%ld 字节 interval=%dms ===\n",dir,g_modn,interval);
    fflush(stdout);
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
                int h=hack_file(p,7.0);
                if(h>0){ total+=h; printf("  [%ld] %s: 替换 %d 块\n",round,e->d_name,h); fflush(stdout); }
            }
            closedir(d);
        }
        if(round%50==0){ printf("  ...轮 %ld, 累计替换 %ld 块\n",round,total); fflush(stdout); }
        usleep(interval*1000);
    }
    return 0;
}
