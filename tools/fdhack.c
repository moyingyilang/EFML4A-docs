#include <fcntl.h>
/* fdhack —— 通过 /proc/<pid>/fd 修改游戏的 Temp 文件（已 unlink 的）
 *   用法: fdhack <pid> <modfile> [interval_ms=200] [minent=7.0]
 *
 * ★ 原理: 游戏创建 Temp 文件后【立即 unlink】，磁盘上看不到，
 *         但 fd 仍有效 ⇒ 必须通过 /proc/<pid>/fd/<N> 访问
 *
 * 判据（避开几何数据）: 熵>7.0 且 (m6+m7)>0.40 且 (m0+m1)<0.05
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
static int is_bc7(const uint8_t*p,long n,double*hi_o,double*lo_o){
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
    double hi=(double)(m[6]+m[7])/blks, lo=(double)(m[0]+m[1])/blks;
    if(hi_o)*hi_o=hi; if(lo_o)*lo_o=lo;
    if(valid < blks/2) return 0;
    return (hi>0.40 && lo<0.05)?1:0;
}
static int hack_fd(const char* path, double ment){
    int fd=open(path,O_RDWR);
    if(fd<0) return 0;
    off_t n=lseek(fd,0,SEEK_END);
    if(n < 8L*1024*1024){ close(fd); return 0; }
    uint8_t* buf=malloc(BLK);
    long hits=0;
    for(long off=0; off+BLK<=n; off+=BLK){
        if(pread(fd,buf,BLK,off)!=(ssize_t)BLK) break;
        double e=ent(buf,BLK);
        if(e<ment) continue;
        double hi=0,lo=0;
        if(!is_bc7(buf,BLK,&hi,&lo)) continue;
        if(pwrite(fd,g_mod,g_modn<BLK?g_modn:BLK,off)!=(ssize_t)(g_modn<BLK?g_modn:BLK)) break;
        printf("    @%ld 熵=%.3f hi=%.3f lo=%.3f ★替换\n",off,e,hi,lo); fflush(stdout);
        hits++;
    }
    close(fd); free(buf);
    return hits;
}
int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,"用法: fdhack <pid> <modfile> [interval_ms=200] [minent=7.0]\n"); return 2; }
    int pid=atoi(argv[1]);
    int interval=argc>3?atoi(argv[3]):200;
    double ment=argc>4?atof(argv[4]):7.0;
    FILE*m=fopen(argv[2],"rb"); if(!m){perror(argv[2]);return 1;}
    fseek(m,0,SEEK_END); g_modn=ftell(m); fseek(m,0,SEEK_SET);
    g_mod=malloc(g_modn); if(fread(g_mod,1,g_modn,m)!=(size_t)g_modn) return 1; fclose(m);
    printf("=== fdhack: pid=%d mod=%ld interval=%d minent=%.2f ===\n",pid,g_modn,interval,ment);
    double hi=0,lo=0;
    printf("  mod 基准: 熵=%.3f hi=%.3f lo=%.3f ⇒ %s\n",ent(g_mod,g_modn),hi,lo,
           is_bc7(g_mod,g_modn,&hi,&lo)?"★ BC7 纹理":"✗");
    fflush(stdout);
    char fddir[64]; snprintf(fddir,sizeof fddir,"/proc/%d/fd",pid);
    long round=0,total=0;
    for(;;){
        round++;
        DIR*d=opendir(fddir);
        if(!d){
            printf("  ✗ 无法打开 %s（进程可能已退出）\n",fddir); fflush(stdout);
            sleep(2); continue;
        }
        struct dirent*e;
        while((e=readdir(d))){
            if(e->d_name[0]=='.') continue;
            char link[512], path[600];
            snprintf(path,sizeof path,"%s/%s",fddir,e->d_name);
            ssize_t k=readlink(path,link,sizeof link-1);
            if(k<=0) continue;
            link[k]=0;
            if(!strstr(link,"Temp/")) continue;
            int h=hack_fd(path,ment);
            if(h>0){ total+=h; printf("  [%ld] fd %s (%s): 替换 %d 块\n",round,e->d_name,link,h); fflush(stdout); }
        }
        closedir(d);
        if(round%100==0){ printf("  ...轮 %ld, 累计 %ld 块\n",round,total); fflush(stdout); }
        usleep(interval*1000);
    }
    return 0;
}
