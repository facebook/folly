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

#include <folly/concurrency/ThreadCachedSynchronized.h>

#include <folly/Benchmark.h>
#include <folly/Synchronized.h>
#include <folly/lang/Keep.h>

extern "C" FOLLY_KEEP int check_thread_cached_synchronized_int_get(
    folly::thread_cached_synchronized<int> const& ptr) {
  return ptr;
}

struct load_fn {
  template <typename T>
  T operator()(folly::relaxed_atomic<T> const& obj) const {
    return obj;
  }
  template <typename T>
  T operator()(folly::Synchronized<T> const& obj) const {
    return obj.copy();
  }
  template <typename T>
  T operator()(folly::thread_cached_synchronized<T> const& obj) const {
    return obj;
  }
};
inline constexpr load_fn load{};

template <typename Obj>
FOLLY_NOINLINE void bm_read_int(Obj& obj, std::size_t iters) {
  std::size_t sum = 0;
  while (iters--) {
    auto loaded = load(obj);
    folly::makeUnpredictable(loaded);
    sum += loaded;
  }
  folly::doNotOptimizeAway(sum);
}

BENCHMARK(read_int_atomic, iters) {
  folly::relaxed_atomic<int> obj{3};
  bm_read_int(obj, iters);
}

BENCHMARK(read_int_synchronized, iters) {
  folly::BenchmarkSuspender braces;
  folly::Synchronized<int> obj{3};
  braces.dismissing([&] { bm_read_int(obj, iters); });
}

BENCHMARK(read_int_thread_cached_synchronized, iters) {
  folly::BenchmarkSuspender braces;
  folly::thread_cached_synchronized<int> obj{3};
  braces.dismissing([&] { bm_read_int(obj, iters); });
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
