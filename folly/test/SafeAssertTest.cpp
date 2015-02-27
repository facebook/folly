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

#include <folly/SafeAssert.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <folly/Benchmark.h>

using namespace folly;

void fail() {
  FOLLY_SAFE_CHECK(0, "hello");
}

void succeed() {
  FOLLY_SAFE_CHECK(1, "world");
}

TEST(SafeAssert, AssertionFailure) {
  succeed();
  EXPECT_DEATH(fail(), ".*Assertion failure:.*hello.*");
}
