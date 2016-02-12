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

#include <folly/futures/ThreadedExecutor.h>
#include <folly/futures/Future.h>
#include <folly/Conv.h>

#include <gtest/gtest.h>

using namespace std;
using namespace folly;
using namespace folly::futures;

class ThreadedExecutorTest : public testing::Test {};

TEST_F(ThreadedExecutorTest, example) {
  ThreadedExecutor x;
  auto ret = via(&x)
    .then([&] { return 17; })
    .then([&](int x) { return to<string>(x); })
    .wait()
    .getTry();
  EXPECT_EQ("17", ret.value());
}
