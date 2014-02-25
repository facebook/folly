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

#include "folly/Memory.h"
#include "folly/wangle/Try.h"

using namespace folly::wangle;

TEST(Try, makeTryFunction) {
  auto func = []() {
    return folly::make_unique<int>(1);
  };

  auto result = makeTryFunction(func);
  EXPECT_TRUE(result.hasValue());
  EXPECT_EQ(*result.value(), 1);
}

TEST(Try, makeTryFunctionThrow) {
  auto func = []() {
    throw std::runtime_error("Runtime");
    return folly::make_unique<int>(1);
  };

  auto result = makeTryFunction(func);
  EXPECT_TRUE(result.hasException());
}

TEST(Try, makeTryFunctionVoid) {
  auto func = []() {
    return;
  };

  auto result = makeTryFunction(func);
  EXPECT_TRUE(result.hasValue());
}

TEST(Try, makeTryFunctionVoidThrow) {
  auto func = []() {
    throw std::runtime_error("Runtime");
    return;
  };

  auto result = makeTryFunction(func);
  EXPECT_TRUE(result.hasException());
}
