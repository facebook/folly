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

#include <folly/executors/task_queue/UnboundedBlockingQueue.h>

#include <cstddef>
#include <numeric>
#include <thread>
#include <vector>

#include <boost/thread/barrier.hpp>
#include <glog/logging.h>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>

DEFINE_int32(
    drain_full_queue_qlen,
    4096,
    "drain_full_queue initial queue length multiplier");
DEFINE_int32(
    drain_full_queue_nthreads,
    4,
    "drain_full_queue number of draining threads");

BENCHMARK(drain_full_queue, iters) {
  // goal: measure contention in take()
  folly::BenchmarkSuspender braces;

  auto const qlen = size_t(FLAGS_drain_full_queue_qlen * iters);
  auto const nthreads = size_t(FLAGS_drain_full_queue_nthreads);

  CHECK_GE(qlen, nthreads);

  folly::UnboundedBlockingQueue<int> q;
  for (auto i = qlen; i != 0; --i) {
    q.add(i);
  }
  for (auto i = nthreads; i != 0; --i) {
    q.add(0); // poison
  }

  std::vector<size_t> counts(nthreads);
  std::vector<std::thread> threads(nthreads);

  boost::barrier barrier(1 + nthreads);

  for (auto i = 0u; i < nthreads; ++i) {
    threads[i] = std::thread([&, i] {
      barrier.wait(); // A - wait for thread start
      barrier.wait(); // B - init the work
      while (auto j = q.take()) { // take until poison
        counts[i] += j;
      }
      barrier.wait(); // C - join the work
    });
  }

  barrier.wait(); // A - wait for thread start
  braces.dismissing([&] {
    barrier.wait(); // B - init the work
    barrier.wait(); // C - join the work
  });

  for (auto i = 0u; i < nthreads; ++i) {
    threads[i].join();
  }

  size_t sum = std::accumulate(counts.begin(), counts.end(), size_t(0));
  CHECK_EQ((qlen * (qlen + 1) / 2), sum);
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
