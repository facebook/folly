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

#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include <folly/Traits.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/CallOnce.h>

static size_t const kNumThreads = 16;

template <typename CallOnceFunc>
void bm_impl(CallOnceFunc&& fn, size_t iters) {
  std::deque<std::thread> threads;
  for (size_t i = 0u; i < kNumThreads; ++i) {
    threads.emplace_back([&fn, iters] {
      for (size_t j = 0u; j < iters; ++j) {
        fn();
      }
    });
  }
  for (std::thread& t : threads) {
    t.join();
  }
}

TEST(FollyCallOnce, Simple) {
  folly::once_flag flag;
  auto fn = [&](int* outp) { ++*outp; };
  int out = 0;
  ASSERT_FALSE(folly::test_once(folly::as_const(flag)));
  folly::call_once(flag, fn, &out);
  ASSERT_TRUE(folly::test_once(folly::as_const(flag)));
  ASSERT_EQ(1, out);
  folly::call_once(flag, fn, &out);
  ASSERT_TRUE(folly::test_once(folly::as_const(flag)));
  ASSERT_EQ(1, out);
}

TEST(FollyCallOnce, Exception) {
  struct ExpectedException {};
  folly::once_flag flag;
  size_t numCalls = 0;
  EXPECT_THROW(
      folly::call_once(
          flag,
          [&] {
            ++numCalls;
            throw ExpectedException();
          }),
      ExpectedException);
  ASSERT_FALSE(folly::test_once(folly::as_const(flag)));
  EXPECT_EQ(1, numCalls);
  folly::call_once(flag, [&] { ++numCalls; });
  ASSERT_TRUE(folly::test_once(folly::as_const(flag)));
  EXPECT_EQ(2, numCalls);
}

TEST(FollyCallOnce, Stress) {
  for (int i = 0; i < 100; ++i) {
    folly::once_flag flag;
    int out = 0;
    bm_impl([&] { folly::call_once(flag, [&] { ++out; }); }, 100);
    ASSERT_EQ(1, out);
  }
}

namespace {
template <typename T>
struct Lazy {
  folly::aligned_storage_for_t<T> storage;
  folly::once_flag once;

  ~Lazy() {
    if (folly::test_once(once)) {
      reinterpret_cast<T&>(storage).~T();
    }
  }

  template <typename... A>
  T& construct_or_fetch(A&&... a) {
    folly::call_once(once, [&] { new (&storage) T(std::forward<A>(a)...); });
    return reinterpret_cast<T&>(storage);
  }
};
struct MaybeRaise {
  std::unique_ptr<int> check{std::make_unique<int>(7)};

  explicit MaybeRaise(bool raise) {
    if (raise) {
      throw std::runtime_error("raise");
    }
  }
};
} // namespace

TEST(FollyCallOnce, Lazy) {
  Lazy<MaybeRaise> lazy;
  EXPECT_THROW(lazy.construct_or_fetch(true), std::runtime_error);
  auto& num = *lazy.construct_or_fetch(false).check;
  EXPECT_EQ(7, num);
}

TEST(FollyTryCallOnce, example) {
  folly::once_flag once;
  EXPECT_FALSE(folly::try_call_once(once, []() noexcept { return false; }));
  EXPECT_FALSE(folly::test_once(once));
  EXPECT_TRUE(folly::try_call_once(once, []() noexcept { return true; }));
  EXPECT_TRUE(folly::test_once(once));
}
