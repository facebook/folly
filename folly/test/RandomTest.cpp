/*
 * Copyright 2016 Facebook, Inc.
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

#include <folly/Random.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <thread>
#include <vector>
#include <random>

using namespace folly;

TEST(Random, StateSize) {
  using namespace folly::detail;

  // uint_fast32_t is uint64_t on x86_64, w00t
  EXPECT_EQ(sizeof(uint_fast32_t) / 4 + 3,
            StateSize<std::minstd_rand0>::value);
  EXPECT_EQ(624, StateSize<std::mt19937>::value);
#if FOLLY_HAVE_EXTRANDOM_SFMT19937
  EXPECT_EQ(624, StateSize<__gnu_cxx::sfmt19937>::value);
#endif
  EXPECT_EQ(24, StateSize<std::ranlux24_base>::value);
}

TEST(Random, Simple) {
  uint32_t prev = 0, seed = 0;
  for (int i = 0; i < 1024; ++i) {
    EXPECT_NE(seed = randomNumberSeed(), prev);
    prev = seed;
  }
}

TEST(Random, FixedSeed) {
  // clang-format off
  struct ConstantRNG {
    typedef uint32_t result_type;
    result_type operator()() {
      return 4; // chosen by fair dice roll.
                // guaranteed to be random.
    }
    result_type min() {
      return std::numeric_limits<result_type>::min();
    }
    result_type max() {
      return std::numeric_limits<result_type>::max();
    }
  };
  // clang-format on

  ConstantRNG gen;
  // Loop to make sure it really is constant.
  for (int i = 0; i < 1024; ++i) {
    auto result = Random::rand32(10, gen);
    // TODO: This is a little bit brittle; standard library changes could break
    // it, if it starts implementing distribution types differently.
    EXPECT_EQ(0, result);
  }
}

TEST(Random, MultiThreaded) {
  const int n = 100;
  std::vector<uint32_t> seeds(n);
  std::vector<std::thread> threads;
  for (int i = 0; i < n; ++i) {
    threads.push_back(std::thread([i, &seeds] {
      seeds[i] = randomNumberSeed();
    }));
  }
  for (auto& t : threads) {
    t.join();
  }
  std::sort(seeds.begin(), seeds.end());
  for (int i = 0; i < n-1; ++i) {
    EXPECT_LT(seeds[i], seeds[i+1]);
  }
}
