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

#include <folly/BenchmarkUtil.h>

#include <folly/portability/GTest.h>

namespace folly {

struct BenchmarkUtilTest : ::testing::Test {};

TEST_F(BenchmarkUtilTest, bm_llc_size_fallback) {
  EXPECT_NE(0, detail::bm_llc_size_fallback());
}

TEST_F(BenchmarkUtilTest, bm_llc_size) {
  EXPECT_NE(0, bm_llc_size());
}

TEST_F(BenchmarkUtilTest, bm_llc_evict) {
  bm_llc_evict(0xdeadbeef); // in santizer builds, should not abort
  SUCCEED();
}

} // namespace folly
