/*
 * Copyright 2014 Facebook, Inc.
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

#include <folly/wangle/Future.h>
#include <folly/wangle/Promise.h>

using namespace folly::wangle;

TEST(Interrupts, raise) {
  std::runtime_error eggs("eggs");
  Promise<void> p;
  p.setInterruptHandler([&](std::exception_ptr e) {
    EXPECT_THROW(std::rethrow_exception(e), decltype(eggs));
  });
  p.getFuture().raise(eggs);
}

TEST(Interrupts, cancel) {
  Promise<void> p;
  p.setInterruptHandler([&](std::exception_ptr e) {
    EXPECT_THROW(std::rethrow_exception(e), FutureCancellation);
  });
  p.getFuture().cancel();
}

TEST(Interrupts, handleThenInterrupt) {
  Promise<int> p;
  bool flag = false;
  p.setInterruptHandler([&](std::exception_ptr e) { flag = true; });
  p.getFuture().cancel();
  EXPECT_TRUE(flag);
}

TEST(Interrupts, interruptThenHandle) {
  Promise<int> p;
  bool flag = false;
  p.getFuture().cancel();
  p.setInterruptHandler([&](std::exception_ptr e) { flag = true; });
  EXPECT_TRUE(flag);
}

TEST(Interrupts, interruptAfterFulfilNoop) {
  Promise<void> p;
  bool flag = false;
  p.setInterruptHandler([&](std::exception_ptr e) { flag = true; });
  p.setValue();
  p.getFuture().cancel();
  EXPECT_FALSE(flag);
}

TEST(Interrupts, secondInterruptNoop) {
  Promise<void> p;
  int count = 0;
  p.setInterruptHandler([&](std::exception_ptr e) { count++; });
  auto f = p.getFuture();
  f.cancel();
  f.cancel();
  EXPECT_EQ(1, count);
}
