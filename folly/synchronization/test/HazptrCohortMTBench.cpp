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

#include <folly/synchronization/Hazptr.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/lang/Align.h>
#include <folly/portability/GFlags.h>
#include <folly/synchronization/test/Barrier.h>

using namespace folly;

namespace {

// A cohort pushes its tagged objects to the domain once it has this many.
constexpr size_t kObjsPerCohort = 20;

struct Obj : hazptr_obj_base<Obj> {
  size_t value = 0;
};

struct alignas(hardware_destructive_interference_size) PaddedCohort {
  hazptr_obj_cohort<> cohort;
};

void retire_tagged(hazptr_obj_cohort<>& cohort, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    auto obj = new Obj;
    obj->set_cohort_tag(&cohort);
    obj->retire();
  }
}

/// Tagged objects that stay in the domain for the duration of a benchmark,
/// because they are protected, spread across cohorts that are not destroyed
/// until the end.
class Background {
  std::vector<std::unique_ptr<hazptr_obj_cohort<>>> cohorts_;
  std::vector<hazptr_holder<>> holders_; // destroyed before cohorts_

 public:
  explicit Background(size_t count) {
    holders_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      if (i % kObjsPerCohort == 0) {
        cohorts_.push_back(std::make_unique<hazptr_obj_cohort<>>());
      }
      auto obj = new Obj;
      obj->set_cohort_tag(cohorts_.back().get());
      std::atomic<Obj*> channel{obj};
      holders_.push_back(make_hazard_pointer<>());
      std::ignore = holders_.back().protect(channel);
      obj->retire();
    }
  }
};

/// Runs iters calls of func(thread_index), spread across nthreads threads, and
/// measures from when all threads are released until all have finished.
template <typename Func>
void run_threads(
    BenchmarkSuspender& braces, size_t iters, size_t nthreads, Func func) {
  test::Barrier start(nthreads + 1);
  std::vector<std::thread> threads;
  threads.reserve(nthreads);
  for (size_t t = 0; t < nthreads; ++t) {
    threads.emplace_back([&, t] {
      start.wait();
      for (size_t i = t; i < iters; i += nthreads) {
        func(t);
      }
    });
  }
  braces.dismissing([&] {
    start.wait();
    for (auto& thread : threads) {
      thread.join();
    }
  });
}

} // namespace

/// Cohorts that are created, retire enough tagged objects to push them to the
/// domain, and are destroyed, concurrently in many threads. This is the
/// pattern of a RequestContext per request. Destruction must find and reclaim
/// the cohort's tagged objects in the domain, among the background objects.
static void cohort_lifecycle(
    size_t iters, size_t const nthreads, size_t const background) {
  BenchmarkSuspender braces;
  {
    Background bg(background);
    run_threads(braces, iters, nthreads, [](size_t) {
      hazptr_obj_cohort<> cohort;
      retire_tagged(cohort, kObjsPerCohort);
    });
  }
  hazptr_cleanup();
}

BENCHMARK_NAMED_PARAM(cohort_lifecycle, t1_bg0, 1, 0)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t4_bg0, 4, 0)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t16_bg0, 16, 0)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t64_bg0, 64, 0)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t1_bg1000, 1, 1000)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t4_bg1000, 4, 1000)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t16_bg1000, 16, 1000)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t64_bg1000, 64, 1000)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t1_bg10000, 1, 10000)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t4_bg10000, 4, 10000)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t16_bg10000, 16, 10000)
BENCHMARK_NAMED_PARAM(cohort_lifecycle, t64_bg10000, 64, 10000)

BENCHMARK_DRAW_LINE();

/// Retiring tagged objects to long-lived cohorts, one per thread, which push
/// them to the domain in batches and reclaim them once found to be safe.
static void cohort_retire(size_t iters, size_t const nthreads) {
  BenchmarkSuspender braces;
  {
    std::vector<PaddedCohort> cohorts(nthreads);
    run_threads(braces, iters, nthreads, [&](size_t t) {
      retire_tagged(cohorts[t].cohort, 1);
    });
  }
  hazptr_cleanup();
}

BENCHMARK_PARAM(cohort_retire, 1)
BENCHMARK_PARAM(cohort_retire, 4)
BENCHMARK_PARAM(cohort_retire, 16)
BENCHMARK_PARAM(cohort_retire, 64)

BENCHMARK_DRAW_LINE();

/// A reclamation pass over tagged objects in the domain, spread across the
/// given number of cohorts. The number of objects is kept below the domain's
/// reclamation threshold (1000), so that no pass runs during setup.
static void cohort_cleanup(size_t iters, size_t const ncohorts) {
  BenchmarkSuspender braces;
  while (iters--) {
    std::vector<hazptr_obj_cohort<>> cohorts(ncohorts);
    for (auto& cohort : cohorts) {
      retire_tagged(cohort, kObjsPerCohort);
    }
    braces.dismissing([] { hazptr_cleanup(); });
  }
}

BENCHMARK_PARAM(cohort_cleanup, 1)
BENCHMARK_PARAM(cohort_cleanup, 4)
BENCHMARK_PARAM(cohort_cleanup, 16)
BENCHMARK_PARAM(cohort_cleanup, 48)

int main(int argc, char* argv[]) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  runBenchmarks();
  return 0;
}
