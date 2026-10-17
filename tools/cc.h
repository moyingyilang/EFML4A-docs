#ifndef CC_H
#define CC_H
#include <stdint.h>
#include <string.h>
static inline uint32_t cc_rotl(uint32_t x,int n){return (x<<n)|(x>>(32-n));}
#define CC_QR(a,b,c,d) a+=b;d^=a;d=cc_rotl(d,16); c+=d;b^=c;b=cc_rotl(b,12); \
                       a+=b;d^=a;d=cc_rotl(d,8);  c+=d;b^=c;b=cc_rotl(b,7);

/* variant: 0=IETF(12B nonce, 32b counter)  1=DJB(8B nonce, 64b counter) */
static inline void cc_block(const uint8_t* key, uint32_t counter,
                            const uint8_t* nonce, int variant, uint32_t* out){
    uint32_t st[16], w[16];
    memcpy(&st[0], "expand 32-byte k", 16);
    memcpy(&st[4], key, 32);
    memset(&st[12], 0, 16);
    if (variant == 0){                 /* IETF */
        st[12] = counter;
        memcpy(&st[13], nonce, 12);
    } else {                           /* DJB */
        st[12] = counter; st[13] = 0;
        memcpy(&st[14], nonce, 8);
    }
    memcpy(w, st, sizeof(st));
    for (int i=0;i<10;i++){
        CC_QR(w[0],w[4],w[8], w[12]); CC_QR(w[1],w[5],w[9], w[13]);
        CC_QR(w[2],w[6],w[10],w[14]); CC_QR(w[3],w[7],w[11],w[15]);
        CC_QR(w[0],w[5],w[10],w[15]); CC_QR(w[1],w[6],w[11],w[12]);
        CC_QR(w[2],w[7],w[8], w[13]); CC_QR(w[3],w[4],w[9], w[14]);
    }
    for (int i=0;i<16;i++) out[i]=w[i]+st[i];
}
#endif
