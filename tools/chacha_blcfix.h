/* ChaCha20（提取自 blcfix.c，已验证 CRC 通过）
 * counter 从 1 开始，nonce = .blc 前 12 字节 */
#ifndef CHACHA_VFS_H
#define CHACHA_VFS_H
static void chacha_block_vfs(const uint8_t key[32],const uint8_t nonce[12],uint32_t cnt,uint8_t out[64]){
    uint32_t st[16], w[16];
    st[0]=0x61707865; st[1]=0x3320646e; st[2]=0x79622d32; st[3]=0x6b206574;
    for(int i=0;i<8;i++) st[4+i]=key[i*4]|(key[i*4+1]<<8)|(key[i*4+2]<<16)|((uint32_t)key[i*4+3]<<24);
    st[12]=cnt;
    for(int i=0;i<3;i++) st[13+i]=nonce[i*4]|(nonce[i*4+1]<<8)|(nonce[i*4+2]<<16)|((uint32_t)nonce[i*4+3]<<24);
    memcpy(w,st,sizeof w);
    for(int i=0;i<10;i++){
        qr(w,0,4,8,12);  qr(w,1,5,9,13);  qr(w,2,6,10,14); qr(w,3,7,11,15);
        qr(w,0,5,10,15); qr(w,1,6,11,12); qr(w,2,7,8,13);  qr(w,3,4,9,14);
    }
    for(int i=0;i<16;i++){
        uint32_t v=w[i]+st[i];
        out[i*4]=v&0xff; out[i*4+1]=(v>>8)&0xff; out[i*4+2]=(v>>16)&0xff; out[i*4+3]=(v>>24)&0xff;
    }
}
static void chacha_xor_vfs(const uint8_t key[32],const uint8_t nonce[12],uint8_t*buf,size_t n){
    uint8_t ks[64]; uint32_t c=1; size_t off=0;
    while(off<n){
        chacha_block_vfs(key,nonce,c++,ks);
        size_t m=(n-off<64)?(n-off):64;
        for(size_t i=0;i<m;i++) buf[off+i]^=ks[i];
        off+=m;
    }
}
#endif
