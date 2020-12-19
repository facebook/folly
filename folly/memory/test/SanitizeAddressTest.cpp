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

#include <folly/memory/SanitizeAddress.h>

#include <folly/portability/GTest.h>

class SanitizeAddressTest : public testing::Test {};

TEST_F(SanitizeAddressTest, asan_region_is_poisoned) {
  auto data = malloc(4096);
  EXPECT_EQ(nullptr, folly::asan_region_is_poisoned(data, 4096));
  free(data);
  EXPECT_EQ(
      folly::kIsSanitizeAddress ? data : nullptr,
      folly::asan_region_is_poisoned(data, 4096));
}
