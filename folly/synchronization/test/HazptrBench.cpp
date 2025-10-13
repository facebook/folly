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
#include <random>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/concurrency/AtomicSharedPtr.h>
#include <folly/portability/GFlags.h>

using namespace folly;

namespace {

/// simple object for testing hazptr operations
struct TestObj : public hazptr_obj_base<TestObj> {
  size_t value{0};
  explicit TestObj(size_t v) noexcept : value(v) {}
};

} // namespace

/// benchmark copying a std::shared_ptr, including copy and dtor
BENCHMARK(shared_ptr_copy, iters) {
  BenchmarkSuspender braces;
  auto obj = copy_to_shared_ptr(0);

  int sum = 0;
  braces.dismissing([&] {
    while (iters--) {
      auto copy = obj;
      folly::compiler_must_not_predict(copy);
      sum += *copy;
    }
  });
  folly::compiler_must_not_elide(sum);
}

/// benchmark copying a std::shared_ptr, including copy and dtor, under a shared
/// lock
BENCHMARK(folly_shared_mutex_shared_ptr_copy, iters) {
  BenchmarkSuspender braces;
  folly::Synchronized obj{copy_to_shared_ptr(0)};

  int sum = 0;
  braces.dismissing([&] {
    while (iters--) {
      auto copy = obj.copy();
      folly::compiler_must_not_predict(copy);
      sum += *copy;
    }
  });
  folly::compiler_must_not_elide(sum);
}

/// benchmark copying a std::shared_ptr, including copy and dtor, from a
/// folly::atomic_shared_ptr
BENCHMARK(folly_atomic_shared_ptr_copy, iters) {
  BenchmarkSuspender braces;
  folly::atomic_shared_ptr obj{copy_to_shared_ptr(0)};

  int sum = 0;
  braces.dismissing([&] {
    while (iters--) {
      auto copy = obj.load(std::memory_order_relaxed);
      folly::compiler_must_not_predict(copy);
      sum += *copy;
    }
  });
  folly::compiler_must_not_elide(sum);
}

/// benchmark copying a std::shared_ptr, including copy and dtor, using
/// std::atomic_load (precursor to std::atomic_shared_ptr)
BENCHMARK(std_atomic_shared_ptr_copy, iters) {
  BenchmarkSuspender braces;
  auto obj = copy_to_shared_ptr(0);

  int sum = 0;
  braces.dismissing([&] {
    while (iters--) {
      auto copy = std::atomic_load_explicit(&obj, std::memory_order_relaxed);
      folly::compiler_must_not_predict(copy);
      sum += *copy;
    }
  });
  folly::compiler_must_not_elide(sum);
}

BENCHMARK_DRAW_LINE();

/// benchmark hazptr-protecting a pointer, including protection and unprotection
template <template <typename> class Atom>
static void do_hazptr_protect(
    BenchmarkSuspender& braces, hazptr_domain<Atom>& domain, size_t iters) {
  auto own = std::make_unique<TestObj>(42);
  Atom<TestObj*> ptr{own.get()};

  int sum = 0;
  braces.dismissing([&] {
    auto h = make_hazard_pointer(domain);
    while (iters--) {
      auto* obj = h.protect(ptr);
      folly::compiler_must_not_predict(obj);
      sum += obj->value;
    }
  });
  folly::compiler_must_not_elide(sum);
}

BENCHMARK(hazptr_protect, iters) {
  BenchmarkSuspender braces;
  auto&& domain = hazptr_domain{};
  do_hazptr_protect(braces, domain, iters);
}

BENCHMARK(hazptr_protect_default, iters) {
  BenchmarkSuspender braces;
  auto&& domain = default_hazptr_domain();
  do_hazptr_protect(braces, domain, iters);
}

BENCHMARK_DRAW_LINE();

/// benchmark creating a hazard pointer (aka a hazptr-holder) without using it,
/// including ctor and dtor
template <template <typename> class Atom>
static void do_hazptr_make(
    BenchmarkSuspender& braces, hazptr_domain<Atom>& domain, size_t iters) {
  braces.dismissing([&] {
    while (iters--) {
      make_hazard_pointer(domain);
    }
  });
}

BENCHMARK(hazptr_make, iters) {
  BenchmarkSuspender braces;
  auto&& domain = hazptr_domain();
  do_hazptr_make(braces, domain, iters);
}

BENCHMARK(hazptr_make_default, iters) {
  BenchmarkSuspender braces;
  auto&& domain = default_hazptr_domain();
  do_hazptr_make(braces, domain, iters);
}

BENCHMARK_DRAW_LINE();

/// benchmark creating a hazard pointer array (aka a hazptr-array) without using
/// it, including ctor and dtor
template <size_t ArraySize>
void hazptr_make_array_default(size_t iters, folly::index_constant<ArraySize>) {
  // make_hazard_pointer_array assumes the default domain
  while (iters--) {
    make_hazard_pointer_array<ArraySize>();
  }
}

BENCHMARK_PARAM(hazptr_make_array_default, 1_uzic)
BENCHMARK_PARAM(hazptr_make_array_default, 2_uzic)
BENCHMARK_PARAM(hazptr_make_array_default, 4_uzic)
BENCHMARK_PARAM(hazptr_make_array_default, 8_uzic)

BENCHMARK_DRAW_LINE();

/// benchmark creating a hazard pointer and using it to protect a pointer,
/// including hazard pointer ctor/dtor and protection/unprotection
template <template <typename> class Atom>
static void do_hazptr_make_protect(
    BenchmarkSuspender& braces, hazptr_domain<Atom>& domain, size_t iters) {
  auto own = std::make_unique<TestObj>(42);
  std::atomic<TestObj*> ptr{own.get()};

  int sum = 0;
  braces.dismissing([&] {
    while (iters--) {
      auto h = make_hazard_pointer(domain);
      auto* obj = h.protect(ptr);
      folly::compiler_must_not_predict(obj);
      sum += obj->value;
    }
  });
  folly::compiler_must_not_elide(sum);
}

BENCHMARK(hazptr_make_protect, iters) {
  BenchmarkSuspender braces;
  auto&& domain = hazptr_domain();
  do_hazptr_make_protect(braces, domain, iters);
}

BENCHMARK(hazptr_make_protect_default, iters) {
  BenchmarkSuspender braces;
  auto&& domain = default_hazptr_domain();
  do_hazptr_make_protect(braces, domain, iters);
}

BENCHMARK_DRAW_LINE();

/// benchmark retiring an object to a hazptr domain
template <template <typename> class Atom>
static void do_hazptr_retire(
    BenchmarkSuspender& braces, hazptr_domain<Atom>& domain, size_t iters) {
  std::vector<std::unique_ptr<TestObj>> objs;
  objs.reserve(iters);
  for (size_t i = 0; i < iters; ++i) {
    objs.push_back(std::make_unique<TestObj>(42));
  }

  braces.dismissing([&] {
    while (iters--) {
      auto* obj = objs[iters].release();
      obj->retire(domain);
    }
  });
}

BENCHMARK(hazptr_retire, iters) {
  BenchmarkSuspender braces;
  auto&& domain = hazptr_domain();
  do_hazptr_retire(braces, domain, iters);
}

BENCHMARK(hazptr_retire_default, iters) {
  BenchmarkSuspender braces;
  auto&& domain = default_hazptr_domain();
  do_hazptr_retire(braces, domain, iters);
}

int main(int argc, char* argv[]) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  runBenchmarks();
  return 0;
}
