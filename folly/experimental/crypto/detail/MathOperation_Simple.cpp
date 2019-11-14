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

// Implementation of the MathOperation<MathEngine::SIMPLE> template
// specializations.
#include <folly/experimental/crypto/detail/LtHashInternal.h>

#include <glog/logging.h>

#include <folly/Memory.h>
#include <folly/lang/Bits.h>

namespace folly {
namespace crypto {
namespace detail {

// static
template <>
bool MathOperation<MathEngine::SIMPLE>::isImplemented() {
  return true;
}

// static
template <>
void MathOperation<MathEngine::SIMPLE>::add(
    uint64_t dataMask,
    size_t bitsPerElement,
    ByteRange b1,
    ByteRange b2,
    MutableByteRange out) {
  DCHECK_EQ(b1.size(), b2.size());
  DCHECK_EQ(b1.size(), out.size());
  DCHECK_EQ(0, b1.size() % kCacheLineSize);
  static_assert(
      kCacheLineSize % sizeof(uint64_t) == 0,
      "kCacheLineSize must be a multiple of sizeof(uint64_t)");
  static constexpr size_t kValsPerCacheLine = kCacheLineSize / sizeof(uint64_t);
  static_assert(
      kValsPerCacheLine > 0, "kCacheLineSize must be >= sizeof(uint64_t)");
  alignas(kCacheLineSize) std::array<uint64_t, kValsPerCacheLine> results;

  if (bitsPerElement == 16 || bitsPerElement == 32) {
    // When bitsPerElement is 16:
    // There are no padding bits, 4x 16-bit values fit exactly into a uint64_t:
    // uint64_t U = [ uint16_t W, uint16_t X, uint16_t Y, uint16_t Z ].
    // We break them up into A and B groups, with each group containing
    // alternating elements, such that A | B = the original number:
    // uint64_t A = [ uint16_t W,          0, uint16_t Y,          0 ]
    // uint64_t B = [          0, uint16_t X,          0, uint16_t Z ]
    // Then we add the A group and B group independently, and bitwise-OR
    // the results.
    // When bitsPerElement is 32:
    // There are no padding bits, 2x 32-bit values fit exactly into a uint64_t.
    // We independently add the high and low halves and then XOR them together.
    const uint64_t kMaskA =
        bitsPerElement == 16 ? 0xffff0000ffff0000ULL : 0xffffffff00000000ULL;
    const uint64_t kMaskB = ~kMaskA;
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      auto v1p = reinterpret_cast<const uint64_t*>(b1.data() + pos);
      auto v2p = reinterpret_cast<const uint64_t*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        uint64_t v1 = Endian::little(*(v1p + i));
        uint64_t v2 = Endian::little(*(v2p + i));
        uint64_t v1a = v1 & kMaskA;
        uint64_t v1b = v1 & kMaskB;
        uint64_t v2a = v2 & kMaskA;
        uint64_t v2b = v2 & kMaskB;
        uint64_t v3a = (v1a + v2a) & kMaskA;
        uint64_t v3b = (v1b + v2b) & kMaskB;
        results[i] = Endian::little(v3a | v3b);
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  } else {
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      auto v1p = reinterpret_cast<const uint64_t*>(b1.data() + pos);
      auto v2p = reinterpret_cast<const uint64_t*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        uint64_t v1 = Endian::little(*(v1p + i));
        uint64_t v2 = Endian::little(*(v2p + i));
        results[i] = Endian::little((v1 + v2) & dataMask);
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  }
}

// static
template <>
void MathOperation<MathEngine::SIMPLE>::sub(
    uint64_t dataMask,
    size_t bitsPerElement,
    ByteRange b1,
    ByteRange b2,
    MutableByteRange out) {
  DCHECK_EQ(b1.size(), b2.size());
  DCHECK_EQ(b1.size(), out.size());
  DCHECK_EQ(0, b1.size() % kCacheLineSize);
  static_assert(
      kCacheLineSize % sizeof(uint64_t) == 0,
      "kCacheLineSize must be a multiple of sizeof(uint64_t)");
  static constexpr size_t kValsPerCacheLine = kCacheLineSize / sizeof(uint64_t);
  static_assert(
      kValsPerCacheLine > 0, "kCacheLineSize must be >= sizeof(uint64_t)");
  alignas(kCacheLineSize) std::array<uint64_t, kValsPerCacheLine> results;

  if (bitsPerElement == 16 || bitsPerElement == 32) {
    // When bitsPerElement is 16:
    // There are no padding bits, 4x 16-bit values fit exactly into a uint64_t:
    // uint64_t U = [ uint16_t W, uint16_t X, uint16_t Y, uint16_t Z ].
    // We break them up into A and B groups, with each group containing
    // alternating elements, such that A | B = the original number:
    // uint64_t A = [ uint16_t W,          0, uint16_t Y,          0 ]
    // uint64_t B = [          0, uint16_t X,          0, uint16_t Z ]
    // Then we add the A group and B group independently, and bitwise-OR
    // the results.
    // When bitsPerElement is 32:
    // There are no padding bits, 2x 32-bit values fit exactly into a uint64_t.
    // We independently add the high and low halves and then XOR them together.
    const uint64_t kMaskA =
        bitsPerElement == 16 ? 0xffff0000ffff0000ULL : 0xffffffff00000000ULL;
    const uint64_t kMaskB = ~kMaskA;
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      auto v1p = reinterpret_cast<const uint64_t*>(b1.data() + pos);
      auto v2p = reinterpret_cast<const uint64_t*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        uint64_t v1 = Endian::little(*(v1p + i));
        uint64_t v2 = Endian::little(*(v2p + i));
        uint64_t v1a = v1 & kMaskA;
        uint64_t v1b = v1 & kMaskB;
        uint64_t v2a = v2 & kMaskA;
        uint64_t v2b = v2 & kMaskB;
        uint64_t v3a = (v1a + (kMaskB - v2a)) & kMaskA;
        uint64_t v3b = (v1b + (kMaskA - v2b)) & kMaskB;
        results[i] = Endian::little(v3a | v3b);
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  } else {
    for (size_t pos = 0; pos < b1.size(); pos += kCacheLineSize) {
      auto v1p = reinterpret_cast<const uint64_t*>(b1.data() + pos);
      auto v2p = reinterpret_cast<const uint64_t*>(b2.data() + pos);
      for (size_t i = 0; i < kValsPerCacheLine; ++i) {
        uint64_t v1 = Endian::little(*(v1p + i));
        uint64_t v2 = Endian::little(*(v2p + i));
        results[i] =
            Endian::little((v1 + ((~dataMask - v2) & dataMask)) & dataMask);
      }
      std::memcpy(out.data() + pos, results.data(), sizeof(results));
    }
  }
}

template <>
void MathOperation<MathEngine::SIMPLE>::clearPaddingBits(
    uint64_t dataMask,
    MutableByteRange buf) {
  if (dataMask == 0xffffffffffffffffULL) {
    return;
  }

  DCHECK_EQ(0, buf.size() % kCacheLineSize);
  static_assert(
      kCacheLineSize % sizeof(uint64_t) == 0,
      "kCacheLineSize must be a multiple of sizeof(uint64_t)");
  static constexpr size_t kValsPerCacheLine = kCacheLineSize / sizeof(uint64_t);
  static_assert(
      kValsPerCacheLine > 0, "kCacheLineSize must be >= sizeof(uint64_t)");
  alignas(kCacheLineSize) std::array<uint64_t, kValsPerCacheLine> results;
  for (size_t pos = 0; pos < buf.size(); pos += kCacheLineSize) {
    auto p = reinterpret_cast<const uint64_t*>(buf.data() + pos);
    for (size_t i = 0; i < kValsPerCacheLine; ++i) {
      results[i] = Endian::little(Endian::little(*(p + i)) & dataMask);
    }
    std::memcpy(buf.data() + pos, results.data(), sizeof(results));
  }
}

template <>
bool MathOperation<MathEngine::SIMPLE>::checkPaddingBits(
    uint64_t dataMask,
    ByteRange buf) {
  if (dataMask == 0xffffffffffffffffULL) {
    return true;
  }

  DCHECK_EQ(0, buf.size() % sizeof(uint64_t));
  for (size_t pos = 0; pos < buf.size(); pos += sizeof(uint64_t)) {
    uint64_t val =
        Endian::little(*reinterpret_cast<const uint64_t*>(buf.data() + pos));
    if ((val & ~dataMask) != 0ULL) {
      return false;
    }
  }
  return true;
}

template struct MathOperation<MathEngine::SIMPLE>;

} // namespace detail
} // namespace crypto
} // namespace folly
