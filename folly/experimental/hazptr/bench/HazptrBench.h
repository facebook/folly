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

template <typename InitFunc, typename Func, typename EndFunc>
inline uint64_t run_once(
    int nthreads,
    const InitFunc& init,
    const Func& fn,
    const EndFunc& endFn) {
  folly::BenchmarkSuspender susp;
  std::atomic<bool> start{false};
  std::atomic<int> started{0};

  init();

  std::vector<std::thread> threads(nthreads);
  for (int tid = 0; tid < nthreads; ++tid) {
    threads[tid] = std::thread([&, tid] {
      started.fetch_add(1);
      while (!start.load())
        /* spin */;
      fn(tid);
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
  endFn();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(tend - tbegin)
      .count();
}

template <typename RepFunc>
inline uint64_t bench(std::string name, int ops, const RepFunc& repFn) {
  int reps = 10;
  uint64_t min = UINTMAX_MAX;
  uint64_t max = 0;
  uint64_t sum = 0;

  repFn(); // sometimes first run is outlier
  for (int r = 0; r < reps; ++r) {
    uint64_t dur = repFn();
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
  std::cout << std::endl;
  return res;
}

inline uint64_t listBench(std::string name, int nthreads, int size) {
  int ops = 100000;
  auto repFn = [&] {
    SWMRListSet<uint64_t> s;
    auto init = [&] {
      for (int i = 0; i < size; ++i) {
        s.add(i);
      }
    };
    auto fn = [&](int tid) {
      for (int j = tid; j < ops; j += nthreads) {
        s.contains(size);
      }
    };
    auto endFn = [] {};
    return run_once(nthreads, init, fn, endFn);
  };
  return bench(name, ops, repFn);
}

inline uint64_t holderBench(std::string name, int nthreads) {
  int ops = 100000;
  auto repFn = [&] {
    auto init = [] {};
    auto fn = [&](int tid) {
      for (int j = tid; j < ops; j += nthreads) {
        hazptr_holder a[10];
      }
    };
    auto endFn = [] {};
    return run_once(nthreads, init, fn, endFn);
  };
  return bench(name, ops, repFn);
}

inline uint64_t retireBench(std::string name, int nthreads) {
  struct Foo : hazptr_obj_base<Foo> {
    int x;
  };
  int ops = 100000;
  auto repFn = [&] {
    auto init = [] {};
    auto fn = [&](int tid) {
      for (int j = tid; j < ops; j += nthreads) {
        Foo* p = new Foo;
        p->retire();
      }
    };
    auto endFn = [] {};
    return run_once(nthreads, init, fn, endFn);
  };
  return bench(name, ops, repFn);
}

const int nthr[] = {1, 10};
const int sizes[] = {10, 100};

inline void benches(std::string name) {
  std::cout << "------------------------------------------- " << name << "\n";
  for (int i : nthr) {
    std::cout << i << " threads -- construct/destruct 10 hazptr_holder-s"
              << std::endl;
    holderBench(name + "              ", i);
    holderBench(name + " - dup        ", i);
    std::cout << i << " threads -- allocate/retire/reclaim object" << std::endl;
    retireBench(name + "              ", i);
    retireBench(name + " - dup        ", i);
    for (int j : sizes) {
      std::cout << i << " threads -- " << j << "-item list" << std::endl;
      listBench(name + "              ", i, j);
      listBench(name + " - dup        ", i, j);
    }
  }
  std::cout << "----------------------------------------------------------\n";
}

} // namespace hazptr
} // namespace folly

/*
------------------------------------------- no amb - no tc
1 threads -- construct/destruct 10 hazptr_holder-s
no amb - no tc                 2518 ns   2461 ns   2431 ns
no amb - no tc - dup           2499 ns   2460 ns   2420 ns
1 threads -- allocate/retire/reclaim object
no amb - no tc                   85 ns     83 ns     81 ns
no amb - no tc - dup             83 ns     82 ns     81 ns
1 threads -- 10-item list
no amb - no tc                  655 ns    644 ns    639 ns
no amb - no tc - dup            658 ns    645 ns    641 ns
1 threads -- 100-item list
no amb - no tc                 2175 ns   2142 ns   2124 ns
no amb - no tc - dup           2294 ns   2228 ns   2138 ns
10 threads -- construct/destruct 10 hazptr_holder-s
no amb - no tc                 3893 ns   2932 ns   1391 ns
no amb - no tc - dup           3157 ns   2927 ns   2726 ns
10 threads -- allocate/retire/reclaim object
no amb - no tc                  152 ns    134 ns    127 ns
no amb - no tc - dup            141 ns    133 ns    128 ns
10 threads -- 10-item list
no amb - no tc                  532 ns    328 ns    269 ns
no amb - no tc - dup            597 ns    393 ns    271 ns
10 threads -- 100-item list
no amb - no tc                  757 ns    573 ns    412 ns
no amb - no tc - dup            819 ns    643 ns    420 ns
----------------------------------------------------------
-------------------------------------------    amb - no tc
1 threads -- construct/destruct 10 hazptr_holder-s
   amb - no tc                 2590 ns   2481 ns   2422 ns
   amb - no tc - dup           2519 ns   2468 ns   2424 ns
1 threads -- allocate/retire/reclaim object
   amb - no tc                   69 ns     68 ns     67 ns
   amb - no tc - dup             69 ns     68 ns     67 ns
1 threads -- 10-item list
   amb - no tc                  524 ns    510 ns    492 ns
   amb - no tc - dup            514 ns    507 ns    496 ns
1 threads -- 100-item list
   amb - no tc                  761 ns    711 ns    693 ns
   amb - no tc - dup            717 ns    694 ns    684 ns
10 threads -- construct/destruct 10 hazptr_holder-s
   amb - no tc                 3302 ns   2908 ns   1612 ns
   amb - no tc - dup           3220 ns   2909 ns   1641 ns
10 threads -- allocate/retire/reclaim object
   amb - no tc                  129 ns    123 ns    110 ns
   amb - no tc - dup            135 ns    127 ns    120 ns
10 threads -- 10-item list
   amb - no tc                  512 ns    288 ns    256 ns
   amb - no tc - dup            275 ns    269 ns    263 ns
10 threads -- 100-item list
   amb - no tc                  297 ns    289 ns    284 ns
   amb - no tc - dup            551 ns    358 ns    282 ns
----------------------------------------------------------
------------------------------------------- no amb -    tc
1 threads -- construct/destruct 10 hazptr_holder-s
no amb -    tc                   56 ns     55 ns     55 ns
no amb -    tc - dup             56 ns     54 ns     54 ns
1 threads -- allocate/retire/reclaim object
no amb -    tc                   63 ns     62 ns     62 ns
no amb -    tc - dup             64 ns     63 ns     62 ns
1 threads -- 10-item list
no amb -    tc                  190 ns    188 ns    187 ns
no amb -    tc - dup            193 ns    186 ns    182 ns
1 threads -- 100-item list
no amb -    tc                 1859 ns   1698 ns   1666 ns
no amb -    tc - dup           1770 ns   1717 ns   1673 ns
10 threads -- construct/destruct 10 hazptr_holder-s
no amb -    tc                   19 ns     11 ns      7 ns
no amb -    tc - dup             11 ns      8 ns      7 ns
10 threads -- allocate/retire/reclaim object
no amb -    tc                    9 ns      8 ns      8 ns
no amb -    tc - dup             10 ns      9 ns      8 ns
10 threads -- 10-item list
no amb -    tc                   40 ns     25 ns     21 ns
no amb -    tc - dup             24 ns     23 ns     21 ns
10 threads -- 100-item list
no amb -    tc                  215 ns    208 ns    188 ns
no amb -    tc - dup            215 ns    209 ns    197 ns
----------------------------------------------------------
-------------------------------------------    amb -    tc
1 threads -- construct/destruct 10 hazptr_holder-s
   amb -    tc                   56 ns     54 ns     54 ns
   amb -    tc - dup             55 ns     54 ns     53 ns
1 threads -- allocate/retire/reclaim object
   amb -    tc                   62 ns     61 ns     61 ns
   amb -    tc - dup             62 ns     61 ns     61 ns
1 threads -- 10-item list
   amb -    tc                   36 ns     35 ns     33 ns
   amb -    tc - dup             37 ns     35 ns     34 ns
1 threads -- 100-item list
   amb -    tc                  262 ns    247 ns    230 ns
   amb -    tc - dup            249 ns    238 ns    230 ns
10 threads -- construct/destruct 10 hazptr_holder-s
   amb -    tc                   14 ns     12 ns     11 ns
   amb -    tc - dup             12 ns     11 ns     11 ns
10 threads -- allocate/retire/reclaim object
   amb -    tc                   18 ns     17 ns     15 ns
   amb -    tc - dup             18 ns     17 ns     15 ns
10 threads -- 10-item list
   amb -    tc                    9 ns      8 ns      8 ns
   amb -    tc - dup              8 ns      8 ns      7 ns
10 threads -- 100-item list
   amb -    tc                   52 ns     42 ns     28 ns
   amb -    tc - dup             44 ns     37 ns     28 ns
----------------------------------------------------------
-------------------------------------------     one domain
1 threads -- construct/destruct 10 hazptr_holder-s
    one domain                   57 ns     56 ns     55 ns
    one domain - dup             56 ns     54 ns     53 ns
1 threads -- allocate/retire/reclaim object
    one domain                   87 ns     71 ns     64 ns
    one domain - dup             69 ns     68 ns     68 ns
1 threads -- 10-item list
    one domain                   32 ns     30 ns     29 ns
    one domain - dup             31 ns     30 ns     29 ns
1 threads -- 100-item list
    one domain                  269 ns    238 ns    226 ns
    one domain - dup            237 ns    232 ns    227 ns
10 threads -- construct/destruct 10 hazptr_holder-s
    one domain                   16 ns     12 ns     10 ns
    one domain - dup             11 ns     10 ns     10 ns
10 threads -- allocate/retire/reclaim object
    one domain                   19 ns     17 ns     16 ns
    one domain - dup             19 ns     17 ns     15 ns
10 threads -- 10-item list
    one domain                    6 ns      5 ns      5 ns
    one domain - dup              6 ns      5 ns      5 ns
10 threads -- 100-item list
    one domain                   40 ns     39 ns     35 ns
    one domain - dup             40 ns     39 ns     35 ns
----------------------------------------------------------
 */
