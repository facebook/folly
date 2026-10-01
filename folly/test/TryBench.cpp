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

#include <folly/Try.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/ExceptionWrapper.h>
#include <folly/portability/GFlags.h>

namespace {

constexpr std::size_t kBatchSize = 256;

template <typename T>
struct TypeTag {};

constexpr std::uint8_t permute(std::uint8_t value) {
  value ^= value >> 4;
  value *= 0x9d;
  value ^= value >> 3;
  value *= 0xa7;
  value ^= value >> 4;
  return value;
}

template <typename T>
std::vector<folly::Try<T>> makeTries(std::size_t exceptionCount) {
  auto const error =
      folly::make_exception_wrapper<std::runtime_error>("benchmark");
  std::vector<folly::Try<T>> tries;
  tries.reserve(kBatchSize);
  for (std::size_t i = 0; i < kBatchSize; ++i) {
    if (permute(static_cast<std::uint8_t>(i)) < exceptionCount) {
      tries.emplace_back(error);
    } else if constexpr (std::is_void_v<T>) {
      tries.emplace_back();
    } else {
      tries.emplace_back(static_cast<T>(i));
    }
  }
  return tries;
}

template <typename T>
unsigned int move_construct(
    unsigned int iters, TypeTag<T>, std::size_t exceptionCount) {
  using Try = folly::Try<T>;
  using Storage = std::aligned_storage_t<sizeof(Try), alignof(Try)>;

  folly::BenchmarkSuspender suspender;
  auto tries = makeTries<T>(exceptionCount);
  std::array<Storage, kBatchSize> storage{};
  auto* const sources = tries.data();
  suspender.dismissing([&] {
    for (unsigned int iteration = 0; iteration < iters; ++iteration) {
      for (std::size_t i = 0; i < kBatchSize; ++i) {
        auto* const destination =
            ::new (static_cast<void*>(&storage[i])) Try(std::move(sources[i]));
        folly::doNotOptimizeAway(*destination);
        sources[i].~Try();
      }
      for (std::size_t i = 0; i < kBatchSize; ++i) {
        auto* const source = std::launder(reinterpret_cast<Try*>(&storage[i]));
        ::new (static_cast<void*>(sources + i)) Try(std::move(*source));
        folly::doNotOptimizeAway(sources[i]);
        source->~Try();
      }
    }
  });
  return iters * kBatchSize * 2;
}

template <typename T>
unsigned int move_assign(
    unsigned int iters, TypeTag<T>, std::size_t exceptionCount) {
  folly::BenchmarkSuspender suspender;
  auto left = makeTries<T>(exceptionCount);
  auto right = makeTries<T>(exceptionCount);
  suspender.dismissing([&] {
    for (unsigned int iteration = 0; iteration < iters; ++iteration) {
      for (std::size_t i = 0; i < kBatchSize; ++i) {
        left[i] = std::move(right[i]);
        folly::doNotOptimizeAway(left[i]);
      }
      for (std::size_t i = 0; i < kBatchSize; ++i) {
        right[i] = std::move(left[i]);
        folly::doNotOptimizeAway(right[i]);
      }
    }
  });
  return iters * kBatchSize * 2;
}

BENCHMARK_NAMED_PARAM_MULTI(
    move_construct, void_value100_exception0, TypeTag<void>{}, 0)
BENCHMARK_NAMED_PARAM_MULTI(
    move_construct, void_value0_exception100, TypeTag<void>{}, 256)
BENCHMARK_NAMED_PARAM_MULTI(
    move_construct, void_value50_exception50, TypeTag<void>{}, 128)
BENCHMARK_NAMED_PARAM_MULTI(
    move_construct, void_value80_exception20, TypeTag<void>{}, 51)
BENCHMARK_NAMED_PARAM_MULTI(
    move_construct, void_value20_exception80, TypeTag<void>{}, 205)

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM_MULTI(
    move_construct, int_value100_exception0, TypeTag<int>{}, 0)
BENCHMARK_NAMED_PARAM_MULTI(
    move_construct, int_value0_exception100, TypeTag<int>{}, 256)
BENCHMARK_NAMED_PARAM_MULTI(
    move_construct, int_value50_exception50, TypeTag<int>{}, 128)
BENCHMARK_NAMED_PARAM_MULTI(
    move_construct, int_value80_exception20, TypeTag<int>{}, 51)
BENCHMARK_NAMED_PARAM_MULTI(
    move_construct, int_value20_exception80, TypeTag<int>{}, 205)

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM_MULTI(
    move_assign, void_value100_exception0, TypeTag<void>{}, 0)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign, void_value0_exception100, TypeTag<void>{}, 256)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign, void_value50_exception50, TypeTag<void>{}, 128)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign, void_value80_exception20, TypeTag<void>{}, 51)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign, void_value20_exception80, TypeTag<void>{}, 205)

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM_MULTI(
    move_assign, int_value100_exception0, TypeTag<int>{}, 0)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign, int_value0_exception100, TypeTag<int>{}, 256)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign, int_value50_exception50, TypeTag<int>{}, 128)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign, int_value80_exception20, TypeTag<int>{}, 51)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign, int_value20_exception80, TypeTag<int>{}, 205)

} // namespace

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
