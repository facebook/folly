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

TEST(SelfDestruct, then) {
  auto* p = new Promise<int>();
  auto future = p->getFuture().then([p](int x) {
    delete p;
    return x + 1;
  });
  p->setValue(123);
  EXPECT_EQ(future.get(), 124);
}

TEST(SelfDestruct, ensure) {
  auto* p = new Promise<int>();
  auto future = p->getFuture().ensure([p] { delete p; });
  p->setValue(123);
  EXPECT_EQ(future.get(), 123);
}
