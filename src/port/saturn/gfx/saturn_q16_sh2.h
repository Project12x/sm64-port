/*
 * Copyright (c) 2012-2024 Johannes Fetz (johannesfetz@gmail.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain this notice, this list of
 *    conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce this notice, this list of
 *    conditions and the following disclaimer in the documentation and/or
 *    other materials provided with the distribution.
 * 3. Neither the name of Johannes Fetz nor contributors' names may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DAMAGES ARISING FROM USE.
 *
 * Direct adaptation of johannes-fetz/joengine, jo_engine/math.c:57-70,
 * commit 556d081146211b6a1cfa6591d70f9487d406758b. Material changes: this
 * header exposes only the Q16.16 primitive, uses project naming/types, and
 * supplies a host-reference fallback. See docs/saturn/PROVENANCE.md.
 */
#ifndef SM64_SATURN_Q16_SH2_H
#define SM64_SATURN_Q16_SH2_H

#include <stdint.h>

static inline int32_t
sm64_saturn_q16_mul_sh2(int32_t a, int32_t b)
{
#if defined(__sh__)
    int32_t result;
    __asm__ volatile(
        "dmuls.l %1,%2\n\t"
        "sts mach,r1\n\t"
        "sts macl,%0\n\t"
        "xtrct r1,%0"
        : "=&r" (result) : "r" (a), "r" (b) : "r1", "mach", "macl");
    return result;
#else
    return (int32_t)(((int64_t)a * (int64_t)b) >> 16);
#endif
}

#endif
