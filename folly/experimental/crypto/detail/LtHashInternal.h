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

#pragma once

#include <folly/Range.h>

namespace folly {
namespace crypto {
namespace detail {

// As of 2019, most (or all?) modern Intel CPUs have 64-byte L1 cache lines,
// and aligning data buffers on cache line boundaries on such CPUs
// noticeably benefits performance (up to 10% difference).
//
// If you change this, code that depends on it in MathOperation_*.cpp may
// break and could need fixing.
constexpr size_t kCacheLineSize = 64;

// Invariants about kCacheLineSize that other logic depends on: it must be
// a power of 2 and cannot be zero.
static_assert(kCacheLineSize > 0, "kCacheLineSize cannot be 0");
static_assert(
    (kCacheLineSize & (kCacheLineSize - 1)) == 0,
    "kCacheLineSize must be a power of 2");

/**
 * Defines available math engines that we can use to perform element-wise
 * modular addition or subtraction of element vectors.
 * - AUTO: pick the best available, from best to worst: AVX2, SSE2, SIMPLE
 * - SIMPLE: perform addition/subtraction using uint64_t values
 * - SSE2: perform addition/subtraction using 128-bit __m128i values.
 *   Intel only, requires SSE2 instruction support.
 * - AVX2: perform addition/subtraction using 256-bit __m256i values.
 *   Intel only, requires AVX2 instruction support.
 */
enum class MathEngine { AUTO, SIMPLE, SSE2, AVX2 };

/**
 * This actually implements the bulk addition/subtraction operations.
 */
template <MathEngine E>
struct MathOperation {
  /**
   * Returns true if the math engine E is supported by the CPU and OS and is
   * implemented.
   */
  static bool isAvailable();

  /**
   * Returns true if the math engine E is implemented.
   */
  static bool isImplemented();

  /**
   * Performs element-wise modular addition of 2 vectors of elements packed
   * into the buffers b1 and b2. Writes the output into the buffer out. The
   * output buffer may be the same as one of the input buffers. The dataMask
   * parameter should be Bits<B>::kDataMask() where B is the element size
   * in bits.
   */
  static void add(
      uint64_t dataMask,
      size_t bitsPerElement,
      ByteRange b1,
      ByteRange b2,
      MutableByteRange out);

  /**
   * Performs element-wise modular subtraction of 2 groups of elements packed
   * into the buffers b1 and b2. Note that (a - b) % M == (a + (M - b)) % M,
   *  which is how we actually implement it to avoid underflow issues. The
   * dataMask parameter should be Bits<B>::kDataMask() where B is the element
   * size in bits.
   */
  static void sub(
      uint64_t dataMask,
      size_t bitsPerElement,
      ByteRange b1,
      ByteRange b2,
      MutableByteRange out);

  /**
   * Clears the padding bits of the given buffer according to the given
   * data mask: for each uint64_t in the input buffer, all 0 bits in the
   * data mask are cleared, and all 1 bits in the data mask are preserved.
   */
  static void clearPaddingBits(uint64_t dataMask, MutableByteRange buf);

  /**
   * Returns true if the given checksum buffer contains 0 bits at the padding
   * bit positions, according to the given data mask.
   */
  static bool checkPaddingBits(uint64_t dataMask, ByteRange buf);
};

// These forward declarations of explicit template instantiations seem to be
// required to get things to compile. I tried to get things to work without it,
// but the compiler complained when I had any AVX2 types in this header, so I
// think they need to be hidden in the .cpp file for some reason.
#define FORWARD_DECLARE_EXTERN_TEMPLATE(E)                                   \
  template <>                                                                \
  bool MathOperation<E>::isAvailable();                                      \
  template <>                                                                \
  bool MathOperation<E>::isImplemented();                                    \
  template <>                                                                \
  void MathOperation<E>::add(                                                \
      uint64_t dataMask,                                                     \
      size_t bitsPerElement,                                                 \
      ByteRange b1,                                                          \
      ByteRange b2,                                                          \
      MutableByteRange out);                                                 \
  template <>                                                                \
  void MathOperation<E>::sub(                                                \
      uint64_t dataMask,                                                     \
      size_t bitsPerElement,                                                 \
      ByteRange b1,                                                          \
      ByteRange b2,                                                          \
      MutableByteRange out);                                                 \
  template <>                                                                \
  void MathOperation<E>::clearPaddingBits(                                   \
      uint64_t dataMask, MutableByteRange buf);                              \
  template <>                                                                \
  bool MathOperation<E>::checkPaddingBits(uint64_t dataMask, ByteRange buf); \
  extern template struct MathOperation<E>

FORWARD_DECLARE_EXTERN_TEMPLATE(MathEngine::AUTO);
FORWARD_DECLARE_EXTERN_TEMPLATE(MathEngine::SIMPLE);
FORWARD_DECLARE_EXTERN_TEMPLATE(MathEngine::SSE2);
FORWARD_DECLARE_EXTERN_TEMPLATE(MathEngine::AVX2);

#undef FORWARD_DECLARE_EXTERN_TEMPLATE

} // namespace detail
} // namespace crypto
} // namespace folly
