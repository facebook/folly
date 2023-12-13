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

#include <folly/experimental/SingleWriterFixedHashMap.h>

#include <folly/Benchmark.h>
#include <folly/container/Array.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/test/Barrier.h>

#include <boost/thread/barrier.hpp>
#include <glog/logging.h>

#include <atomic>
#include <thread>

DEFINE_bool(bench, false, "run benchmark");
DEFINE_int32(reps, 10, "number of reps");
DEFINE_int32(ops, 1000000, "number of operations per rep");

using SWFHM = folly::SingleWriterFixedHashMap<int, int>;

void basic_test() {
  SWFHM m(32);

  ASSERT_EQ(m.used(), 0);
  ASSERT_FALSE(m.erase(0));
  ASSERT_EQ(m.size(), 0);
  ASSERT_EQ(m.find(0), m.end());
  ASSERT_EQ(m.available(), 32);

  ASSERT_TRUE(m.insert(1, 1));
  ASSERT_EQ(m.size(), 1);
  ASSERT_EQ(m.used(), 1);
  ASSERT_NE(m.find(1), m.end());
  ASSERT_TRUE(m.contains(1));
  ASSERT_EQ(m.available(), 31);

  ASSERT_TRUE(m.insert(2, 2));
  ASSERT_EQ(m.size(), 2);
  ASSERT_EQ(m.used(), 2);
  ASSERT_NE(m.find(2), m.end());
  ASSERT_EQ(m.available(), 30);

  ASSERT_TRUE(m.erase(1));
  ASSERT_TRUE(m.erase(2));
  ASSERT_EQ(m.size(), 0);
  ASSERT_EQ(m.used(), 2);
  ASSERT_EQ(m.available(), 30);
  ASSERT_EQ(m.find(1), m.end());
  ASSERT_EQ(m.find(2), m.end());
}

TEST(SingleWriterFixedHashMap, basic) {
  basic_test();
}

void iterator_test() {
  SWFHM m(32);
  for (int i = 0; i < 20; ++i) {
    m.insert(i, i);
  }
  for (int i = 0; i < 10; ++i) {
    m.erase(i);
  }
  int sum = 0;
  for (auto it = m.begin(); it != m.end(); ++it) {
    sum += it.value();
  }
  ASSERT_EQ(sum, 145);
}

TEST(SingleWriterFixedHashMap, iterator) {
  iterator_test();
}

void copy_test() {
  SWFHM m1(32);
  for (int i = 0; i < 20; ++i) {
    m1.insert(i, i);
  }
  SWFHM m2(64, m1);
  for (int i = 0; i < 10; ++i) {
    m2.erase(i);
  }
  int sum = 0;
  for (auto it = m2.begin(); it != m2.end(); ++it) {
    sum += it.value();
  }
  ASSERT_EQ(sum, 145);
}

TEST(SingleWriterFixedHashMap, copy) {
  copy_test();
}

void drf_test() {
  SWFHM m(32);
  int nthr = 5;
  folly::test::Barrier b1(nthr + 1);
  std::atomic<bool> stop{false};

  auto writer = std::thread([&] {
    b1.wait();
    for (int i = 0; i < 10000; ++i) {
      for (int j = 0; j < 10; ++j) {
        m.insert(j, j);
      }
      for (int j = 0; j < 10; ++j) {
        m.erase(j);
      }
    }
    stop.store(true);
  });

  std::vector<std::thread> readers(nthr - 1);
  for (int i = 0; i < nthr - 1; ++i) {
    readers[i] = std::thread([&] {
      b1.wait();
      while (!stop) {
        int sum = 0;
        for (auto it = m.begin(); it != m.end(); ++it) {
          sum += it.value();
        }
        ASSERT_LE(sum, 45);
      }
    });
  }

  b1.wait();
  writer.join();
  for (int i = 0; i < nthr - 1; ++i) {
    readers[i].join();
  }
}

TEST(SingleWriterFixedHashMap, drf) {
  drf_test();
}

void copy_tombstones_test() {
  SWFHM* m = new SWFHM(4);
  for (int i = 0; i < 100; ++i) {
    SWFHM* m2 = new SWFHM(4, *m);
    delete m;
    ASSERT_LT(m2->used(), m2->capacity());
    m2->insert(i, i);
    m2->erase(i);
    m = m2;
  }
  delete m;
}

TEST(SingleWriterFixedHashMap, copyTombstones) {
  copy_tombstones_test();
}

// Benchmarks

template <typename Func>
inline uint64_t run_once(int nthr, const Func& fn) {
  folly::test::Barrier b1(nthr + 1);

  std::vector<std::thread> thr(nthr);
  for (int tid = 0; tid < nthr; ++tid) {
    thr[tid] = std::thread([&, tid] {
      b1.wait();
      fn(tid);
    });
  }

  b1.wait();
  /* begin time measurement */
  auto const tbegin = std::chrono::steady_clock::now();
  /* wait for completion */
  for (int i = 0; i < nthr; ++i) {
    thr[i].join();
  }
  /* end time measurement */
  auto const tend = std::chrono::steady_clock::now();
  auto const dur = tend - tbegin;
  return std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();
}

template <typename RepFunc>
uint64_t runBench(int ops, const RepFunc& repFn) {
  uint64_t reps = FLAGS_reps;
  uint64_t min = UINTMAX_MAX;
  uint64_t max = 0;
  uint64_t sum = 0;
  std::vector<uint64_t> durs(reps);
  for (uint64_t r = 0; r < reps; ++r) {
    uint64_t dur = repFn();
    durs[r] = dur;
    sum += dur;
    min = std::min(min, dur);
    max = std::max(max, dur);
    // if each rep takes too long run at least 3 reps
    const uint64_t minute = 60000000000ULL;
    if (sum > minute && r >= 2) {
      reps = r + 1;
      break;
    }
  }
  const std::string ns_unit = " ns";
  uint64_t avg = sum / reps;
  uint64_t res = min;
  uint64_t varsum = 0;
  for (uint64_t r = 0; r < reps; ++r) {
    auto term = int64_t(reps * durs[r]) - int64_t(sum);
    varsum += term * term;
  }
  uint64_t dev = uint64_t(std::sqrt(varsum) * std::pow(reps, -1.5));
  std::cout << "   " << std::setw(4) << max / ops << ns_unit;
  std::cout << "   " << std::setw(4) << avg / ops << ns_unit;
  std::cout << "   " << std::setw(4) << dev / ops << ns_unit;
  std::cout << "   " << std::setw(4) << res / ops << ns_unit;
  std::cout << std::endl;
  return res;
}

uint64_t bench_find(const int nthr, const uint64_t ops) {
  auto repFn = [&] {
    SWFHM m(64);
    for (int i = 0; i < 10; ++i) {
      m.insert(i, i);
    }
    auto fn = [&](int tid) {
      for (uint64_t i = tid; i < 10 * ops; i += nthr) {
        auto it = m.find(5);
        if (it.value() != 5) {
          ASSERT_EQ(it.value(), 5);
        }
      }
    };
    return run_once(nthr, fn);
  };
  return runBench(ops, repFn);
}

uint64_t bench_iterate(const int nthr, const uint64_t ops) {
  auto repFn = [&] {
    SWFHM m(16);
    for (int i = 0; i < 10; ++i) {
      m.insert(i, i);
    }
    auto fn = [&](int tid) {
      for (uint64_t i = tid; i < ops; i += nthr) {
        int sum = 0;
        for (auto it = m.begin(); it != m.end(); ++it) {
          sum += it.value();
        }
        if (sum != 55) {
          ASSERT_EQ(sum, 45);
        }
      }
    };
    return run_once(nthr, fn);
  };
  return runBench(ops, repFn);
}

uint64_t bench_ctor_dtor(const int nthr, const uint64_t ops) {
  auto repFn = [&] {
    auto fn = [&](int tid) {
      for (uint64_t i = tid; i < ops; i += nthr) {
        SWFHM m(16);
        folly::doNotOptimizeAway(m);
      }
    };
    return run_once(nthr, fn);
  };
  return runBench(ops, repFn);
}

uint64_t bench_copy_empty_dtor(const int nthr, const uint64_t ops) {
  auto repFn = [&] {
    SWFHM m0(16);
    auto fn = [&](int tid) {
      for (uint64_t i = tid; i < ops; i += nthr) {
        SWFHM m(m0.capacity(), m0);
      }
    };
    return run_once(nthr, fn);
  };
  return runBench(ops, repFn);
}

uint64_t bench_copy_nonempty_dtor(const int nthr, const uint64_t ops) {
  auto repFn = [&] {
    SWFHM m0(16);
    m0.insert(10, 10);
    auto fn = [&](int tid) {
      for (uint64_t i = tid; i < ops; i += nthr) {
        SWFHM m(m0.capacity(), m0);
      }
    };
    return run_once(nthr, fn);
  };
  return runBench(ops, repFn);
}

uint64_t bench_copy_resize_dtor(const int nthr, const uint64_t ops) {
  auto repFn = [&] {
    SWFHM m0(16);
    m0.insert(10, 10);
    auto fn = [&](int tid) {
      for (uint64_t i = tid; i < ops; i += nthr) {
        SWFHM m(2 * m0.capacity(), m0);
      }
    };
    return run_once(nthr, fn);
  };
  return runBench(ops, repFn);
}

void dottedLine() {
  std::cout
      << "........................................................................"
      << std::endl;
}

constexpr auto nthr = folly::make_array<int>(1, 10);

TEST(SingleWriterFixedHashMapBench, Bench) {
  if (!FLAGS_bench) {
    return;
  }
  std::cout
      << "========================================================================"
      << std::endl;
  std::cout << std::setw(2) << FLAGS_reps << " reps of " << std::setw(8)
            << FLAGS_ops << " operations\n";
  dottedLine();
  std::cout << "$ numactl -N 1 $dir/single_writer_hash_map_test --bench\n";
  std::cout
      << "========================================================================"
      << std::endl;
  std::cout
      << "Test name                         Max time  Avg time  Dev time  Min time"
      << std::endl;
  for (int i : nthr) {
    std::cout << "============================== " << std::setw(2) << i
              << " threads "
              << "==============================" << std::endl;
    const uint64_t ops = FLAGS_ops;
    std::cout << "10x find                       ";
    bench_find(i, ops);
    std::cout << "iterate 10-element 32-slot map ";
    bench_iterate(i, ops);
    std::cout << "construct / destruct           ";
    bench_ctor_dtor(i, ops);
    std::cout << "copy empty / destruct          ";
    bench_copy_empty_dtor(i, ops);
    std::cout << "copy nonempty / destruct       ";
    bench_copy_nonempty_dtor(i, ops);
    std::cout << "copy resize / destruct         ";
    bench_copy_resize_dtor(i, ops);
  }
  std::cout
      << "========================================================================"
      << std::endl;
}

/*
========================================================================
10 reps of  1000000 operations
........................................................................
$ numactl -N 1 $dir/single_writer_hash_map_test --bench
========================================================================
Test name                         Max time  Avg time  Dev time  Min time
==============================  1 threads ==============================
10x find                            36 ns     35 ns      0 ns     34 ns
iterate 10-element 32-slot map      20 ns     19 ns      0 ns     19 ns
construct / destruct                 1 ns      1 ns      0 ns      1 ns
copy empty / destruct                5 ns      4 ns      0 ns      4 ns
copy nonempty / destruct            25 ns     24 ns      0 ns     23 ns
copy resize / destruct              40 ns     38 ns      0 ns     37 ns
============================== 10 threads ==============================
10x find                             6 ns      4 ns      1 ns      3 ns
iterate 10-element 32-slot map       3 ns      3 ns      0 ns      1 ns
construct / destruct                 0 ns      0 ns      0 ns      0 ns
copy empty / destruct                0 ns      0 ns      0 ns      0 ns
copy nonempty / destruct             4 ns      3 ns      0 ns      2 ns
copy resize / destruct               8 ns      6 ns      1 ns      4 ns
========================================================================
 */
