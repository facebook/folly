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
#include <folly/lang/Keep.h>

#if FOLLY_HAS_RESULT

#include <folly/result/result.h>

namespace folly {

// Simple result coroutine that adds 1 to the input
result<int> result_coro(result<int>&& r) {
  co_return co_await std::move(r) + 1;
}

// Non-coroutine equivalent using value_or_throw()
result<int> catching_result_func(result<int>&& r) {
  return result_catch_all([&]() -> result<int> {
    if (r.has_value()) {
      return r.value_or_throw() + 1;
    }
    return std::move(r).non_value();
  });
}

// Not QUITE equivalent to the coro -- lacks the exception boundary
result<int> non_catching_result_func(result<int>&& r) {
  if (r.has_value()) {
    return r.value_or_throw() + 1;
  }
  return std::move(r).non_value();
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

BENCHMARK(result_coro_success, iters) {
  BenchmarkSuspender suspender;
  result<int> input{42};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(result_coro(input.copy()));
    }
  });
}

BENCHMARK(non_catching_result_func_success, iters) {
  BenchmarkSuspender suspender;
  result<int> input{42};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(non_catching_result_func(input.copy()));
    }
  });
}

BENCHMARK(catching_result_func_success, iters) {
  BenchmarkSuspender suspender;
  result<int> input{42};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(catching_result_func(input.copy()));
    }
  });
}

BENCHMARK(result_coro_error, iters) {
  BenchmarkSuspender suspender;
  result<int> input{non_value_result{std::runtime_error{"test error"}}};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(result_coro(input.copy()));
    }
  });
}

BENCHMARK(non_catching_result_func_error, iters) {
  BenchmarkSuspender suspender;
  result<int> input{non_value_result{std::runtime_error{"test error"}}};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(non_catching_result_func(input.copy()));
    }
  });
}

BENCHMARK(catching_result_func_error, iters) {
  BenchmarkSuspender suspender;
  result<int> input{non_value_result{std::runtime_error{"test error"}}};
  suspender.dismissing([&] {
    while (iters--) {
      folly::compiler_must_not_elide(catching_result_func(input.copy()));
    }
  });
}

} // namespace folly

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}

#endif // FOLLY_HAS_RESULT
