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

  hazptr_owner<void> dummy_hptr[100];

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
------------------------------------------- No AMB - No Tc
1 threads -- 10-item list
no amb - no tc                  756 ns    688 ns    674 ns
no amb - no tc - dup            725 ns    688 ns    676 ns
1 threads -- 100-item list
no amb - no tc                 2469 ns   2366 ns   2334 ns
no amb - no tc - dup           2404 ns   2353 ns   2328 ns
10 threads -- 10-item list
no amb - no tc                  802 ns    764 ns    750 ns
no amb - no tc - dup            798 ns    776 ns    733 ns
10 threads -- 100-item list
no amb - no tc                 2209 ns   2157 ns   2118 ns
no amb - no tc - dup           2266 ns   2152 ns   1993 ns
----------------------------------------------------------
---------------------------------------------- AMB - No TC
1 threads -- 10-item list
amb - no tc                     554 ns    538 ns    525 ns
amb - no tc - dup               540 ns    530 ns    524 ns
1 threads -- 100-item list
amb - no tc                     731 ns    721 ns    715 ns
amb - no tc - dup               745 ns    724 ns    714 ns
10 threads -- 10-item list
amb - no tc                     777 ns    717 ns    676 ns
amb - no tc - dup               726 ns    669 ns    638 ns
10 threads -- 100-item list
amb - no tc                    1015 ns    985 ns    955 ns
amb - no tc - dup              1000 ns    978 ns    952 ns
----------------------------------------------------------
---------------------------------------------- No AMB - TC
1 threads -- 10-item list
no amb - tc                     209 ns    203 ns    199 ns
no amb - tc - dup               210 ns    202 ns    196 ns
1 threads -- 100-item list
no amb - tc                    1872 ns   1849 ns   1840 ns
no amb - tc - dup              1902 ns   1865 ns   1838 ns
10 threads -- 10-item list
no amb - tc                     136 ns     50 ns     23 ns
no amb - tc - dup               178 ns     85 ns     23 ns
10 threads -- 100-item list
no amb - tc                    1594 ns    651 ns    201 ns
no amb - tc - dup              1492 ns    615 ns    203 ns
----------------------------------------------------------
------------------------------------------------- AMB - TC
1 threads -- 10-item list
amb - tc                         45 ns     44 ns     44 ns
amb - tc - dup                   46 ns     46 ns     45 ns
1 threads -- 100-item list
amb - tc                        256 ns    246 ns    240 ns
amb - tc - dup                  242 ns    240 ns    238 ns
10 threads -- 10-item list
amb - tc                        120 ns     35 ns     13 ns
amb - tc - dup                  104 ns     34 ns      9 ns
10 threads -- 100-item list
amb - tc                        267 ns    129 ns     49 ns
amb - tc - dup                  766 ns    147 ns     42 ns
----------------------------------------------------------
 */
