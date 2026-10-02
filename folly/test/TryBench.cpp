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
constexpr std::size_t kFreshAssignmentBatchSize = 1u << 16;
// Keep mixed-state chains larger than the branch predictor's practical history.
constexpr unsigned int kChainBatchSize = 1u << 20;

enum class TryState {
  empty,
  value,
  exception,
};

enum class StatePattern {
  random,
  alternating,
};

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

constexpr std::uint64_t mix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

std::vector<std::uint8_t> makeStatePattern(
    std::size_t exceptionCount, StatePattern pattern) {
  std::vector<std::uint8_t> states;
  states.reserve(kChainBatchSize);
  for (std::size_t i = 0; i < kChainBatchSize; ++i) {
    states.push_back(
        static_cast<std::uint8_t>(
            pattern == StatePattern::alternating
                ? i & 1
                : (mix(i) >> 56) < exceptionCount));
  }
  return states;
}

// The existing benchmarks cover live exception transfers. Empty wrappers here
// isolate state-transition and branch costs from exception refcount traffic.
template <typename T>
folly::Try<T> makeStateTry(bool exception, std::size_t value = 0) {
  if (exception) {
    return folly::Try<T>(folly::exception_wrapper{});
  }
  if constexpr (std::is_void_v<T>) {
    return folly::Try<void>();
  } else {
    return folly::Try<T>(static_cast<T>(value));
  }
}

template <typename T>
std::vector<folly::Try<T>> makeUniformTries(bool exception) {
  std::vector<folly::Try<T>> tries;
  tries.reserve(kBatchSize);
  for (std::size_t i = 0; i < kBatchSize; ++i) {
    tries.push_back(makeStateTry<T>(exception, i));
  }
  return tries;
}

template <typename T>
void resetTries(std::vector<folly::Try<T>>& tries, TryState state) {
  tries.clear();
  tries.reserve(kFreshAssignmentBatchSize);
  for (std::size_t i = 0; i < kFreshAssignmentBatchSize; ++i) {
    switch (state) {
      case TryState::empty:
        tries.emplace_back();
        break;
      case TryState::value:
        if constexpr (std::is_void_v<T>) {
          tries.emplace_back();
        } else {
          tries.emplace_back(static_cast<T>(i));
        }
        break;
      case TryState::exception:
        tries.emplace_back(folly::exception_wrapper{});
        break;
    }
  }
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

template <typename T>
unsigned int move_assign_fresh(
    unsigned int iters,
    TypeTag<T>,
    TryState destinationState,
    TryState sourceState) {
  folly::BenchmarkSuspender suspender;
  std::vector<folly::Try<T>> destinations;
  std::vector<folly::Try<T>> sources;
  resetTries(destinations, destinationState);
  resetTries(sources, sourceState);

  // Moving a value or an empty exception_wrapper preserves the source state.
  for (unsigned int iteration = 0; iteration < iters; ++iteration) {
    suspender.dismissing([&] {
      for (std::size_t i = 0; i < kFreshAssignmentBatchSize; ++i) {
        destinations[i] = std::move(sources[i]);
        folly::doNotOptimizeAway(destinations[i]);
      }
    });
    if (iteration + 1 != iters && destinationState != sourceState) {
      resetTries(destinations, destinationState);
    }
  }
  return iters * kFreshAssignmentBatchSize;
}

template <typename T>
unsigned int move_assign_transition(unsigned int iters, TypeTag<T>) {
  folly::BenchmarkSuspender suspender;
  auto destinations = makeUniformTries<T>(false);
  auto exceptionSources = makeUniformTries<T>(true);
  auto valueSources = makeUniformTries<T>(false);
  suspender.dismissing([&] {
    for (unsigned int iteration = 0; iteration < iters; ++iteration) {
      for (std::size_t i = 0; i < kBatchSize; ++i) {
        destinations[i] = std::move(exceptionSources[i]);
        folly::doNotOptimizeAway(destinations[i]);
      }
      for (std::size_t i = 0; i < kBatchSize; ++i) {
        destinations[i] = std::move(valueSources[i]);
        folly::doNotOptimizeAway(destinations[i]);
      }
    }
  });
  return iters * kBatchSize * 2;
}

template <typename T>
unsigned int move_assign_chain(
    unsigned int iters,
    TypeTag<T>,
    std::size_t exceptionCount,
    StatePattern pattern) {
  folly::BenchmarkSuspender suspender;
  const auto states = makeStatePattern(exceptionCount, pattern);
  auto valueSource = makeStateTry<T>(false);
  auto exceptionSource = makeStateTry<T>(true);
  const std::array<folly::Try<T>*, 2> sources{&valueSource, &exceptionSource};
  auto destination = makeStateTry<T>(states.back() != 0);
  suspender.dismissing([&] {
    for (unsigned int iteration = 0; iteration < iters; ++iteration) {
      for (auto const state : states) {
        destination = std::move(*sources[state]);
        folly::doNotOptimizeAway(destination);
      }
    }
  });
  return iters * kChainBatchSize;
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

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_fresh,
    void_success_from_value,
    TypeTag<void>{},
    TryState::value,
    TryState::value)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_fresh,
    void_success_from_exception,
    TypeTag<void>{},
    TryState::value,
    TryState::exception)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_fresh,
    int_empty_from_value,
    TypeTag<int>{},
    TryState::empty,
    TryState::value)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_fresh,
    int_empty_from_exception,
    TypeTag<int>{},
    TryState::empty,
    TryState::exception)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_fresh,
    int_value_from_value,
    TypeTag<int>{},
    TryState::value,
    TryState::value)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_fresh,
    int_value_from_exception,
    TypeTag<int>{},
    TryState::value,
    TryState::exception)

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_transition, void_value_to_exception_and_back, TypeTag<void>{})
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_transition, int_value_to_exception_and_back, TypeTag<int>{})

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    void_value100_exception0,
    TypeTag<void>{},
    0,
    StatePattern::random)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    void_value80_exception20,
    TypeTag<void>{},
    51,
    StatePattern::random)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    void_value50_exception50_random,
    TypeTag<void>{},
    128,
    StatePattern::random)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    void_value50_exception50_alternating,
    TypeTag<void>{},
    128,
    StatePattern::alternating)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    void_value20_exception80,
    TypeTag<void>{},
    205,
    StatePattern::random)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    void_value0_exception100,
    TypeTag<void>{},
    256,
    StatePattern::random)

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    int_value100_exception0,
    TypeTag<int>{},
    0,
    StatePattern::random)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    int_value80_exception20,
    TypeTag<int>{},
    51,
    StatePattern::random)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    int_value50_exception50_random,
    TypeTag<int>{},
    128,
    StatePattern::random)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    int_value50_exception50_alternating,
    TypeTag<int>{},
    128,
    StatePattern::alternating)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    int_value20_exception80,
    TypeTag<int>{},
    205,
    StatePattern::random)
BENCHMARK_NAMED_PARAM_MULTI(
    move_assign_chain,
    int_value0_exception100,
    TypeTag<int>{},
    256,
    StatePattern::random)

} // namespace

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
