#ifndef SM64_SATURN_CART_BANK_H
#define SM64_SATURN_CART_BANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct sm64_saturn_cart_bank {
    uint8_t *base;
    size_t capacity;
    bool available;
} sm64_saturn_cart_bank_t;

/* Initializes the optional runtime bank. A missing cart is a supported
 * emulator fallback; callers must keep using their WRAM source bank then. */
bool sm64_saturn_cart_bank_init(sm64_saturn_cart_bank_t *bank);
bool sm64_saturn_cart_bank_stage(sm64_saturn_cart_bank_t *bank,
                                 size_t offset, const void *source, size_t bytes);
bool sm64_saturn_cart_bank_read(const sm64_saturn_cart_bank_t *bank,
                                size_t offset, void *destination, size_t bytes);

#endif
