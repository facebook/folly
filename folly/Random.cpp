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

#include "folly/Random.h"

#include <atomic>
#include <unistd.h>
#include <sys/time.h>
#include <random>
#include <array>

#if __GNUC_PREREQ(4, 8)
#include <ext/random>
#define USE_SIMD_PRNG
#endif

namespace folly {

namespace {
std::atomic<uint32_t> seedInput(0);
}

uint32_t randomNumberSeed() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  const uint32_t kPrime0 = 51551;
  const uint32_t kPrime1 = 61631;
  const uint32_t kPrime2 = 64997;
  const uint32_t kPrime3 = 111857;
  return kPrime0 * (seedInput++)
       + kPrime1 * static_cast<uint32_t>(getpid())
       + kPrime2 * static_cast<uint32_t>(tv.tv_sec)
       + kPrime3 * static_cast<uint32_t>(tv.tv_usec);
}


folly::ThreadLocalPtr<ThreadLocalPRNG::LocalInstancePRNG>
ThreadLocalPRNG::localInstance;

class ThreadLocalPRNG::LocalInstancePRNG {
#ifdef USE_SIMD_PRNG
  typedef  __gnu_cxx::sfmt19937 RNG;
#else
  typedef std::mt19937 RNG;
#endif

  static RNG makeRng() {
    std::array<int, RNG::state_size> seed_data;
    std::random_device r;
    std::generate_n(seed_data.data(), seed_data.size(), std::ref(r));
    std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
    return RNG(seq);
  }

 public:
  LocalInstancePRNG() : rng(std::move(makeRng())) {}

  RNG rng;
};

ThreadLocalPRNG::LocalInstancePRNG* ThreadLocalPRNG::initLocal() {
  auto ret = new LocalInstancePRNG;
  localInstance.reset(ret);
  return ret;
}

uint32_t ThreadLocalPRNG::getImpl(LocalInstancePRNG* local) {
  return local->rng();
}

}
