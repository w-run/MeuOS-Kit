/* mz_ed25519.c — Ed25519 via dlopen(libsodium)
 *
 * Same pattern as mz_decompress_zlib/mz_decompress_zstd.
 * Falls back silently if libsodium is not available at runtime. */

#include "mz_ed25519.h"
#include <dlfcn.h>
#include <errno.h>
#include <string.h>

static int sodium_loaded = 0;
static void *sodium_handle = NULL;
static int (*sodium_init_fn)(void) = NULL;
static int (*csign_seed_keypair)(unsigned char *, unsigned char *, const unsigned char *) = NULL;
static int (*csign_detached)(unsigned char *, unsigned long long *,
                              const unsigned char *, unsigned long long,
                              const unsigned char *) = NULL;
static int (*csign_verify_detached)(const unsigned char *,
                                     const unsigned char *, unsigned long long,
                                     const unsigned char *) = NULL;

static int ensure_sodium(void) {
    if (sodium_loaded) return sodium_handle != NULL;
    sodium_loaded = 1;
    sodium_handle = dlopen("libsodium.so", RTLD_LAZY | RTLD_LOCAL);
    if (!sodium_handle) sodium_handle = dlopen("libsodium.so.26", RTLD_LAZY | RTLD_LOCAL);
    if (!sodium_handle) sodium_handle = dlopen("libsodium.so.23", RTLD_LAZY | RTLD_LOCAL);
    if (!sodium_handle) return 0;

    sodium_init_fn = (int (*)(void))dlsym(sodium_handle, "sodium_init");
    csign_seed_keypair = (int (*)(unsigned char *, unsigned char *, const unsigned char *))
        dlsym(sodium_handle, "crypto_sign_ed25519_seed_keypair");
    csign_detached = (int (*)(unsigned char *, unsigned long long *,
                               const unsigned char *, unsigned long long,
                               const unsigned char *))
        dlsym(sodium_handle, "crypto_sign_ed25519_detached");
    csign_verify_detached = (int (*)(const unsigned char *,
                                      const unsigned char *, unsigned long long,
                                      const unsigned char *))
        dlsym(sodium_handle, "crypto_sign_ed25519_verify_detached");

    if (!sodium_init_fn || !csign_seed_keypair || !csign_detached || !csign_verify_detached) {
        dlclose(sodium_handle); sodium_handle = NULL; return 0;
    }
    if ((*sodium_init_fn)() != 0) {
        dlclose(sodium_handle); sodium_handle = NULL; return 0;
    }
    return 1;
}

int mz_ed25519_keypair(const uint8_t seed[32], uint8_t sk[64], uint8_t pk[32]) {
    if (!ensure_sodium()) { errno = ENOPKG; memset(sk, 0, 64); memset(pk, 0, 32); return -1; }
    (*csign_seed_keypair)(pk, sk, seed);
    return 0;
}

void mz_ed25519_sign(const uint8_t sk[64], const uint8_t *msg, size_t msglen, uint8_t sig[64]) {
    if (!ensure_sodium()) { memset(sig, 0, 64); return; }
    unsigned long long slen = 64;
    (*csign_detached)(sig, &slen, msg, (unsigned long long)msglen, sk);
}

int mz_ed25519_verify(const uint8_t pk[32], const uint8_t *msg, size_t msglen, const uint8_t sig[64]) {
    if (!ensure_sodium()) return 0;
    return (*csign_verify_detached)(sig, msg, (unsigned long long)msglen, pk) == 0;
}

int mz_sign_block(uint8_t *block, size_t size, const uint8_t secret_key[32]) {
    uint8_t sk[64], pk[32];
    if (mz_ed25519_keypair(secret_key, sk, pk) != 0) return -1;
    uint8_t sig[64];
    mz_ed25519_sign(sk, block, size, sig);
    /* Append signature to end of block — caller must ensure room */
    if (size >= 64) memcpy(block + size - 64, sig, 64);
    return 0;
}

int mz_verify_block(const uint8_t *block, size_t size, const uint8_t public_key[32]) {
    if (size < 64) return 0;
    const uint8_t *sig = block + size - 64;
    return mz_ed25519_verify(public_key, block, size - 64, sig);
}
