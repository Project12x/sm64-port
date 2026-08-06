/* Tiny runtime primitives required by GCC-generated aggregate copies/clears.
 * The MC68000 image has no hosted C library. */
typedef __SIZE_TYPE__ sm64_size_t;

void *memset(void *destination, int value, sm64_size_t count)
{
    unsigned char *bytes = (unsigned char *)destination;
    sm64_size_t i;
    for (i = 0U; i < count; ++i) bytes[i] = (unsigned char)value;
    return destination;
}

void *memcpy(void *destination, const void *source, sm64_size_t count)
{
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    sm64_size_t i;
    for (i = 0U; i < count; ++i) out[i] = in[i];
    return destination;
}
