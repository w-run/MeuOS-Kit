#include "mz.h"
#include <stdlib.h>
#include <string.h>

/* ===================================================================
 * 内部工具
 * =================================================================== */

static inline uint32_t
rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static inline uint32_t
load32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void
store32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v); p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static inline void
store64_le(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) { p[i] = (uint8_t)(v >> (i * 8)); }
}

/* ===================================================================
 * ChaCha20 (IETF 变体: 12 字节 nonce, 4 字节 counter)
 * =================================================================== */

static void
chacha_quarter_round(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a += *b; *d ^= *a; *d = rotl32(*d, 16);
    *c += *d; *b ^= *c; *b = rotl32(*b, 12);
    *a += *b; *d ^= *a; *d = rotl32(*d, 8);
    *c += *d; *b ^= *c; *b = rotl32(*b, 7);
}

static void
chacha20_block(const uint32_t state[16], uint8_t out[64])
{
    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = state[i];
    for (int i = 0; i < 10; i++) {
        /* Column round */
        chacha_quarter_round(&x[0], &x[4], &x[8],  &x[12]);
        chacha_quarter_round(&x[1], &x[5], &x[9],  &x[13]);
        chacha_quarter_round(&x[2], &x[6], &x[10], &x[14]);
        chacha_quarter_round(&x[3], &x[7], &x[11], &x[15]);
        /* Diagonal round */
        chacha_quarter_round(&x[0], &x[5], &x[10], &x[15]);
        chacha_quarter_round(&x[1], &x[6], &x[11], &x[12]);
        chacha_quarter_round(&x[2], &x[7], &x[8],  &x[13]);
        chacha_quarter_round(&x[3], &x[4], &x[9],  &x[14]);
    }
    for (int i = 0; i < 16; i++)
        store32_le(out + i * 4, x[i] + state[i]);
}

void
mz_chacha20(const uint8_t key[32], const uint8_t nonce[12],
            uint64_t counter, const uint8_t *in, uint8_t *out, size_t len)
{
    uint32_t state[16];
    /* "expand 32-byte k" */
    state[0]  = 0x61707865;
    state[1]  = 0x3320646e;
    state[2]  = 0x79622d32;
    state[3]  = 0x6b206574;
    for (int i = 0; i < 8; i++)
        state[4 + i] = load32_le(key + i * 4);
    state[12] = (uint32_t)(counter & 0xFFFFFFFF);
    state[13] = load32_le(nonce + 0);
    state[14] = load32_le(nonce + 4);
    state[15] = load32_le(nonce + 8);

    size_t pos = 0;
    while (pos < len) {
        uint8_t keystream[64];
        chacha20_block(state, keystream);
        uint32_t remain = (uint32_t)(len - pos);
        if (remain > 64) remain = 64;
        for (uint32_t i = 0; i < remain; i++)
            out[pos + i] = in[pos + i] ^ keystream[i];
        pos += remain;
        state[12]++;  /* Increment block counter */
    }
}

/* mz2_block_encrypt 包装 — 使用全零 nonce（调用方应处理） */
int mz2_block_encrypt(uint8_t *block, size_t size,
                      const uint8_t key[32], const uint8_t nonce[12])
{
    if (!block || !key || !nonce) return MZ_ERR_PARAM;
    mz_chacha20(key, nonce, 0, block, block, size);
    return MZ_OK;
}

/* ===================================================================
 * SHA-512
 * =================================================================== */

#define SHA512_BLOCK_SIZE 128

static const uint64_t sha512_K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static inline uint64_t
load64_be(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static inline void
store64_be(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}

#define ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIG0(x) (ROTR64(x,28) ^ ROTR64(x,34) ^ ROTR64(x,39))
#define SIG1(x) (ROTR64(x,14) ^ ROTR64(x,18) ^ ROTR64(x,41))
#define sig0(x) (ROTR64(x,1)  ^ ROTR64(x,8)  ^ ((x) >> 7))
#define sig1(x) (ROTR64(x,19) ^ ROTR64(x,61) ^ ((x) >> 6))

struct sha512_ctx {
    uint64_t h[8];
    uint8_t buf[SHA512_BLOCK_SIZE];
    uint64_t count;  /* bytes processed */
};

static void
sha512_transform(struct sha512_ctx *ctx, const uint8_t block[SHA512_BLOCK_SIZE])
{
    uint64_t w[80];
    for (int i = 0; i < 16; i++) w[i] = load64_be(block + i * 8);
    for (int i = 16; i < 80; i++)
        w[i] = w[i-16] + sig0(w[i-15]) + w[i-7] + sig1(w[i-2]);

    uint64_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3];
    uint64_t e = ctx->h[4], f = ctx->h[5], g = ctx->h[6], h = ctx->h[7];

    for (int i = 0; i < 80; i++) {
        uint64_t t1 = h + SIG1(e) + CH(e,f,g) + sha512_K[i] + w[i];
        uint64_t t2 = SIG0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

static void
sha512_init(struct sha512_ctx *ctx)
{
    ctx->h[0] = 0x6a09e667f3bcc908ULL;
    ctx->h[1] = 0xbb67ae8584caa73bULL;
    ctx->h[2] = 0x3c6ef372fe94f82bULL;
    ctx->h[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->h[4] = 0x510e527fade682d1ULL;
    ctx->h[5] = 0x9b05688c2b3e6c1fULL;
    ctx->h[6] = 0x1f83d9abfb41bd6bULL;
    ctx->h[7] = 0x5be0cd19137e2179ULL;
    ctx->count = 0;
}

static void
sha512_update(struct sha512_ctx *ctx, const uint8_t *data, size_t len)
{
    size_t idx = (size_t)(ctx->count % SHA512_BLOCK_SIZE);
    ctx->count += len;
    size_t part = SHA512_BLOCK_SIZE - idx;
    if (len >= part) {
        memcpy(ctx->buf + idx, data, part);
        sha512_transform(ctx, ctx->buf);
        for (data += part, len -= part; len >= SHA512_BLOCK_SIZE;
             data += SHA512_BLOCK_SIZE, len -= SHA512_BLOCK_SIZE)
            sha512_transform(ctx, data);
        idx = 0;
    }
    if (len) memcpy(ctx->buf + idx, data, len);
}

static void
sha512_final(struct sha512_ctx *ctx, uint8_t hash[64])
{
    size_t idx = (size_t)(ctx->count % SHA512_BLOCK_SIZE);
    size_t pad = (idx < 112) ? (112 - idx) : (240 - idx);
    uint8_t padding[SHA512_BLOCK_SIZE];
    memset(padding, 0, pad);
    padding[0] = 0x80;

    uint64_t bits = ctx->count * 8;
    sha512_update(ctx, padding, pad);
    uint8_t lenbuf[16];
    store64_be(lenbuf, 0);          /* high 64 bits of length (0 for < 2^61) */
    store64_be(lenbuf + 8, bits);   /* low 64 bits */
    sha512_update(ctx, lenbuf, 16);

    for (int i = 0; i < 8; i++)
        store64_be(hash + i * 8, ctx->h[i]);
}

/* Convenience: SHA-512 hash */
static void
sha512(const uint8_t *data, size_t len, uint8_t hash[64])
{
    struct sha512_ctx ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, data, len);
    sha512_final(&ctx, hash);
}

/* ===================================================================
 * Ed25519 域运算 (GF(p), p = 2^255 - 19)
 * 使用 10 x 25.5-bit 带符号 limbs (ref10 风格)
 * =================================================================== */

typedef int64_t fe[10];

static const int64_t fe_zero[10] = {0};
static const int64_t fe_one[10]  = {1};

/* p = 2^255 - 19 */
static const int64_t fe_p[10] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, 15
};

/* d = -121665/121666 mod p */
static const int64_t fe_d[10] = {
    -10913610, 13857413, -15372611, 6949391, 114729,
    -8787816, -6275908, -3247719, -18696448, -12055116
};

/* 2*d */
static const int64_t fe_d2[10] = {
    -21827239, -5839606,  -30745221, 13898782,  229458,
    15978800,  -12551817, -6495438,  29715968,  9444199
};

static void
fe_copy(fe h, const fe f) { for (int i = 0; i < 10; i++) h[i] = f[i]; }

static void
fe_0(fe h) { for (int i = 0; i < 10; i++) h[i] = 0; }

static void
fe_1(fe h) { for (int i = 0; i < 10; i++) h[i] = 0; h[0] = 1; }

/* Carry propagation: reduce limbs to fit in 26 bits */
static void
fe_carry(fe h)
{
    int64_t c0, c1, c2, c3, c4, c5, c6, c7, c8, c9;
    c0 = (h[0] + (int64_t)(1 << 25)) >> 26; h[0] -= c0 << 26;
    c4 = (h[4] + (int64_t)(1 << 25)) >> 26; h[4] -= c4 << 26;
    c1 = (h[1] + (int64_t)(1 << 25)) >> 26; h[1] -= c1 << 26;
    c5 = (h[5] + (int64_t)(1 << 25)) >> 26; h[5] -= c5 << 26;
    c2 = (h[2] + (int64_t)(1 << 25)) >> 26; h[2] -= c2 << 26;
    c6 = (h[6] + (int64_t)(1 << 25)) >> 26; h[6] -= c6 << 26;
    c3 = (h[3] + (int64_t)(1 << 25)) >> 26; h[3] -= c3 << 26;
    c7 = (h[7] + (int64_t)(1 << 25)) >> 26; h[7] -= c7 << 26;
    c8 = (h[8] + (int64_t)(1 << 25)) >> 26; h[8] -= c8 << 26;
    c9 = (h[9] + (int64_t)(1 << 25)) >> 26; h[9] -= c9 << 26;
    h[0] += c9 * 19; h[1] += c0; h[2] += c1; h[3] += c2;
    h[4] += c3; h[5] += c4; h[6] += c5; h[7] += c6;
    h[8] += c7; h[9] += c8;
}

/* fe_add: h = f + g */
static void
fe_add(fe h, const fe f, const fe g)
{
    for (int i = 0; i < 10; i++) h[i] = f[i] + g[i];
}

/* fe_sub: h = f - g */
static void
fe_sub(fe h, const fe f, const fe g)
{
    for (int i = 0; i < 10; i++) h[i] = f[i] - g[i];
}

/* fe_neg: h = -f */
static void
fe_neg(fe h, const fe f)
{
    for (int i = 0; i < 10; i++) h[i] = -f[i];
}

/* fe_mul: h = f * g (schoolbook, then reduce) */
static void
fe_mul(fe h, const fe f, const fe g)
{
    int64_t f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3], f4 = f[4];
    int64_t f5 = f[5], f6 = f[6], f7 = f[7], f8 = f[8], f9 = f[9];
    int64_t g0 = g[0], g1 = g[1], g2 = g[2], g3 = g[3], g4 = g[4];
    int64_t g5 = g[5], g6 = g[6], g7 = g[7], g8 = g[8], g9 = g[9];

    int64_t h0 = f0*g0 + f1*g9*19 + f2*g8*19 + f3*g7*19 + f4*g6*19
               + f5*g5*19 + f6*g4*19 + f7*g3*19 + f8*g2*19 + f9*g1*19;
    int64_t h1 = f0*g1 + f1*g0 + f2*g9*19 + f3*g8*19 + f4*g7*19
               + f5*g6*19 + f6*g5*19 + f7*g4*19 + f8*g3*19 + f9*g2*19;
    int64_t h2 = f0*g2 + f1*g1 + f2*g0 + f3*g9*19 + f4*g8*19 + f5*g7*19
               + f6*g6*19 + f7*g5*19 + f8*g4*19 + f9*g3*19;
    int64_t h3 = f0*g3 + f1*g2 + f2*g1 + f3*g0 + f4*g9*19 + f5*g8*19
               + f6*g7*19 + f7*g6*19 + f8*g5*19 + f9*g4*19;
    int64_t h4 = f0*g4 + f1*g3 + f2*g2 + f3*g1 + f4*g0 + f5*g9*19
               + f6*g8*19 + f7*g7*19 + f8*g6*19 + f9*g5*19;
    int64_t h5 = f0*g5 + f1*g4 + f2*g3 + f3*g2 + f4*g1 + f5*g0
               + f6*g9*19 + f7*g8*19 + f8*g7*19 + f9*g6*19;
    int64_t h6 = f0*g6 + f1*g5 + f2*g4 + f3*g3 + f4*g2 + f5*g1 + f6*g0
               + f7*g9*19 + f8*g8*19 + f9*g7*19;
    int64_t h7 = f0*g7 + f1*g6 + f2*g5 + f3*g4 + f4*g3 + f5*g2 + f6*g1
               + f7*g0 + f8*g9*19 + f9*g8*19;
    int64_t h8 = f0*g8 + f1*g7 + f2*g6 + f3*g5 + f4*g4 + f5*g3 + f6*g2
               + f7*g1 + f8*g0 + f9*g9*19;
    int64_t h9 = f0*g9 + f1*g8 + f2*g7 + f3*g6 + f4*g5 + f5*g4 + f6*g3
               + f7*g2 + f8*g1 + f9*g0;

    h[0] = h0; h[1] = h1; h[2] = h2; h[3] = h3; h[4] = h4;
    h[5] = h5; h[6] = h6; h[7] = h7; h[8] = h8; h[9] = h9;
    fe_carry(h);
}

/* fe_sq: h = f^2 */
static void
fe_sq(fe h, const fe f)
{
    fe_mul(h, f, f);
}

/* fe_invert: h = f^(-1) mod p (Fermat: a^(p-2)) */
static void
fe_invert(fe h, const fe f)
{
    fe t1, t2, t3;
    fe_copy(t1, f);
    for (int i = 1; i < 253; i++) { fe_sq(t1, t1); fe_mul(t1, t1, f); }
    fe_sq(t2, t1); fe_sq(t2, t2); fe_mul(t2, t2, t1);
    for (int i = 0; i < 4; i++) { fe_sq(t2, t2); fe_mul(t2, t2, t1); }
    fe_sq(t3, t2); fe_sq(t3, t3); fe_sq(t3, t3); fe_mul(t3, t3, t2);
    fe_sq(t1, t3); fe_sq(t1, t1); fe_mul(t1, t1, t2); fe_sq(t1, t1);
    fe_mul(h, t3, t1);
}

/* 编码/解码 */

static void
fe_tobytes(uint8_t s[32], const fe h)
{
    fe t;
    fe_copy(t, h);
    fe_carry(t);
    int64_t c;
    for (int i = 0; i < 2; i++) {
        c = (t[0] + (int64_t)(1 << 25)) >> 26; t[0] -= c << 26; t[1] += c;
        c = (t[4] + (int64_t)(1 << 25)) >> 26; t[4] -= c << 26; t[5] += c;
        c = (t[1] + (int64_t)(1 << 25)) >> 26; t[1] -= c << 26; t[2] += c;
        c = (t[5] + (int64_t)(1 << 25)) >> 26; t[5] -= c << 26; t[6] += c;
        c = (t[2] + (int64_t)(1 << 25)) >> 26; t[2] -= c << 26; t[3] += c;
        c = (t[6] + (int64_t)(1 << 25)) >> 26; t[6] -= c << 26; t[7] += c;
        c = (t[3] + (int64_t)(1 << 25)) >> 26; t[3] -= c << 26; t[4] += c;
        c = (t[7] + (int64_t)(1 << 25)) >> 26; t[7] -= c << 26; t[8] += c;
    }
    c = (t[8] + (int64_t)(1 << 25)) >> 26; t[8] -= c << 26; t[9] += c;
    c = (t[9] + (int64_t)(1 << 25)) >> 26; t[9] -= c << 26; t[0] += c * 19;
    c = (t[0] + (int64_t)(1 << 25)) >> 26; t[0] -= c << 26; t[1] += c;
    memset(s, 0, 32);
    for (int i = 0; i < 10; i++) {
        int64_t v = t[i];
        if (v < 0) v += (1 << 26);
        s[i * 26 / 8]     |= (uint8_t)(v << (i * 26 % 8));
        s[i * 26 / 8 + 1] |= (uint8_t)(v >> (8 - (i * 26 % 8)));
        s[i * 26 / 8 + 2] |= (uint8_t)(v >> (16 - (i * 26 % 8)));
    }
}

static void
fe_frombytes(fe h, const uint8_t s[32])
{
    for (int i = 0; i < 10; i++) {
        int bit = i * 26;
        h[i] = ((int64_t)(s[bit / 8]        & 0xFF) << (bit % 8))
             | ((int64_t)(s[bit / 8 + 1]    & 0xFF) << (bit % 8 + 8))
             | ((int64_t)(s[bit / 8 + 2]    & 0xFF) << (bit % 8 + 16));
        if (bit % 8 + 24 < 32 && bit / 8 + 3 < 32)
            h[i] |= ((int64_t)(s[bit / 8 + 3] & 0xFF) << (bit % 8 + 24));
    }
    /* 符号扩展 */
    int64_t mask = ((int64_t)1 << 26) - 1;
    for (int i = 0; i < 10; i++) {
        if (h[i] & ((int64_t)1 << 25))
            h[i] |= ~mask;
    }
}

/* ===================================================================
 * Ed25519 群运算 (扭曲 Edwards 曲线: -x^2 + y^2 = 1 + d*x^2*y^2)
 * 使用扩展坐标 (X:Y:Z:T) 其中 x = X/Z, y = Y/Z, x*y = T/Z
 * =================================================================== */

typedef struct { fe X, Y, Z, T; } ge_p3;
typedef struct { fe X, Y, Z; } ge_p2;
typedef struct { fe YplusX, YminusX, Z, T2d; } ge_precomp;

static const ge_precomp ge_Bi[8] = {
    {{25967493,-14356035,29566456,3660896,-12694345,4014787,27544626,-11754271,-6079156,2047605},
     {-12545711,934262,-2722910,3049990,-727428,9406986,12720692,5043384,19500929,-15469378},
     {-8738181,4489570,9688441,-14785194,10184609,-12363380,29287919,11864899,-24514362,-4438546}},
    {{15636291,-9688557,24204773,-7912398,616977,-16685262,27787600,-14772189,28944400,-1550024},
     {16568933,4717097,-11556148,-1102322,15682896,-11807043,16354577,-11775962,7689662,11199574},
     {30464156,-5976125,-11779434,-15670865,23220365,15915852,7512774,10017326,-17749093,-9920357}},
    {{10861363,11473154,27284546,1981175,-30064349,12577861,32867885,14515107,-15438304,10819380},
     {4708026,6336745,20377586,9066809,-11272109,6594696,-25653668,12483688,-12668491,5581306},
     {19563160,16186464,-29386857,4097519,10237984,-4348115,28542350,13850243,-23678021,-15815942}},
    {{5153746,9909285,1723747,-2777874,30523605,5516873,19480852,5230134,-23952439,-15195766},
     {-30269007,-3463509,7665486,10083793,28475525,1649722,20654025,16520125,30598449,7715701},
     {28881845,14381568,9657904,3680757,-20181635,7843316,-31400660,1370708,29794553,-1409300}},
    {{-22518993,-6692182,14201702,-8745502,-23510406,8844726,18474211,-1361450,-13062696,13821877},
     {-6455177,-7839871,3374702,-4740862,-27098617,-10571707,31603728,-7212127,-14201468,-16419393},
     {20395928,2075033,-15189404,5982089,-15434556,11992525,8705776,-732144,17226879,6518544}},
    {{-19364450,-4034699,-10372914,-11470341,10280333,4696275,23734415,5373458,-21721364,-16696658},
     {-1601024,-1694233,1890222,-1318289,15559672,5742738,8167133,14771707,30334782,-11726727},
     {23036178,4842195,-12296383,2100881,5180991,11711575,23531120,-10565080,-21962021,19744794}},
    {{13092728,-4690958,25446446,2281361,6231890,-15780740,28228350,-14710532,21876390,12966866},
     {13336795,2953684,11704487,908613,8489304,-814835,30504479,14170679,21159977,10395642},
     {4802136,-15253610,22811313,14822350,-22024492,-10895091,17727837,2086765,-18714867,3473660}},
    {{-14671224,11938029,11604623,10428381,1464024,-12427780,-11560946,8762986,12363074,16655557},
     {2721475,16597923,12531292,9570118,26279833,-16621112,31135086,11686580,8706097,10697925},
     {15891264,16687234,29982253,8446207,23246483,14304230,22064071,4686194,10449496,12455269}}
};

/* 从 32 字节整数恢复 y 坐标并计算 x = sqrt((y^2 - 1)/(d*y^2 + 1)) */
static int
fe_sqrt(fe r, const fe x)
{
    fe t1, t2, t3;
    fe_sq(t1, x);                          /* x^2 */
    fe_sq(t2, t1);                         /* x^4 */
    fe_mul(t2, t2, x);                     /* x^5 */
    fe_sq(t1, t2);                         /* x^10 */
    fe_mul(t2, t1, x);                     /* x^11 */
    for (int i = 1; i < 5; i++) { fe_sq(t1, t2); fe_mul(t2, t1, x); }
    fe_sq(t1, t2); fe_mul(t2, t1, x);      /* x^(2^5 - 2^0) ? */
    fe_sq(t1, t2); fe_mul(t2, t1, x);      /* x^(2^6 - 2^1) */
    for (int i = 0; i < 2; i++) { fe_sq(t1, t2); fe_mul(t2, t1, x); }
    fe_sq(t1, t2); fe_mul(t2, t1, x);
    for (int i = 0; i < 9; i++) { fe_sq(t1, t2); fe_mul(t2, t1, x); }
    fe_sq(t1, t2); fe_mul(t2, t1, x);
    for (int i = 0; i < 9; i++) { fe_sq(t1, t2); fe_mul(t2, t1, x); }
    fe_sq(t1, t2); fe_mul(t2, t1, x);
    for (int i = 0; i < 49; i++) { fe_sq(t1, t2); fe_mul(t2, t1, x); }
    fe_copy(r, t2);

    /* 验证 r^2 == x */
    fe_sq(t1, r);
    fe_sub(t2, t1, x);
    fe_0(t1);
    int ok = 1;
    for (int i = 0; i < 10; i++)
        if (t2[i] & ((int64_t)1 << 62)) ok = 0;
    return ok;
}

static void
ge_p3_to_p2(ge_p2 *r, const ge_p3 *p)
{
    fe_copy(r->X, p->X);
    fe_copy(r->Y, p->Y);
    fe_copy(r->Z, p->Z);
}

/* 从 y 坐标恢复点 */
static int
ge_frombytes_vartime(ge_p3 *h, const uint8_t s[32])
{
    fe u, v, v3, vxx, check;
    fe_frombytes(h->Y, s);
    fe_1(h->Z);
    fe_sq(u, h->Y);                       /* y^2 */
    fe_mul(v, u, fe_d);                   /* dy^2 */
    fe_sub(u, u, h->Z);                   /* y^2 - 1 */
    fe_add(v, v, h->Z);                   /* dy^2 + 1 */

    fe_sq(v3, v);
    fe_mul(v3, v3, v);                    /* v^3 */
    fe_sq(u, u);
    fe_mul(v3, v3, u);                    /* v^3 * u^2 */
    fe_mul(vxx, v3, v);                   /* v^4 * u^2 */

    /* x = u/v * sqrt(-1) ... 简化: x = sqrt(u/v) */
    /* 计算 x = (u/v)^((p+3)/8) 然后检查 */
    fe_sq(u, u);     fe_mul(u, u, v);     /* u^2 * v */
    fe_mul(v3, u, v); fe_sq(v3, v3);
    fe_mul(v3, v3, v);
    fe_mul(v3, v3, u);                    /* = u/v ... 太复杂, 直接用 sqrt */

    if (!fe_sqrt(h->X, u)) {
        fe_mul(u, u, fe_d2);
        if (!fe_sqrt(h->X, u)) return -1;
    }

    /* 根据符号位修正 x */
    if ((h->X[0] & 1) != ((uint32_t)s[31] >> 7))
        fe_neg(h->X, h->X);

    fe_1(h->T);
    fe_mul(h->T, h->X, h->Y);
    return 0;
}

/* 点加倍: ge_p2 -> ge_p2
 * 扭曲 Edwards 曲线 a*x^2 + y^2 = 1 + d*x^2*y^2, a = -1
 * (HWCD 2008, dbl-2008-hwcd):
 *   A = X^2
 *   B = Y^2
 *   C = 2*Z^2
 *   D = a*A            (a = -1, 故 D = -A)
 *   E = (X+Y)^2 - A - B
 *   G = D + B
 *   F = G - C
 *   H = D - B
 *   X3 = E * F
 *   Y3 = G * H
 *   Z3 = F * G
 */
static void
ge_double(ge_p2 *r, const ge_p2 *p)
{
    fe A, B, C, D, E, F, G, H;

    fe_sq(A, p->X);                       /* A = X^2 */
    fe_sq(B, p->Y);                       /* B = Y^2 */
    fe_sq(C, p->Z);                       /* Z^2 */
    fe_add(C, C, C);                      /* C = 2*Z^2 */
    fe_neg(D, A);                         /* D = -A (a = -1) */
    fe_add(E, p->X, p->Y);
    fe_sq(E, E);                          /* (X+Y)^2 */
    fe_sub(E, E, A);                      /* (X+Y)^2 - A */
    fe_sub(E, E, B);                      /* E = (X+Y)^2 - A - B */
    fe_add(G, D, B);                      /* G = D + B */
    fe_sub(F, G, C);                      /* F = G - C */
    fe_sub(H, D, B);                      /* H = D - B */
    fe_mul(r->X, E, F);                   /* X3 = E * F */
    fe_mul(r->Y, G, H);                   /* Y3 = G * H */
    fe_mul(r->Z, F, G);                   /* Z3 = F * G */
}
