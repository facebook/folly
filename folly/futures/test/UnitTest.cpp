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

#include <gtest/gtest.h>

#include <folly/futures/Future.h>

using namespace folly;

std::runtime_error eggs("eggs");

TEST(Unit, futureDefaultCtor) {
  Future<Unit>();
}

TEST(Unit, operatorEq) {
  EXPECT_TRUE(Unit{} == Unit{});
}

TEST(Unit, operatorNe) {
  EXPECT_FALSE(Unit{} != Unit{});
}

TEST(Unit, voidOrUnit) {
  EXPECT_TRUE(is_void_or_unit<Unit>::value);
  EXPECT_TRUE(is_void_or_unit<Unit>::value);
  EXPECT_FALSE(is_void_or_unit<int>::value);
}

TEST(Unit, promiseSetValue) {
  Promise<Unit> p;
  p.setValue();
}

TEST(Unit, liftInt) {
  using Lifted = Unit::Lift<int>;
  EXPECT_FALSE(Lifted::value);
  auto v = std::is_same<int, Lifted::type>::value;
  EXPECT_TRUE(v);
}

TEST(Unit, liftVoid) {
  using Lifted = Unit::Lift<Unit>;
  EXPECT_TRUE(Lifted::value);
  auto v = std::is_same<Unit, Lifted::type>::value;
  EXPECT_TRUE(v);
}

TEST(Unit, dropInt) {
  using dropped = typename Unit::Drop<int>;
  EXPECT_FALSE(dropped::value);
  EXPECT_TRUE((std::is_same<int, dropped::type>::value));
}

TEST(Unit, dropUnit) {
  using dropped = typename Unit::Drop<Unit>;
  EXPECT_TRUE(dropped::value);
  EXPECT_TRUE((std::is_void<dropped::type>::value));
}

TEST(Unit, dropVoid) {
  using dropped = typename Unit::Drop<void>;
  EXPECT_TRUE(dropped::value);
  EXPECT_TRUE((std::is_void<dropped::type>::value));
}

TEST(Unit, futureToUnit) {
  Future<Unit> fu = makeFuture(42).unit();
  fu.value();
  EXPECT_TRUE(makeFuture<int>(eggs).unit().hasException());
}

TEST(Unit, voidFutureToUnit) {
  Future<Unit> fu = makeFuture().unit();
  fu.value();
  EXPECT_TRUE(makeFuture<Unit>(eggs).unit().hasException());
}

TEST(Unit, unitFutureToUnitIdentity) {
  Future<Unit> fu = makeFuture(Unit{}).unit();
  fu.value();
  EXPECT_TRUE(makeFuture<Unit>(eggs).unit().hasException());
}

TEST(Unit, toUnitWhileInProgress) {
  Promise<int> p;
  Future<Unit> fu = p.getFuture().unit();
  EXPECT_FALSE(fu.isReady());
  p.setValue(42);
  EXPECT_TRUE(fu.isReady());
}

TEST(Unit, makeFutureWith) {
  int count = 0;
  Future<Unit> fu = makeFutureWith([&]{ count++; });
  EXPECT_EQ(1, count);
}
