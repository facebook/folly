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

// folly/lang/Align.h takes hardware_destructive_interference_size from the
// standard library when it is available.  libstdc++ from GCC 12 onwards
// provides it on aarch64, and its value depends on -mcpu/-mtune (e.g. 256 for
// generic, 64 for -mcpu=neoverse-v2).  So an application and libfolly built
// with different flags can disagree on the layout of types that use it.
//
// This translation unit emulates that by providing a value that differs from
// what the rest of folly is built with.  lock_combine() is instantiated here,
// while lock() and unlock() come from the explicit instantiation of
// DistributedMutex<> in DistributedMutex.cpp, so wait nodes built here are
// walked by code built with the other value.

#include <cstddef>
#include <new>

// GCC < 12 does not know -Winterference-size, which Align.h disables when the
// standard library provides the constant
#if !defined(__cpp_lib_hardware_interference_size) && \
    (defined(__clang__) || __GNUC__ >= 12)
#define __cpp_lib_hardware_interference_size 201703L
namespace std {
inline constexpr std::size_t hardware_destructive_interference_size = 64;
inline constexpr std::size_t hardware_constructive_interference_size = 64;
} // namespace std
#endif

#include <folly/synchronization/DistributedMutex.h>

#include <glog/logging.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <folly/ScopeGuard.h>
#include <folly/portability/GTest.h>

namespace folly {

TEST(DistributedMutexInterferenceSize, CombineWithLockAcrossTranslationUnits) {
  auto&& mutex = DistributedMutex{};
  auto&& stop = std::atomic<bool>{false};
  auto&& finished = std::atomic<int>{0};
  auto&& counter = std::uint64_t{0};
  auto&& threads = std::vector<std::thread>{};
  constexpr auto kNumThreads = 8;

  for (auto i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i] {
      SCOPE_EXIT {
        finished.fetch_add(1);
      };
      while (!stop.load(std::memory_order_relaxed)) {
        if (i % 2) {
          auto lck = std::unique_lock{mutex};
          ++counter;
          continue;
        }
        auto [before, after] = mutex.lock_combine([&] {
          auto value = counter++;
          return std::make_pair(value, counter);
        });
        EXPECT_EQ(before + 1, after);
      }
    });
  }

  /* sleep override */
  std::this_thread::sleep_for(std::chrono::seconds{2});
  stop.store(true);

  // a lost wakeup leaves the threads waiting forever, so abort instead of
  // hanging in join()
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  while (finished.load() != kNumThreads) {
    CHECK(std::chrono::steady_clock::now() < deadline)
        << "DistributedMutex waiters never woke up";
    /* sleep override */
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace folly
