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

#pragma once
#define FOLLY_RANDOM_H_

#include <array>
#include <cstdint>
#include <random>
#include <type_traits>

#include <folly/Portability.h>
#include <folly/Traits.h>
#include <folly/functional/Invoke.h>
#include <folly/lang/Bits.h>

#if FOLLY_HAVE_EXTRANDOM_SFMT19937
#include <ext/random>
#endif

namespace folly {

namespace detail {

#if FOLLY_HAVE_EXTRANDOM_SFMT19937
using DefaultGenerator = __gnu_cxx::sfmt19937;
#else
using DefaultGenerator = std::mt19937;
#endif

} // namespace detail

/**
 * A PRNG with one instance per thread. This PRNG uses a mersenne twister random
 * number generator and is seeded from /dev/urandom. It should not be used for
 * anything which requires security, only for statistical randomness.
 */
class ThreadLocalPRNG {
  using Generator = detail::DefaultGenerator;

 public:
  using result_type = Generator::result_type;

  result_type operator()();

  static constexpr result_type min() { return Generator::min(); }
  static constexpr result_type max() { return Generator::max(); }
};

class Random {
 private:
  template <class RNG>
  using ValidRNG = typename std::
      enable_if<std::is_unsigned<invoke_result_t<RNG&>>::value, RNG>::type;

  template <class T>
  class SecureRNG {
   public:
    using result_type = typename std::enable_if<
        std::is_integral<T>::value && !std::is_same<T, bool>::value,
        T>::type;

    result_type operator()() { return Random::secureRandom<result_type>(); }

    static constexpr result_type min() {
      return std::numeric_limits<result_type>::min();
    }

    static constexpr result_type max() {
      return std::numeric_limits<result_type>::max();
    }
  };

  // Whether RNG output is surjective and uniform when truncated to ResultType.
  template <class RNG, class ResultType>
  static constexpr bool UniformRNG =
      (std::is_unsigned<ResultType>::value &&
       std::is_unsigned<typename RNG::result_type>::value &&
       // RNG range covers ResultType.
       RNG::min() == 0 &&
       RNG::max() >= std::numeric_limits<ResultType>::max() &&
       // Truncating the output maintains uniformness.
       (~RNG::max() == 0 || isPowTwo(RNG::max() + 1)));

 public:
  using DefaultGenerator = detail::DefaultGenerator;

  /**
   * Get secure random bytes. (On Linux and OSX, this means /dev/urandom).
   */
  static void secureRandom(void* data, size_t size);

  /**
   * Shortcut to get a secure random value of integral type.
   */
  template <class T>
  static typename std::enable_if<
      std::is_integral<T>::value && !std::is_same<T, bool>::value,
      T>::type
  secureRandom() {
    T val;
    secureRandom(&val, sizeof(val));
    return val;
  }

  /**
   * Returns a secure random uint32_t
   */
  static uint32_t secureRand32() { return secureRandom<uint32_t>(); }

  /**
   * Returns a secure random uint32_t in [0, max). If max == 0, returns 0.
   */
  static uint32_t secureRand32(uint32_t max) {
    SecureRNG<uint32_t> srng;
    return rand32(max, srng);
  }

  /**
   * Returns a secure random uint32_t in [min, max). If min == max, returns min.
   */
  static uint32_t secureRand32(uint32_t min, uint32_t max) {
    SecureRNG<uint32_t> srng;
    return rand32(min, max, srng);
  }

  /**
   * Returns a secure random uint64_t
   */
  static uint64_t secureRand64() { return secureRandom<uint64_t>(); }

  /**
   * Returns a secure random uint64_t in [0, max). If max == 0, returns 0.
   */
  static uint64_t secureRand64(uint64_t max) {
    SecureRNG<uint64_t> srng;
    return rand64(max, srng);
  }

  /**
   * Returns a secure random uint64_t in [min, max). If min == max, returns min.
   */
  static uint64_t secureRand64(uint64_t min, uint64_t max) {
    SecureRNG<uint64_t> srng;
    return rand64(min, max, srng);
  }

  /**
   * Returns true 1/n of the time. If n == 0, always returns false
   */
  static bool secureOneIn(uint32_t n) {
    if (n < 2) {
      return n;
    }
    SecureRNG<uint32_t> srng;
    return rand32(0, n, srng) == 0;
  }

  /**
   * Returns true 1/n of the time. If n == 0, always returns false
   */
  static bool secureOneIn64(uint64_t n) {
    if (n < 2) {
      return n;
    }
    SecureRNG<uint64_t> srng;
    return rand64(0, n, srng) == 0;
  }

  /**
   * Returns a secure double in [0, 1)
   */
  static double secureRandDouble01() {
    SecureRNG<uint64_t> srng;
    return randDouble01(srng);
  }

  /**
   * Returns a secure double in [min, max), if min == max, returns min.
   */
  static double secureRandDouble(double min, double max) {
    SecureRNG<uint64_t> srng;
    return randDouble(min, max, srng);
  }

  /**
   * (Re-)Seed an existing RNG with a good seed.
   *
   * Note that you should usually use ThreadLocalPRNG unless you need
   * reproducibility (such as during a test), in which case you'd want
   * to create a RNG with a good seed in production, and seed it yourself
   * in test.
   */
  template <class RNG = DefaultGenerator, class /* EnableIf */ = ValidRNG<RNG>>
  static void seed(RNG& rng);

  /**
   * Create a new RNG, seeded with a good seed.
   *
   * Note that you should usually use ThreadLocalPRNG unless you need
   * reproducibility (such as during a test), in which case you'd want
   * to create a RNG with a good seed in production, and seed it yourself
   * in test.
   */
  template <class RNG = DefaultGenerator, class /* EnableIf */ = ValidRNG<RNG>>
  static RNG create();

  /**
   * Create a new RNG, which can be used for applications that require secure
   * randomness.
   *
   * The resulting RNG will have worse performance than one created with
   * create(), so use it if you need the security.
   */
  static SecureRNG<uint32_t> createSecure() { return SecureRNG<uint32_t>(); }

  /**
   * Returns a random uint32_t
   */
  static uint32_t rand32() { return rand32(ThreadLocalPRNG()); }

  /**
   * Returns a random uint32_t given a specific RNG
   */
  template <class RNG, class /* EnableIf */ = ValidRNG<RNG>>
  static uint32_t rand32(RNG&& rng) {
    if FOLLY_CXX17_CONSTEXPR (UniformRNG<std::decay_t<RNG>, uint32_t>) {
      return static_cast<uint32_t>(rng());
    } else {
      return std::uniform_int_distribution<uint32_t>(
          0, std::numeric_limits<uint32_t>::max())(rng);
    }
  }

  /**
   * Returns a random uint32_t in [0, max). If max == 0, returns 0.
   */
  static uint32_t rand32(uint32_t max) {
    return rand32(0, max, ThreadLocalPRNG());
  }

  /**
   * Returns a random uint32_t in [0, max) given a specific RNG.
   * If max == 0, returns 0.
   */
  template <class RNG = ThreadLocalPRNG, class /* EnableIf */ = ValidRNG<RNG>>
  static uint32_t rand32(uint32_t max, RNG&& rng) {
    return rand32(0, max, rng);
  }

  /**
   * Returns a random uint32_t in [min, max). If min == max, returns min.
   */
  static uint32_t rand32(uint32_t min, uint32_t max) {
    return rand32(min, max, ThreadLocalPRNG());
  }

  /**
   * Returns a random uint32_t in [min, max) given a specific RNG.
   * If min == max, returns min.
   */
  template <class RNG = ThreadLocalPRNG, class /* EnableIf */ = ValidRNG<RNG>>
  static uint32_t rand32(uint32_t min, uint32_t max, RNG&& rng) {
    if (min == max) {
      return min;
    }
    return std::uniform_int_distribution<uint32_t>(min, max - 1)(rng);
  }

  /**
   * Returns a random uint64_t
   */
  static uint64_t rand64() { return rand64(ThreadLocalPRNG()); }

  /**
   * Returns a random uint64_t
   */
  template <class RNG = ThreadLocalPRNG, class /* EnableIf */ = ValidRNG<RNG>>
  static uint64_t rand64(RNG&& rng) {
    if FOLLY_CXX17_CONSTEXPR (UniformRNG<std::decay_t<RNG>, uint64_t>) {
      return rng();
    } else if FOLLY_CXX17_CONSTEXPR (UniformRNG<std::decay_t<RNG>, uint32_t>) {
      return (static_cast<uint64_t>(rng()) << 32) |
          static_cast<uint32_t>(rng());
    } else {
      return std::uniform_int_distribution<uint64_t>(
          0, std::numeric_limits<uint64_t>::max())(rng);
    }
  }

  /**
   * Returns a random uint64_t in [0, max). If max == 0, returns 0.
   */
  static uint64_t rand64(uint64_t max) {
    return rand64(0, max, ThreadLocalPRNG());
  }

  /**
   * Returns a random uint64_t in [0, max). If max == 0, returns 0.
   */
  template <class RNG = ThreadLocalPRNG, class /* EnableIf */ = ValidRNG<RNG>>
  static uint64_t rand64(uint64_t max, RNG&& rng) {
    return rand64(0, max, rng);
  }

  /**
   * Returns a random uint64_t in [min, max). If min == max, returns min.
   */
  static uint64_t rand64(uint64_t min, uint64_t max) {
    return rand64(min, max, ThreadLocalPRNG());
  }

  /**
   * Returns a random uint64_t in [min, max). If min == max, returns min.
   */
  template <class RNG = ThreadLocalPRNG, class /* EnableIf */ = ValidRNG<RNG>>
  static uint64_t rand64(uint64_t min, uint64_t max, RNG&& rng) {
    if (min == max) {
      return min;
    }
    return std::uniform_int_distribution<uint64_t>(min, max - 1)(rng);
  }

  /**
   * Returns true 1/n of the time. If n == 0, always returns false
   */
  static bool oneIn(uint32_t n) { return oneIn(n, ThreadLocalPRNG()); }

  /**
   * Returns true 1/n of the time. If n == 0, always returns false
   */
  static bool oneIn64(uint64_t n) { return oneIn64(n, ThreadLocalPRNG()); }

  /**
   * Returns true 1/n of the time. If n == 0, always returns false
   */
  template <class RNG = ThreadLocalPRNG, class /* EnableIf */ = ValidRNG<RNG>>
  static bool oneIn(uint32_t n, RNG&& rng) {
    if (n < 2) {
      return n;
    }
    return rand32(0, n, std::forward<RNG>(rng)) == 0;
  }

  template <class RNG = ThreadLocalPRNG, class /* EnableIf */ = ValidRNG<RNG>>
  static bool oneIn64(uint64_t n, RNG&& rng) {
    if (n < 2) {
      return n;
    }
    return rand64(0, n, std::forward<RNG>(rng)) == 0;
  }

  /**
   * Returns true with the probability of p, false otherwise
   */
  static bool randBool(double p) { return randBool(p, ThreadLocalPRNG()); }

  /**
   * Returns true with the probability of p, false otherwise
   */
  template <class RNG = ThreadLocalPRNG, class /* EnableIf */ = ValidRNG<RNG>>
  static bool randBool(double p, RNG&& rng) {
    return randDouble01(std::forward<RNG>(rng)) < p;
  }

  /**
   * Returns a double in [0, 1)
   */
  static double randDouble01() { return randDouble01(ThreadLocalPRNG()); }

  /**
   * Returns a double in [0, 1)
   */
  template <class RNG = ThreadLocalPRNG, class /* EnableIf */ = ValidRNG<RNG>>
  static double randDouble01(RNG&& rng) {
    // Assuming 64-bit IEEE754 doubles, numbers in the form k/2^53 can be
    // represented exactly, so we can sample uniformly in [0, 1) by sampling an
    // integer in [0, 2^53) and scaling accordingly. This is the highest
    // precision we can obtain if we want a symmetric output distribution.
    // See https://prng.di.unimi.it/#remarks for more details.
    static_assert(
        std::numeric_limits<double>::digits == 53, "Unsupported double type");
    return (rand64(std::forward<RNG>(rng)) >> 11) * 0x1.0p-53;
  }

  /**
   * Returns a double in [min, max), if min == max, returns min.
   */
  static double randDouble(double min, double max) {
    return randDouble(min, max, ThreadLocalPRNG());
  }

  /**
   * Returns a double in [min, max), if min == max, returns min.
   */
  template <class RNG = ThreadLocalPRNG, class /* EnableIf */ = ValidRNG<RNG>>
  static double randDouble(double min, double max, RNG&& rng) {
    if (std::fabs(max - min) < std::numeric_limits<double>::epsilon()) {
      return min;
    }
    return std::uniform_real_distribution<double>(min, max)(rng);
  }
};

/*
 * Return a good seed for a random number generator.
 * Note that this is a legacy function, as it returns a 32-bit value, which
 * is too small to be useful as a "real" RNG seed. Use the functions in class
 * Random instead.
 */
inline uint32_t randomNumberSeed() {
  return Random::rand32();
}

} // namespace folly

#include <folly/Random-inl.h>
