/*
 * Copyright 2014 Facebook, Inc.
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

#ifndef FOLLY_BASE_RANDOM_H_
#define FOLLY_BASE_RANDOM_H_

#include <stdint.h>
#include "folly/ThreadLocal.h"

namespace folly {

/*
 * Return a good seed for a random number generator.
 */
uint32_t randomNumberSeed();

class Random;

/**
 * A PRNG with one instance per thread. This PRNG uses a mersenne twister random
 * number generator and is seeded from /dev/urandom. It should not be used for
 * anything which requires security, only for statistical randomness.
 *
 * An instance of this class represents the current threads PRNG. This means
 * copying an instance of this class across threads will result in corruption
 *
 * Most users will use the Random class which implicitly creates this class.
 * However, if you are worried about performance, you can memoize the TLS
 * lookups that get the per thread state by manually using this class:
 *
 * ThreadLocalPRNG rng = Random::threadLocalPRNG()
 * for (...) {
 *   Random::rand32(rng);
 * }
 */
class ThreadLocalPRNG {
 public:
  typedef uint32_t result_type;

  uint32_t operator()() {
    // Using a static method allows the compiler to avoid allocating stack space
    // for this class.
    return getImpl(local_);
  }

  static constexpr result_type min() {
    return std::numeric_limits<result_type>::min();
  }
  static constexpr result_type max() {
    return std::numeric_limits<result_type>::max();
  }
  friend class Random;

  ThreadLocalPRNG() {
    local_ = localInstance.get();
    if (!local_) {
      local_ = initLocal();
    }
  }

 private:
  class LocalInstancePRNG;
  static LocalInstancePRNG* initLocal();
  static folly::ThreadLocalPtr<ThreadLocalPRNG::LocalInstancePRNG>
    localInstance;

  static result_type getImpl(LocalInstancePRNG* local);
  LocalInstancePRNG* local_;
};



class Random {

 private:
  template<class RNG>
  using ValidRNG = typename std::enable_if<
   std::is_unsigned<typename std::result_of<RNG&()>::type>::value,
   RNG>::type;

 public:

  /**
   * Returns a random uint32_t
   */
  template<class RNG = ThreadLocalPRNG>
  static uint32_t rand32(ValidRNG<RNG>  rrng = RNG()) {
    uint32_t r = rrng.operator()();
    return r;
  }

  /**
   * Returns a random uint32_t in [0, max). If max == 0, returns 0.
   */
  template<class RNG = ThreadLocalPRNG>
  static uint32_t rand32(uint32_t max, ValidRNG<RNG> rng = RNG()) {
    if (max == 0) {
      return 0;
    }

    return std::uniform_int_distribution<uint32_t>(0, max - 1)(rng);
  }

  /**
   * Returns a random uint32_t in [min, max). If min == max, returns 0.
   */
  template<class RNG = ThreadLocalPRNG>
  static uint32_t rand32(uint32_t min,
                         uint32_t max,
                         ValidRNG<RNG> rng = RNG()) {
    if (min == max) {
      return 0;
    }

    return std::uniform_int_distribution<uint32_t>(min, max - 1)(rng);
  }

  /**
   * Returns a random uint64_t
   */
  template<class RNG = ThreadLocalPRNG>
  static uint64_t rand64(ValidRNG<RNG> rng = RNG()) {
    return ((uint64_t) rng() << 32) | rng();
  }

  /**
   * Returns a random uint64_t in [0, max). If max == 0, returns 0.
   */
  template<class RNG = ThreadLocalPRNG>
  static uint64_t rand64(uint64_t max, ValidRNG<RNG> rng = RNG()) {
    if (max == 0) {
      return 0;
    }

    return std::uniform_int_distribution<uint64_t>(0, max - 1)(rng);
  }

  /**
   * Returns a random uint64_t in [min, max). If min == max, returns 0.
   */
  template<class RNG = ThreadLocalPRNG>
  static uint64_t rand64(uint64_t min,
                         uint64_t max,
                         ValidRNG<RNG> rng = RNG()) {
    if (min == max) {
      return 0;
    }

    return std::uniform_int_distribution<uint64_t>(min, max - 1)(rng);
  }

  /**
   * Returns true 1/n of the time. If n == 0, always returns false
   */
  template<class RNG = ThreadLocalPRNG>
  static bool oneIn(uint32_t n, ValidRNG<RNG> rng = RNG()) {
    if (n == 0) {
      return false;
    }

    return rand32(n, rng) == 0;
  }

  /**
   * Returns a double in [0, 1)
   */
  template<class RNG = ThreadLocalPRNG>
  static double randDouble01(ValidRNG<RNG> rng = RNG()) {
    return std::generate_canonical<double, std::numeric_limits<double>::digits>
      (rng);
  }

  /**
    * Returns a double in [min, max), if min == max, returns 0.
    */
  template<class RNG = ThreadLocalPRNG>
  static double randDouble(double min, double max, ValidRNG<RNG> rng = RNG()) {
    if (std::fabs(max - min) < std::numeric_limits<double>::epsilon()) {
      return 0;
    }
    return std::uniform_real_distribution<double>(min, max)(rng);
  }

};

}

#endif
