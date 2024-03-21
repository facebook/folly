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

#include <folly/executors/SerialExecutor.h>

#include <chrono>

#include <folly/ScopeGuard.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/InlineExecutor.h>
#include <folly/io/async/Request.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

namespace {

void sleepMs(uint64_t ms) {
  /* sleep override */ std::this_thread::sleep_for(
      std::chrono::milliseconds(ms));
}

} // namespace

template <typename T>
class SerialExecutorTest : public testing::Test {};

using SerialExecutorTypes = ::testing::Types<folly::SerialExecutor>;
TYPED_TEST_SUITE(SerialExecutorTest, SerialExecutorTypes);

template <class SerialExecutorType>
void simpleTest(folly::Executor& parent) {
  class SerialExecutorContextData : public folly::RequestData {
   public:
    static std::string kCtxKey() {
      return typeid(SerialExecutorContextData).name();
    }
    explicit SerialExecutorContextData(int id) : id_(id) {}
    bool hasCallback() override { return false; }
    int getId() const { return id_; }

   private:
    const int id_;
  };

  auto executor = SerialExecutorType::create(&parent);

  std::vector<int> values;
  std::vector<int> expected;

  for (int i = 0; i < 20; ++i) {
    folly::RequestContextScopeGuard ctxGuard;
    folly::RequestContext::try_get()->setContextData(
        SerialExecutorContextData::kCtxKey(),
        std::make_unique<SerialExecutorContextData>(i));
    auto checkReqCtx = [i] {
      EXPECT_EQ(
          i,
          dynamic_cast<SerialExecutorContextData*>(
              folly::RequestContext::get()->getContextData(
                  SerialExecutorContextData::kCtxKey()))
              ->getId());
    };
    executor->add([i, checkReqCtx, g = folly::makeGuard(checkReqCtx), &values] {
      checkReqCtx();
      // make this extra vulnerable to concurrent execution
      values.push_back(0);
      sleepMs(10);
      values.back() = i;
    });
    expected.push_back(i);
  }

  // wait until last task has executed
  folly::Baton<> finished_baton;
  executor->add([&finished_baton] { finished_baton.post(); });
  finished_baton.wait();

  EXPECT_EQ(expected, values);
}

TYPED_TEST(SerialExecutorTest, Simple) {
  folly::CPUThreadPoolExecutor parent{4};
  simpleTest<TypeParam>(parent);
}
TYPED_TEST(SerialExecutorTest, SimpleInline) {
  simpleTest<TypeParam>(folly::InlineExecutor::instance());
}

// The Afterlife test only works with an asynchronous executor (not the
// InlineExecutor), because we want execution of tasks to happen after we
// destroy the SerialExecutor
TYPED_TEST(SerialExecutorTest, Afterlife) {
  folly::CPUThreadPoolExecutor parent{4};
  auto executor = TypeParam::create(&parent);

  // block executor until we call start_baton.post()
  folly::Baton<> start_baton;
  executor->add([&start_baton] { start_baton.wait(); });

  std::vector<int> values;
  std::vector<int> expected;

  for (int i = 0; i < 20; ++i) {
    executor->add([i, &values] {
      // make this extra vulnerable to concurrent execution
      values.push_back(0);
      sleepMs(10);
      values.back() = i;
    });
    expected.push_back(i);
  }

  folly::Baton<> finished_baton;
  executor->add([&finished_baton] { finished_baton.post(); });

  // destroy SerialExecutor
  executor.reset();

  // now kick off the tasks
  start_baton.post();

  // wait until last task has executed
  finished_baton.wait();

  EXPECT_EQ(expected, values);
}

template <class SerialExecutorType>
void recursiveAddTest(folly::Executor& parent) {
  auto executor = SerialExecutorType::create(&parent);

  folly::Baton<> finished_baton;

  std::vector<int> values;
  std::vector<int> expected = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}};

  int i = 0;
  std::function<void()> lambda = [&] {
    if (i < 10) {
      // make this extra vulnerable to concurrent execution
      values.push_back(0);
      sleepMs(10);
      values.back() = i;
      executor->add(lambda);
    } else if (i < 12) {
      // Below we will post this lambda three times to the executor. When
      // executed, the lambda will re-post itself during the first ten
      // executions. Afterwards we do nothing twice (this else-branch), and
      // then on the 13th execution signal that we are finished.
    } else {
      finished_baton.post();
    }
    ++i;
  };

  executor->add(lambda);
  executor->add(lambda);
  executor->add(lambda);

  // wait until last task has executed
  finished_baton.wait();

  EXPECT_EQ(expected, values);
}

TYPED_TEST(SerialExecutorTest, RecursiveAdd) {
  folly::CPUThreadPoolExecutor parent{4};
  recursiveAddTest<TypeParam>(parent);
}
TYPED_TEST(SerialExecutorTest, RecursiveAddInline) {
  recursiveAddTest<TypeParam>(folly::InlineExecutor::instance());
}

TYPED_TEST(SerialExecutorTest, ExecutionThrows) {
  auto executor = TypeParam::create();

  // an empty Func will throw std::bad_function_call when invoked,
  // but SerialExecutor should catch that exception
  executor->add(folly::Func{});
}

TYPED_TEST(SerialExecutorTest, ParentExecutorDiscardsFunc) {
  struct FakeExecutor : folly::Executor {
    void add(folly::Func) override {}
  };

  FakeExecutor ex;
  auto se = TypeParam::create(&ex);
  bool ran = false;
  se->add([&, ka = folly::getKeepAliveToken(se.get())] { ran = true; });
  se.reset();
  ASSERT_FALSE(ran);
}
