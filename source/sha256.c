#include "sha256.h"
// SHA-256, FIPS180-4 sections4.1.2,4.2.2,5 and6.2; no MMIO or allocation.
static const uint32_t k[64]={
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
static uint32_t rotr(uint32_t x,unsigned n) { return (x>>n)|(x<<(32U-n)); }
static void compress(struct sha256_state *s)
{
    uint32_t w[64];
    for (unsigned n=0;n<16U;++n) {
        const uint8_t *p=s->block+n*4U;
        w[n]=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
    }
    for (unsigned n=16;n<64U;++n) {
        const uint32_t a=w[n-15U],b=w[n-2U];
        w[n]=w[n-16U]+(rotr(a,7)^rotr(a,18)^(a>>3))+w[n-7U]+(rotr(b,17)^rotr(b,19)^(b>>10));
    }
    uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for (unsigned n=0;n<64U;++n) {
        const uint32_t t1=h+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+k[n]+w[n];
        const uint32_t t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
void sha256_start(struct sha256_state *s)
{
    const uint32_t initial[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
        0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    for (unsigned n=0;n<8U;++n) { s->h[n]=initial[n]; }
    s->bytes=0;s->used=0;
}
void sha256_update(struct sha256_state *s,const uint8_t *data,uint32_t count)
{
    s->bytes+=count;
    for (uint32_t n=0;n<count;++n) {
        s->block[s->used++]=data[n];
        if (s->used==64U) { compress(s);s->used=0; }
    }
}
void sha256_finish(struct sha256_state *s,uint8_t digest[32])
{
    const uint64_t bits=s->bytes*8U;
    const uint8_t one=0x80U,zero=0U;
    sha256_update(s,&one,1U);
    while (s->used!=56U) { sha256_update(s,&zero,1U); }
    uint8_t length[8];
    for (unsigned n=0;n<8U;++n) { length[n]=(uint8_t)(bits>>(56U-n*8U)); }
    sha256_update(s,length,8U);
    for (unsigned n=0;n<32U;++n) { digest[n]=(uint8_t)(s->h[n/4U]>>(24U-(n%4U)*8U)); }
}
