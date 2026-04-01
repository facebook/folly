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

// Benchmark comparing character class test strategies across all three
// execution engines (backtracking, NFA, DFA).

#include <string>

#include <folly/Benchmark.h>
#include <folly/BenchmarkUtil.h>
#include <folly/regex/Regex.h>
#include <folly/regex/detail/CharClass.h>

using namespace folly::regex;
using namespace folly::regex::detail;

namespace {

const std::string kAlphaNum = "Hello123World456Test789End";
const std::string kDigits = "9876543210123456789";
const std::string kEmail = "user.name+tag@sub.example.com";
const std::string kMixed = "abc 123 def 456 ghi 789";
const std::string kWord = "hello_world_foo_bar_baz";
const std::string kLong = [] {
  std::string s;
  for (int i = 0; i < 100; ++i) {
    s += "abcXYZ123";
  }
  return s;
}();

// --- Micro-benchmarks: raw character test throughput ---

// Range-based test (sorted, flat pool style)
static constexpr CharRange kRangesAlphaNum[] = {
    {'0', '9'},
    {'A', 'Z'},
    {'a', 'z'},
};

// Compact bitmap (covers [48-122], 2 words)
static constexpr auto kCompactAlphaNum = [] {
  struct {
    uint64_t words[2] = {};
    unsigned char lo = '0';
    int word_count = 2;
  } bm;
  setRangeBits(bm.words, bm.lo, '0', '9');
  setRangeBits(bm.words, bm.lo, 'A', 'Z');
  setRangeBits(bm.words, bm.lo, 'a', 'z');
  return bm;
}();

static constexpr CharRange kRangesDigit[] = {{'0', '9'}};

BENCHMARK(MicroRangeTest_AlphaNum, n) {
  const auto& input = kAlphaNum;
  for (std::size_t i = 0; i < n; ++i) {
    int count = 0;
    for (char c : input) {
      count += charClassTest(kRangesAlphaNum, 3, false, c);
    }
    folly::doNotOptimizeAway(count);
  }
}

BENCHMARK_RELATIVE(MicroCompactBitmapTest_AlphaNum, n) {
  const auto& input = kAlphaNum;
  for (std::size_t i = 0; i < n; ++i) {
    int count = 0;
    for (char c : input) {
      count += compactBitmapTest(
          kCompactAlphaNum.words,
          kCompactAlphaNum.lo,
          kCompactAlphaNum.word_count,
          false,
          c);
    }
    folly::doNotOptimizeAway(count);
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(MicroRangeTest_Digit, n) {
  const auto& input = kDigits;
  for (std::size_t i = 0; i < n; ++i) {
    int count = 0;
    for (char c : input) {
      count += charClassTest(kRangesDigit, 1, false, c);
    }
    folly::doNotOptimizeAway(count);
  }
}

BENCHMARK_DRAW_LINE();

// === Full regex match benchmarks: [a-zA-Z0-9]+ ===

BENCHMARK(AlphaNumMatch_Backtrack, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(
        match<"[a-zA-Z0-9]+", Flags::ForceBacktracking>(kAlphaNum));
  }
}

BENCHMARK_RELATIVE(AlphaNumMatch_NFA, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(match<"[a-zA-Z0-9]+", Flags::ForceNFA>(kAlphaNum));
  }
}

BENCHMARK_RELATIVE(AlphaNumMatch_DFA, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(match<"[a-zA-Z0-9]+", Flags::ForceDFA>(kAlphaNum));
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(DigitMatch_Backtrack, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(
        match<R"(\d+)", Flags::ForceBacktracking>(kDigits));
  }
}

BENCHMARK_RELATIVE(DigitMatch_NFA, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(match<R"(\d+)", Flags::ForceNFA>(kDigits));
  }
}

BENCHMARK_RELATIVE(DigitMatch_DFA, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(match<R"(\d+)", Flags::ForceDFA>(kDigits));
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(EmailSearch_Backtrack, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(
        search<R"(\w+@\w+\.\w+)", Flags::ForceBacktracking>(kEmail));
  }
}

BENCHMARK_RELATIVE(EmailSearch_NFA, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(
        search<R"(\w+@\w+\.\w+)", Flags::ForceNFA>(kEmail));
  }
}

BENCHMARK_RELATIVE(EmailSearch_DFA, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(
        search<R"(\w+@\w+\.\w+)", Flags::ForceDFA>(kEmail));
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(LongAlphaNumMatch_Backtrack, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(
        match<"[a-zA-Z0-9]+", Flags::ForceBacktracking>(kLong));
  }
}

BENCHMARK_RELATIVE(LongAlphaNumMatch_NFA, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(match<"[a-zA-Z0-9]+", Flags::ForceNFA>(kLong));
  }
}

BENCHMARK_RELATIVE(LongAlphaNumMatch_DFA, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(match<"[a-zA-Z0-9]+", Flags::ForceDFA>(kLong));
  }
}

BENCHMARK_DRAW_LINE();

BENCHMARK(WordMatch_Backtrack, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(match<R"(\w+)", Flags::ForceBacktracking>(kWord));
  }
}

BENCHMARK_RELATIVE(WordMatch_NFA, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(match<R"(\w+)", Flags::ForceNFA>(kWord));
  }
}

BENCHMARK_RELATIVE(WordMatch_DFA, n) {
  for (std::size_t i = 0; i < n; ++i) {
    folly::doNotOptimizeAway(match<R"(\w+)", Flags::ForceDFA>(kWord));
  }
}

} // namespace

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
