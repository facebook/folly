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

// Implementation of the MathOperation<MathEngine::AVX2> template
// specializations.
#include <folly/experimental/crypto/detail/LtHashInternal.h>

#include <glog/logging.h>

#ifdef __AVX2__
#include <immintrin.h>
#include <sodium.h>

#include <folly/lang/Bits.h>
#endif // __AVX2__

#include <folly/Memory.h>

namespace folly {
namespace crypto {
namespace detail {

#ifdef __AVX2__

// static
template <>
bool MathOperation<MathEngine::AVX2>::isImplemented() {
  return true;
}

// static
template <>
void MathOperation<MathEngine::AVX2>::add(
    uint64_t dataMask,
    size_t bitsPerElement,
    ByteRange b1,
    ByteRange b2,
    MutableByteRange out) {
  DCHECK_EQ(b1.size(), b2.size());
  DCHECK_EQ(b1.size(), out.size());
  DCHECK_EQ(0, b1.size() % kCacheLineSize);
  static_assert(
      kCacheLineSize % sizeof(__m256i) == 0,
      "kCacheLineSize must be a multiple of sizeof(__m256i)");
  static constexpr size_t kValsPerCacheLine = kCacheLineSize / sizeof(__m256i);
  static_assert(
      kValsPerCacheLine > 0, "kCacheLineSize must be >= sizeof(__m256i)");

  // gcc issues 'ignoring attributes on template argument' warning if
  // __m256i is used below, so have to type explicitly
  alignas(kCacheLineSize) std::array<
      long long __attribute__((__vector_size__(sizeof(__m256i)))),
      kValsPerCacheLine>
      results;

  // Note: AVX2 is Intel x86_64 only which is little-endian, so we don't need
  // the Endian::little() conversions when loading or storing data.
  if (bitsPerElement == 16 || bitsPerElement == 32) {
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      const __m256i* v1p = reinterpret_cast<const __m256i*>(b1.data() + pos);
      const __m256i* v2p = reinterpret_cast<const __m256i*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        __m256i v1 = _mm256_load_si256(v1p + i);
        __m256i v2 = _mm256_load_si256(v2p + i);
        if (bitsPerElement == 16) {
          results[i] = _mm256_add_epi16(v1, v2);
        } else { // bitsPerElement == 32
          results[i] = _mm256_add_epi32(v1, v2);
        }
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  } else {
    __m256i mask = _mm256_set1_epi64x(dataMask);
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      const __m256i* v1p = reinterpret_cast<const __m256i*>(b1.data() + pos);
      const __m256i* v2p = reinterpret_cast<const __m256i*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        __m256i v1 = _mm256_load_si256(v1p + i);
        __m256i v2 = _mm256_load_si256(v2p + i);
        results[i] = _mm256_and_si256(_mm256_add_epi64(v1, v2), mask);
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  }
}

// static
template <>
void MathOperation<MathEngine::AVX2>::sub(
    uint64_t dataMask,
    size_t bitsPerElement,
    ByteRange b1,
    ByteRange b2,
    MutableByteRange out) {
  DCHECK_EQ(b1.size(), b2.size());
  DCHECK_EQ(b1.size(), out.size());
  DCHECK_EQ(0, b1.size() % kCacheLineSize);
  static_assert(
      kCacheLineSize % sizeof(__m256i) == 0,
      "kCacheLineSize must be a multiple of sizeof(__m256i)");
  static constexpr size_t kValsPerCacheLine = kCacheLineSize / sizeof(__m256i);
  static_assert(
      kValsPerCacheLine > 0, "kCacheLineSize must be >= sizeof(__m256i)");

  // gcc issues 'ignoring attributes on template argument' warning if
  // __m256i is used below, so have to type explicitly
  alignas(kCacheLineSize) std::array<
      long long __attribute__((__vector_size__(sizeof(__m256i)))),
      kValsPerCacheLine>
      results;

  // Note: AVX2 is Intel x86_64 only which is little-endian, so we don't need
  // the Endian::little() conversions when loading or storing data.
  if (bitsPerElement == 16 || bitsPerElement == 32) {
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      const __m256i* v1p = reinterpret_cast<const __m256i*>(b1.data() + pos);
      const __m256i* v2p = reinterpret_cast<const __m256i*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        __m256i v1 = _mm256_load_si256(v1p + i);
        __m256i v2 = _mm256_load_si256(v2p + i);
        if (bitsPerElement == 16) {
          results[i] = _mm256_sub_epi16(v1, v2);
        } else { // bitsPerElement == 32
          results[i] = _mm256_sub_epi32(v1, v2);
        }
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  } else {
    __m256i mask = _mm256_set1_epi64x(dataMask);
    __m256i paddingMask = _mm256_set1_epi64x(~dataMask);
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      const __m256i* v1p = reinterpret_cast<const __m256i*>(b1.data() + pos);
      const __m256i* v2p = reinterpret_cast<const __m256i*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        __m256i v1 = _mm256_load_si256(v1p + i);
        __m256i v2 = _mm256_load_si256(v2p + i);
        __m256i negV2 =
            _mm256_and_si256(_mm256_sub_epi64(paddingMask, v2), mask);
        results[i] = _mm256_and_si256(_mm256_add_epi64(v1, negV2), mask);
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  }
}

template <>
void MathOperation<MathEngine::AVX2>::clearPaddingBits(
    uint64_t dataMask,
    MutableByteRange buf) {
  if (dataMask == 0xffffffffffffffffULL) {
    return;
  }
  DCHECK_EQ(0, buf.size() % kCacheLineSize);
  static_assert(
      kCacheLineSize % sizeof(__m256i) == 0,
      "kCacheLineSize must be a multiple of sizeof(__m256i)");
  static constexpr size_t kValsPerCacheLine = kCacheLineSize / sizeof(__m256i);
  static_assert(
      kValsPerCacheLine > 0, "kCacheLineSize must be >= sizeof(__m256i)");
  // gcc issues 'ignoring attributes on template argument' warning if
  // __m256i is used below, so have to type explicitly
  alignas(kCacheLineSize) std::array<
      long long __attribute__((__vector_size__(sizeof(__m256i)))),
      kValsPerCacheLine>
      results;
  __m256i mask = _mm256_set1_epi64x(dataMask);
  for (size_t pos = 0; pos < buf.size(); pos += kCacheLineSize) {
    const __m256i* p = reinterpret_cast<const __m256i*>(buf.data() + pos);
    for (size_t i = 0; i < kValsPerCacheLine; ++i) {
      results[i] = _mm256_and_si256(_mm256_load_si256(p + i), mask);
    }
    std::memcpy(buf.data() + pos, results.data(), sizeof(results));
  }
}

template <>
bool MathOperation<MathEngine::AVX2>::checkPaddingBits(
    uint64_t dataMask,
    ByteRange buf) {
  if (dataMask == 0xffffffffffffffffULL) {
    return true;
  }
  DCHECK_EQ(0, buf.size() % sizeof(__m256i));
  __m256i paddingMask = _mm256_set1_epi64x(~dataMask);
  static const __m256i kZero = _mm256_setzero_si256();
  for (size_t pos = 0; pos < buf.size(); pos += sizeof(__m256i)) {
    __m256i val =
        _mm256_load_si256(reinterpret_cast<const __m256i*>(buf.data() + pos));
    __m256i paddingBits = _mm256_and_si256(val, paddingMask);
    if (sodium_memcmp(&paddingBits, &kZero, sizeof(kZero)) != 0) {
      return false;
    }
  }
  return true;
}

#else // !__AVX2__

// static
template <>
bool MathOperation<MathEngine::AVX2>::isImplemented() {
  return false;
}

// static
template <>
void MathOperation<MathEngine::AVX2>::add(
    uint64_t /* dataMask */,
    size_t bitsPerElement,
    ByteRange /* b1 */,
    ByteRange /* b2 */,
    MutableByteRange /* out */) {
  if (bitsPerElement != 0) { // hack to defeat [[noreturn]] compiler warning
    LOG(FATAL) << "Unimplemented function MathOperation<MathEngine::AVX2>::"
               << "add() called";
  }
}

// static
template <>
void MathOperation<MathEngine::AVX2>::sub(
    uint64_t /* dataMask */,
    size_t bitsPerElement,
    ByteRange /* b1 */,
    ByteRange /* b2 */,
    MutableByteRange /* out */) {
  if (bitsPerElement != 0) { // hack to defeat [[noreturn]] compiler warning
    LOG(FATAL) << "Unimplemented function MathOperation<MathEngine::AVX2>::"
               << "sub() called";
  }
}

template <>
void MathOperation<MathEngine::AVX2>::clearPaddingBits(
    uint64_t /* dataMask */,
    MutableByteRange buf) {
  if (buf.data() != nullptr) { // hack to defeat [[noreturn]] compiler warning
    LOG(FATAL) << "Unimplemented function MathOperation<MathEngine::AVX2>::"
               << "clearPaddingBits() called";
  }
}

template <>
bool MathOperation<MathEngine::AVX2>::checkPaddingBits(
    uint64_t /* dataMask */,
    ByteRange buf) {
  if (buf.data() != nullptr) { // hack to defeat [[noreturn]] compiler warning
    LOG(FATAL) << "Unimplemented function MathOperation<MathEngine::AVX2>::"
               << "checkPaddingBits() called";
  }
  return false;
}

#endif // __AVX2__

template struct MathOperation<MathEngine::AVX2>;

} // namespace detail
} // namespace crypto
} // namespace folly
