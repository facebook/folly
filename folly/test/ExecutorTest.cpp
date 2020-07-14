/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <atomic>

#include <folly/Executor.h>
#include <folly/portability/GTest.h>

namespace folly {

class KeepAliveTestExecutor : public Executor {
 public:
  void add(Func) override {
    // this executor does nothing
  }

  bool keepAliveAcquire() noexcept override {
    ++refCount;
    return true;
  }

  void keepAliveRelease() noexcept override {
    --refCount;
  }

  std::atomic<int> refCount{0};
};

TEST(ExecutorTest, KeepAliveBasic) {
  KeepAliveTestExecutor exec;

  {
    auto ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(&exec, ka.get());
    EXPECT_EQ(1, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, KeepAliveMoveConstructor) {
  KeepAliveTestExecutor exec;

  {
    auto ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(&exec, ka.get());
    EXPECT_EQ(1, exec.refCount);

    // member move constructor
    Executor::KeepAlive<KeepAliveTestExecutor> ka2(std::move(ka));
    EXPECT_FALSE(ka);
    EXPECT_TRUE(ka2);
    EXPECT_EQ(&exec, ka2.get());
    EXPECT_EQ(1, exec.refCount);

    // template move constructor
    Executor::KeepAlive<Executor> ka3(std::move(ka2));
    EXPECT_FALSE(ka2);
    EXPECT_TRUE(ka3);
    EXPECT_EQ(&exec, ka3.get());
    EXPECT_EQ(1, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, KeepAliveCopyConstructor) {
  KeepAliveTestExecutor exec;

  {
    auto ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(&exec, ka.get());
    EXPECT_EQ(1, exec.refCount);

    // member copy constructor
    Executor::KeepAlive<KeepAliveTestExecutor> ka2(ka);
    EXPECT_TRUE(ka);
    EXPECT_TRUE(ka2);
    EXPECT_EQ(&exec, ka2.get());
    EXPECT_EQ(2, exec.refCount);

    // template copy constructor
    Executor::KeepAlive<Executor> ka3(ka);
    EXPECT_TRUE(ka);
    EXPECT_TRUE(ka3);
    EXPECT_EQ(&exec, ka3.get());
    EXPECT_EQ(3, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, KeepAliveImplicitConstructorFromRawPtr) {
  KeepAliveTestExecutor exec;

  {
    auto myFunc = [&exec](Executor::KeepAlive<> /*ka*/) mutable {
      EXPECT_EQ(1, exec.refCount);
    };
    myFunc(&exec);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, KeepAliveMoveAssignment) {
  KeepAliveTestExecutor exec;

  {
    auto ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(&exec, ka.get());
    EXPECT_EQ(1, exec.refCount);

    Executor::KeepAlive<> ka2;
    ka2 = std::move(ka);
    EXPECT_FALSE(ka);
    EXPECT_TRUE(ka2);
    EXPECT_EQ(&exec, ka2.get());
    EXPECT_EQ(1, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, KeepAliveCopyAssignment) {
  KeepAliveTestExecutor exec;

  {
    auto ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(&exec, ka.get());
    EXPECT_EQ(1, exec.refCount);

    decltype(ka) ka2;
    ka2 = ka;
    EXPECT_TRUE(ka);
    EXPECT_TRUE(ka2);
    EXPECT_EQ(&exec, ka2.get());
    EXPECT_EQ(2, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, KeepAliveConvert) {
  KeepAliveTestExecutor exec;

  {
    auto ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(&exec, ka.get());
    EXPECT_EQ(1, exec.refCount);

    Executor::KeepAlive<Executor> ka2 = std::move(ka); // conversion
    EXPECT_FALSE(ka);
    EXPECT_TRUE(ka2);
    EXPECT_EQ(&exec, ka2.get());
    EXPECT_EQ(1, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, KeepAliveCopy) {
  KeepAliveTestExecutor exec;

  {
    auto ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(&exec, ka.get());
    EXPECT_EQ(1, exec.refCount);

    auto ka2 = ka.copy();
    EXPECT_TRUE(ka);
    EXPECT_TRUE(ka2);
    EXPECT_EQ(&exec, ka2.get());
    EXPECT_EQ(2, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, GetKeepAliveTokenFromToken) {
  KeepAliveTestExecutor exec;

  {
    auto ka = getKeepAliveToken(exec);
    EXPECT_TRUE(ka);
    EXPECT_EQ(&exec, ka.get());
    EXPECT_EQ(1, exec.refCount);

    auto ka2 = getKeepAliveToken(ka);
    EXPECT_TRUE(ka);
    EXPECT_TRUE(ka2);
    EXPECT_EQ(&exec, ka2.get());
    EXPECT_EQ(2, exec.refCount);
  }

  EXPECT_EQ(0, exec.refCount);
}

TEST(ExecutorTest, CopyExpired) {
  struct Ex : Executor {
    void add(Func) override {}
  };

  Ex* ptr = nullptr;
  Executor::KeepAlive<Ex> ka;

  {
    auto ex = std::make_unique<Ex>();
    ptr = ex.get();
    ka = getKeepAliveToken(ptr);
  }

  ka = ka.copy(); // if copy() doesn't check dummy, expect segfault

  EXPECT_EQ(ptr, ka.get());
}

} // namespace folly
