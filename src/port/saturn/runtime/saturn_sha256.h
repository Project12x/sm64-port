#ifndef SM64_SATURN_SHA256_H
#define SM64_SATURN_SHA256_H

#include <stdbool.h>
#include <stdint.h>

typedef struct sm64_saturn_sha256 {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    uint32_t used;
} sm64_saturn_sha256_t;

void sm64_saturn_sha256_init(sm64_saturn_sha256_t *state);
bool sm64_saturn_sha256_update(sm64_saturn_sha256_t *state,
                               const void *bytes, uint32_t byte_count);
bool sm64_saturn_sha256_finish(sm64_saturn_sha256_t *state,
                               uint8_t digest[32]);
bool sm64_saturn_sha256_digest(const void *bytes, uint32_t byte_count,
                               uint8_t digest[32]);

#endif
