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
#include <folly/futures/SharedPromise.h>
#include <gtest/gtest.h>

using namespace folly;

TEST(SharedPromise, SetGet) {
  SharedPromise<int> p;
  p.setValue(1);
  auto f1 = p.getFuture();
  auto f2 = p.getFuture();
  EXPECT_EQ(1, f1.value());
  EXPECT_EQ(1, f2.value());
}
TEST(SharedPromise, GetSet) {
  SharedPromise<int> p;
  auto f1 = p.getFuture();
  auto f2 = p.getFuture();
  p.setValue(1);
  EXPECT_EQ(1, f1.value());
  EXPECT_EQ(1, f2.value());
}

TEST(SharedPromise, GetSetGet) {
  SharedPromise<int> p;
  auto f1 = p.getFuture();
  p.setValue(1);
  auto f2 = p.getFuture();
  EXPECT_EQ(1, f1.value());
  EXPECT_EQ(1, f2.value());
}

TEST(SharedPromise, Reset) {
  SharedPromise<int> p;

  auto f1 = p.getFuture();
  p.setValue(1);
  EXPECT_EQ(1, f1.value());

  p = SharedPromise<int>();
  auto f2 = p.getFuture();
  EXPECT_FALSE(f2.isReady());
  p.setValue(2);
  EXPECT_EQ(2, f2.value());
}

TEST(SharedPromise, GetMoveSet) {
  SharedPromise<int> p;
  auto f = p.getFuture();
  auto p2 = std::move(p);
  p2.setValue(1);
  EXPECT_EQ(1, f.value());
}

TEST(SharedPromise, SetMoveGet) {
  SharedPromise<int> p;
  p.setValue(1);
  auto p2 = std::move(p);
  auto f = p2.getFuture();
  EXPECT_EQ(1, f.value());
}

TEST(SharedPromise, MoveSetGet) {
  SharedPromise<int> p;
  auto p2 = std::move(p);
  p2.setValue(1);
  auto f = p2.getFuture();
  EXPECT_EQ(1, f.value());
}

TEST(SharedPromise, MoveGetSet) {
  SharedPromise<int> p;
  auto p2 = std::move(p);
  auto f = p2.getFuture();
  p2.setValue(1);
  EXPECT_EQ(1, f.value());
}

TEST(SharedPromise, MoveMove) {
  SharedPromise<std::shared_ptr<int>> p;
  auto f1 = p.getFuture();
  auto f2 = p.getFuture();
  auto p2 = std::move(p);
  p = std::move(p2);
  p.setValue(std::make_shared<int>(1));
}
