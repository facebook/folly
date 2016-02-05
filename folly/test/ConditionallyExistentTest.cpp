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

#include <folly/ConditionallyExistent.h>

#include <gtest/gtest.h>

using namespace std;
using namespace folly;

namespace {

class ConditionallyExistentTest : public testing::Test {};
}

TEST_F(ConditionallyExistentTest, WhenConditionFalse) {
  folly::ConditionallyExistent<string, false> foobar("hello world");
  EXPECT_FALSE(foobar.present());
  bool called = false;
  foobar.with([&](const string&) { called = true; });
  EXPECT_FALSE(called);
}

TEST_F(ConditionallyExistentTest, WhenConditionTrue) {
  folly::ConditionallyExistent<string, true> foobar("hello world");
  EXPECT_TRUE(foobar.present());
  bool called = false;
  foobar.with([&](const string&) { called = true; });
  EXPECT_TRUE(called);
}
