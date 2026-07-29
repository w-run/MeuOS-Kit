#ifndef MT_ED25519_H
#define MT_ED25519_H

#include <stddef.h>
#include <stdint.h>

#define ED25519_SEED_LEN    32
#define ED25519_SK_LEN      64
#define ED25519_PK_LEN      32
#define ED25519_SIG_LEN     64

/* Generate a key pair from a 32-byte seed.
 *   seed:  32 bytes of key material
 *   sk:    output, 64 bytes (seed || public key)
 *   pk:    output, 32 bytes public key (may be NULL to compute in-place in sk[32..63])
 *   Returns 0 on success, -1 on error (errno set, outputs zeroed). */
int ed25519_keypair(const uint8_t seed[32],
                     uint8_t sk[64], uint8_t pk[32]);

/* Sign a message.
 *   sk:    64-byte secret key (seed || public key)
 *   msg:   message to sign
 *   msglen: message length
 *   sig:   output, 64-byte signature */
void ed25519_sign(const uint8_t sk[64],
                  const uint8_t *msg, size_t msglen,
                  uint8_t sig[64]);

/* Verify a signature.
 *   pk:    32-byte public key
 *   msg:   signed message
 *   msglen: message length
 *   sig:   64-byte signature
 *   Returns 1 if valid, 0 if invalid. */
int ed25519_verify(const uint8_t pk[32],
                   const uint8_t *msg, size_t msglen,
                   const uint8_t sig[64]);

#endif /* MT_ED25519_H */
