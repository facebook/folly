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
#include <folly/experimental/wangle/concurrent/GlobalExecutor.h>
#include <folly/experimental/wangle/concurrent/IOExecutor.h>

using namespace folly::wangle;

TEST(GlobalExecutorTest, GlobalIOExecutor) {
  class DummyExecutor : public IOExecutor {
   public:
    void add(folly::Func f) override {
      count++;
    }
    folly::EventBase* getEventBase() override {
      return nullptr;
    }
    int count{0};
  };

  auto f = [](){};

  // Don't explode, we should create the default global IOExecutor lazily here.
  getIOExecutor()->add(f);

  {
    DummyExecutor dummy;
    setIOExecutor(&dummy);
    getIOExecutor()->add(f);
    // Make sure we were properly installed.
    EXPECT_EQ(1, dummy.count);
  }

  // Don't explode, we should restore the default global IOExecutor when dummy
  // is destructed.
  getIOExecutor()->add(f);
}
