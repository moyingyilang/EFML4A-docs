/* astcenc_dl —— 通过 dlopen 调用 astcenc（从 astc_encoder 的 .so）
 *   用法: astcenc_dl <in.rgba> <out.astc> <w> <h> [blockDim 4|6]
 *   in.rgba: 原始 RGBA8 数据（w*h*4 字节）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>

/* astcenc 4.x 结构体（按头文件布局，均为 uint32 字段） */
typedef struct {
    uint32_t profile;
    uint32_t block_x, block_y, block_z;
    float quality;
    uint32_t flags;
    uint32_t tune_costs;
    uint32_t tune_db;
    uint32_t flags2;
} cfg_t;
typedef struct { uint32_t dim_x, dim_y, dim_z; uint32_t data_type; uint8_t* data[1]; } img_t;
typedef struct { uint32_t x,y,z,w; } swz_t;  /* ★ 枚举，不是 float */

typedef int (*fn_config_init)(uint32_t,uint32_t,uint32_t,uint32_t,float,uint32_t,void*);
typedef int (*fn_ctx_alloc)(void*,uint32_t,void**);
typedef int (*fn_compress)(void*,void*,void*,uint8_t*,size_t,uint32_t);
typedef void (*fn_ctx_free)(void*);
typedef const char* (*fn_errstr)(int);

int main(int c,char**v){
    if(c<5){ fprintf(stderr,"用法: astcenc_dl <in.rgba> <out.astc> <w> <h> [block=4]\n"); return 2; }
    int W=atoi(v[3]), H=atoi(v[4]), BD=c>5?atoi(v[5]):4;
    const char* SO="/data/data/com.termux/files/usr/lib/python3.14/site-packages/astc_encoder/_encoder_none.abi3.so";
    void* h=dlopen(SO, RTLD_NOW|RTLD_LOCAL);
    if(!h){ fprintf(stderr,"  ✗ dlopen 失败: %s\n", dlerror()); return 1; }
    fn_config_init config_init=(fn_config_init)dlsym(h,"_Z19astcenc_config_init15astcenc_profilejjjfjP14astcenc_config");
    fn_ctx_alloc ctx_alloc=(fn_ctx_alloc)dlsym(h,"_Z21astcenc_context_allocPK14astcenc_configjPP15astcenc_context");
    fn_compress compress=(fn_compress)dlsym(h,"_Z22astcenc_compress_imageP15astcenc_contextP13astcenc_imagePK15astcenc_swizzlePhmj");
    fn_ctx_free ctx_free=(fn_ctx_free)dlsym(h,"_Z20astcenc_context_freeP15astcenc_context");
    fn_errstr errstr=(fn_errstr)dlsym(h,"_Z24astcenc_get_error_string13astcenc_error");
    if(!config_init||!ctx_alloc||!compress){ fprintf(stderr,"  ✗ dlsym 失败\n"); return 1; }
    printf("  ✓ astcenc 符号解析成功\n");
    /* 读 RGBA */
    FILE*f=fopen(v[1],"rb"); if(!f){perror(v[1]);return 1;}
    size_t need=(size_t)W*H*4; uint8_t*px=malloc(need);
    if(fread(px,1,need,f)!=need){ fprintf(stderr,"  ✗ 输入不足 %zu 字节\n",need); return 1; }
    fclose(f);
    /* config: profile=LDR(1), block 4x4x1, quality=MEDIUM(60), flags=0 */
    uint8_t cfg[512]; memset(cfg,0,sizeof cfg);  /* ★ 大缓冲区 */
    int r=config_init(1, BD,BD,1, 60.0f, 0, cfg);
    printf("  config_init → %d\n", r);
    if(r!=0){ if(errstr) fprintf(stderr,"  err: %s\n", errstr(r)); return 1; }
    void* ctx=NULL;
    r=ctx_alloc(cfg, 1, &ctx);
    printf("  context_alloc → %d\n", r);
    if(r!=0) return 1;
    /* image */
    img_t img; memset(&img,0,sizeof img);
    img.dim_x=W; img.dim_y=H; img.dim_z=1; img.data_type=0/*U8*/; img.data[0]=px;
    swz_t swz={0,1,2,3};  /* R,G,B,A */
    size_t outSize=((W+BD-1)/BD)*((H+BD-1)/BD)*16;
    uint8_t*out=malloc(outSize);
    r=compress(ctx,&img,&swz,out,outSize,0);
    printf("  compress_image → %d, 输出 %zu 字节\n", r, outSize);
    if(r==0){
        FILE*g=fopen(v[2],"wb"); fwrite(out,1,outSize,g); fclose(g);
        printf("  ✓ 已写出 %s\n", v[2]);
    }
    if(ctx_free) ctx_free(ctx);
    free(px); free(out);
    return r==0?0:1;
}
