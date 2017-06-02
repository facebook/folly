/*
 * Copyright 2017 Facebook, Inc.
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
#pragma once

#include <folly/Benchmark.h>
#include <folly/experimental/hazptr/example/SWMRList.h>
#include <folly/portability/GTest.h>

#include <glog/logging.h>

#include <atomic>
#include <thread>

namespace folly {
namespace hazptr {

inline uint64_t run_once(int nthreads, int size, int ops) {
  folly::BenchmarkSuspender susp;
  SWMRListSet<uint64_t> s;
  std::atomic<bool> start{false};
  std::atomic<int> started{0};

  for (int i = 0; i < size; ++i) {
    s.add(i);
  }

  std::vector<std::thread> threads(nthreads);
  for (int tid = 0; tid < nthreads; ++tid) {
    threads[tid] = std::thread([&, tid] {
      started.fetch_add(1);
      while (!start.load())
        /* spin */;

      for (int j = tid; j < ops; j += nthreads) {
        s.contains(j);
      }
    });
  }

  while (started.load() < nthreads)
    /* spin */;

  // begin time measurement
  auto tbegin = std::chrono::steady_clock::now();
  susp.dismiss();
  start.store(true);

  for (auto& t : threads) {
    t.join();
  }

  susp.rehire();
  // end time measurement
  uint64_t duration = 0;
  auto tend = std::chrono::steady_clock::now();
  duration = std::chrono::duration_cast<std::chrono::nanoseconds>(tend - tbegin)
                 .count();
  return duration;
}

inline uint64_t bench(std::string name, int nthreads, int size, uint64_t base) {
  int reps = 10;
  int ops = 100000;
  uint64_t min = UINTMAX_MAX;
  uint64_t max = 0;
  uint64_t sum = 0;

  run_once(nthreads, size, ops); // sometimes first run is outlier
  for (int r = 0; r < reps; ++r) {
    uint64_t dur = run_once(nthreads, size, ops);
    sum += dur;
    min = std::min(min, dur);
    max = std::max(max, dur);
  }

  uint64_t avg = sum / reps;
  uint64_t res = min;
  std::cout << name;
  std::cout << "   " << std::setw(4) << max / ops << " ns";
  std::cout << "   " << std::setw(4) << avg / ops << " ns";
  std::cout << "   " << std::setw(4) << res / ops << " ns";
  if (base) {
    std::cout << " " << std::setw(3) << 100 * base / res << "%";
  }
  std::cout << std::endl;
  return res;
}

const int nthr[] = {1, 10};
const int sizes[] = {10, 100};
const std::string header = "Test_name, Max time, Avg time, Min time";

} // namespace folly {
} // namespace hazptr {

/*
--------------------------------------------------- No AMB
1 threads -- 10-item list
no amb                          210 ns    204 ns    202 ns
no amb - dup                    213 ns    207 ns    203 ns
1 threads -- 100-item list
no amb                         1862 ns   1810 ns   1778 ns
no amb - dup                   1791 ns   1785 ns   1777 ns
10 threads -- 10-item list
no amb                          227 ns    161 ns    143 ns
no amb - dup                    145 ns    144 ns    143 ns
10 threads -- 100-item list
no amb                          520 ns    518 ns    515 ns
no amb - dup                    684 ns    536 ns    516 ns
----------------------------------------------------------
------------------------------------------------------ AMB
1 threads -- 10-item list
amb                              48 ns     46 ns     45 ns
amb - dup                        47 ns     45 ns     45 ns
1 threads -- 100-item list
amb                             242 ns    236 ns    234 ns
amb - dup                       243 ns    238 ns    234 ns
10 threads -- 10-item list
amb                             226 ns    130 ns    109 ns
amb - dup                       161 ns    115 ns    109 ns
10 threads -- 100-item list
amb                             192 ns    192 ns    191 ns
amb - dup                       416 ns    324 ns    192 ns
----------------------------------------------------------
 */
