/* mempatch —— 运行时内存补丁（磁盘不改，校验不触发）
 *   用法: mempatch <pid> <findHex> <replaceHex> [maxHits] [threads]
 *
 * 原理: 用 /proc/<pid>/mem 搜索模式并原地替换
 *       ⇒ 磁盘文件始终原始 ⇒ 完整性校验永不失败
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define MAXMAPS 65536
typedef struct { unsigned long start, end; int rw; } Map;
static Map g_maps[MAXMAPS];
static int g_nmaps = 0;

typedef struct { uint8_t *pat; uint8_t *rep; size_t plen; int hit; unsigned long addr; } Task;
static Task g_task;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_maxhits = 8;
static int g_found = 0;

static int parse_maps(const char *path){
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    g_nmaps = 0;
    while (fgets(line, sizeof line, f) && g_nmaps < MAXMAPS){
        unsigned long s, e; char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perms) != 3) continue;
        if (!strchr(perms, 'r')) continue;
        g_maps[g_nmaps].start = s;
        g_maps[g_nmaps].end = e;
        g_maps[g_nmaps].rw = (perms[1] == 'w');
        g_nmaps++;
    }
    fclose(f);
    return g_nmaps;
}

static void *worker(void *arg){
    long idx = (long)arg;
    int fd = open("/proc/self/mem", O_RDWR | O_SYNC);
    if (fd < 0) return NULL;
    for (int i = (int)idx; i < g_nmaps; i += 32){
        Map *m = &g_maps[i];
        if (!m->rw) continue;                 /* 只补可写区 */
        unsigned long len = m->end - m->start;
        if (len < g_task.plen || len > (1UL<<31)) continue;
        uint8_t *buf = malloc(len);
        if (!buf) continue;
        if (pread(fd, buf, len, (off_t)m->start) != (ssize_t)len){ free(buf); continue; }
        for (unsigned long off = 0; off + g_task.plen <= len; off++){
            if (memcmp(buf + off, g_task.pat, g_task.plen) == 0){
                pthread_mutex_lock(&g_lock);
                if (g_found < g_maxhits){
                    unsigned long a = m->start + off;
                    printf("  HIT 0x%lx (map 0x%lx+0x%lx)\n", a, m->start, off);
                    if (g_task.rep){
                        if (pwrite(fd, g_task.rep, g_task.plen, (off_t)a) == (ssize_t)g_task.plen)
                            printf("       → 已替换 %zu 字节\n", g_task.plen);
                        else
                            printf("       → 替换失败 (errno)\n");
                    }
                    g_found++;
                }
                pthread_mutex_unlock(&g_lock);
            }
        }
        free(buf);
    }
    close(fd);
    return NULL;
}

static int hex2bin_(const char *h, uint8_t *o, size_t n){
    for (size_t i = 0; i < n; i++){
        unsigned x = 0;
        if (sscanf(h + i*2, "%2x", &x) != 1) return -1;
        o[i] = (uint8_t)x;
    }
    return 0;
}

int main(int c, char **v){
    if (c < 3){
        fprintf(stderr,
          "用法: mempatch <pid> <findHex> [replaceHex] [maxHits] [threads]\n"
          "  例: mempatch 1234 aabbccdd eeff0011 8\n"
          "  ⇒ 在目标进程可写内存中找 findHex，替换为 replaceHex\n");
        return 2;
    }
    int pid = atoi(v[1]);
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/maps", pid);
    if (parse_maps(path) <= 0){ fprintf(stderr, "无法读 %s\n", path); return 1; }

    size_t plen = strlen(v[2]) / 2;
    if (plen == 0 || plen > 4096){ fprintf(stderr, "模式长度非法\n"); return 1; }
    g_task.pat = malloc(plen);
    if (hex2bin_(v[2], g_task.pat, plen) != 0){ fprintf(stderr, "findHex 非法\n"); return 1; }
    g_task.rep = NULL;
    if (c >= 4 && strlen(v[3]) >= plen*2){
        g_task.rep = malloc(plen);
        if (hex2bin_(v[3], g_task.rep, plen) != 0){ fprintf(stderr, "replaceHex 非法\n"); return 1; }
    }
    g_task.plen = plen;
    if (c >= 5) g_maxhits = atoi(v[4]);
    int nth = (c >= 6) ? atoi(v[5]) : 8;
    if (nth < 1) nth = 1;
    if (nth > 32) nth = 32;

    printf("  PID=%d maps=%d 模式=%zuB 替换=%s 最大命中=%d 线程=%d\n",
           pid, g_nmaps, plen, g_task.rep ? "是" : "否(只搜索)", g_maxhits, nth);

    pthread_t th[32];
    for (int i = 0; i < nth; i++) pthread_create(&th[i], NULL, worker, (void*)(long)i);
    for (int i = 0; i < nth; i++) pthread_join(th[i], NULL);

    printf("  完成: 命中 %d 处\n", g_found);
    return g_found ? 0 : 1;
}
