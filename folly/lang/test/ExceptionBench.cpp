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

#include <folly/lang/Exception.h>

#include <stdexcept>

#include <folly/Benchmark.h>
#include <folly/lang/Keep.h>

namespace {

template <int I>
struct Virt {
  virtual ~Virt() {}
  int value = I;
  operator int() const { return value; }
};

using A0 = Virt<0>;
using A1 = Virt<1>;
using A2 = Virt<2>;
struct B0 : virtual A1, virtual A2 {};
struct B1 : virtual A2, virtual A0 {};
struct B2 : virtual A0, virtual A1 {};
struct C : B0, B1, B2 {};
struct D : B0, B1, B2 {};

} // namespace

extern "C" FOLLY_KEEP std::exception*
check_folly_exception_ptr_try_get_object_exact_fast(
    std::exception_ptr const& ptr) {
  return folly::exception_ptr_try_get_object_exact_fast<std::exception>(
      ptr, folly::tag<std::logic_error, std::range_error, std::bad_cast>);
}

extern "C" FOLLY_KEEP std::exception* //
check_folly_exception_ptr_get_object_hint( //
    std::exception_ptr const& ptr) {
  return folly::exception_ptr_get_object_hint<std::exception>(
      ptr, folly::tag<std::logic_error, std::range_error, std::bad_cast>);
}

extern "C" FOLLY_KEEP A0* //
check_folly_exception_ptr_try_get_object_exact_fast_vmi(
    std::exception_ptr const& ptr) {
  return folly::exception_ptr_try_get_object_exact_fast<A0>(
      ptr, folly::tag<B1, C, B2>);
}

extern "C" FOLLY_KEEP A0* //
check_folly_exception_ptr_get_object_hint_vmi( //
    std::exception_ptr const& ptr) {
  return folly::exception_ptr_get_object_hint<A0>(ptr, folly::tag<B1, C, B2>);
}

BENCHMARK(get_object_fail, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(std::range_error("foo"));
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_get_object<std::bad_cast>(ptr);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_pass_0, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(std::range_error("foo"));
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_get_object<std::range_error>(ptr);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_pass_1, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(std::range_error("foo"));
  braces.dismissing([&] {
    while (iters--) {
      auto const match =
          folly::exception_ptr_get_object<std::runtime_error>(ptr);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_pass_2, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(std::range_error("foo"));
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_get_object<std::exception>(ptr);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_vmi_fail, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(C());
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_get_object<D>(ptr);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_vmi_pass_0, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(C());
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_get_object<C>(ptr);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_vmi_pass_1, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(C());
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_get_object<B0>(ptr);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_vmi_pass_2, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(C());
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_get_object<A0>(ptr);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_hint_fail, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(std::range_error("foo"));
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_get_object_hint<std::exception>(
          ptr, folly::tag<std::logic_error, std::bad_cast>);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_hint_pass, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(std::range_error("foo"));
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_get_object_hint<std::exception>(
          ptr, folly::tag<std::logic_error, std::range_error, std::bad_cast>);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_hint_vmi_fail, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(C());
  braces.dismissing([&] {
    while (iters--) {
      auto const match =
          folly::exception_ptr_get_object_hint<A0>(ptr, folly::tag<B1, B2>);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(get_object_hint_vmi_pass, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(C());
  braces.dismissing([&] {
    while (iters--) {
      auto const match =
          folly::exception_ptr_get_object_hint<A0>(ptr, folly::tag<B1, C, B2>);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(try_get_object_exact_fast_fail, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(std::range_error("foo"));
  braces.dismissing([&] {
    while (iters--) {
      auto const match =
          folly::exception_ptr_try_get_object_exact_fast<std::exception>(
              ptr, folly::tag<std::logic_error, std::bad_cast>);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(try_get_object_exact_fast_pass, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(std::range_error("foo"));
  braces.dismissing([&] {
    while (iters--) {
      auto const match =
          folly::exception_ptr_try_get_object_exact_fast<std::exception>(
              ptr,
              folly::tag<std::logic_error, std::range_error, std::bad_cast>);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(try_get_object_exact_fast_vmi_fail, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(C());
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_try_get_object_exact_fast<A0>(
          ptr, folly::tag<B1, B2>);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

BENCHMARK(try_get_object_exact_fast_vmi_pass, iters) {
  folly::BenchmarkSuspender braces;
  bool result = false;
  auto const ptr = std::make_exception_ptr(C());
  braces.dismissing([&] {
    while (iters--) {
      auto const match = folly::exception_ptr_try_get_object_exact_fast<A0>(
          ptr, folly::tag<B1, C, B2>);
      folly::compiler_must_not_predict(match);
      result = result || match;
    }
  });
  folly::compiler_must_not_elide(result);
}

int main() {
  folly::runBenchmarks();
  return 0;
}
