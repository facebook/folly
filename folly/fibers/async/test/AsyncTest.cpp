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

#include <folly/fibers/async/Async.h>

#include <folly/fibers/FiberManager.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/fibers/async/AsyncStack.h>
#include <folly/fibers/async/Baton.h>
#include <folly/fibers/async/Collect.h>
#include <folly/fibers/async/FiberManager.h>
#include <folly/fibers/async/Future.h>
#include <folly/fibers/async/Promise.h>
#include <folly/fibers/async/WaitUtils.h>
#include <folly/io/async/EventBase.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

#include <folly/experimental/coro/BlockingWait.h>
#include <folly/experimental/coro/Sleep.h>
#include <folly/fibers/async/Task.h>

using namespace ::testing;
using namespace folly::fibers;

namespace {
std::string getString() {
  return "foo";
}
async::Async<void> getAsyncNothing() {
  return {};
}
async::Async<std::string> getAsyncString() {
  return getString();
}
async::Async<folly::Optional<std::string>> getOptionalAsyncString() {
  // use move constructor to convert Async<std::string> to
  // Async<folly::Optional<std::string>>
  return getAsyncString();
}
async::Async<std::tuple<int, float, std::string>> getTuple() {
  return {0, 0.0, "0"};
}

struct NonCopyableNonMoveable {
  constexpr NonCopyableNonMoveable() noexcept = default;
  ~NonCopyableNonMoveable() = default;

  NonCopyableNonMoveable(const NonCopyableNonMoveable&) = delete;
  NonCopyableNonMoveable(NonCopyableNonMoveable&&) = delete;
  NonCopyableNonMoveable& operator=(NonCopyableNonMoveable const&) = delete;
  NonCopyableNonMoveable& operator=(NonCopyableNonMoveable&&) = delete;
};

async::Async<NonCopyableNonMoveable const&> getReference() {
  thread_local NonCopyableNonMoveable const value{};

  return value;
}

} // namespace

TEST(AsyncTest, asyncAwait) {
  folly::EventBase evb;
  auto& fm = getFiberManager(evb);

  EXPECT_NO_THROW(
      fm.addTaskFuture([&]() {
          async::init_await(getAsyncNothing());
          EXPECT_EQ(getString(), async::init_await(getAsyncString()));
          EXPECT_EQ(getString(), *async::init_await(getOptionalAsyncString()));
          async::init_await(getTuple());
          decltype(auto) ref = async::init_await(getReference());
          static_assert(
              std::is_same<decltype(ref), NonCopyableNonMoveable const&>::value,
              "");
        }).getVia(&evb));
}

TEST(AsyncTest, asyncBaton) {
  folly::EventBase evb;
  auto& fm = getFiberManager(evb);
  std::chrono::steady_clock::time_point start;

  EXPECT_NO_THROW(
      fm.addTaskFuture([&]() {
          constexpr auto kTimeout = std::chrono::milliseconds(230);
          {
            Baton baton;
            baton.post();
            EXPECT_NO_THROW(async::await(async::baton_wait(baton)));
          }
          {
            Baton baton;
            start = std::chrono::steady_clock::now();
            auto res = async::await(async::baton_try_wait_for(baton, kTimeout));
            EXPECT_FALSE(res);
            EXPECT_LE(start + kTimeout, std::chrono::steady_clock::now());
          }
          {
            Baton baton;
            start = std::chrono::steady_clock::now();
            auto res = async::await(
                async::baton_try_wait_until(baton, start + kTimeout));
            EXPECT_FALSE(res);
            EXPECT_LE(start + kTimeout, std::chrono::steady_clock::now());
          }
        }).getVia(&evb));
}

TEST(AsyncTest, asyncPromise) {
  folly::EventBase evb;
  auto& fm = getFiberManager(evb);

  fm.addTaskFuture([&]() {
      auto res = async::await(
          async::promiseWait([](Promise<int> p) { p.setValue(42); }));
      EXPECT_EQ(res, 42);
    }).getVia(&evb);
}

TEST(AsyncTest, asyncFuture) {
  folly::EventBase evb;
  auto& fm = getFiberManager(evb);

  // Return format: Info about where continuation runs (thread_id,
  // in_fiber_loop, on_active_fiber)
  auto getSemiFuture = []() {
    return folly::futures::sleep(std::chrono::milliseconds(1))
        .defer([](auto&&) {
          return std::make_tuple(
              std::this_thread::get_id(),
              FiberManager::getFiberManagerUnsafe() != nullptr,
              onFiber());
        });
  };

  fm.addTaskFuture([&]() {
      auto this_thread_id = std::this_thread::get_id();
      {
        // Unspecified executor: Deferred work will be executed inline on
        // producer thread
        auto res = async::init_await(
            async::futureWait(getSemiFuture().toUnsafeFuture()));
        EXPECT_NE(this_thread_id, std::get<0>(res));
        EXPECT_FALSE(std::get<1>(res));
        EXPECT_FALSE(std::get<2>(res));
      }

      {
        // Specified executor: Deferred work will be executed on evb
        auto res =
            async::init_await(async::futureWait(getSemiFuture().via(&evb)));
        EXPECT_EQ(this_thread_id, std::get<0>(res));
        EXPECT_FALSE(std::get<1>(res));
        EXPECT_FALSE(std::get<2>(res));
      }

      {
        // Unspecified executor: Deferred work will be executed inline on
        // consumer thread main-context
        auto res = async::init_await(async::futureWait(getSemiFuture()));
        EXPECT_EQ(this_thread_id, std::get<0>(res));
        EXPECT_TRUE(std::get<1>(res));
        EXPECT_FALSE(std::get<2>(res));
      }
    }).getVia(&evb);
}

#if FOLLY_HAS_COROUTINES
TEST(AsyncTest, asyncTask) {
  auto coroFn =
      []() -> folly::coro::Task<std::tuple<std::thread::id, bool, bool>> {
    co_await folly::coro::sleep(std::chrono::milliseconds(1));
    co_return std::make_tuple(
        std::this_thread::get_id(),
        FiberManager::getFiberManagerUnsafe() != nullptr,
        onFiber());
  };

  auto voidCoroFn = []() -> folly::coro::Task<void> {
    co_await folly::coro::sleep(std::chrono::milliseconds(1));
  };

  folly::EventBase evb;
  auto& fm = getFiberManager(evb);

  fm.addTaskFuture([&]() {
      // Coroutine should run to completion on fiber main context
      EXPECT_EQ(
          std::make_tuple(std::this_thread::get_id(), true, false),
          async::init_await(async::taskWait(coroFn())));
      async::init_await(async::taskWait(voidCoroFn()));
    }).getVia(&evb);
}
#endif

TEST(AsyncTest, asyncTraits) {
  static_assert(!async::is_async_v<int>);
  static_assert(async::is_async_v<async::Async<int>>);
  static_assert(
      std::is_same<int, async::async_inner_type_t<async::Async<int>>>::value);
  static_assert(
      std::is_same<int&, async::async_inner_type_t<async::Async<int&>>>::value);
}

#if __cpp_deduction_guides >= 201703
TEST(AsyncTest, asyncConstructorGuides) {
  auto getLiteral = []() { return async::Async(1); };
  // int&& -> int
  static_assert(
      std::is_same<int, async::async_inner_type_t<decltype(getLiteral())>>::
          value);

  int i = 0;
  auto tryGetRef = [&]() { return async::Async(static_cast<int&>(i)); };
  // int& -> int
  static_assert(
      std::is_same<int, async::async_inner_type_t<decltype(tryGetRef())>>::
          value);

  auto tryGetConstRef = [&]() {
    return async::Async(static_cast<const int&>(i));
  };
  // const int& -> int
  static_assert(
      std::is_same<int, async::async_inner_type_t<decltype(tryGetConstRef())>>::
          value);

  // int& explicitly constructed
  auto getRef = [&]() { return async::Async<int&>(i); };
  static_assert(
      std::is_same<int&, async::async_inner_type_t<decltype(getRef())>>::value);
}
#endif

TEST(FiberManager, asyncFiberManager) {
  {
    folly::EventBase evb;
    bool completed = false;
    async::addFiberFuture(
        [&]() -> async::Async<void> {
          completed = true;
          return {};
        },
        getFiberManager(evb))
        .getVia(&evb);
    EXPECT_TRUE(completed);

    size_t count = 0;
    EXPECT_EQ(
        1,
        async::addFiberFuture(
            [count]() mutable -> async::Async<int> { return ++count; },
            getFiberManager(evb))
            .getVia(&evb));
  }

  {
    bool outerCompleted = false;
    async::executeOnFiberAndWait([&]() -> async::Async<void> {
      bool innerCompleted = false;
      async::await(async::executeOnNewFiber([&]() -> async::Async<void> {
        innerCompleted = true;
        return {};
      }));
      EXPECT_TRUE(innerCompleted);
      outerCompleted = true;
      return {};
    });
    EXPECT_TRUE(outerCompleted);
  }

  {
    bool outerCompleted = false;
    bool innerCompleted = false;
    async::executeOnFiberAndWait([&]() -> async::Async<void> {
      outerCompleted = true;
      async::addFiber(
          [&]() -> async::Async<void> {
            innerCompleted = true;
            return {};
          },
          FiberManager::getFiberManager());
      return {};
    });
    EXPECT_TRUE(outerCompleted);
    EXPECT_TRUE(innerCompleted);
  }
}

TEST(AsyncTest, collect) {
  auto makeVoidTask = [](bool& ref) {
    return [&]() -> async::Async<void> {
      ref = true;
      return {};
    };
  };
  auto makeRetTask = [](int val) {
    return [=]() -> async::Async<int> { return val; };
  };
  async::executeOnFiberAndWait([&]() -> async::Async<void> {
    {
      std::array<bool, 3> cs{{false, false, false}};
      std::vector<folly::Function<async::Async<void>()>> tasks;
      tasks.emplace_back(makeVoidTask(cs[0]));
      tasks.emplace_back(makeVoidTask(cs[1]));
      tasks.emplace_back(makeVoidTask(cs[2]));
      async::await(async::collectAll(tasks));
      EXPECT_THAT(cs, ElementsAre(true, true, true));
    }

    {
      std::vector<folly::Function<async::Async<int>()>> tasks;
      tasks.emplace_back(makeRetTask(1));
      tasks.emplace_back(makeRetTask(2));
      tasks.emplace_back(makeRetTask(3));
      EXPECT_THAT(async::await(async::collectAll(tasks)), ElementsAre(1, 2, 3));
    }

    {
      std::array<bool, 3> cs{{false, false, false}};
      async::await(async::collectAll(
          makeVoidTask(cs[0]), makeVoidTask(cs[1]), makeVoidTask(cs[2])));
      EXPECT_THAT(cs, ElementsAre(true, true, true));
    }

    {
      EXPECT_EQ(
          std::make_tuple(1, 2, 3),
          async::await(async::collectAll(
              makeRetTask(1), makeRetTask(2), makeRetTask(3))));
    }

    return {};
  });
}

TEST(FiberManager, remoteFiberManager) {
  folly::EventBase evb;
  auto& fm = getFiberManager(evb);

  bool test1Finished = false;
  bool test2Finished = false;
  std::atomic<bool> remoteFinished = false;
  std::thread remote([&]() {
    async::executeOnRemoteFiberAndWait(
        [&]() -> async::Async<void> {
          test1Finished = true;
          return {};
        },
        fm);

    async::executeOnFiberAndWait([&] {
      return async::executeOnRemoteFiber(
          [&]() -> async::Async<void> {
            test2Finished = true;
            return {};
          },
          fm);
    });

    remoteFinished = true;
  });

  while (!remoteFinished) {
    evb.loopOnce();
  }
  remote.join();

  EXPECT_TRUE(test1Finished);
  EXPECT_TRUE(test2Finished);
}

folly::AsyncStackFrame* FOLLY_NULLABLE currentFrame() {
  auto* root = folly::tryGetCurrentAsyncStackRoot();
  return root ? root->getTopFrame() : nullptr;
}

FOLLY_NOINLINE std::vector<std::uintptr_t> getAsyncStack() {
  static constexpr size_t maxFrames = 100;
  std::array<std::uintptr_t, maxFrames> result;

  result[0] =
      reinterpret_cast<std::uintptr_t>(FOLLY_ASYNC_STACK_RETURN_ADDRESS());
  auto numFrames = folly::getAsyncStackTraceFromInitialFrame(
      currentFrame(), result.data() + 1, maxFrames - 1);

  return std::vector<std::uintptr_t>(
      std::make_move_iterator(result.begin()),
      std::make_move_iterator(result.begin()) + numFrames + 1);
}

template <typename F>
void expectStackSize(size_t size, F&& func) {
  EXPECT_EQ(size, func().size());
}

TEST(AsyncStack, fiberEntryPoint) {
  // Only frame will be getAsyncStack. No connection to callers
  expectStackSize(1, []() {
    return async::executeOnFiberAndWait(
        []() -> async::Async<std::vector<std::uintptr_t>> {
          // fiber
          return getAsyncStack();
        });
  });

  // [0] - getAsyncStack
  // [1] - fiber
  expectStackSize(2, []() {
    return async::executeOnFiberAndWait([]() {
      return async::executeWithNewRoot(
          [&]() -> async::Async<std::vector<std::uintptr_t>> {
            // fiber
            return getAsyncStack();
          },
          currentFrame());
    });
  });
}

TEST(AsyncStack, awaitTry) {
  async::executeOnFiberAndWait([]() -> async::Async<void> {
    auto res =
        async::await(async::awaitTry([]() -> async::Async<int> { return 1; }));
    EXPECT_EQ(*res, 1);
    res = async::await(async::awaitTry(
        []() -> async::Async<int> { throw std::logic_error(""); }));
    EXPECT_THROW(*res, std::logic_error);

    auto void_res = async::await(
        async::awaitTry([]() -> async::Async<void> { return {}; }));
    EXPECT_NO_THROW(*void_res);
    void_res = async::await(async::awaitTry(
        []() -> async::Async<void> { throw std::logic_error(""); }));
    EXPECT_THROW(*void_res, std::logic_error);

    auto try_res = async::await(
        async::awaitTry([]() -> async::Async<folly::Try<int>> { return 1; }));
    EXPECT_EQ(**try_res, 1);
    try_res =
        async::await(async::awaitTry([]() -> async::Async<folly::Try<int>> {
          return folly::makeTryWith(
              []() -> int { throw std::logic_error(""); });
        }));
    EXPECT_NO_THROW(*try_res);
    EXPECT_THROW(**try_res, std::logic_error);
    return {};
  });
}

TEST(AsyncStack, fiberToFiber) {
  // Only frame will be getAsyncStack. No connection to callers
  expectStackSize(1, []() {
    return async::executeOnFiberAndWait([]() {
      // fiber1
      return async::executeOnNewFiber(
          []() -> async::Async<std::vector<std::uintptr_t>> {
            // fiber2
            return getAsyncStack();
          });
    });
  });

  // [0] - getAsyncStack
  // [1] - fiber2
  // [2] - fiber1
  expectStackSize(3, []() {
    return async::executeOnFiberAndWait([]() {
      return async::executeWithNewRoot(
          [&]() {
            // fiber1
            auto* cf = CHECK_NOTNULL(currentFrame());
            return async::executeOnNewFiber([cf]() {
              return async::executeWithNewRoot(
                  []() -> async::Async<std::vector<std::uintptr_t>> {
                    // fiber2
                    return getAsyncStack();
                  },
                  cf);
            });
          },
          currentFrame());
    });
  });
}

TEST(AsyncStack, fiberToMainContext) {
  // Only frame will be getAsyncStack. No connection to callers
  expectStackSize(1, []() {
    return async::executeOnFiberAndWait(
        []() -> async::Async<std::vector<std::uintptr_t>> {
          // fiber
          return runInMainContext([]() { return getAsyncStack(); });
        });
  });

  // [0] - getAsyncStack
  // [1] - mainContext
  // [2] - fiber
  expectStackSize(3, []() {
    return async::executeOnFiberAndWait([]() {
      return async::executeWithNewRoot(
          [&]() -> async::Async<std::vector<std::uintptr_t>> {
            // fiber
            return async::runInMainContextWithTracing([]() {
              // mainContext
              return getAsyncStack();
            });
          },
          currentFrame());
    });
  });
}

#if FOLLY_HAS_COROUTINES
TEST(AsyncStack, coroToFiber) {
  // Only frame will be getAsyncStack. No connection to callers
  expectStackSize(1, [&]() {
    folly::EventBase evb;
    return folly::coro::blockingWait(
        [&]() -> folly::coro::Task<std::vector<std::uintptr_t>> {
          // coro
          co_return co_await async::addFiberFuture(
              []() -> async::Async<std::vector<std::uintptr_t>> {
                // fiber
                return getAsyncStack();
              },
              getFiberManager(evb));
        }(),
        &evb);
  });

  // [0] - getAsyncStack
  // [1] - fiber
  // [2] - coro
  // [3] - BlockingWaitTask
  // [4] - blockingWait
  expectStackSize(5, [&]() {
    folly::EventBase evb;
    return folly::coro::blockingWait(
        [&]() -> folly::coro::Task<std::vector<std::uintptr_t>> {
          // coro
          auto* cf = currentFrame();
          co_return co_await async::addFiberFuture(
              [cf]() {
                return async::executeWithNewRoot(
                    []() -> async::Async<std::vector<std::uintptr_t>> {
                      // fiber
                      return getAsyncStack();
                    },
                    cf);
              },
              getFiberManager(evb));
        }(),
        &evb);
  });
}
#endif
