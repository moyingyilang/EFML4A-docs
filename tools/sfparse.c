/* sfparse —— Unity SerializedFile 解析器（找 Texture2D）
 *   用法: sfparse <file.ab> [--types]
 *   Unity 2021.3 格式（version 17/21/22）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t* g; static long gN, gP;
static int be_flag = 1;

static uint32_t r32(void){
    uint32_t v;
    if(be_flag){ v=(g[gP]<<24)|(g[gP+1]<<16)|(g[gP+2]<<8)|g[gP+3]; }
    else { v=g[gP]|(g[gP+1]<<8)|(g[gP+2]<<16)|((uint32_t)g[gP+3]<<24); }
    gP+=4; return v;
}
static uint16_t r16(void){
    uint16_t v;
    if(be_flag){ v=(g[gP]<<8)|g[gP+1]; } else { v=g[gP]|(g[gP+1]<<8); }
    gP+=2; return v;
}
static uint8_t r8(void){ return g[gP++]; }
static uint64_t r64(void){
    uint64_t lo=r32(), hi=r32();
    return be_flag? ((hi<<32)|lo) : ((lo<<32)|hi);
}
static void align(int n){ while(gP % n) gP++; }
static char* rstr(void){
    static char buf[512]; int k=0;
    while(gP<gN && g[gP] && k<511) buf[k++]=g[gP++];
    buf[k]=0; if(gP<gN) gP++;
    return buf;
}
/* 常见 Unity classID */
static const char* classname(int id){
    switch(id){
        case 1: return "GameObject";
        case 4: return "Transform";
        case 21: return "Material";
        case 28: return "Texture2D";
        case 43: return "Mesh";
        case 48: return "Shader";
        case 49: return "TextAsset";
        case 74: return "AnimationClip";
        case 83: return "AudioClip";
        case 114: return "MonoBehaviour";
        case 115: return "MonoScript";
        case 128: return "Font";
        case 142: return "AssetBundle";
        case 213: return "Sprite";
        case 222: return "CanvasRenderer";
        default: return "?";
    }
}
int main(int c,char**v){
    if(c<2){ fprintf(stderr,"用法: sfparse <file> [--types]\n"); return 2; }
    FILE*f=fopen(v[1],"rb"); if(!f){perror(v[1]);return 1;}
    fseek(f,0,SEEK_END); gN=ftell(f); fseek(f,0,SEEK_SET);
    g=malloc(gN); if(fread(g,1,gN,f)!=(size_t)gN) return 1; fclose(f);
    printf("  文件 %ld 字节\n", gN);
    gP=0;
    be_flag=1;
    uint32_t metaSize=r32(), fileSize=r32(), version=r32(), dataOff=r32();
    uint8_t endian=r8(); gP+=3;
    printf("  SerializedFile: metaSize=%u fileSize=%u version=%u dataOffset=%u endian=%u\n",
           metaSize, fileSize, version, dataOff, endian);
    be_flag = (endian==0)? 0 : 1;   /* 0 = little */
    if(version>=22){
        gP=0;
        be_flag=1;
        metaSize=r32(); fileSize=r32(); version=r32(); dataOff=r32();
        /* v22: 64-bit fields */
        uint32_t d1=r32(), d2=r32();
        (void)d1;(void)d2;
    }
    char ubuf[64]; int k=0;
    while(gP<gN && g[gP] && k<63) ubuf[k++]=g[gP++];
    ubuf[k]=0; if(gP<gN) gP++;
    while(gP%4) gP++;
    printf("  Unity 版本: \"%s\"\n", ubuf);
    int32_t platform = (int32_t)r32();
    uint8_t typeTree = r8();
    printf("  platform=%d enableTypeTree=%u\n", platform, typeTree);
    int32_t typeCount = (int32_t)r32();
    printf("  类型数: %d\n", typeCount);
    if(typeCount<0||typeCount>10000){ printf("  ✗ 不合理 ⇒ 可能不是 SerializedFile\n"); return 1; }
    for(int i=0;i<typeCount;i++){
        int32_t classID=(int32_t)r32();
        uint8_t stripped=r8();
        int16_t scriptIdx=r16();
        if(version>=21 && version<22) { /* 可能还有 scriptTypeIndex */ }
        printf("    类型%2d: classID=%-5d (%-16s) stripped=%u scriptIdx=%d\n",
               i, classID, classname(classID), stripped, scriptIdx);
        if(typeTree){
            /* 跳过 TypeTree（需要递归解析节点数） */
            int32_t nodeCount=(int32_t)r32();
            int32_t strBufSize=(int32_t)r32();
            printf("            TypeTree 节点数=%d 字符串大小=%d\n", nodeCount, strBufSize);
            if(nodeCount<0||nodeCount>100000){ printf("  ✗ TypeTree 不合理\n"); return 1; }
            /* 每个节点 32 字节 (v<19) 或含更多字段 */
            for(int n=0;n<nodeCount;n++){
                /* v17: version(u16) level(u8) typeFlags(u8) typeStrOff(u32) nameStrOff(u32)
                        byteSize(i32) index(i32) metaFlag(i32) = 24 字节 */
                gP += 24;
            }
            gP += strBufSize;
        }
    }
    int32_t objCount=(int32_t)r32();
    printf("  对象数: %d\n", objCount);
    if(objCount<0||objCount>1000000){ printf("  ✗ 对象数不合理\n"); return 1; }
    align(4);
    int texFound=0;
    for(int i=0;i<objCount;i++){
        align(4);
        uint64_t pathID=r64();
        uint32_t byteStart=r32(), byteSize=r32();
        int32_t typeID=(int32_t)r32();
        int16_t classID=r16();
        if(version<17){ /* v<17 无 classID */ }
        if(i<80 || classID==28){
            printf("    对象%3d: pathID=%llu byteStart=%u byteSize=%u typeID=%d classID=%d (%s)%s\n",
                   i,(unsigned long long)pathID,byteStart,byteSize,typeID,classID,
                   classname(classID), classID==28?" ★Texture2D":"");
            if(classID==28) texFound++;
        }
    }
    printf("\n  ⇒ Texture2D 对象数: %d\n", texFound);
    free(g);
    return 0;
}
