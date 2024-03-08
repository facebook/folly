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

#include <folly/Expected.h>

#include <folly/Benchmark.h>
#include <folly/ExceptionWrapper.h>
#include <folly/init/Init.h>
#include <folly/lang/Keep.h>

enum class V {};
enum class E {};

FOLLY_ALWAYS_INLINE E check_folly_expected_coro_await_unexpected_i(E i) {
  using X = folly::Expected<V, E>;
  auto fun = [i]() -> X {
    co_await folly::makeUnexpected(i);
    folly::compiler_may_unsafely_assume_unreachable();
  };
  return fun().error();
}

FOLLY_ALWAYS_INLINE E check_folly_expected_coro_await_expected_error_i(E i) {
  using X = folly::Expected<V, E>;
  auto fun = [i]() -> X {
    co_await X{folly::makeUnexpected(i)};
    folly::compiler_may_unsafely_assume_unreachable();
  };
  return fun().error();
}

FOLLY_ALWAYS_INLINE E check_folly_expected_coro_return_unexpected_i(E i) {
  using X = folly::Expected<V, E>;
  auto fun = [i]() -> X { //
    co_return folly::makeUnexpected(i);
  };
  return fun().error();
}

FOLLY_ALWAYS_INLINE E check_folly_expected_coro_return_expected_error_i(E i) {
  using X = folly::Expected<V, E>;
  auto fun = [i]() -> X { //
    co_return X{folly::makeUnexpected(i)};
  };
  return fun().error();
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE E
check_folly_expected_coro_await_unexpected(E i) {
  return check_folly_expected_coro_await_unexpected_i(i);
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE E
check_folly_expected_coro_await_expected_error(E i) {
  return check_folly_expected_coro_await_expected_error_i(i);
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE E
check_folly_expected_coro_return_unexpected(E i) {
  return check_folly_expected_coro_return_unexpected_i(i);
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE E
check_folly_expected_coro_return_expected_error(E i) {
  return check_folly_expected_coro_return_expected_error_i(i);
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE E
check_folly_expected_coro_await_unexpected_eptr( //
    folly::exception_wrapper eptr) {
  using X = folly::Expected<V, folly::exception_wrapper>;
  auto fun = [&]() -> X {
    co_await folly::makeUnexpected(std::move(eptr));
    folly::compiler_may_unsafely_assume_unreachable();
  };
  auto e = fun().error().get_exception<E>();
  return e ? *e : E{};
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE E
check_folly_expected_coro_await_expected_error_eptr(
    folly::exception_wrapper eptr) {
  using X = folly::Expected<V, folly::exception_wrapper>;
  auto fun = [&]() -> X {
    co_await X{folly::makeUnexpected(std::move(eptr))};
    folly::compiler_may_unsafely_assume_unreachable();
  };
  auto e = fun().error().get_exception<E>();
  return e ? *e : E{};
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE E
check_folly_expected_coro_return_unexpected_eptr(
    folly::exception_wrapper eptr) {
  using X = folly::Expected<V, folly::exception_wrapper>;
  auto fun = [&]() -> X { //
    co_return folly::makeUnexpected(std::move(eptr));
  };
  auto e = fun().error().get_exception<E>();
  return e ? *e : E{};
}

extern "C" FOLLY_KEEP FOLLY_NOINLINE E
check_folly_expected_coro_return_expected_error_eptr(
    folly::exception_wrapper eptr) {
  using X = folly::Expected<V, folly::exception_wrapper>;
  auto fun = [&]() -> X { //
    co_return X{folly::makeUnexpected(std::move(eptr))};
  };
  auto e = fun().error().get_exception<E>();
  return e ? *e : E{};
}

BENCHMARK(noop_i_0x100, iters) {
  while (iters--) {
    for (size_t i = 0; i < 0x100; ++i) {
      auto res = E(7);
      folly::compiler_must_not_elide(res);
    }
  }
}

BENCHMARK(await_unexpected_i_0x100, iters) {
  while (iters--) {
    for (size_t i = 0; i < 0x100; ++i) {
      auto res = check_folly_expected_coro_await_unexpected_i(E(7));
      folly::compiler_must_not_elide(res);
    }
  }
}

BENCHMARK(await_expected_error_i_0x100, iters) {
  while (iters--) {
    for (size_t i = 0; i < 0x100; ++i) {
      auto res = check_folly_expected_coro_await_expected_error_i(E(7));
      folly::compiler_must_not_elide(res);
    }
  }
}

BENCHMARK(return_unexpected_i_0x100, iters) {
  while (iters--) {
    for (size_t i = 0; i < 0x100; ++i) {
      auto res = check_folly_expected_coro_return_unexpected_i(E(7));
      folly::compiler_must_not_elide(res);
    }
  }
}

BENCHMARK(return_expected_error_i_0x100, iters) {
  while (iters--) {
    for (size_t i = 0; i < 0x100; ++i) {
      auto res = check_folly_expected_coro_return_expected_error_i(E(7));
      folly::compiler_must_not_elide(res);
    }
  }
}

BENCHMARK(noop_0x10, iters) {
  while (iters--) {
    for (size_t i = 0; i < 0x10; ++i) {
      auto res = std::invoke([]() FOLLY_NOINLINE { return E(7); });
      folly::compiler_must_not_elide(res);
    }
  }
}

BENCHMARK(await_unexpected_0x10, iters) {
  while (iters--) {
    for (size_t i = 0; i < 0x10; ++i) {
      auto res = check_folly_expected_coro_await_unexpected(E(7));
      folly::compiler_must_not_elide(res);
    }
  }
}

BENCHMARK(await_expected_error_0x10, iters) {
  while (iters--) {
    for (size_t i = 0; i < 0x10; ++i) {
      auto res = check_folly_expected_coro_await_expected_error(E(7));
      folly::compiler_must_not_elide(res);
    }
  }
}

BENCHMARK(return_unexpected_0x10, iters) {
  while (iters--) {
    for (size_t i = 0; i < 0x10; ++i) {
      auto res = check_folly_expected_coro_return_unexpected(E(7));
      folly::compiler_must_not_elide(res);
    }
  }
}

BENCHMARK(return_expected_error_0x10, iters) {
  while (iters--) {
    for (size_t i = 0; i < 0x10; ++i) {
      auto res = check_folly_expected_coro_return_expected_error(E(7));
      folly::compiler_must_not_elide(res);
    }
  }
}

BENCHMARK(await_unexpected_eptr, iters) {
  auto eptr = folly::make_exception_wrapper<E>(7);
  while (iters--) {
    auto res = check_folly_expected_coro_await_unexpected_eptr(eptr);
    folly::compiler_must_not_elide(res);
  }
}

BENCHMARK(await_expected_error_eptr, iters) {
  auto eptr = folly::make_exception_wrapper<E>(7);
  while (iters--) {
    auto res = check_folly_expected_coro_await_expected_error_eptr(eptr);
    folly::compiler_must_not_elide(res);
  }
}

BENCHMARK(return_unexpected_eptr, iters) {
  auto eptr = folly::make_exception_wrapper<E>(7);
  while (iters--) {
    auto res = check_folly_expected_coro_return_unexpected_eptr(eptr);
    folly::compiler_must_not_elide(res);
  }
}

BENCHMARK(return_expected_error_eptr, iters) {
  auto eptr = folly::make_exception_wrapper<E>(7);
  while (iters--) {
    auto res = check_folly_expected_coro_return_expected_error_eptr(eptr);
    folly::compiler_must_not_elide(res);
  }
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
