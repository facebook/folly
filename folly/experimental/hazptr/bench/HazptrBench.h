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

  hazptr_holder dummy_hptr[100];

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
  auto tend = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(tend - tbegin)
      .count();
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

  const std::string unit = " ns";
  uint64_t avg = sum / reps;
  uint64_t res = min;
  std::cout << name;
  std::cout << "   " << std::setw(4) << max / ops << unit;
  std::cout << "   " << std::setw(4) << avg / ops << unit;
  std::cout << "   " << std::setw(4) << res / ops << unit;
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
------------------------------------------- No AMB - No Tc
1 threads -- 10-item list
no amb - no tc                  713 ns    672 ns    648 ns
no amb - no tc - dup            692 ns    660 ns    648 ns
1 threads -- 100-item list
no amb - no tc                 2167 ns   2146 ns   2133 ns
no amb - no tc - dup           2210 ns   2153 ns   2133 ns
10 threads -- 10-item list
no amb - no tc                  716 ns    614 ns    285 ns
no amb - no tc - dup            750 ns    546 ns    285 ns
10 threads -- 100-item list
no amb - no tc                 1923 ns   1482 ns    862 ns
no amb - no tc - dup           1978 ns   1614 ns   1112 ns
----------------------------------------------------------
---------------------------------------------- AMB - No TC
1 threads -- 10-item list
amb - no tc                     519 ns    504 ns    501 ns
amb - no tc - dup               533 ns    511 ns    500 ns
1 threads -- 100-item list
amb - no tc                     721 ns    696 ns    689 ns
amb - no tc - dup               786 ns    718 ns    688 ns
10 threads -- 10-item list
amb - no tc                     695 ns    565 ns    380 ns
amb - no tc - dup               710 ns    450 ns    242 ns
10 threads -- 100-item list
amb - no tc                     921 ns    773 ns    573 ns
amb - no tc - dup               594 ns    441 ns    409 ns
----------------------------------------------------------
---------------------------------------------- No AMB - TC
1 threads -- 10-item list
no amb - tc                     182 ns    180 ns    178 ns
no amb - tc - dup               205 ns    183 ns    178 ns
1 threads -- 100-item list
no amb - tc                    1756 ns   1697 ns   1670 ns
no amb - tc - dup              1718 ns   1681 ns   1666 ns
10 threads -- 10-item list
no amb - tc                     174 ns    120 ns     55 ns
no amb - tc - dup               174 ns    143 ns    114 ns
10 threads -- 100-item list
no amb - tc                    1480 ns   1058 ns    565 ns
no amb - tc - dup              1834 ns   1327 ns   1065 ns
----------------------------------------------------------
------------------------------------------------- AMB - TC
1 threads -- 10-item list
amb - tc                         32 ns     32 ns     31 ns
amb - tc - dup                   32 ns     32 ns     31 ns
1 threads -- 100-item list
amb - tc                        238 ns    229 ns    221 ns
amb - tc - dup                  224 ns    222 ns    221 ns
10 threads -- 10-item list
amb - tc                         27 ns     20 ns     13 ns
amb - tc - dup                   28 ns     22 ns     18 ns
10 threads -- 100-item list
amb - tc                        221 ns    165 ns    116 ns
amb - tc - dup                  277 ns    169 ns    120 ns
----------------------------------------------------------
 */
