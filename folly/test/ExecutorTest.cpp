/*
 * Copyright 2016-present Facebook, Inc.
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

#include <atomic>

#include <folly/Executor.h>
#include <folly/portability/GTest.h>

namespace folly {

class KeepAliveTestExecutor : public Executor {
 public:
  void add(Func) override {
    // this executor does nothing
  }

  bool keepAliveAcquire() override {
    ++refCount;
    return true;
  }

  void keepAliveRelease() override {
    --refCount;
  }

  std::atomic<int> refCount{0};
};

TEST(ExecutorTest, KeepAliveBasic) {
  KeepAliveTestExecutor exec;

  {
    Executor::KeepAlive<KeepAliveTestExecutor>&& ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(std::addressof(exec), ka.get());
    EXPECT_EQ(1, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, KeepAliveMove) {
  KeepAliveTestExecutor exec;

  {
    Executor::KeepAlive<KeepAliveTestExecutor>&& ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(std::addressof(exec), ka.get());
    EXPECT_EQ(1, exec.refCount);

    Executor::KeepAlive<KeepAliveTestExecutor> ka2{std::move(ka)};
    EXPECT_FALSE(ka);
    EXPECT_TRUE(ka2);
    EXPECT_EQ(std::addressof(exec), ka2.get());
    EXPECT_EQ(1, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, KeepAliveConvert) {
  KeepAliveTestExecutor exec;

  {
    Executor::KeepAlive<KeepAliveTestExecutor>&& ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(std::addressof(exec), ka.get());
    EXPECT_EQ(1, exec.refCount);

    // convert to Executor::KeepAlive<Executor>
    Executor::KeepAlive<Executor> ka2{std::move(ka)};
    EXPECT_FALSE(ka);
    EXPECT_TRUE(ka2);
    EXPECT_EQ(std::addressof(exec), ka2.get());
    EXPECT_EQ(1, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

} // namespace folly
