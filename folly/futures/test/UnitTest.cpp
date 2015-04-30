/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/futures/Future.h>
#include <gtest/gtest.h>

using namespace folly;

TEST(Unit, FutureDefaultCtor) {
  Future<Unit>();
}

TEST(Unit, voidOrUnit) {
  EXPECT_TRUE(is_void_or_unit<void>::value);
  EXPECT_TRUE(is_void_or_unit<Unit>::value);
  EXPECT_FALSE(is_void_or_unit<int>::value);
}

TEST(Unit, PromiseSetValue) {
  Promise<Unit> p;
  p.setValue();
}

TEST(Unit, LiftInt) {
  using Lifted = Unit::Lift<int>;
  EXPECT_FALSE(Lifted::value);
  auto v = std::is_same<int, Lifted::type>::value;
  EXPECT_TRUE(v);
}

TEST(Unit, LiftVoid) {
  using Lifted = Unit::Lift<void>;
  EXPECT_TRUE(Lifted::value);
  auto v = std::is_same<Unit, Lifted::type>::value;
  EXPECT_TRUE(v);
}
