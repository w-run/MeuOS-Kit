#ifndef MZ_ED25519_H
#define MZ_ED25519_H

#include <stddef.h>
#include <stdint.h>

#define MZ_ED25519_SEED_LEN  32
#define MZ_ED25519_SK_LEN    64
#define MZ_ED25519_PK_LEN    32
#define MZ_ED25519_SIG_LEN   64

int mz_ed25519_keypair(const uint8_t seed[32],
                        uint8_t sk[64], uint8_t pk[32]);

void mz_ed25519_sign(const uint8_t sk[64],
                      const uint8_t *msg, size_t msglen,
                      uint8_t sig[64]);

int mz_ed25519_verify(const uint8_t pk[32],
                       const uint8_t *msg, size_t msglen,
                       const uint8_t sig[64]);

#endif /* MZ_ED25519_H */
