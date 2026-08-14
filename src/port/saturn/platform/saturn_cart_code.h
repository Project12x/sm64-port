#ifndef SM64_SATURN_CART_CODE_H
#define SM64_SATURN_CART_CODE_H

/* Scene-load validation runs only after SOURCE.DAT has been copied into the
 * 32-Mbit DRAM cart. Keep that cold code out of scarce HWRAM while retaining
 * ordinary host and non-sourceboot linkage. Frame-time code must not use this
 * annotation. */
#if defined(TARGET_SATURN) && defined(SATURN_SOURCEBOOT)
#define SM64_SATURN_CART_COLD \
    __attribute__((section(".cart_cold_text"), noinline))
#else
#define SM64_SATURN_CART_COLD
#endif

#endif
