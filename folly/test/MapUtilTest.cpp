/*
 * Copyright 2012 Facebook, Inc.
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

#include "folly/MapUtil.h"

#include <map>
#include <gtest/gtest.h>

using namespace folly;

TEST(MapUtil, Simple) {
  std::map<int, int> m;
  m[1] = 2;
  EXPECT_EQ(2, get_default(m, 1, 42));
  EXPECT_EQ(42, get_default(m, 2, 42));
  EXPECT_EQ(0, get_default(m, 3));
  EXPECT_EQ(2, *get_ptr(m, 1));
  EXPECT_TRUE(get_ptr(m, 2) == nullptr);
  *get_ptr(m, 1) = 4;
  EXPECT_EQ(4, m.at(1));
}
