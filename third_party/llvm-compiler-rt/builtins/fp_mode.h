//===----- lib/fp_mode.h - Floaing-point environment mode utilities --C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is not part of the interface of this library.
//
// This file defines an interface for accessing hardware floating point
// environment mode.
//
//===----------------------------------------------------------------------===//

#ifndef FP_MODE_H
#define FP_MODE_H

typedef enum {
  CRT_FE_TONEAREST,
  CRT_FE_DOWNWARD,
  CRT_FE_UPWARD,
  CRT_FE_TOWARDZERO
} CRT_FE_ROUND_MODE;

// LOCAL CHANGE (sm64-saturn-port, 2026-08-21): upstream declares these and
// links compiler-rt's fp_mode.c, which costs a real call per arithmetic op on
// a CPU with no FPU status register to query. The SH-2 has no dynamic rounding
// mode at all -- GCC soft-fp is compiled here with a fixed round-to-nearest
// FP_ROUNDMODE and no exception handling -- so these are defined inline as the
// same constants fp_mode.c returns, and constant-fold away.
// Upstream declarations were:
//   CRT_FE_ROUND_MODE __fe_getround(void);
//   int __fe_raise_inexact(void);
static __inline CRT_FE_ROUND_MODE __fe_getround(void) { return CRT_FE_TONEAREST; }
static __inline int __fe_raise_inexact(void) { return 0; }

#endif // FP_MODE_H
