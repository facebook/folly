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

#include <folly/io/async/EpollBackend.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseBackendBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/portability/GTest.h>

#include <string>
#include <vector>

#include <folly/portability/Unistd.h>

#if FOLLY_HAS_EPOLL

namespace folly {
namespace test {

namespace {
class TestEventHandler : public folly::EventHandler {
 public:
  TestEventHandler(folly::EventBase* evb, int fd)
      : folly::EventHandler(evb, folly::NetworkSocket::fromFd(fd)) {}
  void handlerReady(uint16_t) noexcept override { fired = true; }
  bool fired = false;
};

struct HookStats {
  int preHookCalls = 0;
  int postHookCalls = 0;
  int lastEventCount = -1;
};
} // namespace

// Verify both pre and post hooks fire with correct event count.
TEST(PollLoopHooksTest, EpollBackendInvokesBothHooks) {
  auto evbOptions = folly::EventBase::Options();
  evbOptions.setBackendFactory([]() {
    return std::make_unique<folly::EpollBackend>(
        folly::EpollBackend::Options());
  });
  folly::EventBase evb(std::move(evbOptions));

  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);

  TestEventHandler handler(&evb, fds[0]);
  handler.registerHandler(folly::EventHandler::READ);

  HookStats stats;
  EventBaseBackendBase::PollLoopHook hook;
  hook.preLoopHook = [](void* ctx) {
    static_cast<HookStats*>(ctx)->preHookCalls++;
  };
  hook.postLoopHook = [](void* ctx, int numEvents) {
    auto* s = static_cast<HookStats*>(ctx);
    s->postHookCalls++;
    s->lastEventCount = numEvents;
  };
  hook.hookCtx = &stats;
  evb.getBackend()->setPollLoopHook(hook);

  // Write to the pipe to make the read end ready.
  char c = 'x';
  ASSERT_EQ(::write(fds[1], &c, 1), 1);

  evb.loopOnce();

  ASSERT_TRUE(handler.fired);
  ASSERT_EQ(stats.preHookCalls, 1);
  ASSERT_EQ(stats.postHookCalls, 1);
  ASSERT_GE(stats.lastEventCount, 1);

  handler.unregisterHandler();
  ::close(fds[0]);
  ::close(fds[1]);
}

// Verify no crash when hooks are unset.
TEST(PollLoopHooksTest, EpollBackendNoHooksNoCrash) {
  auto evbOptions = folly::EventBase::Options();
  evbOptions.setBackendFactory([]() {
    return std::make_unique<folly::EpollBackend>(
        folly::EpollBackend::Options());
  });
  folly::EventBase evb(std::move(evbOptions));

  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);

  TestEventHandler handler(&evb, fds[0]);
  handler.registerHandler(folly::EventHandler::READ);

  // No hooks set — should not crash.
  char c = 'x';
  ASSERT_EQ(::write(fds[1], &c, 1), 1);

  evb.loopOnce();
  ASSERT_TRUE(handler.fired);

  handler.unregisterHandler();
  ::close(fds[0]);
  ::close(fds[1]);
}

// Verify setPollLoopHook(empty-hooks) stops invocation.
TEST(PollLoopHooksTest, EpollBackendHooksClearable) {
  auto evbOptions = folly::EventBase::Options();
  evbOptions.setBackendFactory([]() {
    return std::make_unique<folly::EpollBackend>(
        folly::EpollBackend::Options());
  });
  folly::EventBase evb(std::move(evbOptions));

  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);

  TestEventHandler handler(&evb, fds[0]);
  handler.registerHandler(
      folly::EventHandler::READ | folly::EventHandler::PERSIST);

  HookStats stats;
  EventBaseBackendBase::PollLoopHook hook;
  hook.preLoopHook = [](void* ctx) {
    static_cast<HookStats*>(ctx)->preHookCalls++;
  };
  hook.postLoopHook = [](void* ctx, int) {
    static_cast<HookStats*>(ctx)->postHookCalls++;
  };
  hook.hookCtx = &stats;
  evb.getBackend()->setPollLoopHook(hook);

  char c = 'x';
  ASSERT_EQ(::write(fds[1], &c, 1), 1);
  evb.loopOnce();
  ASSERT_EQ(stats.preHookCalls, 1);
  ASSERT_EQ(stats.postHookCalls, 1);

  // Clear the hooks and verify they are no longer called.
  hook.preLoopHook = nullptr;
  hook.postLoopHook = nullptr;
  hook.hookCtx = nullptr;
  evb.getBackend()->setPollLoopHook(hook);

  // Drain the pipe before writing again.
  ::read(fds[0], &c, 1);
  ASSERT_EQ(::write(fds[1], &c, 1), 1);
  evb.loopOnce();
  ASSERT_EQ(stats.preHookCalls, 1); // Should not have incremented.
  ASSERT_EQ(stats.postHookCalls, 1);

  handler.unregisterHandler();
  ::close(fds[0]);
  ::close(fds[1]);
}

// Verify that setting only the pre-hook (post is nullptr) works correctly.
TEST(PollLoopHooksTest, EpollBackendPreHookOnly) {
  auto evbOptions = folly::EventBase::Options();
  evbOptions.setBackendFactory([]() {
    return std::make_unique<folly::EpollBackend>(
        folly::EpollBackend::Options());
  });
  folly::EventBase evb(std::move(evbOptions));

  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);

  TestEventHandler handler(&evb, fds[0]);
  handler.registerHandler(folly::EventHandler::READ);

  int preHookCalls = 0;
  EventBaseBackendBase::PollLoopHook hook;
  hook.preLoopHook = [](void* ctx) { (*static_cast<int*>(ctx))++; };
  hook.postLoopHook = nullptr;
  hook.hookCtx = &preHookCalls;
  evb.getBackend()->setPollLoopHook(hook);

  char c = 'x';
  ASSERT_EQ(::write(fds[1], &c, 1), 1);
  evb.loopOnce();

  ASSERT_TRUE(handler.fired);
  ASSERT_EQ(preHookCalls, 1);

  handler.unregisterHandler();
  ::close(fds[0]);
  ::close(fds[1]);
}

// Verify that setting only the post-hook (pre is nullptr) works correctly.
TEST(PollLoopHooksTest, EpollBackendPostHookOnly) {
  auto evbOptions = folly::EventBase::Options();
  evbOptions.setBackendFactory([]() {
    return std::make_unique<folly::EpollBackend>(
        folly::EpollBackend::Options());
  });
  folly::EventBase evb(std::move(evbOptions));

  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);

  TestEventHandler handler(&evb, fds[0]);
  handler.registerHandler(folly::EventHandler::READ);

  int capturedEvents = -1;
  EventBaseBackendBase::PollLoopHook hook;
  hook.preLoopHook = nullptr;
  hook.postLoopHook = [](void* ctx, int numEvents) {
    *static_cast<int*>(ctx) = numEvents;
  };
  hook.hookCtx = &capturedEvents;
  evb.getBackend()->setPollLoopHook(hook);

  char c = 'x';
  ASSERT_EQ(::write(fds[1], &c, 1), 1);
  evb.loopOnce();

  ASSERT_TRUE(handler.fired);
  ASSERT_GE(capturedEvents, 1);

  handler.unregisterHandler();
  ::close(fds[0]);
  ::close(fds[1]);
}

// Verify that the pre-hook is always called before the post-hook.
TEST(PollLoopHooksTest, EpollBackendHookOrdering) {
  auto evbOptions = folly::EventBase::Options();
  evbOptions.setBackendFactory([]() {
    return std::make_unique<folly::EpollBackend>(
        folly::EpollBackend::Options());
  });
  folly::EventBase evb(std::move(evbOptions));

  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);

  TestEventHandler handler(&evb, fds[0]);
  handler.registerHandler(folly::EventHandler::READ);

  // Record the order in which hooks are called.
  struct OrderTracker {
    std::vector<std::string> callOrder;
  } tracker;

  EventBaseBackendBase::PollLoopHook hook;
  hook.preLoopHook = [](void* ctx) {
    static_cast<OrderTracker*>(ctx)->callOrder.emplace_back("pre");
  };
  hook.postLoopHook = [](void* ctx, int) {
    static_cast<OrderTracker*>(ctx)->callOrder.emplace_back("post");
  };
  hook.hookCtx = &tracker;
  evb.getBackend()->setPollLoopHook(hook);

  char c = 'x';
  ASSERT_EQ(::write(fds[1], &c, 1), 1);
  evb.loopOnce();

  ASSERT_EQ(tracker.callOrder.size(), 2);
  ASSERT_EQ(tracker.callOrder[0], "pre");
  ASSERT_EQ(tracker.callOrder[1], "post");

  handler.unregisterHandler();
  ::close(fds[0]);
  ::close(fds[1]);
}

// Verify hooks fire on every poll iteration, not just the first.
TEST(PollLoopHooksTest, EpollBackendMultipleIterations) {
  auto evbOptions = folly::EventBase::Options();
  evbOptions.setBackendFactory([]() {
    return std::make_unique<folly::EpollBackend>(
        folly::EpollBackend::Options());
  });
  folly::EventBase evb(std::move(evbOptions));

  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);

  TestEventHandler handler(&evb, fds[0]);
  handler.registerHandler(
      folly::EventHandler::READ | folly::EventHandler::PERSIST);

  HookStats stats;
  EventBaseBackendBase::PollLoopHook hook;
  hook.preLoopHook = [](void* ctx) {
    static_cast<HookStats*>(ctx)->preHookCalls++;
  };
  hook.postLoopHook = [](void* ctx, int numEvents) {
    auto* s = static_cast<HookStats*>(ctx);
    s->postHookCalls++;
    s->lastEventCount = numEvents;
  };
  hook.hookCtx = &stats;
  evb.getBackend()->setPollLoopHook(hook);

  constexpr int kIterations = 5;
  for (int i = 0; i < kIterations; ++i) {
    char c = 'x';
    ASSERT_EQ(::write(fds[1], &c, 1), 1);
    evb.loopOnce();
    // Drain the pipe for the next iteration.
    ::read(fds[0], &c, 1);
  }

  ASSERT_EQ(stats.preHookCalls, kIterations);
  ASSERT_EQ(stats.postHookCalls, kIterations);

  handler.unregisterHandler();
  ::close(fds[0]);
  ::close(fds[1]);
}

// Verify hooks can be replaced with new ones mid-flight.
TEST(PollLoopHooksTest, EpollBackendHookReplacement) {
  auto evbOptions = folly::EventBase::Options();
  evbOptions.setBackendFactory([]() {
    return std::make_unique<folly::EpollBackend>(
        folly::EpollBackend::Options());
  });
  folly::EventBase evb(std::move(evbOptions));

  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);

  TestEventHandler handler(&evb, fds[0]);
  handler.registerHandler(
      folly::EventHandler::READ | folly::EventHandler::PERSIST);

  // First set of hooks.
  HookStats stats1;
  EventBaseBackendBase::PollLoopHook hook1;
  hook1.preLoopHook = [](void* ctx) {
    static_cast<HookStats*>(ctx)->preHookCalls++;
  };
  hook1.postLoopHook = [](void* ctx, int) {
    static_cast<HookStats*>(ctx)->postHookCalls++;
  };
  hook1.hookCtx = &stats1;
  evb.getBackend()->setPollLoopHook(hook1);

  char c = 'x';
  ASSERT_EQ(::write(fds[1], &c, 1), 1);
  evb.loopOnce();
  ASSERT_EQ(stats1.preHookCalls, 1);
  ASSERT_EQ(stats1.postHookCalls, 1);

  // Replace with a second set of hooks.
  HookStats stats2;
  EventBaseBackendBase::PollLoopHook hook2;
  hook2.preLoopHook = [](void* ctx) {
    static_cast<HookStats*>(ctx)->preHookCalls++;
  };
  hook2.postLoopHook = [](void* ctx, int) {
    static_cast<HookStats*>(ctx)->postHookCalls++;
  };
  hook2.hookCtx = &stats2;
  evb.getBackend()->setPollLoopHook(hook2);

  ::read(fds[0], &c, 1);
  ASSERT_EQ(::write(fds[1], &c, 1), 1);
  evb.loopOnce();

  // First hooks should not have been called again.
  ASSERT_EQ(stats1.preHookCalls, 1);
  ASSERT_EQ(stats1.postHookCalls, 1);
  // Second hooks should have been called.
  ASSERT_EQ(stats2.preHookCalls, 1);
  ASSERT_EQ(stats2.postHookCalls, 1);

  handler.unregisterHandler();
  ::close(fds[0]);
  ::close(fds[1]);
}

// Verify numEvents reflects the actual number of ready file descriptors.
TEST(PollLoopHooksTest, EpollBackendMultipleSimultaneousEvents) {
  auto evbOptions = folly::EventBase::Options();
  evbOptions.setBackendFactory([]() {
    auto opts = folly::EpollBackend::Options();
    opts.setNumLoopEvents(256);
    return std::make_unique<folly::EpollBackend>(std::move(opts));
  });
  folly::EventBase evb(std::move(evbOptions));

  // Create multiple pipes and register handlers for all of them.
  constexpr int kNumPipes = 4;
  int pipeFds[kNumPipes][2];
  std::vector<std::unique_ptr<TestEventHandler>> handlers;

  for (int i = 0; i < kNumPipes; ++i) {
    ASSERT_EQ(::pipe(pipeFds[i]), 0);
    auto h = std::make_unique<TestEventHandler>(&evb, pipeFds[i][0]);
    h->registerHandler(folly::EventHandler::READ);
    handlers.push_back(std::move(h));
  }

  int capturedEvents = -1;
  EventBaseBackendBase::PollLoopHook hook;
  hook.preLoopHook = nullptr;
  hook.postLoopHook = [](void* ctx, int numEvents) {
    *static_cast<int*>(ctx) = numEvents;
  };
  hook.hookCtx = &capturedEvents;
  evb.getBackend()->setPollLoopHook(hook);

  // Write to ALL pipes before polling, so all are ready simultaneously.
  for (int i = 0; i < kNumPipes; ++i) {
    char c = 'x';
    ASSERT_EQ(::write(pipeFds[i][1], &c, 1), 1);
  }

  evb.loopOnce();

  // The event count should be at least kNumPipes (could be more due to
  // internal events like the notification queue eventfd).
  ASSERT_GE(capturedEvents, kNumPipes);
  for (int i = 0; i < kNumPipes; ++i) {
    ASSERT_TRUE(handlers[i]->fired);
  }

  for (int i = 0; i < kNumPipes; ++i) {
    handlers[i]->unregisterHandler();
    ::close(pipeFds[i][0]);
    ::close(pipeFds[i][1]);
  }
}

// Verify hooks are NOT invoked on the default LibEvent backend.
TEST(PollLoopHooksTest, LibEventBackendDoesNotInvokeHooks) {
  // Default EventBase uses LibEvent backend.
  folly::EventBase evb;

  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);

  TestEventHandler handler(&evb, fds[0]);
  handler.registerHandler(folly::EventHandler::READ);

  HookStats stats;
  EventBaseBackendBase::PollLoopHook hook;
  hook.preLoopHook = [](void* ctx) {
    static_cast<HookStats*>(ctx)->preHookCalls++;
  };
  hook.postLoopHook = [](void* ctx, int) {
    static_cast<HookStats*>(ctx)->postHookCalls++;
  };
  hook.hookCtx = &stats;
  evb.getBackend()->setPollLoopHook(hook);

  char c = 'x';
  ASSERT_EQ(::write(fds[1], &c, 1), 1);
  evb.loopOnce();

  // LibEvent backend does not call the hooks.
  ASSERT_TRUE(handler.fired);
  ASSERT_EQ(stats.preHookCalls, 0);
  ASSERT_EQ(stats.postHookCalls, 0);

  handler.unregisterHandler();
  ::close(fds[0]);
  ::close(fds[1]);
}

// Verify hooks coexist with the global weak-symbol hooks without interfering.
// (The weak-symbol hooks resolve to nullptr in this test binary, so this
// verifies the per-EventBase hooks work independently.)
TEST(PollLoopHooksTest, EpollBackendHooksCoexistWithWeakSymbolHooks) {
  auto evbOptions = folly::EventBase::Options();
  evbOptions.setBackendFactory([]() {
    return std::make_unique<folly::EpollBackend>(
        folly::EpollBackend::Options());
  });
  folly::EventBase evb(std::move(evbOptions));

  int fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds), 0);

  TestEventHandler handler(&evb, fds[0]);
  handler.registerHandler(folly::EventHandler::READ);

  HookStats stats;
  EventBaseBackendBase::PollLoopHook hook;
  hook.preLoopHook = [](void* ctx) {
    static_cast<HookStats*>(ctx)->preHookCalls++;
  };
  hook.postLoopHook = [](void* ctx, int numEvents) {
    auto* s = static_cast<HookStats*>(ctx);
    s->postHookCalls++;
    s->lastEventCount = numEvents;
  };
  hook.hookCtx = &stats;
  evb.getBackend()->setPollLoopHook(hook);

  char c = 'x';
  ASSERT_EQ(::write(fds[1], &c, 1), 1);
  evb.loopOnce();

  // Per-EventBase hooks should fire regardless of whether the global
  // weak-symbol hooks are defined.
  ASSERT_TRUE(handler.fired);
  ASSERT_EQ(stats.preHookCalls, 1);
  ASSERT_EQ(stats.postHookCalls, 1);
  ASSERT_GE(stats.lastEventCount, 1);

  handler.unregisterHandler();
  ::close(fds[0]);
  ::close(fds[1]);
}

// Verify two different EventBases can have independent hooks.
TEST(PollLoopHooksTest, EpollBackendIndependentHooksPerEventBase) {
  auto makeEvb = []() {
    auto evbOptions = folly::EventBase::Options();
    evbOptions.setBackendFactory([]() {
      return std::make_unique<folly::EpollBackend>(
          folly::EpollBackend::Options());
    });
    return std::make_unique<folly::EventBase>(std::move(evbOptions));
  };

  auto evb1 = makeEvb();
  auto evb2 = makeEvb();

  int fds1[2] = {-1, -1};
  int fds2[2] = {-1, -1};
  ASSERT_EQ(::pipe(fds1), 0);
  ASSERT_EQ(::pipe(fds2), 0);

  TestEventHandler handler1(evb1.get(), fds1[0]);
  handler1.registerHandler(folly::EventHandler::READ);
  TestEventHandler handler2(evb2.get(), fds2[0]);
  handler2.registerHandler(folly::EventHandler::READ);

  HookStats stats1;
  EventBaseBackendBase::PollLoopHook hook1;
  hook1.preLoopHook = [](void* ctx) {
    static_cast<HookStats*>(ctx)->preHookCalls++;
  };
  hook1.postLoopHook = [](void* ctx, int) {
    static_cast<HookStats*>(ctx)->postHookCalls++;
  };
  hook1.hookCtx = &stats1;
  evb1->getBackend()->setPollLoopHook(hook1);

  HookStats stats2;
  EventBaseBackendBase::PollLoopHook hook2;
  hook2.preLoopHook = [](void* ctx) {
    static_cast<HookStats*>(ctx)->preHookCalls++;
  };
  hook2.postLoopHook = [](void* ctx, int) {
    static_cast<HookStats*>(ctx)->postHookCalls++;
  };
  hook2.hookCtx = &stats2;
  evb2->getBackend()->setPollLoopHook(hook2);

  // Only trigger events on evb1.
  char c = 'x';
  ASSERT_EQ(::write(fds1[1], &c, 1), 1);
  evb1->loopOnce();

  ASSERT_TRUE(handler1.fired);
  ASSERT_EQ(stats1.preHookCalls, 1);
  ASSERT_EQ(stats1.postHookCalls, 1);
  // evb2 hooks should not have been called.
  ASSERT_EQ(stats2.preHookCalls, 0);
  ASSERT_EQ(stats2.postHookCalls, 0);

  // Now trigger events on evb2.
  ASSERT_EQ(::write(fds2[1], &c, 1), 1);
  evb2->loopOnce();

  ASSERT_TRUE(handler2.fired);
  ASSERT_EQ(stats2.preHookCalls, 1);
  ASSERT_EQ(stats2.postHookCalls, 1);
  // evb1 hooks should still be at 1.
  ASSERT_EQ(stats1.preHookCalls, 1);
  ASSERT_EQ(stats1.postHookCalls, 1);

  handler1.unregisterHandler();
  handler2.unregisterHandler();
  ::close(fds1[0]);
  ::close(fds1[1]);
  ::close(fds2[0]);
  ::close(fds2[1]);
}

} // namespace test
} // namespace folly

#endif
