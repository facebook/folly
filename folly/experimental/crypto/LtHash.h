/*
 * Copyright 2017-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstddef>
#include <memory>

#include <folly/Range.h>
#include <folly/experimental/crypto/Blake2xb.h>
#include <folly/io/IOBuf.h>

namespace folly {
namespace crypto {

namespace detail {
/**
 * Allocates an IOBuf of the given size, aligned on a cache line boundary.
 * Similar to folly::IOBuf::create(), the returned IOBuf has an initial
 * capacity == size and an initial length == 0.
 */
folly::IOBuf allocateCacheAlignedIOBuf(size_t size);

/**
 * Similar to allocateCacheAlignedIOBuf(), but returns a unique_ptr to an IOBuf
 * instead of an IOBuf.
 */
std::unique_ptr<folly::IOBuf> allocateCacheAlignedIOBufUnique(size_t size);

/**
 * Returns true if the given memory address is aligned on a cache line boundary
 * and false if it isn't.
 */
bool isCacheAlignedAddress(const void* addr);

} // namespace detail

/**
 * Templated homomorphic hash, using LtHash (lattice-based crypto).
 * Template parameters: B = element size in bits, N = number of elements.
 *
 * Current constraints (checked at compile time with static asserts):
 * (1) B must be 16, 20 or 32.
 * (2) N must be > 999.
 * (3) when B is 16, N must be divisible by 32.
 * (4) when B is 20, N must be divisible by 24.
 * (5) when B is 32, N must be divisible by 16.
 */
template <std::size_t B, std::size_t N>
class LtHash {
 public:
  explicit LtHash(const folly::IOBuf& initialChecksum = {});

  /**
   * Like the above constructor but takes ownership of the checksum buffer,
   * avoiding a copy if these conditions about the input buffer are met:
   * - initialChecksum->isChained() is false
   * - initialChecksum->isShared() is false
   * - detail::isCacheAlignedAddress(initialChecksum.data()) is true
   *
   * If you want to take advantage of this and need to make sure your IOBuf
   * address is aligned on a cache line boundary, you can use the
   * function detail::allocateCacheAlignedIOBufUnique() to do it.
   */
  explicit LtHash(std::unique_ptr<folly::IOBuf> initialChecksum);

  // Note: we explicitly implement copy constructor and copy assignment
  // operator to make sure the checksum_ IOBuf is deep-copied.
  LtHash(const LtHash<B, N>& that);
  LtHash<B, N>& operator=(const LtHash<B, N>& that);

  LtHash(LtHash<B, N>&& that) noexcept = default;
  LtHash<B, N>& operator=(LtHash<B, N>&& that) noexcept = default;
  ~LtHash() = default;

  /**
   * Resets the checksum in this LtHash. This puts the hash into the same
   * state as if it was just constructed with the zero-argument constructor.
   */
  void reset();

  /**
   * IMPORTANT: Unlike regular hash, the incremental hash functions operate on
   * individual objects, not a stream of data. For example, the following
   * example codes will lead to different checksum values.
   * (1) addObject("Hello"); addObject(" World");
   * (2) addObject("Hello World");
   * because addObject() calculates hashes for the two words separately, and
   * aggregate them to update checksum.
   *
   * addObject() is commutative. LtHash generates the same checksum over a
   * given set of objects regardless of the order they were added.
   * Example: H(a + b + c) = H(b + c + a)
   *
   * addObject() can be called with multiple ByteRange parameters, in which
   * case it will behave as if it was called with a single ByteRange which
   * contained the concatenation of all the input ByteRanges. This allows
   * adding an object whose hash is computed from several non-contiguous
   * ranges of data, without having to copy the data to a contiguous
   * piece of memory.
   *
   * Example: addObject(r1, r2, r3) is equivalent to
   * addObject(r4) where r4 contains the concatenation of r1 + r2 + r3.
   */
  template <typename... Args>
  LtHash<B, N>& addObject(folly::ByteRange firstRange, Args&&... moreRanges);

  /**
   * removeObject() is the inverse function of addObject(). Note that it does
   * NOT check whether the object has been actually added to LtHash. The caller
   * should ensure that the object is valid.
   *
   * Example: H(a - a + b - b + c - c) = H(a + b + c - a - b - c) = H()
   *
   * Similar to addObject(), removeObject() can be called with more than one
   * ByteRange parameter.
   */
  template <typename... Args>
  LtHash<B, N>& removeObject(folly::ByteRange firstRange, Args&&... moreRanges);

  /**
   * Because the addObject() operation in LtHash is commutative and transitive,
   * it's possible to break down a large LtHash computation (i.e. adding 100k
   * objects) into several parallel steps each of which computes a LtHash of a
   * subset of the objects, and then add the LtHash objects together.
   * Pseudocode:
   *
   *   std::vector<std::string> objects = ...;
   *   Future<LtHash<20, 1008>> h1 = computeInBackgroundThread(
   *       &objects[0], &objects[10000]);
   *   Future<LtHash<20, 1008>> h2 = computeInBackgroundThread(
   *       &objects[10001], &objects[20000]);
   *   LtHash<20, 1008> result = h1.get() + h2.get();
   */
  LtHash<B, N>& operator+=(const LtHash<B, N>& rhs);
  friend LtHash<B, N> operator+(
      const LtHash<B, N>& lhs,
      const LtHash<B, N>& rhs) {
    LtHash<B, N> result = lhs;
    result += rhs;
    return result;
  }
  friend LtHash<B, N> operator+(LtHash<B, N>&& lhs, const LtHash<B, N>& rhs) {
    LtHash<B, N> result = std::move(lhs);
    result += rhs;
    return result;
  }
  friend LtHash<B, N> operator+(const LtHash<B, N>& lhs, LtHash<B, N>&& rhs) {
    // addition is commutative so we can just swap the two arguments
    return std::move(rhs) + lhs;
  }
  friend LtHash<B, N> operator+(LtHash<B, N>&& lhs, LtHash<B, N>&& rhs) {
    LtHash<B, N> result = std::move(lhs);
    result += rhs;
    return result;
  }

  /**
   * The subtraction operator is provided for symmetry, but I'm not sure if
   * anyone will ever actually use it outside of tests.
   */
  LtHash<B, N>& operator-=(const LtHash<B, N>& rhs);
  friend LtHash<B, N> operator-(
      const LtHash<B, N>& lhs,
      const LtHash<B, N>& rhs) {
    LtHash<B, N> result = lhs;
    result -= rhs;
    return result;
  }
  friend LtHash<B, N> operator-(LtHash<B, N>&& lhs, const LtHash<B, N>& rhs) {
    LtHash<B, N> result = std::move(lhs);
    result -= rhs;
    return result;
  }

  /**
   * Equality comparison operator, implemented in a data-independent way to
   * guard against timing attacks. Always use this to check if two LtHash
   * values are equal instead of manually comparing checksum buffers.
   */
  bool operator==(const LtHash<B, N>& that) const;

  /**
   * Equality comparison operator for checksum in ByteRange, implemented in a
   * data-independent way to guard against timing attacks.
   */
  bool checksumEquals(folly::ByteRange otherChecksum) const;

  /**
   * Inequality comparison operator.
   */
  bool operator!=(const LtHash<B, N>& that) const;

  /**
   * Sets the intial checksum value to use for processing objects in the
   * xxxObject() calls.
   */
  void setChecksum(const folly::IOBuf& checksum);

  /**
   * Like the above method but takes ownership of the checksum buffer,
   * avoiding a copy if these conditions about the input buffer are met:
   * - checksum->isChained() is false
   * - checksum->isShared() is false
   * - detail::isCacheAlignedAddress(checksum.data()) is true
   *
   * If you want to take advantage of this and need to make sure your IOBuf
   * address is aligned on a cache line boundary, you can use the
   * function detail::allocateCacheAlignedIOBufUnique() to do it.
   */
  void setChecksum(std::unique_ptr<folly::IOBuf> checksum);

  /**
   * Returns the total length of the checksum (element_count * element_length)
   */
  static constexpr size_t getChecksumSizeBytes();

  /**
   * Returns the template parameter B.
   */
  static constexpr size_t getElementSizeInBits();

  /**
   * Returns the number of elements that get packed into a single uint64_t.
   */
  static constexpr size_t getElementsPerUint64();

  /**
   * Returns the template parameter N.
   */
  static constexpr size_t getElementCount();

  /**
   * Retruns true if the internal checksum uses padding bits between elements.
   */
  static constexpr bool hasPaddingBits();

  /**
   * Returns a copy of the current checksum value
   */
  std::unique_ptr<folly::IOBuf> getChecksum() const;

 private:
  template <typename... Args>
  void hashObject(
      folly::MutableByteRange out,
      folly::ByteRange firstRange,
      Args&&... moreRanges);

  template <typename... Args>
  void
  updateDigest(Blake2xb& digest, folly::ByteRange range, Args&&... moreRanges);

  void updateDigest(Blake2xb& digest);

  // current checksum
  folly::IOBuf checksum_;
};

} // namespace crypto
} // namespace folly

#include <folly/experimental/crypto/LtHash-inl.h>

namespace folly {
namespace crypto {

// This is the fastest and smallest specialization and should be
// preferred in most cases. It provides over 200 bits of security
// which should be good enough for most cases.
using LtHash16_1024 = LtHash<16, 1024>;

// These specializations are available to users who want a higher
// level of cryptographic security. They are slower and larger than
// the one above.
using LtHash20_1008 = LtHash<20, 1008>;
using LtHash32_1024 = LtHash<32, 1024>;

} // namespace crypto
} // namespace folly
