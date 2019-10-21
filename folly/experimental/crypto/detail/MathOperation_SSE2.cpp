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

// Implementation of the MathOperation<MathEngine::SSE2> template
// specializations.
#include <folly/experimental/crypto/detail/LtHashInternal.h>

#include <glog/logging.h>

#ifdef __SSE2__
#include <emmintrin.h>
#include <sodium.h>

#include <folly/lang/Bits.h>
#endif // __SSE2__

#include <folly/Memory.h>

namespace folly {
namespace crypto {
namespace detail {

#ifdef __SSE2__
// static
template <>
bool MathOperation<MathEngine::SSE2>::isImplemented() {
  return true;
}

// static
template <>
void MathOperation<MathEngine::SSE2>::add(
    uint64_t dataMask,
    size_t bitsPerElement,
    ByteRange b1,
    ByteRange b2,
    MutableByteRange out) {
  DCHECK_EQ(b1.size(), b2.size());
  DCHECK_EQ(b1.size(), out.size());
  DCHECK_EQ(0, b1.size() % kCacheLineSize);
  static_assert(
      kCacheLineSize % sizeof(__m128i) == 0,
      "kCacheLineSize must be a multiple of sizeof(__m128i)");
  static constexpr size_t kValsPerCacheLine = kCacheLineSize / sizeof(__m128i);
  static_assert(
      kValsPerCacheLine > 0, "kCacheLineSize must be >= sizeof(__m128i)");

  // gcc issues 'ignoring attributes on template argument' warning if
  // __m128i is used below, so have to type explicitly
  alignas(kCacheLineSize) std::array<
      long long __attribute__((__vector_size__(sizeof(__m128i)))),
      kValsPerCacheLine>
      results;

  // Note: SSE2 is Intel x86(_64) only which is little-endian, so we don't need
  // the Endian::little() conversions when loading or storing data.
  if (bitsPerElement == 16 || bitsPerElement == 32) {
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      auto v1p = reinterpret_cast<const __m128i*>(b1.data() + pos);
      auto v2p = reinterpret_cast<const __m128i*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        __m128i v1 = _mm_load_si128(v1p + i);
        __m128i v2 = _mm_load_si128(v2p + i);
        if (bitsPerElement == 16) {
          results[i] = _mm_add_epi16(v1, v2);
        } else { // bitsPerElement == 32
          results[i] = _mm_add_epi32(v1, v2);
        }
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  } else {
    __m128i mask = _mm_set_epi64x(dataMask, dataMask);
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      auto v1p = reinterpret_cast<const __m128i*>(b1.data() + pos);
      auto v2p = reinterpret_cast<const __m128i*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        __m128i v1 = _mm_load_si128(v1p + i);
        __m128i v2 = _mm_load_si128(v2p + i);
        results[i] = _mm_and_si128(_mm_add_epi64(v1, v2), mask);
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  }
}

// static
template <>
void MathOperation<MathEngine::SSE2>::sub(
    uint64_t dataMask,
    size_t bitsPerElement,
    ByteRange b1,
    ByteRange b2,
    MutableByteRange out) {
  DCHECK_EQ(b1.size(), b2.size());
  DCHECK_EQ(b1.size(), out.size());
  DCHECK_EQ(0, b1.size() % kCacheLineSize);
  static_assert(
      kCacheLineSize % sizeof(__m128i) == 0,
      "kCacheLineSize must be a multiple of sizeof(__m128i)");
  static constexpr size_t kValsPerCacheLine = kCacheLineSize / sizeof(__m128i);
  static_assert(
      kValsPerCacheLine > 0, "kCacheLineSize must be >= sizeof(__m128i)");
  // gcc issues 'ignoring attributes on template argument' warning if
  // __m128i is used below, so have to type explicitly
  alignas(kCacheLineSize) std::array<
      long long __attribute__((__vector_size__(sizeof(__m128i)))),
      kValsPerCacheLine>
      results;

  // Note: SSE2 is Intel x86(_64) only which is little-endian, so we don't need
  // the Endian::little() conversions when loading or storing data.
  if (bitsPerElement == 16 || bitsPerElement == 32) {
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      auto v1p = reinterpret_cast<const __m128i*>(b1.data() + pos);
      auto v2p = reinterpret_cast<const __m128i*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        __m128i v1 = _mm_load_si128(v1p + i);
        __m128i v2 = _mm_load_si128(v2p + i);
        if (bitsPerElement == 16) {
          results[i] = _mm_sub_epi16(v1, v2);
        } else { // bitsPerElement == 32
          results[i] = _mm_sub_epi32(v1, v2);
        }
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  } else {
    __m128i mask = _mm_set_epi64x(dataMask, dataMask);
    __m128i paddingMask = _mm_set_epi64x(~dataMask, ~dataMask);
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      auto v1p = reinterpret_cast<const __m128i*>(b1.data() + pos);
      auto v2p = reinterpret_cast<const __m128i*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        __m128i v1 = _mm_load_si128(v1p + i);
        __m128i v2 = _mm_load_si128(v2p + i);
        __m128i negV2 = _mm_and_si128(_mm_sub_epi64(paddingMask, v2), mask);
        results[i] = _mm_and_si128(_mm_add_epi64(v1, negV2), mask);
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  }
}

template <>
void MathOperation<MathEngine::SSE2>::clearPaddingBits(
    uint64_t dataMask,
    MutableByteRange buf) {
  if (dataMask == 0xffffffffffffffffULL) {
    return;
  }
  DCHECK_EQ(0, buf.size() % kCacheLineSize);
  static_assert(
      kCacheLineSize % sizeof(__m128i) == 0,
      "kCacheLineSize must be a multiple of sizeof(__m128i)");
  static constexpr size_t kValsPerCacheLine = kCacheLineSize / sizeof(__m128i);
  static_assert(
      kValsPerCacheLine > 0, "kCacheLineSize must be >= sizeof(__m128i)");

  // gcc issues 'ignoring attributes on template argument' warning if
  // __m128i is used below, so have to type explicitly
  alignas(kCacheLineSize) std::array<
      long long __attribute__((__vector_size__(sizeof(__m128i)))),
      kValsPerCacheLine>
      results;

  __m128i mask = _mm_set_epi64x(dataMask, dataMask);
  for (size_t pos = 0; pos < buf.size(); pos += kCacheLineSize) {
    auto p = reinterpret_cast<const __m128i*>(buf.data() + pos);
    for (size_t i = 0; i < kValsPerCacheLine; ++i) {
      results[i] = _mm_and_si128(_mm_load_si128(p + i), mask);
    }
    std::memcpy(buf.data() + pos, results.data(), sizeof(results));
  }
}

template <>
bool MathOperation<MathEngine::SSE2>::checkPaddingBits(
    uint64_t dataMask,
    ByteRange buf) {
  if (dataMask == 0xffffffffffffffffULL) {
    return true;
  }
  DCHECK_EQ(0, buf.size() % sizeof(__m128i));
  __m128i paddingMask = _mm_set_epi64x(~dataMask, ~dataMask);
  static const __m128i kZero = _mm_setzero_si128();
  for (size_t pos = 0; pos < buf.size(); pos += sizeof(__m128i)) {
    __m128i val =
        _mm_load_si128(reinterpret_cast<const __m128i*>(buf.data() + pos));
    __m128i paddingBits = _mm_and_si128(val, paddingMask);
    if (sodium_memcmp(&paddingBits, &kZero, sizeof(kZero)) != 0) {
      return false;
    }
  }
  return true;
}

#else // !__SSE2__

// static
template <>
bool MathOperation<MathEngine::SSE2>::isImplemented() {
  return false;
}

// static
template <>
void MathOperation<MathEngine::SSE2>::add(
    uint64_t /* dataMask */,
    size_t bitsPerElement,
    ByteRange /* b1 */,
    ByteRange /* b2 */,
    MutableByteRange /* out */) {
  if (bitsPerElement != 0) { // hack to defeat [[noreturn]] compiler warning
    LOG(FATAL) << "Unimplemented function MathOperation<MathEngine::SSE2>::"
               << "add() called";
  }
}

// static
template <>
void MathOperation<MathEngine::SSE2>::sub(
    uint64_t /* dataMask */,
    size_t bitsPerElement,
    ByteRange /* b1 */,
    ByteRange /* b2 */,
    MutableByteRange /* out */) {
  if (bitsPerElement != 0) { // hack to defeat [[noreturn]] compiler warning
    LOG(FATAL) << "Unimplemented function MathOperation<MathEngine::SSE2>::"
               << "sub() called";
  }
}

template <>
void MathOperation<MathEngine::SSE2>::clearPaddingBits(
    uint64_t /* dataMask */,
    MutableByteRange buf) {
  if (buf.data() != nullptr) { // hack to defeat [[noreturn]] compiler warning
    LOG(FATAL) << "Unimplemented function MathOperation<MathEngine::SSE2>::"
               << "clearPaddingBits() called";
  }
  return; // not reached
}

template <>
bool MathOperation<MathEngine::SSE2>::checkPaddingBits(
    uint64_t /* dataMask */,
    ByteRange buf) {
  if (buf.data() != nullptr) { // hack to defeat [[noreturn]] compiler warning
    LOG(FATAL) << "Unimplemented function MathOperation<MathEngine::SSE2>::"
               << "checkPaddingBits() called";
  }
  return false;
}

#endif // __SSE2__

template struct MathOperation<MathEngine::SSE2>;

} // namespace detail
} // namespace crypto
} // namespace folly
