/* ed25519.c — ed25519 implementation via dlopen(libsodium).
 *
 * Follows the same pattern as decompress_zlib/decompress_zstd.
 * Falls back silently if libsodium is not available at runtime.
 * Users without libsodium can still read .msys archives with signatures,
 * but signature creation and verification will return errors. */

#include "ed25519.h"
#include <dlfcn.h>
#include <errno.h>
#include <string.h>

/* Function pointers loaded lazily from libsodium */
static int sodium_loaded = 0;
static void *sodium_handle = NULL;
static int (*sodium_init_fn)(void) = NULL;
static int (*crypto_sign_seed_keypair)(unsigned char *, unsigned char *, const unsigned char *) = NULL;
static int (*crypto_sign_detached)(unsigned char *, unsigned long long *,
                                    const unsigned char *, unsigned long long,
                                    const unsigned char *) = NULL;
static int (*crypto_sign_verify_detached)(const unsigned char *,
                                           const unsigned char *, unsigned long long,
                                           const unsigned char *) = NULL;

static int ensure_sodium(void)
{
	if (sodium_loaded) return sodium_handle != NULL;
	sodium_loaded = 1;

	sodium_handle = dlopen("libsodium.so", RTLD_LAZY | RTLD_LOCAL);
	if (!sodium_handle) sodium_handle = dlopen("libsodium.so.26", RTLD_LAZY | RTLD_LOCAL);
	if (!sodium_handle) sodium_handle = dlopen("libsodium.so.23", RTLD_LAZY | RTLD_LOCAL);
	if (!sodium_handle) return 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	sodium_init_fn       = (int (*)(void))dlsym(sodium_handle, "sodium_init");
	crypto_sign_seed_keypair = (int (*)(unsigned char *, unsigned char *, const unsigned char *))
		dlsym(sodium_handle, "crypto_sign_ed25519_seed_keypair");
	crypto_sign_detached = (int (*)(unsigned char *, unsigned long long *,
	                                 const unsigned char *, unsigned long long,
	                                 const unsigned char *))
		dlsym(sodium_handle, "crypto_sign_ed25519_detached");
	crypto_sign_verify_detached = (int (*)(const unsigned char *,
	                                        const unsigned char *, unsigned long long,
	                                        const unsigned char *))
		dlsym(sodium_handle, "crypto_sign_ed25519_verify_detached");
#pragma GCC diagnostic pop

	if (!sodium_init_fn || !crypto_sign_seed_keypair ||
	    !crypto_sign_detached || !crypto_sign_verify_detached) {
		dlclose(sodium_handle);
		sodium_handle = NULL;
		return 0;
	}

	/* Initialize sodium (idempotent) — returns 0 on success */
	if ((*sodium_init_fn)() != 0) {
		dlclose(sodium_handle);
		sodium_handle = NULL;
		return 0;
	}
	return 1;
}

int ed25519_keypair(const uint8_t seed[32], uint8_t sk[64], uint8_t pk[32])
{
	if (!ensure_sodium()) {
		errno = ENOPKG;
		memset(sk, 0, 64);
		memset(pk, 0, 32);
		return -1;
	}
	(*crypto_sign_seed_keypair)(pk, sk, seed);
	return 0;
}

void ed25519_sign(const uint8_t sk[64], const uint8_t *msg, size_t msglen,
                   uint8_t sig[64])
{
	if (!ensure_sodium()) {
		memset(sig, 0, 64);
		return;
	}
	unsigned long long slen = 64;
	(*crypto_sign_detached)(sig, &slen, msg, (unsigned long long)msglen, sk);
}

int ed25519_verify(const uint8_t pk[32], const uint8_t *msg, size_t msglen,
                    const uint8_t sig[64])
{
	if (!ensure_sodium()) return 0;
	return (*crypto_sign_verify_detached)(sig, msg, (unsigned long long)msglen, pk) == 0;
}
