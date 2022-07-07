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
 * __folly_memcpy: An optimized memcpy implementation that uses prefetch and
 * AVX2 instructions.
 *
 * This implementation of memcpy acts as a memmove: while overlapping copies
 * are undefined in memcpy, in some implementations they're the same function and
 * legacy programs rely on this behavior.
 *
 * This implementation uses prefetch to avoid dtlb misses. This can
 * substantially reduce dtlb store misses in cases where the destination
 * location is absent from L1 cache and where the copy size is small enough
 * that the hardware prefetcher doesn't have a large impact.
 *
 * The number of branches is limited by the use of overlapping loads & stores.
 * This helps with copies where the source and destination cache lines are already
 * present in L1 because there are fewer instructions to execute and fewer
 * branches to potentially mispredict.
 *   e.g. to copy the last 4 <= n <= 7 bytes: copy the first & last 4 bytes (overlapped):
 *      movl        (%rsi), %r8d
 *      movl        -4(%rsi,%rdx), %r9d
 *      movl        %r8d, (%rdi)
 *      movl        %r9d, -4(%rdi,%rdx)
 *
 *
 * For sizes up to 256 all source data is first read into registers and then written:
 * - n <=  16: overlapping movs
 * - n <=  32: overlapping unaligned 16-byte SSE XMM load/stores
 * - n <= 256: overlapping unaligned 32-byte AVX YMM load/stores
 *
 * Large copies (> 256 bytes) use unaligned loads + aligned stores.
 * This is observed to always be faster than rep movsb, so the rep movsb
 * instruction is not used.
 * - The head & tail may be unaligned => they're always written using unaligned stores.
 *
 * If the copy size is humongous (> 32 KiB) and the source and destination are both
 * aligned, this memcpy will use non-temporal operations (AVX2). This can have
 * a substantial speedup for copies where data is absent from L1, but it
 * is significantly slower if the source and destination data were already
 * in L1. The use of non-temporal operations also has the effect that after
 * the copy is complete, the data will be moved out of L1, even if the data was
 * present before the copy started.
 *
 * For n > 256 and overlapping src & dst buffers (memmove):
 * - use unaligned loads + aligned stores, but not non-temporal stores
 * - for dst < src forward copy in 128 byte batches:
 *   - unaligned load the first 32 bytes & last 4 x 32 bytes
 *   - forward copy (unaligned load + aligned stores) 4 x 32 bytes at a time
 *   - unaligned store the first 32 bytes & last 4 x 32 bytes
 * - for dst > src backward copy in 128 byte batches:
 *   - unaligned load the first 4 x 32 bytes & last 32 bytes
 *   - backward copy (unaligned load + aligned stores) 4 x 32 bytes at a time
 *   - unaligned store the first 4 x 32 bytes & last 32 bytes
 *
 * @author Logan Evans <lpe@fb.com>
 */

#if defined(__AVX2__)

#if defined(PREFETCH)
#undef PREFETCH
#endif
#if __PRFCHW__ // Broadwell+
#define PREFETCH prefetchw
#else
#define PREFETCH prefetcht0
#endif

// This threshold is half of L1 cache on a Skylake machine, which means that
// potentially all of L1 will be populated by this copy once it is executed
// (dst and src are cached for temporal copies).
#define NON_TEMPORAL_STORE_THRESHOLD $32768

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

        PREFETCH    (%rdi)
        PREFETCH    -1(%rdi,%rdx)

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
        vmovdqu     (%rsi), %ymm0
        vmovdqu     -32(%rsi,%rdx), %ymm7

        cmp         $64, %rdx
        jbe         .L_GE33_LE64

        PREFETCH    64(%rdi)

        vmovdqu     32(%rsi), %ymm1
        vmovdqu     -64(%rsi,%rdx), %ymm6

        cmp         $128, %rdx
        jbe         .L_GE65_LE128

        PREFETCH    128(%rdi)

        vmovdqu     64(%rsi), %ymm2
        vmovdqu     -96(%rsi,%rdx), %ymm5

        cmp         $192, %rdx
        jbe         .L_GE129_LE192

        PREFETCH    192(%rdi)

        vmovdqu     96(%rsi), %ymm3
        vmovdqu     -128(%rsi,%rdx), %ymm4

        cmp         $256, %rdx
        jbe         .L_GE193_LE256

.L_GE257:
        PREFETCH    256(%rdi)

        // Check if there is an overlap. If there is an overlap then the caller
        // has a bug since this is undefined behavior. However, for legacy
        // reasons this behavior is expected by some callers.
        //
        // All copies through 256 bytes will operate as a memmove since for
        // those sizes all reads are performed before any writes.
        //
        // This check uses the idea that there is an overlap if
        // (%rdi < (%rsi + %rdx)) && (%rsi < (%rdi + %rdx)),
        // or equivalently, there is no overlap if
        // ((%rsi + %rdx) <= %rdi) || ((%rdi + %rdx) <= %rsi).
        //
        // %r9 will be used after .L_ALIGNED_DST_LOOP to calculate how many
        // bytes remain to be copied.

        // (%rsi + %rdx <= %rdi) => no overlap
        lea         (%rsi,%rdx), %r9
        cmp         %rdi, %r9
        jbe         .L_NO_OVERLAP

        // (%rdi + %rdx <= %rsi) => no overlap
        lea         (%rdi,%rdx), %r8
        cmp         %rsi, %r8
        // If no info is available in branch predictor's cache, Intel CPUs assume
        // forward jumps are not taken. Use a forward jump as overlapping buffers
        // are unlikely.
        ja          .L_OVERLAP

        .align      2
.L_NO_OVERLAP:
        vmovdqu     %ymm0, (%rdi)
        vmovdqu     %ymm1, 32(%rdi)
        vmovdqu     %ymm2, 64(%rdi)
        vmovdqu     %ymm3, 96(%rdi)

        // Align %rdi to a 32 byte boundary.
        // %rcx = 128 - 31 & %rdi
        mov         $128, %rcx
        and         $31, %rdi
        sub         %rdi, %rcx

        lea         (%rsi,%rcx), %rsi
        lea         (%rax,%rcx), %rdi
        sub         %rcx, %rdx

        // %r8 is the end condition for the loop.
        lea         -128(%rsi,%rdx), %r8

        cmp         NON_TEMPORAL_STORE_THRESHOLD, %rdx
        jae         .L_NON_TEMPORAL_LOOP

        .align      2
.L_ALIGNED_DST_LOOP:
        PREFETCH    128(%rdi)
        PREFETCH    192(%rdi)

        vmovdqu     (%rsi), %ymm0
        vmovdqu     32(%rsi), %ymm1
        vmovdqu     64(%rsi), %ymm2
        vmovdqu     96(%rsi), %ymm3
        add         $128, %rsi

        vmovdqa     %ymm0, (%rdi)
        vmovdqa     %ymm1, 32(%rdi)
        vmovdqa     %ymm2, 64(%rdi)
        vmovdqa     %ymm3, 96(%rdi)
        add         $128, %rdi

        cmp         %r8, %rsi
        jb          .L_ALIGNED_DST_LOOP

.L_ALIGNED_DST_LOOP_END:
        sub         %rsi, %r9
        mov         %r9, %rdx

        vmovdqu     %ymm4, -128(%rdi,%rdx)
        vmovdqu     %ymm5, -96(%rdi,%rdx)
        vmovdqu     %ymm6, -64(%rdi,%rdx)
        vmovdqu     %ymm7, -32(%rdi,%rdx)

        vzeroupper
        ret

        .align      2
.L_NON_TEMPORAL_LOOP:
        testb       $31, %sil
        jne         .L_ALIGNED_DST_LOOP
        // This is prefetching the source data unlike ALIGNED_DST_LOOP which
        // prefetches the destination data. This choice is again informed by
        // benchmarks. With a non-temporal store the entirety of the cache line
        // is being written so the previous data can be discarded without being
        // fetched.
        prefetchnta 128(%rsi)
        prefetchnta 196(%rsi)

        vmovntdqa   (%rsi), %ymm0
        vmovntdqa   32(%rsi), %ymm1
        vmovntdqa   64(%rsi), %ymm2
        vmovntdqa   96(%rsi), %ymm3
        add         $128, %rsi

        vmovntdq    %ymm0, (%rdi)
        vmovntdq    %ymm1, 32(%rdi)
        vmovntdq    %ymm2, 64(%rdi)
        vmovntdq    %ymm3, 96(%rdi)
        add         $128, %rdi

        cmp         %r8, %rsi
        jb          .L_NON_TEMPORAL_LOOP

        sfence
        jmp         .L_ALIGNED_DST_LOOP_END


.L_OVERLAP:
        .align      2
        cmp         %rdi, %rsi
        jb          .L_OVERLAP_BWD  // %rsi  < %rdi => backward-copy
        je          .L_RET          // %rsi == %rdi => return, nothing to copy

        // Source & destination buffers overlap. Forward copy.

        vmovdqu     (%rsi), %ymm8

        // Align %rdi to a 32 byte boundary.
        // %rcx = 32 - 31 & %rdi
        mov         $32, %rcx
        and         $31, %rdi
        sub         %rdi, %rcx

        lea         (%rsi,%rcx), %rsi
        lea         (%rax,%rcx), %rdi
        sub         %rcx, %rdx

        // %r8 is the end condition for the loop.
        lea         -128(%rsi,%rdx), %r8


.L_OVERLAP_FWD_ALIGNED_DST_LOOP:
        PREFETCH    128(%rdi)
        PREFETCH    192(%rdi)

        vmovdqu       (%rsi), %ymm0
        vmovdqu     32(%rsi), %ymm1
        vmovdqu     64(%rsi), %ymm2
        vmovdqu     96(%rsi), %ymm3
        add         $128, %rsi

        vmovdqa     %ymm0,   (%rdi)
        vmovdqa     %ymm1, 32(%rdi)
        vmovdqa     %ymm2, 64(%rdi)
        vmovdqa     %ymm3, 96(%rdi)
        add         $128, %rdi

        cmp         %r8, %rsi
        jb          .L_OVERLAP_FWD_ALIGNED_DST_LOOP

        sub         %rsi, %r9
        mov         %r9, %rdx

        vmovdqu     %ymm4, -128(%rdi,%rdx)
        vmovdqu     %ymm5,  -96(%rdi,%rdx)
        vmovdqu     %ymm6,  -64(%rdi,%rdx)
        vmovdqu     %ymm7,  -32(%rdi,%rdx)
        vmovdqu     %ymm8, (%rax)  // %rax == the original (unaligned) %rdi

        vzeroupper

.L_RET:
        ret

.L_OVERLAP_BWD:
        # Save last 32 bytes.
        vmovdqu     -32(%rsi, %rdx), %ymm8
        lea         -32(%rdi, %rdx), %r9


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


.L_OVERLAP_BWD_ALIGNED_DST_LOOP:
        PREFETCH    -128(%rdi)
        PREFETCH    -192(%rdi)

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
