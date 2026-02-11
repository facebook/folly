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

#include <stdexcept>

#include <folly/Benchmark.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/lang/Keep.h>
#include <folly/result/test/common.h>

#if FOLLY_HAS_RESULT

#include <folly/result/coro.h>

namespace folly::test {

TEST(ResultCoroBench, benchmarksDoNotCrash) {
  runBenchmarksAsTest();
}

// Simple result coroutine that adds 1 to the input
result<int> result_coro(result<int>&& r) {
  co_return co_await or_unwind(std::move(r)) + 1;
}

// Non-coroutine equivalent using value_or_throw()
result<int> catching_result_func(result<int>&& r) {
  return result_catch_all([&]() -> result<int> {
    if (r.has_value()) {
      return r.value_or_throw() + 1;
    }
    return std::move(r).error_or_stopped();
  });
}

// Not QUITE equivalent to the coro -- lacks the exception boundary
result<int> non_catching_result_func(result<int>&& r) {
  if (r.has_value()) {
    return r.value_or_throw() + 1;
  }
  return std::move(r).error_or_stopped();
}

// The wrappers are for ease of `objdump --disassemble=FN`.

extern "C" FOLLY_KEEP FOLLY_NOINLINE void result_coro_wrapper(result<int> r) {
  folly::compiler_must_not_elide(result_coro(std::move(r)));
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE void non_catching_result_func_wrapper(
    result<int> r) {
  folly::compiler_must_not_elide(non_catching_result_func(std::move(r)));
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE void catching_result_func_wrapper(
    result<int> r) {
  folly::compiler_must_not_elide(catching_result_func(std::move(r)));
}

// -- Per-call: success --

BENCHMARK(non_catching_result_func_success, iters) {
  BenchmarkSuspender suspender;
  result<int> input{42};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(non_catching_result_func(input.copy()));
    }
  });
}

BENCHMARK_RELATIVE(catching_result_func_success, iters) {
  BenchmarkSuspender suspender;
  result<int> input{42};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(catching_result_func(input.copy()));
    }
  });
}

BENCHMARK_RELATIVE(result_coro_success, iters) {
  BenchmarkSuspender suspender;
  result<int> input{42};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(result_coro(input.copy()));
    }
  });
}

BENCHMARK_DRAW_LINE();

// -- Per-call: error --

BENCHMARK(non_catching_result_func_error, iters) {
  BenchmarkSuspender suspender;
  result<int> input{error_or_stopped{std::runtime_error{"test error"}}};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(non_catching_result_func(input.copy()));
    }
  });
}

BENCHMARK_RELATIVE(catching_result_func_error, iters) {
  BenchmarkSuspender suspender;
  result<int> input{error_or_stopped{std::runtime_error{"test error"}}};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(catching_result_func(input.copy()));
    }
  });
}

BENCHMARK_RELATIVE(result_coro_error, iters) {
  BenchmarkSuspender suspender;
  result<int> input{error_or_stopped{std::runtime_error{"test error"}}};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(result_coro(input.copy()));
    }
  });
}

BENCHMARK_DRAW_LINE();

// -- Inner-loop: or_unwind vs if-check on success path --
//
// Loops inside a single coro frame. dismiss/SCOPE_EXIT isolates
// just the per-unwrap cost, excluding coro frame alloc/dealloc.
//
// Uses BenchResult<42> (from common.h): an array with errors at even
// indices and values at odd indices. Stride by 2 starting at
// withValue, so has_value() is always true at runtime, but the
// compiler can't easily prove it.
//
// As of 2025, or_unwind shows ~2x overhead vs if-check. This is due to
// LLVM's coro lowering: variables live across `co_await` are stored in
// the heap-allocated coro frame, and the compiler reloads/stores them
// every iteration even when `await_ready()` returns true and no
// suspension occurs. See https://github.com/llvm/llvm-project/pull/152623

// Wrappers for `objdump --disassemble=FN`.
// For or_unwind, the hot loop lives in the `.resume` clone.

extern "C" FOLLY_KEEP FOLLY_NOINLINE void or_unwind_inner_loop_wrapper(
    result<int>* inputs, size_t iters) {
  folly::compiler_must_not_elide([&]() -> result<void> {
    size_t idx = BenchResult<42>::withValue;
    while (iters--) {
      folly::compiler_must_not_elide(co_await or_unwind(inputs[idx]));
      idx = BenchResult<42>::advance(idx);
    }
  }());
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE void if_check_inner_loop(
    result<int>* inputs, size_t iters) {
  size_t idx = BenchResult<42>::withValue;
  while (iters--) {
    if (!inputs[idx].has_value()) {
      return;
    }
    folly::compiler_must_not_elide(inputs[idx].value_or_throw());
    idx = BenchResult<42>::advance(idx);
  }
}

BENCHMARK(if_check_inner_success, iters) {
  BenchmarkSuspender suspender;
  BenchResult<42> inputs{BenchResult<42>::withValue};
  folly::compiler_must_not_elide([&]() -> result<void> {
    suspender.dismiss();
    size_t idx = BenchResult<42>::withValue;
    while (iters--) {
      if (!inputs.a[idx].has_value()) {
        return folly::copy(inputs.a[idx].error_or_stopped());
      }
      folly::compiler_must_not_elide(inputs.a[idx].value_or_throw());
      idx = BenchResult<42>::advance(idx);
    }
    return {};
  }());
  SCOPE_EXIT {
    suspender.rehire();
  };
}

BENCHMARK_RELATIVE(or_unwind_inner_success, iters) {
  BenchmarkSuspender suspender;
  BenchResult<42> inputs{BenchResult<42>::withValue};
  folly::compiler_must_not_elide([&]() -> result<void> {
    suspender.dismiss();
    size_t idx = BenchResult<42>::withValue;
    while (iters--) {
      folly::compiler_must_not_elide(co_await or_unwind(inputs.a[idx]));
      idx = BenchResult<42>::advance(idx);
    }
  }());
  SCOPE_EXIT {
    suspender.rehire();
  };
}

} // namespace folly::test

int main(int argc, char** argv) {
  return folly::test::benchmarkMain(argc, argv);
}

#else // FOLLY_HAS_RESULT

int main() {
  return -1;
}

#endif // FOLLY_HAS_RESULT
