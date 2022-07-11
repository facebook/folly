/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#if defined(__AVX2__)

#define LABEL(x)     .L##x

.text
.p2align  5, 0x90
.global __folly_memset
.type   __folly_memset, @function
__folly_memset:
        .cfi_startproc

// RDI is the buffer
// RSI is the value
// RDX is length
        vmovd           %esi, %xmm0
        vpbroadcastb    %xmm0, %ymm0
        mov             %rdi, %rax
        cmp             $0x40, %rdx
        jae             LABEL(above_64)

LABEL(below_64):
        cmp             $0x20, %rdx
        jb              LABEL(below_32)
        vmovdqu         %ymm0, (%rdi)
        vmovdqu         %ymm0, -0x20(%rdi,%rdx)
        vzeroupper
        retq

.align 32
LABEL(below_32):
        cmp             $0x10, %rdx
        jae             LABEL(in_16_to_32)

LABEL(below_16):
        cmp             $0x4, %rdx
        jbe             LABEL(below_4)

LABEL(in_4_to_16):
        // Scalar stores from this point.
        vmovq           %xmm0, %rsi
        cmp             $0x7, %rdx
        jbe             LABEL(in_4_to_8)
        // Two 8-wide stores, up to 16 bytes.
        mov             %rsi, -0x8(%rdi, %rdx)
        mov             %rsi, (%rdi)
        vzeroupper
        retq

.align 32
LABEL(below_4):
        vmovq           %xmm0, %rsi
        vzeroupper
        cmp             $0x1, %rdx
        jbe             LABEL(none_or_one)
        mov             %si, (%rdi)
        mov             %si, -0x2(%rdi, %rdx)

LABEL(exit):
        retq

.align 16
LABEL(in_4_to_8):
        // two 4-wide stores, upto 8 bytes.
        mov             %esi, -0x4(%rdi,%rdx)
        mov             %esi, (%rdi)
        vzeroupper
        retq

.align 32
LABEL(in_16_to_32):
        vmovups         %xmm0, (%rdi)
        vmovups         %xmm0, -0x10(%rdi,%rdx)
        vzeroupper
        retq

LABEL(above_64):
        cmp             $0xb0, %rdx
        ja              LABEL(above_192)
        cmp             $0x80, %rdx
        jbe             LABEL(in_64_to_128)
        // Do some work filling unaligned 32bit words.
        // last_word -> rsi
        lea             -0x20(%rdi,%rdx), %rsi
        // rcx -> fill pointer.
        // We have at least 128 bytes to store.
        vmovdqu         %ymm0, (%rdi)
        vmovdqu         %ymm0, 0x20(%rdi)
        vmovdqu         %ymm0, 0x40(%rdi)
        add             $0x60, %rdi

.align 32
LABEL(fill_32):
        vmovdqu         %ymm0, (%rdi)
        add             $0x20, %rdi
        cmp             %rdi, %rsi
        ja              LABEL(fill_32)
        // Stamp the last unaligned store.
        vmovdqu         %ymm0, (%rsi)
        vzeroupper
        retq

.align 32
LABEL(in_64_to_128):
        // Last_word -> rsi
        vmovdqu         %ymm0, (%rdi)
        vmovdqu         %ymm0,  0x20(%rdi)
        vmovdqu         %ymm0, -0x40(%rdi,%rdx)
        vmovdqu         %ymm0, -0x20(%rdi,%rdx)
        vzeroupper
        retq

.align 32
LABEL(above_192):
// rdi is the buffer address
// rsi is the value
// rdx is length
        cmp             $0x1000, %rdx
        jae             LABEL(large_stosq)
        // Store the first unaligned 32 bytes.
        vmovdqu         %ymm0, (%rdi)
        // The first aligned word is stored in %rsi.
        mov             %rdi, %rsi
        mov             %rdi, %rax
        and             $0xffffffffffffffe0, %rsi
        lea             0x20(%rsi), %rsi
        // Compute the address of the last unaligned word into rdi.
        lea             -0x20(%rdx), %rdx
        add             %rdx, %rdi
        // Check if we can do a full 5x32B stamp.
        lea             0xa0(%rsi), %rcx
        cmp             %rcx, %rdi
        jb              LABEL(stamp_4)

LABEL(fill_192):
        vmovdqa         %ymm0, (%rsi)
        vmovdqa         %ymm0, 0x20(%rsi)
        vmovdqa         %ymm0, 0x40(%rsi)
        vmovdqa         %ymm0, 0x60(%rsi)
        vmovdqa         %ymm0, 0x80(%rsi)
        add             $0xa0, %rsi
        lea             0xa0(%rsi), %rcx
        cmp             %rcx, %rdi
        ja              LABEL(fill_192)

LABEL(fill_192_tail):
        cmp             %rsi, %rdi
        jb              LABEL(fill_192_done)
        vmovdqa         %ymm0, (%rsi)

        lea             0x20(%rsi), %rcx
        cmp             %rcx, %rdi
        jb              LABEL(fill_192_done)
        vmovdqa         %ymm0, 0x20(%rsi)

        lea             0x40(%rsi), %rcx
        cmp             %rcx, %rdi
        jb              LABEL(fill_192_done)
        vmovdqa         %ymm0, 0x40(%rsi)

        lea             0x60(%rsi), %rcx
        cmp             %rcx, %rdi
        jb              LABEL(fill_192_done)
        vmovdqa         %ymm0, 0x60(%rsi)

LABEL(last_wide_store):
        lea             0x80(%rsi), %rcx
        cmp             %rcx, %rdi
        jb              LABEL(fill_192_done)
        vmovdqa         %ymm0, 0x80(%rsi)

.align 16
LABEL(fill_192_done):
        // Stamp the last word.
        vmovdqu         %ymm0, (%rdi)
        vzeroupper
        // FIXME return buffer address
        ret

LABEL(stamp_4):
        vmovdqa         %ymm0, (%rsi)
        vmovdqa         %ymm0, 0x20(%rsi)
        vmovdqa         %ymm0, 0x40(%rsi)
        vmovdqa         %ymm0, 0x60(%rsi)
        jmp             LABEL(last_wide_store)

LABEL(large_stosq):
// rdi is the buffer address
// rsi is the value
// rdx is length
        vmovd           %xmm0, %rax
        mov             %rax, (%rdi)
        mov             %rdi, %rsi
        // Align rdi to 8B
        and             $0xfffffffffffffff8, %rdi
        lea             0x8(%rdi), %rdi
        // Fill buffer using stosq
        mov             %rdx, %rcx
        sub             $0x8, %rcx
        shrq            $0x3, %rcx
        // rcx - number of QWORD elements
        // rax - value
        // rdi - buffer pointer
        rep stosq
        // Fill last 16 bytes
        vmovdqu         %xmm0, -0x10(%rsi, %rdx)
        vzeroupper
        mov             %rsi, %rax
        ret

.align 16
LABEL(none_or_one):
        test            %rdx, %rdx
        je              LABEL(exit)
        // Store one and exit
        mov             %sil, (%rdi)
        ret

        .cfi_endproc
        .size       __folly_memset, .-__folly_memset

#ifdef FOLLY_MEMSET_IS_MEMSET
        .weak       memset
        memset = __folly_memset
#endif // FOLLY_MEMSET_IS_MEMSET

#endif // __AVX2__
#ifdef __linux__
        .section .note.GNU-stack,"",@progbits
#endif
