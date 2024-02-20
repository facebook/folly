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

#include <vector>
#include <folly/json/DynamicConverter.h>
#include <folly/portability/GTest.h>

using namespace folly;

TEST(converter, demo) {
  dynamic arrayOfArrayOfInt =
      dynamic::array(dynamic::array(1, 3, 5), dynamic::array(2, 4, 6));

  auto concrete = convertTo<std::vector<std::vector<int>>>(arrayOfArrayOfInt);

  EXPECT_EQ(concrete[0][2], 5);
}
