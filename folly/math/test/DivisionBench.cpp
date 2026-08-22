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

#include <folly/math/Division.h>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <random>
#include <type_traits>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <folly/lang/Keep.h>

FOLLY_KEEP folly::uint_division_result<std::uint8_t>
check_folly_uint_divisor_divrem_u8(
    std::uint8_t const dividend,
    std::uint8_t const divisor,
    folly::uint_divisor<std::uint8_t>::calc const calc) {
  return calc.divrem(dividend, divisor);
}

FOLLY_KEEP folly::uint_division_result<std::uint8_t>
check_folly_uint_divisor_gt1_divrem_u8(
    std::uint8_t const dividend,
    std::uint8_t const divisor,
    folly::uint_divisor<std::uint8_t>::calc const calc) {
  return calc.divrem_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint8_t check_folly_uint_divisor_div_u8(
    std::uint8_t const dividend,
    std::uint8_t const divisor,
    folly::uint_divisor<std::uint8_t>::calc const calc) {
  return calc.div(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint8_t check_folly_uint_divisor_gt1_div_u8(
    std::uint8_t const dividend,
    std::uint8_t const divisor,
    folly::uint_divisor<std::uint8_t>::calc const calc) {
  return calc.div_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint8_t check_folly_uint_divisor_rem_u8(
    std::uint8_t const dividend,
    std::uint8_t const divisor,
    folly::uint_divisor<std::uint8_t>::calc const calc) {
  return calc.rem(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint8_t check_folly_uint_divisor_gt1_rem_u8(
    std::uint8_t const dividend,
    std::uint8_t const divisor,
    folly::uint_divisor<std::uint8_t>::calc const calc) {
  return calc.rem_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP bool check_folly_uint_divisor_is_divisor_of_u8(
    std::uint8_t const dividend,
    std::uint8_t const divisor,
    folly::uint_divisor<std::uint8_t>::calc const calc) {
  return calc.is_divisor_of(dividend, divisor);
}

extern "C" FOLLY_KEEP bool check_folly_uint_divisor_gt1_is_divisor_of_u8(
    std::uint8_t const dividend,
    std::uint8_t const divisor,
    folly::uint_divisor<std::uint8_t>::calc const calc) {
  return calc.is_divisor_of_gt1(dividend, divisor);
}

FOLLY_KEEP folly::uint_division_result<std::uint16_t>
check_folly_uint_divisor_divrem_u16(
    std::uint16_t const dividend,
    std::uint16_t const divisor,
    folly::uint_divisor<std::uint16_t>::calc const calc) {
  return calc.divrem(dividend, divisor);
}

FOLLY_KEEP folly::uint_division_result<std::uint16_t>
check_folly_uint_divisor_gt1_divrem_u16(
    std::uint16_t const dividend,
    std::uint16_t const divisor,
    folly::uint_divisor<std::uint16_t>::calc const calc) {
  return calc.divrem_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint16_t check_folly_uint_divisor_div_u16(
    std::uint16_t const dividend,
    std::uint16_t const divisor,
    folly::uint_divisor<std::uint16_t>::calc const calc) {
  return calc.div(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint16_t check_folly_uint_divisor_gt1_div_u16(
    std::uint16_t const dividend,
    std::uint16_t const divisor,
    folly::uint_divisor<std::uint16_t>::calc const calc) {
  return calc.div_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint16_t check_folly_uint_divisor_rem_u16(
    std::uint16_t const dividend,
    std::uint16_t const divisor,
    folly::uint_divisor<std::uint16_t>::calc const calc) {
  return calc.rem(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint16_t check_folly_uint_divisor_gt1_rem_u16(
    std::uint16_t const dividend,
    std::uint16_t const divisor,
    folly::uint_divisor<std::uint16_t>::calc const calc) {
  return calc.rem_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP bool check_folly_uint_divisor_is_divisor_of_u16(
    std::uint16_t const dividend,
    std::uint16_t const divisor,
    folly::uint_divisor<std::uint16_t>::calc const calc) {
  return calc.is_divisor_of(dividend, divisor);
}

extern "C" FOLLY_KEEP bool check_folly_uint_divisor_gt1_is_divisor_of_u16(
    std::uint16_t const dividend,
    std::uint16_t const divisor,
    folly::uint_divisor<std::uint16_t>::calc const calc) {
  return calc.is_divisor_of_gt1(dividend, divisor);
}

FOLLY_KEEP folly::uint_division_result<std::uint32_t>
check_folly_uint_divisor_divrem_u32(
    std::uint32_t const dividend,
    std::uint32_t const divisor,
    folly::uint_divisor<std::uint32_t>::calc const calc) {
  return calc.divrem(dividend, divisor);
}

FOLLY_KEEP folly::uint_division_result<std::uint32_t>
check_folly_uint_divisor_gt1_divrem_u32(
    std::uint32_t const dividend,
    std::uint32_t const divisor,
    folly::uint_divisor<std::uint32_t>::calc const calc) {
  return calc.divrem_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint32_t check_folly_uint_divisor_div_u32(
    std::uint32_t const dividend,
    std::uint32_t const divisor,
    folly::uint_divisor<std::uint32_t>::calc const calc) {
  return calc.div(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint32_t check_folly_uint_divisor_gt1_div_u32(
    std::uint32_t const dividend,
    std::uint32_t const divisor,
    folly::uint_divisor<std::uint32_t>::calc const calc) {
  return calc.div_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint32_t check_folly_uint_divisor_rem_u32(
    std::uint32_t const dividend,
    std::uint32_t const divisor,
    folly::uint_divisor<std::uint32_t>::calc const calc) {
  return calc.rem(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint32_t check_folly_uint_divisor_gt1_rem_u32(
    std::uint32_t const dividend,
    std::uint32_t const divisor,
    folly::uint_divisor<std::uint32_t>::calc const calc) {
  return calc.rem_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP bool check_folly_uint_divisor_is_divisor_of_u32(
    std::uint32_t const dividend,
    std::uint32_t const divisor,
    folly::uint_divisor<std::uint32_t>::calc const calc) {
  return calc.is_divisor_of(dividend, divisor);
}

extern "C" FOLLY_KEEP bool check_folly_uint_divisor_gt1_is_divisor_of_u32(
    std::uint32_t const dividend,
    std::uint32_t const divisor,
    folly::uint_divisor<std::uint32_t>::calc const calc) {
  return calc.is_divisor_of_gt1(dividend, divisor);
}

FOLLY_KEEP folly::uint_division_result<std::uint64_t>
check_folly_uint_divisor_divrem_u64(
    std::uint64_t const dividend,
    std::uint64_t const divisor,
    folly::uint_divisor<std::uint64_t>::calc const calc) {
  return calc.divrem(dividend, divisor);
}

FOLLY_KEEP folly::uint_division_result<std::uint64_t>
check_folly_uint_divisor_gt1_divrem_u64(
    std::uint64_t const dividend,
    std::uint64_t const divisor,
    folly::uint_divisor<std::uint64_t>::calc const calc) {
  return calc.divrem_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint64_t check_folly_uint_divisor_div_u64(
    std::uint64_t const dividend,
    std::uint64_t const divisor,
    folly::uint_divisor<std::uint64_t>::calc const calc) {
  return calc.div(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint64_t check_folly_uint_divisor_gt1_div_u64(
    std::uint64_t const dividend,
    std::uint64_t const divisor,
    folly::uint_divisor<std::uint64_t>::calc const calc) {
  return calc.div_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint64_t check_folly_uint_divisor_rem_u64(
    std::uint64_t const dividend,
    std::uint64_t const divisor,
    folly::uint_divisor<std::uint64_t>::calc const calc) {
  return calc.rem(dividend, divisor);
}

extern "C" FOLLY_KEEP std::uint64_t check_folly_uint_divisor_gt1_rem_u64(
    std::uint64_t const dividend,
    std::uint64_t const divisor,
    folly::uint_divisor<std::uint64_t>::calc const calc) {
  return calc.rem_gt1(dividend, divisor);
}

extern "C" FOLLY_KEEP bool check_folly_uint_divisor_is_divisor_of_u64(
    std::uint64_t const dividend,
    std::uint64_t const divisor,
    folly::uint_divisor<std::uint64_t>::calc const calc) {
  return calc.is_divisor_of(dividend, divisor);
}

extern "C" FOLLY_KEEP bool check_folly_uint_divisor_gt1_is_divisor_of_u64(
    std::uint64_t const dividend,
    std::uint64_t const divisor,
    folly::uint_divisor<std::uint64_t>::calc const calc) {
  return calc.is_divisor_of_gt1(dividend, divisor);
}

namespace {

template <typename Word>
std::vector<Word> makeDividends(size_t const n, Word const divisor) {
  std::mt19937_64 rng(42);
  std::uniform_int_distribution<uint64_t> dist(
      0, std::numeric_limits<Word>::max());
  std::vector<Word> dividends;
  dividends.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    Word dividend = static_cast<Word>(dist(rng));
    if (i % 7 == 0) {
      dividend =
          static_cast<Word>(dividend - (dividend % divisor) + (i % divisor));
    }
    dividends.push_back(dividend);
  }
  return dividends;
}

// Obscure multiword state without forcing it through memory.
template <typename Word, typename State>
FOLLY_ALWAYS_INLINE State makeStateUnpredictable(State const state) {
  if constexpr (std::is_empty_v<State>) {
    return state;
  } else {
    static_assert(std::is_trivially_copyable_v<State>);
    static_assert(sizeof(State) % sizeof(Word) == 0);
    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    using Words = std::array<Word, sizeof(State) / sizeof(Word)>;
    auto words = std::bit_cast<Words>(state);
    for (auto& word : words) {
      folly::makeUnpredictable(word);
    }
    return std::bit_cast<State>(words);
  }
}

template <typename Word>
struct NaturalDivision {
  using result_type = folly::uint_division_result<Word>;

  Word divisor;

  void makeUnpredictable() { folly::makeUnpredictable(divisor); }

  result_type operator()(Word const dividend) const {
    return {
        static_cast<Word>(dividend / divisor),
        static_cast<Word>(dividend % divisor)};
  }

  Word div(Word const dividend) const {
    return static_cast<Word>(dividend / divisor);
  }

  Word rem(Word const dividend) const {
    return static_cast<Word>(dividend % divisor);
  }

  bool is_divisor_of(Word const dividend) const {
    return dividend % divisor == 0;
  }
};

template <typename Word>
NaturalDivision<Word> makeNaturalDivision(Word const divisor) {
  return {makeStateUnpredictable<Word>(divisor)};
}

template <typename Word, bool Gt1>
struct DivisorDivision {
  using result_type = folly::uint_division_result<Word>;
  using calc_type = typename folly::uint_divisor<Word>::calc;

  calc_type calc;
  Word divisor;

  void makeUnpredictable() {
    calc = makeStateUnpredictable<Word>(calc);
    folly::makeUnpredictable(divisor);
  }

  result_type operator()(Word const dividend) const {
    if constexpr (Gt1) {
      return calc.divrem_gt1(dividend, divisor);
    } else {
      return calc.divrem(dividend, divisor);
    }
  }

  Word div(Word const dividend) const {
    if constexpr (Gt1) {
      return calc.div_gt1(dividend, divisor);
    } else {
      return calc.div(dividend, divisor);
    }
  }

  Word rem(Word const dividend) const {
    if constexpr (Gt1) {
      return calc.rem_gt1(dividend, divisor);
    } else {
      return calc.rem(dividend, divisor);
    }
  }

  bool is_divisor_of(Word const dividend) const {
    if constexpr (Gt1) {
      return calc.is_divisor_of_gt1(dividend, divisor);
    } else {
      return calc.is_divisor_of(dividend, divisor);
    }
  }
};

template <typename Word>
DivisorDivision<Word, false> makeDivisor(Word const divisor) {
  Word const unpredictableDivisor = makeStateUnpredictable<Word>(divisor);
  using Calc = typename folly::uint_divisor<Word>::calc;
  return {Calc(unpredictableDivisor), unpredictableDivisor};
}

template <typename Word>
DivisorDivision<Word, true> makeGt1Division(Word const divisor) {
  Word const unpredictableDivisor = makeStateUnpredictable<Word>(divisor);
  using Calc = typename folly::uint_divisor<Word>::calc;
  return {Calc(unpredictableDivisor), unpredictableDivisor};
}

template <typename Word, Word Divisor>
struct LiteralDivision {
  using result_type = folly::uint_division_result<Word>;

  void makeUnpredictable() {}

  result_type operator()(Word const dividend) const {
    return {
        static_cast<Word>(dividend / Divisor),
        static_cast<Word>(dividend % Divisor)};
  }

  Word div(Word const dividend) const {
    return static_cast<Word>(dividend / Divisor);
  }

  Word rem(Word const dividend) const {
    return static_cast<Word>(dividend % Divisor);
  }

  bool is_divisor_of(Word const dividend) const {
    return dividend % Divisor == 0;
  }
};

constexpr size_t kN = 1 << 14;

template <typename Word>
using BenchmarkSink = std::
    conditional_t<(sizeof(Word) < sizeof(std::uint32_t)), std::uint32_t, Word>;

template <typename Word, typename Division>
void runDivRem(
    size_t const iterations,
    std::vector<Word> const& dividends,
    Division const& division) {
  size_t iters = iterations;
  BenchmarkSink<Word> sink = 0;
  Division current = division;
  FOLLY_PRAGMA_UNROLL_N(8)
  while (iters--) {
    current.makeUnpredictable();
    auto const qr = current(dividends[iters & (kN - 1)]);
    sink += qr.quotient;
    sink += qr.remainder;
  }
  folly::doNotOptimizeAway(sink);
}

template <typename Word, typename Division>
void runDiv(
    size_t const iterations,
    std::vector<Word> const& dividends,
    Division const& division) {
  size_t iters = iterations;
  BenchmarkSink<Word> sink = 0;
  Division current = division;
  FOLLY_PRAGMA_UNROLL_N(8)
  while (iters--) {
    current.makeUnpredictable();
    sink += current.div(dividends[iters & (kN - 1)]);
  }
  folly::doNotOptimizeAway(sink);
}

template <typename Word, typename Division>
void runRem(
    size_t const iterations,
    std::vector<Word> const& dividends,
    Division const& division) {
  size_t iters = iterations;
  BenchmarkSink<Word> sink = 0;
  Division current = division;
  FOLLY_PRAGMA_UNROLL_N(8)
  while (iters--) {
    current.makeUnpredictable();
    sink += current.rem(dividends[iters & (kN - 1)]);
  }
  folly::doNotOptimizeAway(sink);
}

template <typename Word, typename Division>
void runIsDivisorOf(
    size_t const iterations,
    std::vector<Word> const& dividends,
    Division const& division) {
  size_t iters = iterations;
  BenchmarkSink<Word> sink = 0;
  Division current = division;
  FOLLY_PRAGMA_UNROLL_N(8)
  while (iters--) {
    current.makeUnpredictable();
    sink += current.is_divisor_of(dividends[iters & (kN - 1)]);
  }
  folly::doNotOptimizeAway(sink);
}

template <typename Word, typename Division>
void runDivRemLatency(
    size_t const iterations,
    std::vector<Word> const& dividends,
    Division const& division) {
  size_t iters = iterations;
  Word dividend = dividends[0];
  Division current = division;
  FOLLY_PRAGMA_UNROLL_N(8)
  while (iters--) {
    current.makeUnpredictable();
    auto const qr = current(dividend);
    dividend = static_cast<Word>(
        dividends[iters & (kN - 1)] + qr.quotient + qr.remainder);
  }
  folly::doNotOptimizeAway(dividend);
}

template <typename Word, typename Division>
void runDivLatency(
    size_t const iterations,
    std::vector<Word> const& dividends,
    Division const& division) {
  size_t iters = iterations;
  Word dividend = dividends[0];
  Division current = division;
  FOLLY_PRAGMA_UNROLL_N(8)
  while (iters--) {
    current.makeUnpredictable();
    dividend =
        static_cast<Word>(dividends[iters & (kN - 1)] + current.div(dividend));
  }
  folly::doNotOptimizeAway(dividend);
}

template <typename Word, typename Division>
void runRemLatency(
    size_t const iterations,
    std::vector<Word> const& dividends,
    Division const& division) {
  size_t iters = iterations;
  Word dividend = dividends[0];
  Division current = division;
  FOLLY_PRAGMA_UNROLL_N(8)
  while (iters--) {
    current.makeUnpredictable();
    dividend =
        static_cast<Word>(dividends[iters & (kN - 1)] + current.rem(dividend));
  }
  folly::doNotOptimizeAway(dividend);
}

template <typename Word, typename Division>
void runIsDivisorOfLatency(
    size_t const iterations,
    std::vector<Word> const& dividends,
    Division const& division) {
  size_t iters = iterations;
  Word dividend = dividends[0];
  Division current = division;
  FOLLY_PRAGMA_UNROLL_N(8)
  while (iters--) {
    current.makeUnpredictable();
    dividend = static_cast<Word>(
        dividends[iters & (kN - 1)] + current.is_divisor_of(dividend));
  }
  folly::doNotOptimizeAway(dividend);
}

template <typename Word>
void runControl(
    size_t const iterations,
    std::vector<Word> const& dividends,
    Word const divisor) {
  size_t iters = iterations;
  BenchmarkSink<Word> sink = 0;
  auto current = makeDivisor(divisor);
  FOLLY_PRAGMA_UNROLL_N(8)
  while (iters--) {
    current.makeUnpredictable();
    Word const dividend = dividends[iters & (kN - 1)];
    sink += dividend;
    sink += current.divisor;
    folly::doNotOptimizeAway(sink);
  }
  folly::doNotOptimizeAway(sink);
}

template <typename Word>
void runControlLatency(
    size_t const iterations,
    std::vector<Word> const& dividends,
    Word const divisor) {
  size_t iters = iterations;
  Word dividend = dividends[0];
  auto current = makeDivisor(divisor);
  FOLLY_PRAGMA_UNROLL_N(8)
  while (iters--) {
    current.makeUnpredictable();
    dividend = static_cast<Word>(
        dividends[iters & (kN - 1)] + dividend + current.divisor);
    folly::doNotOptimizeAway(dividend);
  }
  folly::doNotOptimizeAway(dividend);
}

constexpr std::uint8_t kDivisorValue8 = 7;
auto const kDividends8 = makeDividends<std::uint8_t>(kN, kDivisorValue8);
auto const kDivisor8 = makeDivisor(kDivisorValue8);
constexpr LiteralDivision<std::uint8_t, kDivisorValue8> kLiteral8;

constexpr std::uint16_t kDivisorValue16 = 13;
auto const kDividends16 = makeDividends<std::uint16_t>(kN, kDivisorValue16);
auto const kDivisor16 = makeDivisor(kDivisorValue16);
constexpr LiteralDivision<std::uint16_t, kDivisorValue16> kLiteral16;

constexpr std::uint32_t kDivisorValue32 = 7;
auto const kDividends32 = makeDividends<std::uint32_t>(kN, kDivisorValue32);
auto const kDivisor32 = makeDivisor(kDivisorValue32);
constexpr LiteralDivision<std::uint32_t, kDivisorValue32> kLiteral32;

constexpr std::uint64_t kDivisorValue64 = 1000003;
auto const kDividends64 = makeDividends<std::uint64_t>(kN, kDivisorValue64);
auto const kDivisor64 = makeDivisor(kDivisorValue64);
constexpr LiteralDivision<std::uint64_t, kDivisorValue64> kLiteral64;

} // namespace

BENCHMARK(natural_tput_u8, iters) {
  runDivRem(iters, kDividends8, makeNaturalDivision(kDivisorValue8));
}

BENCHMARK(divisor_tput_u8, iters) {
  runDivRem(iters, kDividends8, kDivisor8);
}

BENCHMARK(div_gt1_tput_u8, iters) {
  runDivRem(iters, kDividends8, makeGt1Division(kDivisorValue8));
}

BENCHMARK(literal_tput_u8, iters) {
  runDivRem(iters, kDividends8, kLiteral8);
}

BENCHMARK(control_tput_u8, iters) {
  runControl(iters, kDividends8, kDivisorValue8);
}

BENCHMARK(natural_laty_u8, iters) {
  runDivRemLatency(iters, kDividends8, makeNaturalDivision(kDivisorValue8));
}

BENCHMARK(divisor_laty_u8, iters) {
  runDivRemLatency(iters, kDividends8, kDivisor8);
}

BENCHMARK(div_gt1_laty_u8, iters) {
  runDivRemLatency(iters, kDividends8, makeGt1Division(kDivisorValue8));
}

BENCHMARK(literal_laty_u8, iters) {
  runDivRemLatency(iters, kDividends8, kLiteral8);
}

BENCHMARK(control_laty_u8, iters) {
  runControlLatency(iters, kDividends8, kDivisorValue8);
}

BENCHMARK(natural_tput_div_u8, iters) {
  runDiv(iters, kDividends8, makeNaturalDivision(kDivisorValue8));
}

BENCHMARK(divisor_tput_div_u8, iters) {
  runDiv(iters, kDividends8, kDivisor8);
}

BENCHMARK(div_gt1_tput_div_u8, iters) {
  runDiv(iters, kDividends8, makeGt1Division(kDivisorValue8));
}

BENCHMARK(literal_tput_div_u8, iters) {
  runDiv(iters, kDividends8, kLiteral8);
}

BENCHMARK(control_tput_div_u8, iters) {
  runControl(iters, kDividends8, kDivisorValue8);
}

BENCHMARK(natural_laty_div_u8, iters) {
  runDivLatency(iters, kDividends8, makeNaturalDivision(kDivisorValue8));
}

BENCHMARK(divisor_laty_div_u8, iters) {
  runDivLatency(iters, kDividends8, kDivisor8);
}

BENCHMARK(div_gt1_laty_div_u8, iters) {
  runDivLatency(iters, kDividends8, makeGt1Division(kDivisorValue8));
}

BENCHMARK(literal_laty_div_u8, iters) {
  runDivLatency(iters, kDividends8, kLiteral8);
}

BENCHMARK(control_laty_div_u8, iters) {
  runControlLatency(iters, kDividends8, kDivisorValue8);
}

BENCHMARK(natural_tput_rem_u8, iters) {
  runRem(iters, kDividends8, makeNaturalDivision(kDivisorValue8));
}

BENCHMARK(divisor_tput_rem_u8, iters) {
  runRem(iters, kDividends8, kDivisor8);
}

BENCHMARK(div_gt1_tput_rem_u8, iters) {
  runRem(iters, kDividends8, makeGt1Division(kDivisorValue8));
}

BENCHMARK(literal_tput_rem_u8, iters) {
  runRem(iters, kDividends8, kLiteral8);
}

BENCHMARK(control_tput_rem_u8, iters) {
  runControl(iters, kDividends8, kDivisorValue8);
}

BENCHMARK(natural_laty_rem_u8, iters) {
  runRemLatency(iters, kDividends8, makeNaturalDivision(kDivisorValue8));
}

BENCHMARK(divisor_laty_rem_u8, iters) {
  runRemLatency(iters, kDividends8, kDivisor8);
}

BENCHMARK(div_gt1_laty_rem_u8, iters) {
  runRemLatency(iters, kDividends8, makeGt1Division(kDivisorValue8));
}

BENCHMARK(literal_laty_rem_u8, iters) {
  runRemLatency(iters, kDividends8, kLiteral8);
}

BENCHMARK(control_laty_rem_u8, iters) {
  runControlLatency(iters, kDividends8, kDivisorValue8);
}

BENCHMARK(natural_tput_divisor_of_u8, iters) {
  runIsDivisorOf(iters, kDividends8, makeNaturalDivision(kDivisorValue8));
}

BENCHMARK(divisor_tput_divisor_of_u8, iters) {
  runIsDivisorOf(iters, kDividends8, kDivisor8);
}

BENCHMARK(div_gt1_tput_divisor_of_u8, iters) {
  runIsDivisorOf(iters, kDividends8, makeGt1Division(kDivisorValue8));
}

BENCHMARK(literal_tput_divisor_of_u8, iters) {
  runIsDivisorOf(iters, kDividends8, kLiteral8);
}

BENCHMARK(control_tput_divisor_of_u8, iters) {
  runControl(iters, kDividends8, kDivisorValue8);
}

BENCHMARK(natural_laty_divisor_of_u8, iters) {
  runIsDivisorOfLatency(
      iters, kDividends8, makeNaturalDivision(kDivisorValue8));
}

BENCHMARK(divisor_laty_divisor_of_u8, iters) {
  runIsDivisorOfLatency(iters, kDividends8, kDivisor8);
}

BENCHMARK(div_gt1_laty_divisor_of_u8, iters) {
  runIsDivisorOfLatency(iters, kDividends8, makeGt1Division(kDivisorValue8));
}

BENCHMARK(literal_laty_divisor_of_u8, iters) {
  runIsDivisorOfLatency(iters, kDividends8, kLiteral8);
}

BENCHMARK(control_laty_divisor_of_u8, iters) {
  runControlLatency(iters, kDividends8, kDivisorValue8);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(natural_tput_u16, iters) {
  runDivRem(iters, kDividends16, makeNaturalDivision(kDivisorValue16));
}

BENCHMARK(divisor_tput_u16, iters) {
  runDivRem(iters, kDividends16, kDivisor16);
}

BENCHMARK(div_gt1_tput_u16, iters) {
  runDivRem(iters, kDividends16, makeGt1Division(kDivisorValue16));
}

BENCHMARK(literal_tput_u16, iters) {
  runDivRem(iters, kDividends16, kLiteral16);
}

BENCHMARK(control_tput_u16, iters) {
  runControl(iters, kDividends16, kDivisorValue16);
}

BENCHMARK(natural_laty_u16, iters) {
  runDivRemLatency(iters, kDividends16, makeNaturalDivision(kDivisorValue16));
}

BENCHMARK(divisor_laty_u16, iters) {
  runDivRemLatency(iters, kDividends16, kDivisor16);
}

BENCHMARK(div_gt1_laty_u16, iters) {
  runDivRemLatency(iters, kDividends16, makeGt1Division(kDivisorValue16));
}

BENCHMARK(literal_laty_u16, iters) {
  runDivRemLatency(iters, kDividends16, kLiteral16);
}

BENCHMARK(control_laty_u16, iters) {
  runControlLatency(iters, kDividends16, kDivisorValue16);
}

BENCHMARK(natural_tput_div_u16, iters) {
  runDiv(iters, kDividends16, makeNaturalDivision(kDivisorValue16));
}

BENCHMARK(divisor_tput_div_u16, iters) {
  runDiv(iters, kDividends16, kDivisor16);
}

BENCHMARK(div_gt1_tput_div_u16, iters) {
  runDiv(iters, kDividends16, makeGt1Division(kDivisorValue16));
}

BENCHMARK(literal_tput_div_u16, iters) {
  runDiv(iters, kDividends16, kLiteral16);
}

BENCHMARK(control_tput_div_u16, iters) {
  runControl(iters, kDividends16, kDivisorValue16);
}

BENCHMARK(natural_laty_div_u16, iters) {
  runDivLatency(iters, kDividends16, makeNaturalDivision(kDivisorValue16));
}

BENCHMARK(divisor_laty_div_u16, iters) {
  runDivLatency(iters, kDividends16, kDivisor16);
}

BENCHMARK(div_gt1_laty_div_u16, iters) {
  runDivLatency(iters, kDividends16, makeGt1Division(kDivisorValue16));
}

BENCHMARK(literal_laty_div_u16, iters) {
  runDivLatency(iters, kDividends16, kLiteral16);
}

BENCHMARK(control_laty_div_u16, iters) {
  runControlLatency(iters, kDividends16, kDivisorValue16);
}

BENCHMARK(natural_tput_rem_u16, iters) {
  runRem(iters, kDividends16, makeNaturalDivision(kDivisorValue16));
}

BENCHMARK(divisor_tput_rem_u16, iters) {
  runRem(iters, kDividends16, kDivisor16);
}

BENCHMARK(div_gt1_tput_rem_u16, iters) {
  runRem(iters, kDividends16, makeGt1Division(kDivisorValue16));
}

BENCHMARK(literal_tput_rem_u16, iters) {
  runRem(iters, kDividends16, kLiteral16);
}

BENCHMARK(control_tput_rem_u16, iters) {
  runControl(iters, kDividends16, kDivisorValue16);
}

BENCHMARK(natural_laty_rem_u16, iters) {
  runRemLatency(iters, kDividends16, makeNaturalDivision(kDivisorValue16));
}

BENCHMARK(divisor_laty_rem_u16, iters) {
  runRemLatency(iters, kDividends16, kDivisor16);
}

BENCHMARK(div_gt1_laty_rem_u16, iters) {
  runRemLatency(iters, kDividends16, makeGt1Division(kDivisorValue16));
}

BENCHMARK(literal_laty_rem_u16, iters) {
  runRemLatency(iters, kDividends16, kLiteral16);
}

BENCHMARK(control_laty_rem_u16, iters) {
  runControlLatency(iters, kDividends16, kDivisorValue16);
}

BENCHMARK(natural_tput_divisor_of_u16, iters) {
  runIsDivisorOf(iters, kDividends16, makeNaturalDivision(kDivisorValue16));
}

BENCHMARK(divisor_tput_divisor_of_u16, iters) {
  runIsDivisorOf(iters, kDividends16, kDivisor16);
}

BENCHMARK(div_gt1_tput_divisor_of_u16, iters) {
  runIsDivisorOf(iters, kDividends16, makeGt1Division(kDivisorValue16));
}

BENCHMARK(literal_tput_divisor_of_u16, iters) {
  runIsDivisorOf(iters, kDividends16, kLiteral16);
}

BENCHMARK(control_tput_divisor_of_u16, iters) {
  runControl(iters, kDividends16, kDivisorValue16);
}

BENCHMARK(natural_laty_divisor_of_u16, iters) {
  runIsDivisorOfLatency(
      iters, kDividends16, makeNaturalDivision(kDivisorValue16));
}

BENCHMARK(divisor_laty_divisor_of_u16, iters) {
  runIsDivisorOfLatency(iters, kDividends16, kDivisor16);
}

BENCHMARK(div_gt1_laty_divisor_of_u16, iters) {
  runIsDivisorOfLatency(iters, kDividends16, makeGt1Division(kDivisorValue16));
}

BENCHMARK(literal_laty_divisor_of_u16, iters) {
  runIsDivisorOfLatency(iters, kDividends16, kLiteral16);
}

BENCHMARK(control_laty_divisor_of_u16, iters) {
  runControlLatency(iters, kDividends16, kDivisorValue16);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(natural_tput_u32, iters) {
  runDivRem(iters, kDividends32, makeNaturalDivision(kDivisorValue32));
}

BENCHMARK(divisor_tput_u32, iters) {
  runDivRem(iters, kDividends32, kDivisor32);
}

BENCHMARK(div_gt1_tput_u32, iters) {
  runDivRem(iters, kDividends32, makeGt1Division(kDivisorValue32));
}

BENCHMARK(literal_tput_u32, iters) {
  runDivRem(iters, kDividends32, kLiteral32);
}

BENCHMARK(control_tput_u32, iters) {
  runControl(iters, kDividends32, kDivisorValue32);
}

BENCHMARK(natural_laty_u32, iters) {
  runDivRemLatency(iters, kDividends32, makeNaturalDivision(kDivisorValue32));
}

BENCHMARK(divisor_laty_u32, iters) {
  runDivRemLatency(iters, kDividends32, kDivisor32);
}

BENCHMARK(div_gt1_laty_u32, iters) {
  runDivRemLatency(iters, kDividends32, makeGt1Division(kDivisorValue32));
}

BENCHMARK(literal_laty_u32, iters) {
  runDivRemLatency(iters, kDividends32, kLiteral32);
}

BENCHMARK(control_laty_u32, iters) {
  runControlLatency(iters, kDividends32, kDivisorValue32);
}

BENCHMARK(natural_tput_div_u32, iters) {
  runDiv(iters, kDividends32, makeNaturalDivision(kDivisorValue32));
}

BENCHMARK(divisor_tput_div_u32, iters) {
  runDiv(iters, kDividends32, kDivisor32);
}

BENCHMARK(div_gt1_tput_div_u32, iters) {
  runDiv(iters, kDividends32, makeGt1Division(kDivisorValue32));
}

BENCHMARK(literal_tput_div_u32, iters) {
  runDiv(iters, kDividends32, kLiteral32);
}

BENCHMARK(control_tput_div_u32, iters) {
  runControl(iters, kDividends32, kDivisorValue32);
}

BENCHMARK(natural_laty_div_u32, iters) {
  runDivLatency(iters, kDividends32, makeNaturalDivision(kDivisorValue32));
}

BENCHMARK(divisor_laty_div_u32, iters) {
  runDivLatency(iters, kDividends32, kDivisor32);
}

BENCHMARK(div_gt1_laty_div_u32, iters) {
  runDivLatency(iters, kDividends32, makeGt1Division(kDivisorValue32));
}

BENCHMARK(literal_laty_div_u32, iters) {
  runDivLatency(iters, kDividends32, kLiteral32);
}

BENCHMARK(control_laty_div_u32, iters) {
  runControlLatency(iters, kDividends32, kDivisorValue32);
}

BENCHMARK(natural_tput_rem_u32, iters) {
  runRem(iters, kDividends32, makeNaturalDivision(kDivisorValue32));
}

BENCHMARK(divisor_tput_rem_u32, iters) {
  runRem(iters, kDividends32, kDivisor32);
}

BENCHMARK(div_gt1_tput_rem_u32, iters) {
  runRem(iters, kDividends32, makeGt1Division(kDivisorValue32));
}

BENCHMARK(literal_tput_rem_u32, iters) {
  runRem(iters, kDividends32, kLiteral32);
}

BENCHMARK(control_tput_rem_u32, iters) {
  runControl(iters, kDividends32, kDivisorValue32);
}

BENCHMARK(natural_laty_rem_u32, iters) {
  runRemLatency(iters, kDividends32, makeNaturalDivision(kDivisorValue32));
}

BENCHMARK(divisor_laty_rem_u32, iters) {
  runRemLatency(iters, kDividends32, kDivisor32);
}

BENCHMARK(div_gt1_laty_rem_u32, iters) {
  runRemLatency(iters, kDividends32, makeGt1Division(kDivisorValue32));
}

BENCHMARK(literal_laty_rem_u32, iters) {
  runRemLatency(iters, kDividends32, kLiteral32);
}

BENCHMARK(control_laty_rem_u32, iters) {
  runControlLatency(iters, kDividends32, kDivisorValue32);
}

BENCHMARK(natural_tput_divisor_of_u32, iters) {
  runIsDivisorOf(iters, kDividends32, makeNaturalDivision(kDivisorValue32));
}

BENCHMARK(divisor_tput_divisor_of_u32, iters) {
  runIsDivisorOf(iters, kDividends32, kDivisor32);
}

BENCHMARK(div_gt1_tput_divisor_of_u32, iters) {
  runIsDivisorOf(iters, kDividends32, makeGt1Division(kDivisorValue32));
}

BENCHMARK(literal_tput_divisor_of_u32, iters) {
  runIsDivisorOf(iters, kDividends32, kLiteral32);
}

BENCHMARK(control_tput_divisor_of_u32, iters) {
  runControl(iters, kDividends32, kDivisorValue32);
}

BENCHMARK(natural_laty_divisor_of_u32, iters) {
  runIsDivisorOfLatency(
      iters, kDividends32, makeNaturalDivision(kDivisorValue32));
}

BENCHMARK(divisor_laty_divisor_of_u32, iters) {
  runIsDivisorOfLatency(iters, kDividends32, kDivisor32);
}

BENCHMARK(div_gt1_laty_divisor_of_u32, iters) {
  runIsDivisorOfLatency(iters, kDividends32, makeGt1Division(kDivisorValue32));
}

BENCHMARK(literal_laty_divisor_of_u32, iters) {
  runIsDivisorOfLatency(iters, kDividends32, kLiteral32);
}

BENCHMARK(control_laty_divisor_of_u32, iters) {
  runControlLatency(iters, kDividends32, kDivisorValue32);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(natural_tput_u64, iters) {
  runDivRem(iters, kDividends64, makeNaturalDivision(kDivisorValue64));
}

BENCHMARK(divisor_tput_u64, iters) {
  runDivRem(iters, kDividends64, kDivisor64);
}

BENCHMARK(div_gt1_tput_u64, iters) {
  runDivRem(iters, kDividends64, makeGt1Division(kDivisorValue64));
}

BENCHMARK(literal_tput_u64, iters) {
  runDivRem(iters, kDividends64, kLiteral64);
}

BENCHMARK(control_tput_u64, iters) {
  runControl(iters, kDividends64, kDivisorValue64);
}

BENCHMARK(natural_laty_u64, iters) {
  runDivRemLatency(iters, kDividends64, makeNaturalDivision(kDivisorValue64));
}

BENCHMARK(divisor_laty_u64, iters) {
  runDivRemLatency(iters, kDividends64, kDivisor64);
}

BENCHMARK(div_gt1_laty_u64, iters) {
  runDivRemLatency(iters, kDividends64, makeGt1Division(kDivisorValue64));
}

BENCHMARK(literal_laty_u64, iters) {
  runDivRemLatency(iters, kDividends64, kLiteral64);
}

BENCHMARK(control_laty_u64, iters) {
  runControlLatency(iters, kDividends64, kDivisorValue64);
}

BENCHMARK(natural_tput_div_u64, iters) {
  runDiv(iters, kDividends64, makeNaturalDivision(kDivisorValue64));
}

BENCHMARK(divisor_tput_div_u64, iters) {
  runDiv(iters, kDividends64, kDivisor64);
}

BENCHMARK(div_gt1_tput_div_u64, iters) {
  runDiv(iters, kDividends64, makeGt1Division(kDivisorValue64));
}

BENCHMARK(literal_tput_div_u64, iters) {
  runDiv(iters, kDividends64, kLiteral64);
}

BENCHMARK(control_tput_div_u64, iters) {
  runControl(iters, kDividends64, kDivisorValue64);
}

BENCHMARK(natural_laty_div_u64, iters) {
  runDivLatency(iters, kDividends64, makeNaturalDivision(kDivisorValue64));
}

BENCHMARK(divisor_laty_div_u64, iters) {
  runDivLatency(iters, kDividends64, kDivisor64);
}

BENCHMARK(div_gt1_laty_div_u64, iters) {
  runDivLatency(iters, kDividends64, makeGt1Division(kDivisorValue64));
}

BENCHMARK(literal_laty_div_u64, iters) {
  runDivLatency(iters, kDividends64, kLiteral64);
}

BENCHMARK(control_laty_div_u64, iters) {
  runControlLatency(iters, kDividends64, kDivisorValue64);
}

BENCHMARK(natural_tput_rem_u64, iters) {
  runRem(iters, kDividends64, makeNaturalDivision(kDivisorValue64));
}

BENCHMARK(divisor_tput_rem_u64, iters) {
  runRem(iters, kDividends64, kDivisor64);
}

BENCHMARK(div_gt1_tput_rem_u64, iters) {
  runRem(iters, kDividends64, makeGt1Division(kDivisorValue64));
}

BENCHMARK(literal_tput_rem_u64, iters) {
  runRem(iters, kDividends64, kLiteral64);
}

BENCHMARK(control_tput_rem_u64, iters) {
  runControl(iters, kDividends64, kDivisorValue64);
}

BENCHMARK(natural_laty_rem_u64, iters) {
  runRemLatency(iters, kDividends64, makeNaturalDivision(kDivisorValue64));
}

BENCHMARK(divisor_laty_rem_u64, iters) {
  runRemLatency(iters, kDividends64, kDivisor64);
}

BENCHMARK(div_gt1_laty_rem_u64, iters) {
  runRemLatency(iters, kDividends64, makeGt1Division(kDivisorValue64));
}

BENCHMARK(literal_laty_rem_u64, iters) {
  runRemLatency(iters, kDividends64, kLiteral64);
}

BENCHMARK(control_laty_rem_u64, iters) {
  runControlLatency(iters, kDividends64, kDivisorValue64);
}

BENCHMARK(natural_tput_divisor_of_u64, iters) {
  runIsDivisorOf(iters, kDividends64, makeNaturalDivision(kDivisorValue64));
}

BENCHMARK(divisor_tput_divisor_of_u64, iters) {
  runIsDivisorOf(iters, kDividends64, kDivisor64);
}

BENCHMARK(div_gt1_tput_divisor_of_u64, iters) {
  runIsDivisorOf(iters, kDividends64, makeGt1Division(kDivisorValue64));
}

BENCHMARK(literal_tput_divisor_of_u64, iters) {
  runIsDivisorOf(iters, kDividends64, kLiteral64);
}

BENCHMARK(control_tput_divisor_of_u64, iters) {
  runControl(iters, kDividends64, kDivisorValue64);
}

BENCHMARK(natural_laty_divisor_of_u64, iters) {
  runIsDivisorOfLatency(
      iters, kDividends64, makeNaturalDivision(kDivisorValue64));
}

BENCHMARK(divisor_laty_divisor_of_u64, iters) {
  runIsDivisorOfLatency(iters, kDividends64, kDivisor64);
}

BENCHMARK(div_gt1_laty_divisor_of_u64, iters) {
  runIsDivisorOfLatency(iters, kDividends64, makeGt1Division(kDivisorValue64));
}

BENCHMARK(literal_laty_divisor_of_u64, iters) {
  runIsDivisorOfLatency(iters, kDividends64, kLiteral64);
}

BENCHMARK(control_laty_divisor_of_u64, iters) {
  runControlLatency(iters, kDividends64, kDivisorValue64);
}

int main(int const argc, char** const argv) {
  int mutableArgc = argc;
  char** mutableArgv = argv;
  folly::Init init(&mutableArgc, &mutableArgv);
  folly::runBenchmarks();
  return 0;
}
