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

#include <string>
#include <unordered_map>
#include <folly/MapUtil.h>
#include <folly/portability/GTest.h>

using namespace folly;

TEST(maputil, demo) {
  std::unordered_map<std::string, double> famous_constants = {
      {"pi", 3.14159},
      {"e", 2.71828},
  };

  const double* logs = get_ptr(famous_constants, "e");
  ASSERT_NE(logs, nullptr);
  EXPECT_EQ(*logs, 2.71828);

  const double* beauty = get_ptr(famous_constants, "golden_ratio");
  EXPECT_EQ(beauty, nullptr);
}
