/* xxe1 —— XXE1 = ChaCha20（纯 C）解/加密 VFS 内容
 *   用法:
 *     xxe1 dec <in> <out> <key_hex> <nonce_hex> [counter]
 *     xxe1 enc <in> <out> <key_hex> <nonce_hex> [counter]
 *     xxe1 try <in> <key_hex> <nonce_hex>    仅试解并打印前 64 字节（不解密到文件）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint32_t rotl32(uint32_t x,int n){return (x<<n)|(x>>(32-n));}
static void qr(uint32_t*s,int a,int b,int c,int d){
 s[a]+=s[b];s[d]^=s[a];s[d]=rotl32(s[d],16);s[c]+=s[d];s[b]^=s[c];s[b]=rotl32(s[b],12);
 s[a]+=s[b];s[d]^=s[a];s[d]=rotl32(s[d],8);s[c]+=s[d];s[b]^=s[c];s[b]=rotl32(s[b],7);}
static void cblk(const uint8_t k[32],const uint8_t n[12],uint32_t c,uint8_t o[64]){
 uint32_t st[16],w[16];st[0]=0x61707865;st[1]=0x3320646e;st[2]=0x79622d32;st[3]=0x6b206574;
 for(int i=0;i<8;i++)st[4+i]=k[i*4]|(k[i*4+1]<<8)|(k[i*4+2]<<16)|((uint32_t)k[i*4+3]<<24);
 st[12]=c;for(int i=0;i<3;i++)st[13+i]=n[i*4]|(n[i*4+1]<<8)|(n[i*4+2]<<16)|((uint32_t)n[i*4+3]<<24);
 memcpy(w,st,sizeof w);
 for(int i=0;i<10;i++){qr(w,0,4,8,12);qr(w,1,5,9,13);qr(w,2,6,10,14);qr(w,3,7,11,15);
  qr(w,0,5,10,15);qr(w,1,6,11,12);qr(w,2,7,8,13);qr(w,3,4,9,14);}
 for(int i=0;i<16;i++){uint32_t v=w[i]+st[i];o[i*4]=v&0xff;o[i*4+1]=(v>>8)&0xff;o[i*4+2]=(v>>16)&0xff;o[i*4+3]=(v>>24)&0xff;}}
static int hx(const char*h,uint8_t*o,int n){ if((int)strlen(h)!=n*2)return -1;
 for(int i=0;i<n;i++){unsigned v;if(sscanf(h+i*2,"%2x",&v)!=1)return -1;o[i]=v;} return 0;}
static void xf(const uint8_t k[32],const uint8_t n[12],uint32_t ctr,uint8_t*b,size_t len){
 uint8_t ks[64];uint32_t c=ctr;size_t off=0;int first=1;
 while(off<len){ cblk(k,n,c++,ks);
   /* ChaCha20：首块保留 64 字节（counter 块本身不参与），此处按 RFC7539 标准用法 */
   size_t m=(len-off<64)?(len-off):64;
   for(size_t i=0;i<m;i++)b[off+i]^=ks[i];
   off+=m; first=0; }}
int main(int argc,char**argv){
 if(argc<5){fprintf(stderr,
  "用法:\n  xxe1 dec <in> <out> <key_hex> <nonce_hex> [counter]\n"
  "  xxe1 enc <in> <out> <key_hex> <nonce_hex> [counter]\n"
  "  xxe1 try <in> <key_hex> <nonce_hex> [counter]\n");return 2;}
 const char*cmd=argv[1],*fin=argv[2];
 uint8_t key[32],nonce[12];
 if(!strcmp(cmd,"try")){ fin=argv[2];
   if(hx(argv[3],key,32)||hx(argv[4],nonce,12)){fprintf(stderr,"key 需 64 hex，nonce 需 24 hex\n");return 2;} }
 else { if(hx(argv[4],key,32)||hx(argv[5],nonce,12)){fprintf(stderr,"key 需 64 hex，nonce 需 24 hex\n");return 2;} }
 uint32_t ctr = argc>6? (uint32_t)strtoul(argv[6],0,0) : (argc>5&&!strcmp(cmd,"try")? (uint32_t)strtoul(argv[5],0,0):1);
 FILE*f=fopen(fin,"rb"); if(!f){perror(fin);return 1;}
 fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
 uint8_t*b=malloc(n); if(fread(b,1,n,f)!=(size_t)n){fclose(f);return 1;} fclose(f);
 xf(key,nonce,ctr,b,n);
 if(!strcmp(cmd,"try")){
   printf("  counter=%u  解密后前 64 字节:\n",ctr);
   for(int i=0;i<64 && i<n;i+=16){ printf("  +%04x  ",i);
     for(int k=0;k<16&&i+k<n;k++) printf("%02x ",b[i+k]);
     printf(" |"); for(int k=0;k<16&&i+k<n;k++){uint8_t c=b[i+k];putchar(c>=32&&c<127?c:'.');} printf("|\n"); }
 } else {
   FILE*g=fopen(argv[3],"wb"); if(!g){perror(argv[3]);return 1;}
   fwrite(b,1,n,g); fclose(g);
   printf("  ⇒ 写出 %s (%ld 字节)\n",argv[3],n);
 }
 free(b); return 0;
}
