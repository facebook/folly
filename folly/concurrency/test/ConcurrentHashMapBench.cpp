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

#include <folly/concurrency/ConcurrentHashMap.h>

#include <iomanip>
#include <iostream>
#include <thread>

#include <folly/synchronization/test/Barrier.h>

DEFINE_int32(reps, 10, "number of reps");
DEFINE_int32(ops, 1000 * 1000, "number of operations per rep");
DEFINE_int64(size, 10 * 1000 * 1000, "size");

template <typename Func, typename EndFunc>
inline uint64_t run_once(int nthr, const Func& fn, const EndFunc& endFn) {
  folly::test::Barrier b(nthr + 1);
  std::vector<std::thread> thr(nthr);
  for (int tid = 0; tid < nthr; ++tid) {
    thr[tid] = std::thread([&, tid] {
      b.wait();
      b.wait();
      fn(tid);
    });
  }
  b.wait();
  // begin time measurement
  auto tbegin = std::chrono::steady_clock::now();
  b.wait();
  /* wait for completion */
  for (int i = 0; i < nthr; ++i) {
    thr[i].join();
  }
  /* end time measurement */
  auto tend = std::chrono::steady_clock::now();
  endFn();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(tend - tbegin)
      .count();
}

template <typename RepFunc>
uint64_t runBench(const std::string& name, int ops, const RepFunc& repFn) {
  int reps = FLAGS_reps;
  uint64_t min = UINTMAX_MAX;
  uint64_t max = 0;
  uint64_t sum = 0;

  repFn(); // sometimes first run is outlier
  for (int r = 0; r < reps; ++r) {
    uint64_t dur = repFn();
    sum += dur;
    min = std::min(min, dur);
    max = std::max(max, dur);
    // if each rep takes too long run at least 3 reps
    const uint64_t minute = 60000000000UL;
    if (sum > minute && r >= 2) {
      reps = r + 1;
      break;
    }
  }

  const std::string unit = " ns";
  uint64_t avg = sum / reps;
  uint64_t res = min;
  std::cout << name;
  std::cout << "   " << std::setw(4) << (max + ops / 2) / ops << unit;
  std::cout << "   " << std::setw(4) << (avg + ops / 2) / ops << unit;
  std::cout << "   " << std::setw(4) << (min + ops / 2) / ops << unit;
  std::cout << std::endl;
  return res;
}

uint64_t bench_ctor_dtor(
    const int nthr, const int size, const std::string& name) {
  int ops = FLAGS_ops;
  auto repFn = [&] {
    auto fn = [&](int) {
      for (int i = 0; i < ops; ++i) {
        folly::ConcurrentHashMap<int, int> m;
        for (int j = 0; j < size; ++j) {
          m.insert(j, j);
        }
      }
    };
    auto endfn = [&] {};
    return run_once(nthr, fn, endfn);
  };
  return runBench(name, ops, repFn);
}

uint64_t bench_find(
    const int nthr, const bool sameItem, const std::string& name) {
  int ops = FLAGS_ops;
  folly::ConcurrentHashMap<int, int> m;
  for (int j = 0; j < FLAGS_size; ++j) {
    m.insert(j, j);
  }
  int key = FLAGS_size / 2;
  auto repFn = [&] {
    auto fn = [&](int) {
      if (sameItem) {
        for (int i = 0; i < ops; ++i) {
          m.find(key);
        }
      } else {
        for (int i = 0; i < ops; ++i) {
          m.find(i);
        }
      }
    };
    auto endfn = [&] {};
    return run_once(nthr, fn, endfn);
  };
  return runBench(name, ops, repFn);
}

uint64_t bench_iter(const int nthr, int size, const std::string& name) {
  int reps = size == 0 ? 1000000 : size < 1000000 ? 1000000 / size : 1;
  int ops = size == 0 ? reps : size * reps;
  folly::ConcurrentHashMap<int, int> m;
  for (int j = 0; j < size; ++j) {
    m.insert(j, j);
  }
  auto repFn = [&] {
    auto fn = [&](int) {
      for (int i = 0; i < reps; ++i) {
        for (auto it = m.begin(); it != m.end(); ++it) {
        }
      }
    };
    auto endfn = [&] {};
    return run_once(nthr, fn, endfn);
  };
  return runBench(name, ops, repFn);
}

uint64_t bench_begin(const int nthr, int size, const std::string& name) {
  int ops = FLAGS_ops;
  folly::ConcurrentHashMap<int, int> m;
  for (int j = 0; j < size; ++j) {
    m.insert(j, j);
  }
  auto repFn = [&] {
    auto fn = [&](int) {
      for (int i = 0; i < ops; ++i) {
        auto it = m.begin();
      }
    };
    auto endfn = [&] {};
    return run_once(nthr, fn, endfn);
  };
  return runBench(name, ops, repFn);
}

uint64_t bench_empty(const int nthr, int size, const std::string& name) {
  int ops = FLAGS_ops;
  folly::ConcurrentHashMap<int, int> m;
  for (int j = 0; j < size; ++j) {
    m.insert(j, j);
  }
  auto repFn = [&] {
    auto fn = [&](int) {
      for (int i = 0; i < ops; ++i) {
        m.empty();
      }
    };
    auto endfn = [&] {};
    return run_once(nthr, fn, endfn);
  };
  return runBench(name, ops, repFn);
}

uint64_t bench_size(const int nthr, int size, const std::string& name) {
  int ops = FLAGS_ops;
  folly::ConcurrentHashMap<int, int> m;
  for (int j = 0; j < size; ++j) {
    m.insert(j, j);
  }
  auto repFn = [&] {
    auto fn = [&](int) {
      for (int i = 0; i < ops; ++i) {
        m.size();
      }
    };
    auto endfn = [&] {};
    return run_once(nthr, fn, endfn);
  };
  return runBench(name, ops, repFn);
}

void dottedLine() {
  std::cout << ".............................................................."
            << std::endl;
}

void benches() {
  std::cout << "=============================================================="
            << std::endl;
  std::cout << "Test name                         Max time  Avg time  Min time"
            << std::endl;
  for (int nthr : {1, 10}) {
    std::cout << "========================= " << std::setw(2) << nthr
              << " threads" << " =========================" << std::endl;
    bench_ctor_dtor(nthr, 0, "CHM ctor/dtor -- empty          ");
    bench_ctor_dtor(nthr, 1, "CHM ctor/dtor -- 1 item         ");
    dottedLine();
    bench_find(nthr, false, "CHM find() -- 10M items         ");
    bench_find(nthr, true, "CHM find() -- 1 of 10M items    ");
    dottedLine();
    bench_begin(nthr, 0, "CHM begin() -- empty            ");
    bench_begin(nthr, 1, "CHM begin() -- 1 item           ");
    bench_begin(nthr, 10000, "CHM begin() -- 10K items        ");
    bench_begin(nthr, 10000000, "CHM begin() -- 10M items        ");
    dottedLine();
    bench_iter(nthr, 0, "CHM iterate -- empty            ");
    bench_iter(nthr, 1, "CHM iterate -- 1 item           ");
    bench_iter(nthr, 10, "CHM iterate -- 10 items         ");
    bench_iter(nthr, 100, "CHM iterate -- 100 items        ");
    bench_iter(nthr, 1000, "CHM iterate -- 1K items         ");
    bench_iter(nthr, 10000, "CHM iterate -- 10K items        ");
    bench_iter(nthr, 100000, "CHM iterate -- 100K items       ");
    bench_iter(nthr, 1000000, "CHM iterate -- 1M items         ");
    bench_iter(nthr, 10000000, "CHM iterate -- 10M items        ");
    dottedLine();
    bench_empty(nthr, 0, "CHM empty() -- empty            ");
    bench_empty(nthr, 1, "CHM empty() -- 1 item           ");
    bench_empty(nthr, 10000, "CHM empty() -- 10K items        ");
    bench_empty(nthr, 10000000, "CHM empty() -- 10M items        ");
    dottedLine();
    bench_size(nthr, 0, "CHM size() -- empty             ");
    bench_size(nthr, 1, "CHM size() -- 1 item            ");
    bench_size(nthr, 10, "CHM size() -- 10 items          ");
    bench_size(nthr, 100, "CHM size() -- 100 items         ");
    bench_size(nthr, 1000, "CHM size() -- 1K items          ");
    bench_size(nthr, 10000, "CHM size() -- 10K items         ");
    bench_size(nthr, 100000, "CHM size() -- 100K items        ");
    bench_size(nthr, 1000000, "CHM size() -- 1M items          ");
    bench_size(nthr, 10000000, "CHM size() -- 10M items         ");
  }
  std::cout << "=============================================================="
            << std::endl;
}

int main() {
  benches();
}
