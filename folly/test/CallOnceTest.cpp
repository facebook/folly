/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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

#include <folly/Benchmark.h>
#include <folly/CallOnce.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

DEFINE_int32(threads, 16, "benchmark concurrency");

template <typename CallOnceFunc>
void bm_impl(CallOnceFunc&& fn, int64_t iters) {
  std::deque<std::thread> threads;
  for (int i = 0; i < FLAGS_threads; ++i) {
    threads.emplace_back([&fn, iters] {
      for (int64_t j = 0; j < iters; ++j) {
        fn();
      }
    });
  }
  for (std::thread& t : threads) {
    t.join();
  }
}

BENCHMARK(StdCallOnceBench, iters) {
  std::once_flag flag;
  int out = 0;
  bm_impl([&] { std::call_once(flag, [&] { ++out; }); }, iters);
  ASSERT_EQ(1, out);
}

BENCHMARK(FollyCallOnceBench, iters) {
  folly::once_flag flag;
  int out = 0;
  bm_impl([&] { folly::call_once(flag, [&] { ++out; }); }, iters);
  ASSERT_EQ(1, out);
}

TEST(FollyCallOnce, Simple) {
  folly::once_flag flag;
  auto fn = [&](int* outp) { ++*outp; };
  int out = 0;
  folly::call_once(flag, fn, &out);
  folly::call_once(flag, fn, &out);
  ASSERT_EQ(1, out);
}

TEST(FollyCallOnce, Stress) {
  for (int i = 0; i < 100; ++i) {
    folly::once_flag flag;
    int out = 0;
    bm_impl([&] { folly::call_once(flag, [&] { ++out; }); }, 100);
    ASSERT_EQ(1, out);
  }
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_benchmark) {
    folly::runBenchmarksOnFlag();
    return 0;
  } else {
    return RUN_ALL_TESTS();
  }
}
