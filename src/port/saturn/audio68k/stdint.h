#ifndef SM64_SATURN_AUDIO68K_FREESTANDING_STDINT_H
#define SM64_SATURN_AUDIO68K_FREESTANDING_STDINT_H

/* The approved standalone m68k-elf GCC bundle has no target libc headers.
 * Use GCC's target-width built-ins so this freestanding image does not import
 * host ABI assumptions or require newlib. This header is private to audio68k. */
typedef __UINT8_TYPE__ uint8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT16_TYPE__ int16_t;
typedef __UINTPTR_TYPE__ uintptr_t;

#endif
