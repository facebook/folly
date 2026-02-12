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

// Benchmarks for `result<T>` and `error_or_stopped` hot-path operations.

#include <stdexcept>

#include <folly/Benchmark.h>
#include <folly/ExceptionWrapper.h>
#include <folly/Portability.h>
#include <folly/result/result.h>
#include <folly/result/test/common.h>
#include <folly/result/try.h>

#if FOLLY_HAS_RESULT

namespace folly::test {

TEST(ResultBench, benchmarksDoNotCrash) {
  runBenchmarksAsTest();
}

// has_value:

BENCHMARK(has_value_true, iters) {
  BenchmarkSuspender suspender;
  BenchResult<42> inputs{BenchResult<42>::withValue};
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(inputs.next().has_value());
    }
  });
}

BENCHMARK(has_value_false, iters) {
  BenchmarkSuspender suspender;
  BenchResult<42> inputs{BenchResult<42>::withError};
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(inputs.next().has_value());
    }
  });
}

BENCHMARK_DRAW_LINE(); // has_stopped:

BENCHMARK(has_stopped_on_error, iters) {
  BenchmarkSuspender suspender;
  BenchResult<42> inputs{BenchResult<42>::withError};
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(inputs.next().has_stopped());
    }
  });
}

BENCHMARK(has_stopped_on_stopped, iters) {
  result<int> r{stopped_result};
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(r.has_stopped());
    }
  });
}

BENCHMARK_DRAW_LINE(); // value_or_throw:

BENCHMARK(value_or_throw_int, iters) {
  BenchmarkSuspender suspender;
  BenchResult<42> inputs{BenchResult<42>::withValue};
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(inputs.next().value_or_throw());
    }
  });
}

BENCHMARK_DRAW_LINE(); // error_or_stopped access:

BENCHMARK(error_or_stopped_const_ref, iters) {
  BenchmarkSuspender suspender;
  BenchResult<42> inputs{BenchResult<42>::withError};
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(inputs.next().error_or_stopped());
    }
  });
}

// Includes result copy (~7ns exception_ptr atomic) + move-access.
BENCHMARK(error_or_stopped_move, iters) {
  BenchmarkSuspender suspender;
  BenchResult<42> inputs{BenchResult<42>::withError};
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(copy(inputs.next()).error_or_stopped());
    }
  });
}

BENCHMARK_DRAW_LINE(); // Construct value:

BENCHMARK(construct_value_int, iters) {
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(result<int>{42});
    }
  });
}

BENCHMARK(construct_value_void, iters) {
  BenchmarkSuspender suspender;
  BenchResult<> inputs{BenchResult<>::withValue};
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(inputs.next().copy());
    }
  });
}

BENCHMARK_DRAW_LINE(); // Construct error or stopped:

BENCHMARK(construct_error, iters) {
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(
          result<int>{error_or_stopped{std::runtime_error{"e"}}});
    }
  });
}

BENCHMARK(construct_stopped, iters) {
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(result<int>{stopped_result});
    }
  });
}

BENCHMARK(construct_from_current_exception, iters) {
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      try {
        throw std::runtime_error("e");
      } catch (...) {
        compiler_must_not_elide(
            result<int>{error_or_stopped::from_current_exception()});
      }
    }
  });
}

BENCHMARK_DRAW_LINE(); // error_or_stopped standalone:

BENCHMARK(eos_copy, iters) {
  error_or_stopped eos{std::runtime_error{"e"}};
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
      auto copy = eos;
      compiler_must_not_elide(copy);
    }
  });
}

BENCHMARK(eos_has_stopped_false, iters) {
  error_or_stopped eos{std::runtime_error{"e"}};
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(eos.has_stopped());
    }
  });
}

BENCHMARK(eos_has_stopped_true, iters) {
  error_or_stopped eos{stopped_result};
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(eos.has_stopped());
    }
  });
}

BENCHMARK_DRAW_LINE(); // result_to_try:

BENCHMARK(result_to_try_value, iters) {
  result<int> r{42};
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(result_to_try(r));
    }
  });
}

BENCHMARK(result_to_try_error, iters) {
  result<int> r{error_or_stopped{std::runtime_error{"e"}}};
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(result_to_try(r));
    }
  });
}

BENCHMARK_DRAW_LINE(); // try_to_result:

BENCHMARK(try_to_result_value, iters) {
  BenchmarkSuspender suspender;
  BenchResult<42> inputs{BenchResult<42>::withValue};
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(
          try_to_result(Try<int>{inputs.next().value_or_throw()}));
    }
  });
}

BENCHMARK(try_to_result_error, iters) {
  auto ew = make_exception_wrapper<std::runtime_error>("e");
  BenchmarkSuspender suspender;
  suspender.dismissing([&] {
    while (iters--) {
      compiler_must_not_elide(try_to_result(Try<int>{ew}));
    }
  });
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
