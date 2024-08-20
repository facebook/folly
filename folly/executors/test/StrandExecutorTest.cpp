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

#include <folly/executors/StrandExecutor.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#include <folly/CancellationToken.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/io/async/Request.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>

using namespace folly;
using namespace std::chrono_literals;

namespace {
template <typename Duration>
void burnTime(Duration d) {
  /* sleep override */ std::this_thread::sleep_for(d);
}
} // namespace

TEST(StrandExecutor, SimpleTest) {
  auto exec = StrandExecutor::create();

  // Checks that tasks are serialised (ie. that we don't corrupt the vector)
  // and that task are processed in-order.
  std::vector<int> v;
  for (int i = 0; i < 20; ++i) {
    exec->add([&, i] {
      v.emplace_back();
      burnTime(1ms);
      v.back() = i;
    });
  }

  folly::Baton baton;
  exec->add([&] { baton.post(); });
  baton.wait();

  CHECK_EQ(20, v.size());
  for (int i = 0; i < 20; ++i) {
    CHECK_EQ(i, v[i]);
  }
}

TEST(StrandExecutor, ThreadSafetyTest) {
  auto strandContext = StrandContext::create();

  ManualExecutor ex1;
  ManualExecutor ex2;

  CancellationSource cancelSrc;

  auto runUntilStopped = [&](ManualExecutor& ex) {
    CancellationCallback cb(
        cancelSrc.getToken(), [&]() noexcept { ex.add([] {}); });
    while (!cancelSrc.isCancellationRequested()) {
      ex.makeProgress();
    }
  };

  std::thread t1{[&] { runUntilStopped(ex1); }};
  std::thread t2{[&] { runUntilStopped(ex2); }};

  int value = 0;

  auto incrementValue = [&]() noexcept { ++value; };

  auto strandEx1 =
      StrandExecutor::create(strandContext, getKeepAliveToken(ex1));
  auto strandEx2 =
      StrandExecutor::create(strandContext, getKeepAliveToken(ex2));

  auto submitSomeTasks = [&]() {
    for (int i = 0; i < 10'000; ++i) {
      strandEx1->add(incrementValue);
      strandEx2->add(incrementValue);
    }
  };

  std::thread submitter1{submitSomeTasks};
  std::thread submitter2{submitSomeTasks};

  submitter1.join();
  submitter2.join();

  folly::Baton b1;
  folly::Baton b2;
  strandEx1->add([&] { b1.post(); });
  strandEx2->add([&] { b2.post(); });

  b1.wait();
  b2.wait();

  CHECK_EQ(40'000, value);

  cancelSrc.requestCancellation();

  t1.join();
  t2.join();
}

TEST(StrandExecutor, RequestContextPropagation) {
  auto exec = StrandExecutor::create();
  // Use a number larger than maxItemsToProcessSynchronously so we exercise
  // worker reschedules.
  constexpr size_t kNumTasks = 128;

  size_t numTasksRan = 0;
  for (size_t i = 0; i < kNumTasks; ++i) {
    // Create a unique RequestContext for each task and verify that it is
    // propagated correctly.
    RequestContextScopeGuard ctxGuard;
    auto f = [&, ctx = RequestContext::try_get()] {
      EXPECT_EQ(ctx, RequestContext::try_get());
      // Spend enough time that it is very likely that the queue is never empty.
      burnTime(100us);
      ++numTasksRan;
    };
    if (i % 2 == 0) {
      exec->add(std::move(f));
    } else {
      exec->addWithPriority(std::move(f), -1);
    }
  }

  folly::Baton baton;
  exec->add([&] { baton.post(); });
  baton.wait();

  EXPECT_EQ(numTasksRan, kNumTasks);
}
