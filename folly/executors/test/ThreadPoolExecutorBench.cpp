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

#include <folly/executors/ThreadPoolExecutor.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <random>
#include <shared_mutex>
#include <utility>
#include <vector>

#include <glog/logging.h>

#include <folly/Benchmark.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/portability/GFlags.h>

using namespace folly;

// Sample results, 72-core aarch64, @mode/opt, 64 workers:
//
//   reference_applyPatternOnly                        44.42ns
//   getPoolStats_allIdle                    36.267%  122.47ns
//   getPoolStats_allActive                  36.255%  122.51ns
//   getPoolStats_mostlyIdleUnpredictable    36.391%  122.05ns
//   getPoolStats_mostlyActiveUnpredictable  36.308%  122.33ns
//   getPoolStats_evenlyUnpredictable        36.103%  123.02ns
//   getPoolStatsLight_evenlyUnpredictable   57.816%   76.82ns
//
// reference_applyPatternOnly runs the same setup and the same per-iteration
// flag writes as every case below but never calls the stats accessor, so it is
// the harness cost they all carry. Subtract it to compare the calls themselves:
// net of the reference getPoolStatsLight is 2.4x cheaper, not the 1.6x the raw
// figures suggest.
//
// Where those 44ns go, measured with throwaway probes on a Neoverse V2 at
// 3.4GHz. Writing the same 64 values into one cacheline of local atomics,
// indexed directly, costs 20.0ns. Reaching that same cacheline through a vector
// of pointers, as the loop below does, costs 25.0ns. The real flags cost
// 44.0ns. So the reference is about 20ns of stores, 5ns of pointer indirection,
// and 19ns for those stores landing on 64 distinct lines - each worker is a
// separately heap-allocated Thread. Giving the loop a compile-time trip count
// so that it fully unrolls changed nothing, so it is not front-end bound. None
// of the three shrinks without writing fewer flags or running fewer workers,
// and either would change what is being measured.
//
// The reference and the stats call are close to additive - probes that set the
// flags once at setup and skipped the pattern application put the overlap in
// the out-of-order window at about 5ns - so a net figure understates the call
// by roughly that much.
//
// The getPoolStats cases are flat: how predictable the per-worker idle flag is
// no longer affects cost. getPoolStatsLight is cheaper again, mostly because
// reporting no maxIdleTime lets it skip the steady_clock::now() vDSO read.

namespace {

constexpr size_t kNumThreads = 64;

// getPoolStats() walks every worker and consults its `idle` flag, so its cost
// depends on how predictable that flag is. Driving the flags with real tasks
// would make the measurement depend on scheduler timing and on cross-core
// traffic for the worker cachelines, both of which swamp the effect under
// study, so set the flags directly instead. No task is ever submitted, so the
// workers sit in the task queue and runTask() never overwrites them.
class IdleFlagExecutor : public CPUThreadPoolExecutor {
 public:
  // Passing {max, min} keeps the pool a fixed size. The single-argument
  // constructor honours FLAGS_dynamic_cputhreadpoolexecutor, which pins
  // minThreads at 1 and lets the pool shrink to a fraction of numThreads.
  explicit IdleFlagExecutor(size_t numThreads)
      : CPUThreadPoolExecutor(std::make_pair(numThreads, numThreads)) {}

  // Captured once, then written directly in the timed loop. Valid for the life
  // of the benchmark: no task is ever submitted and the pool is fixed size, so
  // threadList_ never changes and the lock is only needed here.
  std::vector<std::atomic<bool>*> idleFlags() const {
    std::shared_lock g{threadListLock_};
    std::vector<std::atomic<bool>*> out;
    out.reserve(threadList_.get().size());
    for (const auto& thread : threadList_.get()) {
      out.push_back(&thread->idle);
    }
    return out;
  }

  size_t numWorkers() const {
    std::shared_lock g{threadListLock_};
    return threadList_.get().size();
  }
};

// Patterns cycle rather than repeat, so the cycle must be long enough that the
// predictor cannot memorize it: at 64 workers per pattern this is ~500k branch
// outcomes. A 256-entry cycle was measurably learnable - it collapsed
// mostlyIdleUnpredictable to within 6% of allIdle. Power of two so the index is
// a mask; packed one bit per worker so the table stays L2-resident.
constexpr size_t kPatterns = 8192;

// Every case re-applies a pattern each iteration, even the constant ones, so
// that worker-cacheline state is identical across cases and the only variable
// left is how predictable the branch is.
//
// Everything the application consults is precomputed: the flag addresses (so no
// lock and no shared_ptr chase per iteration) and the pattern words (so no PRNG
// draw). What is left inside the timed region is the per-worker stores, which
// are irreducible if every flag must change - see the note above on where the
// reference's 44ns goes - so the reported figure is those stores plus the stats
// call. That addend is the same for every case.

// Whether the timed loop calls getPoolStats at all. Call::none is the
// reference case: same setup and same per-iteration flag writes, no stats call,
// so it measures exactly the part of every other case that is not under test.
enum class Call { none, full, light };

template <Call kCall, typename NextPattern>
void benchPoolStats(unsigned iters, NextPattern next) {
  // Declared before the executor so that the executor is destroyed first, while
  // this is still suspended: pool construction and teardown are both excluded.
  BenchmarkSuspender susp;
  IdleFlagExecutor executor(kNumThreads);
  // A pool that quietly came up short would make every case look alike.
  CHECK_EQ(executor.numWorkers(), kNumThreads);
  const auto flags = executor.idleFlags();

  std::mt19937_64 rng(0x5eed);
  std::vector<uint64_t> patterns(kPatterns);
  for (auto& pattern : patterns) {
    pattern = next(rng);
  }

  size_t k = 0;
  susp.dismissing([&] {
    while (iters--) {
      const uint64_t pattern = patterns[k++ & (kPatterns - 1)];
      for (size_t i = 0; i < flags.size(); ++i) {
        flags[i]->store((pattern >> i) & 1, std::memory_order_relaxed);
      }
      if constexpr (kCall == Call::light) {
        auto stats = executor.getPoolStatsLight();
        compiler_must_not_elide(stats);
      } else if constexpr (kCall == Call::full) {
        auto stats = executor.getPoolStats();
        compiler_must_not_elide(stats);
      }
    }
  });
}

} // namespace

// Reference: the harness cost carried by every case below.
BENCHMARK(reference_applyPatternOnly, iters) {
  benchPoolStats<Call::none>(iters, [](std::mt19937_64& rng) { return rng(); });
}

BENCHMARK_DRAW_LINE();

BENCHMARK_RELATIVE(getPoolStats_allIdle, iters) {
  benchPoolStats<Call::full>(iters, [](std::mt19937_64&) {
    return ~uint64_t(0);
  });
}

BENCHMARK_RELATIVE(getPoolStats_allActive, iters) {
  benchPoolStats<Call::full>(iters, [](std::mt19937_64&) {
    return uint64_t(0);
  });
}

// ~25% active, near a ratio observed on a production service.
BENCHMARK_RELATIVE(getPoolStats_mostlyIdleUnpredictable, iters) {
  benchPoolStats<Call::full>(iters, [](std::mt19937_64& rng) {
    auto a = rng();
    return a | rng();
  });
}

// ~75% active: the mirror of the case above.
BENCHMARK_RELATIVE(getPoolStats_mostlyActiveUnpredictable, iters) {
  benchPoolStats<Call::full>(iters, [](std::mt19937_64& rng) {
    auto a = rng();
    return a & rng();
  });
}

BENCHMARK_RELATIVE(getPoolStats_evenlyUnpredictable, iters) {
  benchPoolStats<Call::full>(iters, [](std::mt19937_64& rng) { return rng(); });
}

// getPoolStatsLight() reports no maxIdleTime, so it neither reads the clock
// nor consults lastActiveTime. Only the hardest pattern is measured; its cost
// is flat across patterns just as the full variant's now is.
BENCHMARK_RELATIVE(getPoolStatsLight_evenlyUnpredictable, iters) {
  benchPoolStats<Call::light>(iters, [](std::mt19937_64& rng) {
    return rng();
  });
}

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
