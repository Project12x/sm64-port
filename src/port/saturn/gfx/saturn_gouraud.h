#ifndef SM64_SATURN_GOURAUD_H
#define SM64_SATURN_GOURAUD_H

#include <stdint.h>

/* VDP1 Gouraud colors are signed corrections around 16.  Using neutral gray
 * as the command's base color makes an interpolated table entry equal the
 * desired final RGB value exactly: 16 + (entry - 16) == entry. */
#define SM64_SATURN_GOURAUD_NEUTRAL 16U

static inline uint16_t
sm64_saturn_gouraud_neutral_color(void)
{
    return 0xC210U;
}

#endif
