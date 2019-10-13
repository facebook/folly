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

#include <sodium.h>

#include <folly/Range.h>

namespace folly {
namespace crypto {

namespace detail {

struct Blake2xbParam {
  uint8_t digestLength; /*  1 */
  uint8_t keyLength; /*  2 */
  uint8_t fanout; /*  3 */
  uint8_t depth; /*  4 */
  uint32_t leafLength; /*  8 */
  uint32_t nodeOffset; /* 12 */
  uint32_t xofLength; /* 16 */
  uint8_t nodeDepth; /* 17 */
  uint8_t innerLength; /* 18 */
  uint8_t reserved[14]; /* 32 */
  uint8_t salt[16]; /* 48 */
  uint8_t personal[16]; /* 64 */
};

static_assert(sizeof(Blake2xbParam) == 64, "wrong sizeof(Blake2xbParam)");

} // namespace detail

/**
 * An implementation of the BLAKE2x XOF (extendable output function)
 * hash function using BLAKE2b as the underlying hash. This hash function
 * can produce cryptographic hashes of arbitrary length (between 1 and 2^32 - 2
 * bytes) from inputs of arbitrary size. Like BLAKE2b, it can be keyed, and can
 * accept optional salt and personlization parameters.
 *
 * Note that if you need to compute hashes between 16 and 64 bytes in length,
 * you should use Blake2b instead - it's more efficient and you will have an
 * easier time interoperating with other languages, since implementations of
 * Blake2b are more common than implementations of Blake2xb. You can generate
 * a blake2b hash using the following functions from libsodium:
 * - crypto_generichash_blake2b()
 * - crypto_generichash_blake2b_salt_personal()
 */
class Blake2xb {
 public:
  /**
   * Minimum output hash size, if it is known in advance.
   */
  static constexpr size_t kMinOutputLength = 1;
  /**
   * Maximum output hash size, if it is known in advance.
   */
  static constexpr size_t kMaxOutputLength = 0xfffffffeULL;
  /**
   * If the amount of output data desired is not known in advance, use this
   * constant as the outputLength parameter to init().
   */
  static constexpr size_t kUnknownOutputLength = 0;

  /**
   * Creates a new uninitialized Blake2xb instance. The init() method must
   * be called before it can be used.
   */
  Blake2xb();

  /**
   * Shorthand for calling the no-argument constructor followed by
   * newInstance.init(outputLength, key, salt, personlization).
   */
  explicit Blake2xb(
      size_t outputLength,
      ByteRange key = {},
      ByteRange salt = {},
      ByteRange personalization = {})
      : Blake2xb() {
    init(outputLength, key, salt, personalization);
  }

  ~Blake2xb();

  /**
   * Initializes the digest object. This must be called after a new instance
   * is constructed and before update() is called. It can also be called on
   * a previously-used instance to reset its internal state and reuse it for
   * a new hash computation.
   */
  void init(
      size_t outputLength,
      ByteRange key = {},
      ByteRange salt = {},
      ByteRange personalization = {});

  /**
   * Hashes some more input data.
   */
  void update(ByteRange data);

  /**
   * Computes the final hash and stores it in the given output. The value of
   * out.size() MUST equal the outputLength parameter that was given to the
   * last init() call, except when the outputLength parameter was
   * kUnknownOutputLength.
   *
   * WARNING: never compare the results of two Blake2xb.finish() calls
   * using non-constant time comparison. The recommended way to compare
   * cryptographic hashes is with sodium_memcmp() (or some other constant-time
   * memory comparison function).
   */
  void finish(MutableByteRange out);

  /**
   * Convenience function, use this if you are hashing a single input buffer,
   * the output length is known in advance, and the output data is allocated
   * and ready to accept the hash value.
   */
  static void hash(
      MutableByteRange out,
      ByteRange data,
      ByteRange key = {},
      ByteRange salt = {},
      ByteRange personalization = {}) {
    Blake2xb d;
    d.init(out.size(), key, salt, personalization);
    d.update(data);
    d.finish(out);
  }

 private:
  static constexpr size_t kUnknownOutputLengthMagic = 0xffffffffULL;

  detail::Blake2xbParam param_;
  crypto_generichash_blake2b_state state_;
  bool outputLengthKnown_;
  bool initialized_;
  bool finished_;
};

} // namespace crypto
} // namespace folly
