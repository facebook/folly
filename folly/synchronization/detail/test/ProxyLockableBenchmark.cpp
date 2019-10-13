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

#include <folly/Benchmark.h>
#include <folly/synchronization/detail/ProxyLockable.h>

#include <mutex>
#include <tuple>

namespace folly {
namespace detail {
namespace {
class StdMutexWrapper {
 public:
  int lock() {
    mutex_.lock();
    return 1;
  }
  void unlock(int) {
    mutex_.unlock();
  }

  std::mutex mutex_{};
};
} // namespace

BENCHMARK(StdMutexWithoutUniqueLock, iters) {
  auto&& mutex = std::mutex{};
  for (auto i = std::size_t{0}; i < iters; ++i) {
    mutex.lock();
    mutex.unlock();
  }
}
BENCHMARK(StdMutexWithUniqueLock, iters) {
  auto&& mutex = std::mutex{};
  for (auto i = std::size_t{0}; i < iters; ++i) {
    auto&& lck = std::unique_lock<std::mutex>{mutex};
    std::ignore = lck;
  }
}
BENCHMARK(StdMutexWithLockGuard, iters) {
  auto&& mutex = std::mutex{};
  for (auto i = std::size_t{0}; i < iters; ++i) {
    auto&& lck = std::lock_guard<std::mutex>{mutex};
    std::ignore = lck;
  }
}
BENCHMARK(StdMutexWithProxyLockableUniqueLock, iters) {
  auto&& mutex = StdMutexWrapper{};
  for (auto i = std::size_t{0}; i < iters; ++i) {
    auto&& lck = ProxyLockableUniqueLock<StdMutexWrapper>{mutex};
    std::ignore = lck;
  }
}
BENCHMARK(StdMutexWithProxyLockableLockGuard, iters) {
  auto&& mutex = StdMutexWrapper{};
  for (auto i = std::size_t{0}; i < iters; ++i) {
    auto&& lck = ProxyLockableLockGuard<StdMutexWrapper>{mutex};
    std::ignore = lck;
  }
}
} // namespace detail
} // namespace folly

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
}
