#ifndef CARRIER_SHA256_H
#define CARRIER_SHA256_H
#include <stdint.h>
struct sha256_state { uint32_t h[8];uint64_t bytes;uint32_t used;uint8_t block[64]; };
void sha256_start(struct sha256_state *s);
void sha256_update(struct sha256_state *s,const uint8_t *data,uint32_t count);
void sha256_finish(struct sha256_state *s,uint8_t digest[32]);
#endif
