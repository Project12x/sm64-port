#include <yaul.h>
#include <string.h>

#include "saturn_cart_bank.h"

#define SM64_SATURN_CART_ID_4MIB DRAM_CART_ID_4MIB
#define SM64_SATURN_CART_BYTES 0x00400000UL

bool
sm64_saturn_cart_bank_init(sm64_saturn_cart_bank_t *bank)
{
    if (bank == NULL) {
        return false;
    }
    bank->base = NULL;
    bank->capacity = 0;
    bank->available = false;

    dram_cart_init();
    if (dram_cart_id_get() != SM64_SATURN_CART_ID_4MIB ||
        dram_cart_size_get() != SM64_SATURN_CART_BYTES ||
        dram_cart_area_get() == NULL) {
        return false;
    }

    bank->base = (uint8_t *)dram_cart_area_get();
    bank->capacity = SM64_SATURN_CART_BYTES;
    bank->available = true;
    return true;
}

static bool
range_valid(const sm64_saturn_cart_bank_t *bank, size_t offset, size_t bytes)
{
    return bank != NULL && bank->available && offset <= bank->capacity &&
           bytes <= bank->capacity - offset;
}

bool
sm64_saturn_cart_bank_stage(sm64_saturn_cart_bank_t *bank,
                            size_t offset, const void *source, size_t bytes)
{
    if (source == NULL || !range_valid(bank, offset, bytes)) {
        return false;
    }
    /* Cartridge writes are deliberately SH-2 CPU copies. Do not assume the
     * SCU can write the expansion DRAM; measured DMA belongs in a later gate. */
    memcpy(bank->base + offset, source, bytes);
    return true;
}

bool
sm64_saturn_cart_bank_read(const sm64_saturn_cart_bank_t *bank,
                           size_t offset, void *destination, size_t bytes)
{
    if (destination == NULL || !range_valid(bank, offset, bytes)) {
        return false;
    }
    memcpy(destination, bank->base + offset, bytes);
    return true;
}
