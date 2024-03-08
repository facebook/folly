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

#include <folly/executors/Async.h>

#include <memory>

#include <folly/executors/ManualExecutor.h>
#include <folly/portability/GTest.h>

using namespace folly;

TEST(AsyncFunc, manualExecutor) {
  FOLLY_PUSH_WARNING
  FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
  auto x = std::make_shared<ManualExecutor>();
  auto oldX = getCPUExecutor();
  setCPUExecutor(x);
  auto f = async([] { return 42; });
  EXPECT_FALSE(f.isReady());
  x->run();
  EXPECT_EQ(42, f.value());
  setCPUExecutor(oldX);
  FOLLY_POP_WARNING
}

TEST(AsyncFunc, valueLambda) {
  auto lambda = [] { return 42; };
  auto future = async(lambda);
  EXPECT_EQ(42, std::move(future).get());
}

TEST(AsyncFunc, voidLambda) {
  auto lambda = [] { /*do something*/ return; };
  auto future = async(lambda);
  // Futures with a void returning function, return Unit type
  EXPECT_TRUE((std::is_same<Unit, decltype(std::move(future).get())>::value));
}

TEST(AsyncFunc, moveonlyLambda) {
  auto lambda = [] { return std::make_unique<int>(42); };
  auto future = async(lambda);
  EXPECT_EQ(42, *std::move(future).get());
}
