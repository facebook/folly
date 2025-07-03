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

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include <folly/hash/Hash.h>

#include <folly/portability/GTest.h>

using namespace folly::hash;

namespace {

std::vector<uint8_t> randomBytes(
    std::default_random_engine& rng, std::size_t n) {
  std::vector<uint8_t> ret(n);
  std::uniform_int_distribution<uint8_t> dist(
      0, std::numeric_limits<uint8_t>::max());
  std::generate(ret.begin(), ret.end(), [&]() { return dist(rng); });
  return ret;
}

} // namespace

TEST(StdCompatibleHashTest, Stress) {
  constexpr std::size_t kMinLen = 1;
  constexpr std::size_t kMaxLen = 2048;
  constexpr std::size_t kEachLenAttempts = 16;

  std::default_random_engine rng(0);

  for (std::size_t len = kMinLen; len < kMaxLen; ++len) {
    for (std::size_t attempt = 0; attempt < kEachLenAttempts; ++attempt) {
      const std::vector<uint8_t> bytes = randomBytes(rng, len);
      const std::string bytesAsStr{
          reinterpret_cast<const char*>(bytes.data()), bytes.size()};
      EXPECT_EQ(
          stdCompatibleHash(bytesAsStr), std::hash<std::string>{}(bytesAsStr));
    }
  }
}
