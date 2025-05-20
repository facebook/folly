/*
 * memchr - find a character in a memory zone
 *
 * Copyright (c) 2014-2022, Arm Limited.
 * Copyright (c) 2025, Nvidia
 * SPDX-License-Identifier: MIT OR Apache-2.0 WITH LLVM-exception
 */

/* Assumptions:
 *
 * ARMv8-a, AArch64
 * Neon Available, sha-512 Available
 */

#include "asmdefs.h"

.arch armv8-a+sha2

/* Arguments and results.  */
#define srcin           x0
#define chrin           w1
#define cntin           x2

#define result          x0

#define src             x3
#define tmp             x4
#define wtmp2           w5
#define synd            x6
#define soff            x7
#define cntrem          x8
#define end_addr        x9

#define vrepchr         v0
#define vdata1          v1
#define vdata2          v2
#define vdata3          v3
#define vdata4          v4
#define vdata5          v5
#define vdata6          v6
#define vdata7          v7
#define vhas_chr1       v8
#define vhas_chr1q      q8
#define vhas_chr2       v9
#define vhas_chr2q      q9
#define vhas_chr3       v10
#define vhas_chr4       v11
#define vhas_chr4q      q11
#define vhas_chr5       v12
#define vhas_chr5q      q12
#define vhas_chr6       v13
#define vhas_chr7       v14
#define vhas_chr7q      q14
#define vrepmask        v15
#define vend            v16

.macro  CHECK_PAIR regA, regB
    cmeq    vhas_chr1.16b, \regA\().16b, vrepchr.16b
    cmeq    vhas_chr2.16b, \regB\().16b, vrepchr.16b
    and     vhas_chr1.16b, vhas_chr1.16b, vrepmask.16b
    and     vhas_chr2.16b, vhas_chr2.16b, vrepmask.16b
    addp    vend.16b, vhas_chr1.16b, vhas_chr2.16b
    addp    vend.16b, vend.16b, vend.16b  // reduces 128 -> 64 bits
    mov     synd, vend.d[0]
    cbnz    synd, L(tail)
.endm

.macro  CHECK_SINGLE reg
    cmeq    vhas_chr2.16b, \reg\().16b, vrepchr.16b
    and     vhas_chr2.16b, vhas_chr2.16b, vrepmask.16b
    // Here we addp the same register to itself, effectively computing OR 
    // across lanes again, then reduce to 64 bits:
    addp    vend.16b, vhas_chr2.16b, vhas_chr2.16b 
    addp    vend.16b, vend.16b, vend.16b
    mov     synd, vend.d[0]
    cbnz    synd, L(tail)
.endm

/*
 * Core algorithm:
 * Unrolled to scan blocks of 112 bytes for match using SHA512H instruction
 * for main loop reduction - should only compile for devices with h/w SHA512H
 * instruction support. It was proven using SAT solver that input chaining
 * for SHA512H instruction used below produces 0 result if and only if all
 * 112 input bytes are 0. This version is not compatible with MTE. After match,
 * for each 32-byte chunk we calculate a 64-bit syndrome value, with two bits
 * per byte. For each tuple, bit 0 is set if the relevant byte matched the
 * requested character and bit 1 is not used (faster than using a 32bit
 * syndrome). Since the bits in the syndrome reflect exactly the order in which
 * things occur in the original string, counting trailing zeros allows to
 * identify exactly which byte has matched. 
 */

ENTRY (__folly_memchr_long_aarch64_sha512)
	/* Do not dereference srcin if no bytes to compare.  */
	cbz	cntin, L(zero_length)
        add end_addr, srcin, cntin
	/*
	 * Magic constant 0x40100401 allows us to identify which lane matches
	 * the requested byte.
	 */
	mov	wtmp2, #0x0401
	movk	wtmp2, #0x4010, lsl #16
	dup	vrepchr.16b, chrin

	/* Work with aligned 32-byte chunks */
	bic	src, srcin, #31
	dup	vrepmask.4s, wtmp2
	ands soff, srcin, #31
	and	cntrem, cntin, #31
	b.eq	L(loop_112)

	/*
	 * Input string is not 32-byte aligned. We calculate the syndrome
	 * value for the aligned 32 bytes block containing the first bytes
	 * and mask the irrelevant part.
	 */
	ld1	{vdata1.16b, vdata2.16b}, [src], #32
	sub	tmp, soff, #32
	adds	cntin, cntin, tmp
	cmeq	vhas_chr1.16b, vdata1.16b, vrepchr.16b
	cmeq	vhas_chr2.16b, vdata2.16b, vrepchr.16b
	and	vhas_chr1.16b, vhas_chr1.16b, vrepmask.16b
	and	vhas_chr2.16b, vhas_chr2.16b, vrepmask.16b
	addp	vend.16b, vhas_chr1.16b, vhas_chr2.16b		/* 256->128 */
	addp	vend.16b, vend.16b, vend.16b			/* 128->64 */
	mov	synd, vend.d[0]

	/* Clear the soff*2 lower bits */
	lsl	tmp, soff, #1
	lsr	synd, synd, tmp
	lsl	synd, synd, tmp
	/* Have we found something already? */
	cbnz	synd, L(tail_32)
	/* The first block can also be the last, and we did not find anything */
	b.ls	L(zero_length)

L(loop_112):
        subs    cntin, cntin, #112
        b.ls    L(load_partial)
        ld1     {vdata1.16b, vdata2.16b, vdata3.16b, vdata4.16b}, [src], #64
        ld1     {vdata5.16b, vdata6.16b, vdata7.16b}, [src], #48
        cmeq    vhas_chr1.16b, vdata1.16b, vrepchr.16b
        cmeq    vhas_chr2.16b, vdata2.16b, vrepchr.16b
        cmeq    vhas_chr3.16b, vdata3.16b, vrepchr.16b
        cmeq    vhas_chr4.16b, vdata4.16b, vrepchr.16b
        /* If all inputs to SHA512H are outputs of CMPEQ,
           then it returns 0 if and only if all bytes in
           X, Y and W inputs are 0.
        */
        sha512h vhas_chr1q, vhas_chr2q, vhas_chr3.2d
        cmeq    vhas_chr5.16b, vdata5.16b, vrepchr.16b
        cmeq    vhas_chr6.16b, vdata6.16b, vrepchr.16b
        sha512h vhas_chr4q, vhas_chr5q, vhas_chr6.2d
        cmeq    vhas_chr7.16b, vdata7.16b, vrepchr.16b
        /* If Y and W inputs to this SHA512H are outputs of
           SHA512H, which only had inputs from CMPEQ calls,
           and X is output from CMPEQ call, then it returns 0
           if and only if all 7 inputs are 0.
        */
        sha512h vhas_chr1q, vhas_chr7q, vhas_chr4.2d
        addp    vend.2d, vhas_chr1.2d, vhas_chr1.2d
        mov     synd, vend.d[0]
        cbz     synd, L(loop_112)

	add cntin, cntin, #112
	b L(pt_1_7)

L(load_partial):
	add cntin, cntin, #112
	ld1 {vdata1.16b, vdata2.16b}, [src], #32
	subs cntin, cntin, #32
	b.ls L(pt_1_2)

	ld1 {vdata3.16b, vdata4.16b}, [src], #32
	subs cntin, cntin, #32
	b.ls L(pt_1_4)

	ld1 {vdata5.16b, vdata6.16b}, [src], #32
	subs cntin, cntin, #32
	b.ls L(pt_1_6)

	ld1 {vdata7.16b}, [src], #16
	subs cntin, cntin, #16
	b.ls L(pt_1_7)

// Now define four partial‐tail labels, each rewinding SRC by exactly the total amount actually loaded, and re‐checking those loaded registers in correct order:
L(pt_1_2):
	sub src, src, #32
	CHECK_PAIR vdata1, vdata2
	b L(zero_length)

L(pt_1_4):
	sub src, src, #64
	CHECK_PAIR vdata1, vdata2

	add src, src, #32
	CHECK_PAIR vdata3, vdata4
	b L(zero_length)

L(pt_1_6):
	sub src, src, #96
	CHECK_PAIR vdata1, vdata2

	add src, src, #32
	CHECK_PAIR vdata3, vdata4

	add src, src, #32
	CHECK_PAIR vdata5, vdata6
	b L(zero_length)

L(pt_1_7):
	sub src, src, #112
	CHECK_PAIR vdata1, vdata2

	add src, src, #32
	CHECK_PAIR vdata3, vdata4

	add src, src, #32
	CHECK_PAIR vdata5, vdata6

	add src, src, #32
	CHECK_SINGLE vdata7
	b L(zero_length)

L(tail_32):
	/* Compensate the first post-increment */
	sub	src, src, #32

L(tail):
	/* Count the trailing zeros using bit reversing */
	rbit	synd, synd
	/* Check that we have found a character */
	cmp	synd, #0
	/* And count the leading zeros */
	clz	synd, synd
	/* Compute the potential result */
	add	result, src, synd, lsr #1
        /* Check if the result is out of bounds because of speculative load */
        cmp result, end_addr
        csel result, xzr, result, hi
	/* Select result or NULL */
	csel	result, xzr, result, eq
	ret

L(zero_length):
	mov	result, #0
	ret

END (__folly_memchr_long_aarch64_sha512)

