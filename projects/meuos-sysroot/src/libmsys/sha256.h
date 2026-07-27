#ifndef MT_SHA256_H
#define MT_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_LENGTH 32
#define SHA256_BLOCK_SIZE 64

struct sha256_ctx {
	uint64_t bitcount;
	uint32_t state[8];
	uint8_t  buf[SHA256_BLOCK_SIZE];
	size_t   buflen;
};

void sha256_init(struct sha256_ctx *ctx);
void sha256_update(struct sha256_ctx *ctx, const void *data, size_t len);
void sha256_final(struct sha256_ctx *ctx, uint8_t out[SHA256_DIGEST_LENGTH]);

/* One-shot convenience */
void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LENGTH]);

#endif /* MT_SHA256_H */
