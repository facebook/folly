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

#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/AsyncScope.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/UnboundedQueue.h>
#include <folly/portability/GTest.h>

// Test RequestContext propagation behavior in various scenarios involving
// coroutines.

static const folly::RequestToken token{"RequestContextTest"};

class TagData : public folly::RequestData {
 public:
  int tag = 0;

  bool hasCallback() override { return false; }

  TagData() = default;
  explicit TagData(int t) : tag(t) {}
};

// -1 if there's no current RequestContext or no TagData on it.
static int getTag() {
  folly::RequestContext* rc = folly::RequestContext::try_get();
  if (rc == nullptr) {
    return -1;
  }
  auto* t = rc->getContextData(token);
  return t ? dynamic_cast<TagData*>(t)->tag : -1;
}

static void setTag(int t) {
  folly::RequestContext::get()->setContextData(
      token, std::make_unique<TagData>(t));
}

static void clearTag() {
  folly::RequestContext::get()->clearContextData(token);
}

// Like folly::ManualExecutor, but records history of request contexts.
class TestExecutor : public folly::Executor {
 public:
  struct Task {
    folly::Function<void()> f;
    std::shared_ptr<folly::RequestContext> rctx;
  };

  std::deque<Task> qu;
  std::vector<int> tags; // for added tasks

  void add(folly::Function<void()> f) override {
    tags.push_back(getTag());
    qu.push_back(Task{std::move(f), folly::RequestContext::saveContext()});
  }

  void drain() {
    while (!qu.empty()) {
      Task t = std::move(qu.front());
      qu.pop_front();
      folly::RequestContextScopeGuard rg(t.rctx);
      t.f();
    }
  }

  void assertTags(std::vector<int> t) {
    ASSERT_EQ(tags.size(), t.size());
    for (size_t i = 0; i < t.size(); ++i) {
      EXPECT_EQ(tags[i], t[i]);
    }
    tags.clear();
  }
};

TEST(RequestContextTest, Main) {
  TestExecutor exec;

  // Various things on which we'll co_await and check that request context is
  // preserved.
  folly::coro::Baton baton;
  folly::coro::UnboundedQueue<int> queue;
  folly::coro::Mutex mutex;
  bool locked = mutex.try_lock();
  EXPECT_TRUE(locked);

  // A generator on which we'll also co_await to see what happens.
  auto generator = [&]() -> folly::coro::AsyncGenerator<int&&> {
    // Request context propagated from the caller.
    EXPECT_EQ(getTag(), 4);
    exec.assertTags({});

    co_await folly::coro::co_reschedule_on_current_executor;
    EXPECT_EQ(getTag(), 4);
    exec.assertTags({4});

    // Change request context before yielding. This change will propagate to the
    // caller.
    folly::RequestContext::create();
    setTag(5);
    co_yield 10;
    // The caller changes request context before calling next(). This change is
    // propagated to us.
    EXPECT_EQ(getTag(), 6);

    co_await folly::coro::co_reschedule_on_current_executor;
    EXPECT_EQ(getTag(), 6);
    exec.assertTags({6});

    co_yield 20;
    EXPECT_EQ(getTag(), 6);
  };

  // Main coroutine of the test. Awaits on various things and checks that
  // request context was preserved/unpreserved when expected.
  auto task = [&]() -> folly::coro::Task<void> {
    // Request context propagated from task start().
    exec.assertTags({1});
    EXPECT_EQ(getTag(), 1);
    clearTag();
    setTag(2);
    EXPECT_EQ(getTag(), 2);

    // co_reschedule_on_current_executor preserves request context
    // (see CurrentExecutor.h).
    co_await folly::coro::co_reschedule_on_current_executor;
    exec.assertTags({2});

    // Baton, UnboundedQueue, Mutex, and other awaitables that don't customize
    // viaIfAsync preserve request context (see ViaIfAsync.h).

    // Baton.
    co_await baton;
    EXPECT_EQ(getTag(), 2);
    exec.assertTags({2});

    // UnboundedQueue.
    int v = co_await queue.dequeue();
    EXPECT_EQ(v, 42);
    EXPECT_EQ(getTag(), 2);
    exec.assertTags({2});

    // Mutex.
    co_await mutex.co_scoped_lock();
    EXPECT_EQ(getTag(), 2);
    exec.assertTags({2});

    // blockingWait.
    folly::coro::blockingWait([]() -> folly::coro::Task<void> {
      co_await folly::coro::co_reschedule_on_current_executor;
      co_return;
    }());
    EXPECT_EQ(getTag(), 2);
    exec.assertTags({});

    // Now on to the things that do leak request context.
    // This is probably intended. I guess the convention is that a
    // function/coroutine should to restore the original request context before
    // returning.

    // Task that doesn't suspend. If it changes request context before
    // returning, the change is propagated to us.
    v = co_await []() -> folly::coro::Task<int> {
      EXPECT_EQ(getTag(), 2);
      folly::RequestContext::create();
      setTag(3);
      EXPECT_EQ(getTag(), 3);
      co_return 30;
    }();
    EXPECT_EQ(v, 30);
    EXPECT_EQ(getTag(), 3);

    // Task that suspends. Same as above, request context gets out.
    v = co_await [&]() -> folly::coro::Task<int> {
      EXPECT_EQ(getTag(), 3);
      folly::RequestContext::create();
      EXPECT_EQ(getTag(), -1);
      setTag(4);
      co_await folly::coro::co_reschedule_on_current_executor;
      EXPECT_EQ(getTag(), 4);
      exec.assertTags({4});
      co_return 40;
    }();
    EXPECT_EQ(v, 40);
    EXPECT_EQ(getTag(), 4);

    // AsyncGenerator.

    auto gen = generator();
    EXPECT_EQ(getTag(), 4);

    // Request context propagates out of the generator.
    auto x = co_await gen.next();
    EXPECT_EQ(x.value(), 10);
    EXPECT_EQ(getTag(), 5);

    co_await folly::coro::co_reschedule_on_current_executor;
    EXPECT_EQ(getTag(), 5);
    exec.assertTags({5});

    // Request context propagates into the generator.
    folly::RequestContext::create();
    setTag(6);
    x = co_await gen.next();
    EXPECT_EQ(x.value(), 20);
    EXPECT_EQ(getTag(), 6);

    co_await folly::coro::co_reschedule_on_current_executor;
    EXPECT_EQ(getTag(), 6);
    exec.assertTags({6});
  };

  // Start the main coroutine.
  folly::SemiFuture<folly::Unit> f;
  {
    folly::RequestContextScopeGuard rg;
    setTag(1);
    f = task().scheduleOn(&exec).start();
  }
  exec.drain();
  EXPECT_FALSE(f.isReady());
  EXPECT_EQ(getTag(), -1);

  // Send the various wakeup signals the main coroutine expects.

  // Baton.
  {
    folly::RequestContextScopeGuard rg;
    setTag(100);
    baton.post();
    EXPECT_EQ(getTag(), 100);
  }
  exec.drain();
  EXPECT_FALSE(f.isReady());
  EXPECT_EQ(getTag(), -1);

  // UnboundedQueue.
  {
    folly::RequestContextScopeGuard rg;
    setTag(200);
    queue.enqueue(42);
    EXPECT_EQ(getTag(), 200);
  }
  exec.drain();
  EXPECT_FALSE(f.isReady());
  EXPECT_EQ(getTag(), -1);

  // Mutex.
  {
    folly::RequestContextScopeGuard rg;
    setTag(300);
    mutex.unlock();
    EXPECT_EQ(getTag(), 300);
  }
  exec.drain();

  // Main coroutine should be done now.
  EXPECT_TRUE(f.isReady());
  std::move(f).get();

  exec.assertTags({});
  EXPECT_EQ(getTag(), -1);
}
