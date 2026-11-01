#ifndef CRYPTO_SHA256_H
#define CRYPTO_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_BLOCK_SIZE 32

typedef struct {
    uint8_t  data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;

void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t hash[]);

/* High level helpers */
void sha256_hash_string(const char *input, char output_hex[65]);
void generate_random_salt(char salt_hex[33]);
void hash_password_with_salt(const char *password, const char *salt, char hash_hex[65]);

#endif /* CRYPTO_SHA256_H */
