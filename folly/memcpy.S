/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * __folly_memcpy
 *
 * This implementation of memcpy acts as a memmove: while overlapping copies
 * are undefined in memcpy, in some implementations they're the same function and
 * legacy programs rely on this behavior.
 *
 * For sizes up to 256 all source data is first read into registers and then written:
 * - n <=  16: overlapping movs
 * - n <=  32: overlapping unaligned 16-byte SSE XMM load/stores
 * - n <= 256: overlapping unaligned 32-byte AVX YMM load/stores
 *
 * For n > 256:
 * - for src >= dst, forward copy:
 *   - if n >= REP_MOVSB_THRESHOLD, use rep movsb
 *   - otherwise, copy in 128 byte batches
 * - for src < dst && (src + n) <= dst, forward copy:
 *   - if n >= REP_MOVSB_THRESHOLD, use rep movsb
 *   - otherwise, copy in 128 byte batches
 * - for src < dst && (src + n) > dst, backward copy in 128 byte batches:
 *   - unaligned load the first 4 x 32 bytes & last 32 bytes
 *   - backward copy (unaligned load + aligned stores) 4 x 32 bytes at a time
 *   - unaligned store the first 4 x 32 bytes & last 32 bytes
 */

#if defined(__AVX2__)

#define REP_MOVSB_THRESHOLD $1024

        .file       "memcpy.S"
        .section    .text,"ax"

        .type       __folly_memcpy_short, @function
__folly_memcpy_short:
        .cfi_startproc

.L_GE1_LE7:
        cmp         $1, %rdx
        je          .L_EQ1

        cmp         $4, %rdx
        jae         .L_GE4_LE7

.L_GE2_LE3:
        movw        (%rsi), %r8w
        movw        -2(%rsi,%rdx), %r9w
        movw        %r8w, (%rdi)
        movw        %r9w, -2(%rdi,%rdx)
        ret

        .align      2
.L_EQ1:
        movb        (%rsi), %r8b
        movb        %r8b, (%rdi)
        ret

        // Aligning the target of a jump to an even address has a measurable
        // speedup in microbenchmarks.
        .align      2
.L_GE4_LE7:
        movl        (%rsi), %r8d
        movl        -4(%rsi,%rdx), %r9d
        movl        %r8d, (%rdi)
        movl        %r9d, -4(%rdi,%rdx)
        ret

        .cfi_endproc
        .size       __folly_memcpy_short, .-__folly_memcpy_short

// memcpy is an alternative entrypoint into the function named __folly_memcpy.
// The compiler is able to call memcpy since the name is global while
// stacktraces will show __folly_memcpy since that is the name of the function.
// This is intended to aid in debugging by making it obvious which version of
// memcpy is being used.
        .align      64
        .globl      __folly_memcpy
        .type       __folly_memcpy, @function

__folly_memcpy:
        .cfi_startproc

        mov         %rdi, %rax    # return: $rdi

        test        %rdx, %rdx
        je          .L_EQ0

        cmp         $8, %rdx
        jb          .L_GE1_LE7

.L_GE8:
        cmp         $32, %rdx
        ja          .L_GE33

.L_GE8_LE32:
        cmp         $16, %rdx
        ja          .L_GE17_LE32

.L_GE8_LE16:
        mov         (%rsi), %r8
        mov         -8(%rsi,%rdx), %r9
        mov         %r8, (%rdi)
        mov         %r9, -8(%rdi,%rdx)
.L_EQ0:
        ret

        .align      2
.L_GE17_LE32:
        movdqu      (%rsi), %xmm0
        movdqu      -16(%rsi,%rdx), %xmm1
        movdqu      %xmm0, (%rdi)
        movdqu      %xmm1, -16(%rdi,%rdx)
        ret

        .align      2
.L_GE193_LE256:
        vmovdqu     %ymm3, 96(%rdi)
        vmovdqu     %ymm4, -128(%rdi,%rdx)

.L_GE129_LE192:
        vmovdqu     %ymm2, 64(%rdi)
        vmovdqu     %ymm5, -96(%rdi,%rdx)

.L_GE65_LE128:
        vmovdqu     %ymm1, 32(%rdi)
        vmovdqu     %ymm6, -64(%rdi,%rdx)

.L_GE33_LE64:
        vmovdqu     %ymm0, (%rdi)
        vmovdqu     %ymm7, -32(%rdi,%rdx)

        vzeroupper
        ret

        .align      2
.L_GE33:
        cmp         $256, %rdx
        ja          .L_GE257

.L_GE33_LE256:
        vmovdqu     (%rsi), %ymm0
        vmovdqu     -32(%rsi,%rdx), %ymm7

        cmp         $64, %rdx
        jbe         .L_GE33_LE64

        vmovdqu     32(%rsi), %ymm1
        vmovdqu     -64(%rsi,%rdx), %ymm6

        cmp         $128, %rdx
        jbe         .L_GE65_LE128

        vmovdqu     64(%rsi), %ymm2
        vmovdqu     -96(%rsi,%rdx), %ymm5

        cmp         $192, %rdx
        jbe         .L_GE129_LE192

        vmovdqu     96(%rsi), %ymm3
        vmovdqu     -128(%rsi,%rdx), %ymm4

        cmp         $256, %rdx
        jbe         .L_GE193_LE256

.L_GE257:
        cmp         %rdi, %rsi
        jae         .L_COPY_FORWARD     # if src >= dst, copy forward

        lea         (%rsi,%rdx), %r10   # r10 = (src + n)
        cmp         %rdi, %r10          # if src < dst && (src + n) > dst, copy backward
        ja          .L_OVERLAP_BWD      # otherwise, fall through to copy forward

.L_COPY_FORWARD:
        mov         %rdx, %rcx          # rcx is the copy length n
        cmp         REP_MOVSB_THRESHOLD, %rdx
        jb          .L_COPY_FORWARD_WITH_LOOP

.L_COPY_FORWARD_WITH_REP_MOVSB:
        rep movsb
        ret

.L_COPY_FORWARD_WITH_LOOP:
        vmovdqu     -32(%rsi,%rdx), %ymm4
        xor         %r8, %r8            # r8 is the length that has been copied
        shr         $7, %rcx            # rcx = n/128
        shl         $7, %rcx            # rcx is the length that should be copied by the loop

        .align 16
.L_COPY_FORWARD_LOOP_BODY:
        vmovdqu     (%rsi,%r8), %ymm0
        vmovdqu     32(%rsi,%r8), %ymm1
        vmovdqu     64(%rsi,%r8), %ymm2
        vmovdqu     96(%rsi,%r8), %ymm3
        vmovdqu     %ymm0, (%rdi,%r8)
        vmovdqu     %ymm1, 32(%rdi,%r8)
        vmovdqu     %ymm2, 64(%rdi,%r8)
        vmovdqu     %ymm3, 96(%rdi,%r8)
        add         $128, %r8
        cmp         %rcx, %r8
        jb          .L_COPY_FORWARD_LOOP_BODY

        mov         %rdx, %rcx          # rcx is the original length n
        sub         %r8, %rdx           # rdx is the tail length
        cmp         $32, %rdx
        jbe         .L_TAIL_LE32
        add         %r8, %rsi           # rsi is the tail of src
        add         %r8, %rdi           # rdi is the tail of dst
        jmp         .L_GE33_LE256

.L_TAIL_LE32:
        vmovdqu     %ymm4, -32(%rdi,%rcx)
        vzeroupper
        ret

.L_OVERLAP_BWD:
        # Save last 32 bytes.
        vmovdqu     -32(%rsi, %rdx), %ymm8
        lea         -32(%rdi, %rdx), %r9
        vmovdqu     (%rsi),   %ymm0     # save the first 128 bytes to ymm0/1/2/3
        vmovdqu     32(%rsi), %ymm1
        vmovdqu     64(%rsi), %ymm2
        vmovdqu     96(%rsi), %ymm3
        // %r8 is the end condition for the loop.
        lea         128(%rsi), %r8

        // Align %rdi+%rdx (destination end) to a 32 byte boundary.
        // %rcx = (%rdi + %rdx - 32) & 31
        mov         %r9, %rcx
        and         $31, %rcx
        // Set %rsi & %rdi to the end of the 32 byte aligned range.
        sub         %rcx, %rdx
        add         %rdx, %rsi
        add         %rdx, %rdi

        .align 16
.L_OVERLAP_BWD_ALIGNED_DST_LOOP:
        vmovdqu      -32(%rsi), %ymm4
        vmovdqu      -64(%rsi), %ymm5
        vmovdqu      -96(%rsi), %ymm6
        vmovdqu     -128(%rsi), %ymm7
        sub         $128, %rsi

        vmovdqa     %ymm4,  -32(%rdi)
        vmovdqa     %ymm5,  -64(%rdi)
        vmovdqa     %ymm6,  -96(%rdi)
        vmovdqa     %ymm7, -128(%rdi)
        sub         $128, %rdi

        cmp         %r8, %rsi
        ja          .L_OVERLAP_BWD_ALIGNED_DST_LOOP

        vmovdqu     %ymm0,   (%rax)  // %rax == the original unaligned %rdi
        vmovdqu     %ymm1, 32(%rax)
        vmovdqu     %ymm2, 64(%rax)
        vmovdqu     %ymm3, 96(%rax)
        vmovdqu     %ymm8, (%r9)

        vzeroupper
	ret

        .cfi_endproc
        .size       __folly_memcpy, .-__folly_memcpy

#ifdef FOLLY_MEMCPY_IS_MEMCPY
        .weak       memcpy
        memcpy = __folly_memcpy

        .weak       memmove
        memmove = __folly_memcpy
#endif

        .ident "GCC: (GNU) 4.8.2"

#endif
#ifdef __linux__
        .section .note.GNU-stack,"",@progbits
#endif
