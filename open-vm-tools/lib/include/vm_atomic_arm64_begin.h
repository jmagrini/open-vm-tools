/*********************************************************
 * Copyright (C) 2017-2018,2022 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/*
 * vm_atomic_arm64_begin.h --
 *
 *      Define private macros used to implement ARM64 atomic operations.
 */

#if !(   defined _ATOMIC_H_                                                   \
      || defined _VM_UNINTERRUPTIBLE_H_                                       \
      || defined _WAIT_UNTIL_LIKELY_H_)
#   error "Only files which implement an atomic API can include this file!"
#endif

#include "vm_basic_asm_arm64.h"

/*
 * Today these are defines, but long-term these will be patchable globals
 * for ESXi kernel-mode code (and something similar for ESXi userspace code).
 *
 * Atomic_HaveLSE should be set to 1 for CPUs that have the LSE extenstion
 * and where the atomic instructions are known to have a performance benefit.
 * Seemingly, on some low-end chips (CA55) there may not be a benefit.
 *
 * Not every operation can be performed using a single-instruction atomic -
 * LSE doesn't cover all kinds of logical/arithmetic operations. For example,
 * there's an ldeor instruction, but not an ldorr. For cases, where there is no
 * combined instruction that atomically performs the load/store and the ALU
 * operation, we fall back to CAS or to LL/SC. On some uarches - e.g. Neoverse
 * N1 - CAS shows better behavior during heavy contention than LL/SC. LL/SC,
 * though, remains the safest option. Atomic_PreferCasForOps controls this.
 */

#ifdef VMK_ARM_LSE
#define Atomic_HaveLSE               1
#else
#define Atomic_HaveLSE               0
#endif
#define Atomic_PreferCasForOps       1

#define _VMATOM_LSE_HAVE(x)  _VMATOM_LSE_HAVE_##x
#define _VMATOM_LSE_HAVE_add 1
#define _VMATOM_LSE_HAVE_sub 0
#define _VMATOM_LSE_HAVE_eor 1
#define _VMATOM_LSE_HAVE_orr 0
#define _VMATOM_LSE_HAVE_and 0

#define Atomic_PreferLSE(op) (Atomic_HaveLSE && \
   (_VMATOM_LSE_HAVE(op) || Atomic_PreferCasForOps))

/*                      bit size, instruction suffix, register prefix, extend suffix */
#define _VMATOM_SIZE_8         8,                  b,               w,             b
#define _VMATOM_SIZE_16       16,                  h,               w,             h
#define _VMATOM_SIZE_32       32,                   ,               w,             w
#define _VMATOM_SIZE_64       64,                   ,               x,             x
/*
 * Expand 'size' (a _VMATOM_SIZE_*) into its 4 components, then expand
 * 'snippet' (a _VMATOM_SNIPPET_*)
 */
#define _VMATOM_X2(snippet, size, ...) snippet(size, __VA_ARGS__)
/* Prepend a prefix to 'shortSnippet' and 'shortSize'. */
#define _VMATOM_X(shortSnippet, shortSize, ...)                               \
   _VMATOM_X2(_VMATOM_SNIPPET_##shortSnippet, _VMATOM_SIZE_##shortSize,       \
              __VA_ARGS__)

/* Read relaxed (returned). */
#define _VMATOM_SNIPPET_R_NF(bs, is, rp, es, atm) ({                          \
   uint##bs _val;                                                             \
                                                                              \
   /* ldr is atomic if and only if 'atm' is aligned on #bs bits. */           \
   __asm__ __volatile__(                                                      \
      "ldr"#is" %"#rp"0, %1                                              \n\t"\
      : "=r" (_val)                                                           \
      : "m" (*atm)                                                            \
   );                                                                         \
                                                                              \
   _val;                                                                      \
})
#define _VMATOM_SNIPPET_R _VMATOM_SNIPPET_R_NF

/* Read acquire/seq_cst (returned). */
#define _VMATOM_SNIPPET_R_SC(bs, is, rp, es, atm) ({                          \
   uint##bs _val;                                                             \
                                                                              \
   /* ldar is atomic if and only if 'atm' is aligned on #bs bits. */          \
   __asm__ __volatile__(                                                      \
      "ldar"#is" %"#rp"0, %1                                             \n\t"\
      : "=r" (_val)                                                           \
      : "Q" (*atm)                                                            \
   );                                                                         \
                                                                              \
   _val;                                                                      \
})

/* Write relaxed. */
#define _VMATOM_SNIPPET_W_NF(bs, is, rp, es, atm, val) ({                     \
   /*                                                                         \
    * str is atomic if and only if 'atm' is aligned on #bs bits.              \
    *                                                                         \
    * Clearing the exclusive monitor is not required. The local monitor is    \
    * cleared on any exception return, and the global monitor (as per B2.10.2,\
    * ARM DDI 0487A.k) is cleared by a successful write.                      \
    */                                                                        \
   __asm__ __volatile__(                                                      \
      "str"#is" %"#rp"1, %0                                              \n\t"\
      : "=m" (*atm)                                                           \
      : "r" (val)                                                             \
   );                                                                         \
})
#define _VMATOM_SNIPPET_W _VMATOM_SNIPPET_W_NF

/* Write release/seq_cst. */
#define _VMATOM_SNIPPET_W_SC(bs, is, rp, es, atm, val) ({                     \
   /*                                                                         \
    * stlr is atomic if and only if 'atm' is aligned on #bs bits.             \
    *                                                                         \
    * Clearing the exclusive monitor is not required. The local monitor is    \
    * cleared on any exception return, and the global monitor (as per B2.10.2,\
    * ARM DDI 0487A.k) is cleared by a successful write.                      \
    */                                                                        \
   __asm__ __volatile__(                                                      \
      "stlr"#is" %"#rp"1, %0                                             \n\t"\
      : "=Q" (*atm)                                                           \
      : "r" (val)                                                             \
   );                                                                         \
})

/*
 * Since on x86, some atomic operations are using LOCK semantics, assumptions
 * have been made about the ordering these operations imply on surrounding
 * code. As a result, on arm64 we have to provide these same guarantees.
 * We do this by making use of DMB barriers both before and after the atomic
 * ldrx/strx sequences.
 */
#define _VMATOM_FENCE(fenced)                                                 \
   if (fenced) {                                                              \
      SMP_RW_BARRIER_RW();                                                    \
   }

/* Read (not returned), op with modval, write. */
#define _VMATOM_SNIPPET_OP(bs, is, rp, es, fenced, atm, op, modval) ({        \
   uint##bs _newval;                                                          \
                                                                              \
   _VMATOM_FENCE(fenced);                                                     \
   if (Atomic_PreferLSE(op)) {                                                \
      if (_VMATOM_LSE_HAVE(op)) {                                             \
         __asm__ __volatile__(                                                \
            ".arch armv8.2-a                                             \n\t"\
            "st" #op #is" %"#rp"1, %0                                    \n\t"\
            : "+Q" (*atm)                                                     \
            : "r" (modval)                                                    \
         );                                                                   \
      } else {                                                                \
         uint##bs _oldval;                                                    \
         uint##bs _clobberedval;                                              \
         __asm__ __volatile__(                                                \
            ".arch armv8.2-a                                             \n\t"\
            "   ldr"#is" %"#rp"1, %3                                     \n\t"\
            "1: mov      %"#rp"0, %"#rp"1                                \n\t"\
            "  "#op"     %"#rp"2, %"#rp"0, %"#rp"4                       \n\t"\
            "   cas"#is" %"#rp"1, %"#rp"2, %3                            \n\t"\
            "   cmp      %"#rp"0, %"#rp"1, uxt"#es"                      \n\t"\
            "   b.ne     1b                                              \n\t"\
            : "=&r" (_oldval),                                                \
              "=&r" (_clobberedval),                                          \
              "=&r" (_newval),                                                \
              "+Q" (*atm)                                                     \
            : "r" (modval)                                                    \
            : "cc"                                                            \
         );                                                                   \
      }                                                                       \
   } else {                                                                   \
      uint32 _failed;                                                         \
      __asm__ __volatile__(                                                   \
         "1: ldxr"#is" %"#rp"0, %2                                       \n\t"\
         "  "#op"      %"#rp"0, %"#rp"0, %"#rp"3                         \n\t"\
         "   stxr"#is" %w1    , %"#rp"0, %2                              \n\t"\
         "   cbnz      %w1    , 1b                                       \n\t"\
         : "=&r" (_newval),                                                   \
           "=&r" (_failed),                                                   \
           "+Q" (*atm)                                                        \
         : "r" (modval)                                                       \
      );                                                                      \
   }                                                                          \
   _VMATOM_FENCE(fenced);                                                     \
})

/* Read (returned), op with modval, write. */
#define _VMATOM_SNIPPET_ROP(bs, is, rp, es, fenced, atm, op, modval) ({       \
   uint##bs _newval;                                                          \
   uint##bs _oldval;                                                          \
                                                                              \
   _VMATOM_FENCE(fenced);                                                     \
   if (Atomic_PreferLSE(op)) {                                                \
      if (_VMATOM_LSE_HAVE(op)) {                                             \
         __asm__ __volatile__(                                                \
            ".arch armv8.2-a                                             \n\t"\
            "ld" #op #is" %"#rp"2, %"#rp"0, %1                           \n\t"\
            : "=r" (_oldval),                                                 \
              "+Q" (*atm)                                                     \
            : "r" (modval)                                                    \
         );                                                                   \
      } else {                                                                \
         uint##bs _clobberedval;                                              \
         __asm__ __volatile__(                                                \
            ".arch armv8.2-a                                             \n\t"\
            "   ldr"#is"  %"#rp"1, %3                                    \n\t"\
            "1: mov      %"#rp"0, %"#rp"1                                \n\t"\
            "  "#op"     %"#rp"2, %"#rp"0, %"#rp"4                       \n\t"\
            "   cas"#is" %"#rp"1, %"#rp"2, %3                            \n\t"\
            "   cmp      %"#rp"0, %"#rp"1, uxt"#es"                      \n\t"\
            "   b.ne     1b                                              \n\t"\
            : "=&r" (_oldval),                                                \
              "=&r" (_clobberedval),                                          \
              "=&r" (_newval),                                                \
              "+Q" (*atm)                                                     \
            : "r" (modval)                                                    \
            : "cc"                                                            \
         );                                                                   \
      }                                                                       \
   } else {                                                                   \
      uint32 _failed;                                                         \
      __asm__ __volatile__(                                                   \
         "1: ldxr"#is" %"#rp"0, %3                                       \n\t"\
         "  "#op"      %"#rp"1, %"#rp"0, %"#rp"4                         \n\t"\
         "   stxr"#is" %w2    , %"#rp"1, %3                              \n\t"\
         "   cbnz      %w2    , 1b                                       \n\t"\
         : "=&r" (_oldval),                                                   \
           "=&r" (_newval),                                                   \
           "=&r" (_failed),                                                   \
           "+Q" (*atm)                                                        \
         : "r" (modval)                                                       \
      );                                                                      \
   }                                                                          \
   _VMATOM_FENCE(fenced);                                                     \
                                                                              \
   _oldval;                                                                   \
})

/* Read (returned), write. */
#define _VMATOM_SNIPPET_RW(bs, is, rp, es, fenced, atm, val) ({               \
   uint##bs _oldval;                                                          \
                                                                              \
   _VMATOM_FENCE(fenced);                                                     \
   if (Atomic_HaveLSE) {                                                      \
      __asm__ __volatile__(                                                   \
         ".arch armv8.2-a                                                \n\t"\
         "swp"#is" %"#rp"2, %"#rp"0, %1                                  \n\t"\
         : "=r" (_oldval),                                                    \
           "+Q" (*atm)                                                        \
         : "r" (val)                                                          \
      );                                                                      \
   } else {                                                                   \
      uint32 _failed;                                                         \
      __asm__ __volatile__(                                                   \
         "1: ldxr"#is" %"#rp"0, %2                                       \n\t"\
         "   stxr"#is" %w1    , %"#rp"3, %2                              \n\t"\
         "   cbnz      %w1    , 1b                                       \n\t"\
         : "=&r" (_oldval),                                                   \
           "=&r" (_failed),                                                   \
           "+Q" (*atm)                                                        \
         : "r" (val)                                                          \
      );                                                                      \
   }                                                                          \
   _VMATOM_FENCE(fenced);                                                     \
                                                                              \
   _oldval;                                                                   \
})

/* Read (returned), if equal to old then write new. */
#define _VMATOM_SNIPPET_RIFEQW(bs, is, rp, es, fenced, atm, old, new) ({      \
   uint##bs _oldval;                                                          \
                                                                              \
   _VMATOM_FENCE(fenced);                                                     \
   if (Atomic_HaveLSE) {                                                      \
      __asm__ __volatile__(                                                   \
         ".arch armv8.2-a                                                \n\t"\
         "cas"#is" %"#rp"0, %"#rp"2, %1                                  \n\t"\
         : "=r" (_oldval),                                                    \
           "+Q" (*atm)                                                        \
         : "r" (new), "0" (old)                                               \
      );                                                                      \
   } else {                                                                   \
      uint32 _failed;                                                         \
      __asm__ __volatile__(                                                   \
         "1: ldxr"#is" %"#rp"0, %2                                       \n\t"\
         "   cmp       %"#rp"0, %"#rp"3, uxt"#es"                        \n\t"\
         "   b.ne      2f                                                \n\t"\
         "   stxr"#is" %w1    , %"#rp"4, %2                              \n\t"\
         "   cbnz      %w1    , 1b                                       \n\t"\
         "2:                                                             \n\t"\
         : "=&r" (_oldval),                                                   \
           "=&r" (_failed),                                                   \
           "+Q" (*atm)                                                        \
         : "r" (old),                                                         \
           "r" (new)                                                          \
         : "cc"                                                               \
      );                                                                      \
   }                                                                          \
   _VMATOM_FENCE(fenced);                                                     \
                                                                              \
   _oldval;                                                                   \
})
