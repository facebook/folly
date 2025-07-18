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

#include <folly/hash/detail/RandomSeed.h>

#include <folly/Portability.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>

TEST(RandomSeedTest, DebugSeed) {
  SKIP_IF(!folly::kIsDebug);

  const uint64_t seed = folly::hash::detail::RandomSeed::seed();
  EXPECT_NE(seed, 0LL);
}

TEST(RandomSeedTest, OptSeed) {
  SKIP_IF(folly::kIsDebug);

  const uint64_t seed = folly::hash::detail::RandomSeed::seed();
  EXPECT_EQ(seed, 0LL);
}
