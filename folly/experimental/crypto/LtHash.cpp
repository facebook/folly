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

#include <folly/experimental/crypto/LtHash.h>
#include <folly/CpuId.h>

#ifdef __SSE2__
#include <emmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif

#include <folly/Memory.h>

namespace folly {
namespace crypto {
namespace detail {

folly::IOBuf allocateCacheAlignedIOBuf(size_t size) {
  void* ptr = folly::aligned_malloc(size, kCacheLineSize);
  if (ptr == nullptr) {
    throw std::bad_alloc();
  }
  return folly::IOBuf(
      folly::IOBuf::TAKE_OWNERSHIP,
      ptr,
      static_cast<uint64_t>(size), // capacity
      0ULL, // initial size
      [](void* addr, void* /* userData*/) { folly::aligned_free(addr); });
}

std::unique_ptr<folly::IOBuf> allocateCacheAlignedIOBufUnique(size_t size) {
  return std::make_unique<folly::IOBuf>(allocateCacheAlignedIOBuf(size));
}

bool isCacheAlignedAddress(const void* addr) {
  auto addrValue = reinterpret_cast<size_t>(addr);
  return (addrValue & (kCacheLineSize - 1)) == 0;
}

// static
template <>
bool MathOperation<MathEngine::SIMPLE>::isAvailable() {
  return true;
}

// static
template <>
bool MathOperation<MathEngine::SSE2>::isAvailable() {
  static const bool kIsAvailable =
      CpuId().sse2() && MathOperation<MathEngine::SSE2>::isImplemented();
  return kIsAvailable;
}

// static
template <>
bool MathOperation<MathEngine::AVX2>::isAvailable() {
  static const bool kIsAvailable =
      CpuId().avx2() && MathOperation<MathEngine::AVX2>::isImplemented();
  return kIsAvailable;
}

// static
template <>
bool MathOperation<MathEngine::AUTO>::isAvailable() {
  return true;
}

// static
template <>
bool MathOperation<MathEngine::AUTO>::isImplemented() {
  return true;
}

// static
template <>
void MathOperation<MathEngine::AUTO>::add(
    uint64_t dataMask,
    size_t bitsPerElement,
    folly::ByteRange b1,
    folly::ByteRange b2,
    folly::MutableByteRange out) {
  // Note: implementation is a function pointer that is initialized to point
  // at the fastest available implementation the first time this function is
  // called.
  static auto implementation = []() {
    if (MathOperation<MathEngine::AVX2>::isAvailable()) {
      LOG(INFO) << "Selected AVX2 MathEngine for add() operation";
      return MathOperation<MathEngine::AVX2>::add;
    } else if (MathOperation<MathEngine::SSE2>::isAvailable()) {
      LOG(INFO) << "Selected SSE2 MathEngine for add() operation";
      return MathOperation<MathEngine::SSE2>::add;
    } else {
      LOG(INFO) << "Selected SIMPLE MathEngine for add() operation";
      return MathOperation<MathEngine::SIMPLE>::add;
    }
  }();
  implementation(dataMask, bitsPerElement, b1, b2, out);
}

// static
template <>
void MathOperation<MathEngine::AUTO>::sub(
    uint64_t dataMask,
    size_t bitsPerElement,
    folly::ByteRange b1,
    folly::ByteRange b2,
    folly::MutableByteRange out) {
  // Note: implementation is a function pointer that is initialized to point
  // at the fastest available implementation the first time this function is
  // called.
  static auto implementation = []() {
    if (MathOperation<MathEngine::AVX2>::isAvailable()) {
      LOG(INFO) << "Selected AVX2 MathEngine for sub() operation";
      return MathOperation<MathEngine::AVX2>::sub;
    } else if (MathOperation<MathEngine::SSE2>::isAvailable()) {
      LOG(INFO) << "Selected SSE2 MathEngine for sub() operation";
      return MathOperation<MathEngine::SSE2>::sub;
    } else {
      LOG(INFO) << "Selected SIMPLE MathEngine for sub() operation";
      return MathOperation<MathEngine::SIMPLE>::sub;
    }
  }();
  implementation(dataMask, bitsPerElement, b1, b2, out);
}

// static
template <>
void MathOperation<MathEngine::AUTO>::clearPaddingBits(
    uint64_t dataMask,
    folly::MutableByteRange buf) {
  // Note: implementation is a function pointer that is initialized to point
  // at the fastest available implementation the first time this function is
  // called.
  static auto implementation = []() {
    if (MathOperation<MathEngine::AVX2>::isAvailable()) {
      LOG(INFO) << "Selected AVX2 MathEngine for clearPaddingBits() operation";
      return MathOperation<MathEngine::AVX2>::clearPaddingBits;
    } else if (MathOperation<MathEngine::SSE2>::isAvailable()) {
      LOG(INFO) << "Selected SSE2 MathEngine for clearPaddingBits() operation";
      return MathOperation<MathEngine::SSE2>::clearPaddingBits;
    } else {
      LOG(INFO)
          << "Selected SIMPLE MathEngine for clearPaddingBits() operation";
      return MathOperation<MathEngine::SIMPLE>::clearPaddingBits;
    }
  }();
  implementation(dataMask, buf);
}

// static
template <>
bool MathOperation<MathEngine::AUTO>::checkPaddingBits(
    uint64_t dataMask,
    folly::ByteRange buf) {
  // Note: implementation is a function pointer that is initialized to point
  // at the fastest available implementation the first time this function is
  // called.
  static auto implementation = []() {
    if (MathOperation<MathEngine::AVX2>::isAvailable()) {
      LOG(INFO) << "Selected AVX2 MathEngine for checkPaddingBits() operation";
      return MathOperation<MathEngine::AVX2>::checkPaddingBits;
    } else if (MathOperation<MathEngine::SSE2>::isAvailable()) {
      LOG(INFO) << "Selected SSE2 MathEngine for checkPaddingBits() operation";
      return MathOperation<MathEngine::SSE2>::checkPaddingBits;
    } else {
      LOG(INFO)
          << "Selected SIMPLE MathEngine for checkPaddingBits() operation";
      return MathOperation<MathEngine::SIMPLE>::checkPaddingBits;
    }
  }();
  return implementation(dataMask, buf);
}

template struct MathOperation<MathEngine::AUTO>;

} // namespace detail
} // namespace crypto
} // namespace folly
