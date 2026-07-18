#ifndef SM64_SATURN_COMPAT_MATH_H
#define SM64_SATURN_COMPAT_MATH_H

/* The SH newlib configuration hides C99 float declarations under strict C11,
 * while the inherited SM64 sources call these portable libm entry points. */
float sinf(float value);
float cosf(float value);
float sqrtf(float value);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif
