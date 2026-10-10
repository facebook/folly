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

// Shared RAII wrappers and benchmark macros for regex engine comparisons.
// Provides PcreCtx/Pcre2Ctx wrappers and composable benchmark macros
// for Folly, RE2, CTRE, PCRE, PCRE2, std::regex, and boost::regex.

#pragma once

#include <cstddef>
#include <exception>
#include <iostream> // NOLINT(facebook-hte-ConditionalBadInclude-iostream) used in macros below
#include <memory>
#include <regex> // NOLINT(facebook-hte-BadInclude-regex) benchmark comparison
#include <string_view>

#if __has_include(<pcre.h>)
#include <pcre.h>
#define FOLLY_REGEX_BENCH_HAS_PCRE 1
#endif

#if __has_include(<boost/regex.hpp>)
#include <boost/regex.hpp> // NOLINT(facebook-hte-BadInclude-boost/regex.hpp) benchmark comparison
#define FOLLY_REGEX_BENCH_HAS_BOOST_REGEX 1
#endif

#if __has_include(<ctre.hpp>)
#include <ctre.hpp>
#define FOLLY_REGEX_BENCH_HAS_CTRE 1
#endif

#if __has_include(<re2/re2.h>)
#include <re2/re2.h>
#define FOLLY_REGEX_BENCH_HAS_RE2 1
#endif

#include <folly/Benchmark.h>
#include <folly/BenchmarkUtil.h>
#include <folly/regex/Regex.h>

#if __has_include(<pcre2.h>)
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h> // @manual=fbsource//third-party/pcre2:pcre2-8
#define FOLLY_REGEX_BENCH_HAS_PCRE2 1
#endif

namespace folly::regex::bench {

#ifdef FOLLY_REGEX_BENCH_HAS_PCRE
struct PcreCtx {
  pcre* searchRe = nullptr;
  pcre_extra* searchExtra = nullptr;
  pcre* matchRe = nullptr;
  pcre_extra* matchExtra = nullptr;

  void compile(const char* pattern, bool jit) {
    const char* error;
    int erroffset;
    searchRe = pcre_compile(pattern, 0, &error, &erroffset, nullptr);
    if (jit) {
      searchExtra = pcre_study(searchRe, PCRE_STUDY_JIT_COMPILE, &error);
    }
    // Wrap in \A(?:...)\z for full-match: forces PCRE to backtrack through
    // alternatives until one consumes the entire input.
    std::string matchPat = "\\A(?:";
    matchPat += pattern;
    matchPat += ")\\z";
    matchRe = pcre_compile(matchPat.c_str(), 0, &error, &erroffset, nullptr);
    if (jit) {
      matchExtra = pcre_study(matchRe, PCRE_STUDY_JIT_COMPILE, &error);
    }
  }

  ~PcreCtx() {
    if (searchExtra) {
      pcre_free_study(searchExtra);
    }
    if (searchRe) {
      pcre_free(searchRe);
    }
    if (matchExtra) {
      pcre_free_study(matchExtra);
    }
    if (matchRe) {
      pcre_free(matchRe);
    }
  }

  bool search(std::string_view input) const {
    int ov[30];
    return pcre_exec(
               searchRe,
               searchExtra,
               input.data(),
               static_cast<int>(input.size()),
               0,
               0,
               ov,
               30) >= 0;
  }

  bool match(std::string_view input) const {
    int ov[30];
    // matchRe is compiled with \A(?:...)\z anchors so PCRE backtracks
    // through alternatives until one consumes the full input.
    return pcre_exec(
               matchRe,
               matchExtra,
               input.data(),
               static_cast<int>(input.size()),
               0,
               0,
               ov,
               30) >= 0;
  }
};
#endif // FOLLY_REGEX_BENCH_HAS_PCRE

#ifdef FOLLY_REGEX_BENCH_HAS_PCRE2
struct Pcre2Ctx {
  pcre2_code* searchRe = nullptr;
  pcre2_match_data* searchMd = nullptr;
  pcre2_code* matchRe = nullptr;
  pcre2_match_data* matchMd = nullptr;
  pcre2_match_context* mctx = nullptr;
  pcre2_jit_stack* jstack = nullptr;

  void compile(const char* pattern, bool jit) {
    int ec;
    PCRE2_SIZE eo;
    searchRe = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(pattern),
        PCRE2_ZERO_TERMINATED,
        0,
        &ec,
        &eo,
        nullptr);
    searchMd = pcre2_match_data_create_from_pattern(searchRe, nullptr);
    // Wrap in \A(?:...)\z for full-match: forces PCRE2 to backtrack through
    // alternatives until one consumes the entire input.
    std::string matchPat = "\\A(?:";
    matchPat += pattern;
    matchPat += ")\\z";
    matchRe = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(matchPat.c_str()),
        PCRE2_ZERO_TERMINATED,
        0,
        &ec,
        &eo,
        nullptr);
    matchMd = pcre2_match_data_create_from_pattern(matchRe, nullptr);
    if (jit) {
      if (pcre2_jit_compile(searchRe, PCRE2_JIT_COMPLETE) == 0) {
        jstack = pcre2_jit_stack_create(32768, 524288, nullptr);
        mctx = pcre2_match_context_create(nullptr);
        pcre2_jit_stack_assign(mctx, nullptr, jstack);
      }
      pcre2_jit_compile(matchRe, PCRE2_JIT_COMPLETE);
    }
  }

  ~Pcre2Ctx() {
    if (searchMd) {
      pcre2_match_data_free(searchMd);
    }
    if (matchMd) {
      pcre2_match_data_free(matchMd);
    }
    if (mctx) {
      pcre2_match_context_free(mctx);
    }
    if (jstack) {
      pcre2_jit_stack_free(jstack);
    }
    if (searchRe) {
      pcre2_code_free(searchRe);
    }
    if (matchRe) {
      pcre2_code_free(matchRe);
    }
  }

  bool search(std::string_view input) const {
    return pcre2_match(
               searchRe,
               reinterpret_cast<PCRE2_SPTR>(input.data()),
               input.size(),
               0,
               0,
               searchMd,
               mctx) >= 0;
  }

  bool match(std::string_view input) const {
    // matchRe is compiled with \A(?:...)\z anchors so PCRE2 backtracks
    // through alternatives until one consumes the full input.
    return pcre2_match(
               matchRe,
               reinterpret_cast<PCRE2_SPTR>(input.data()),
               input.size(),
               0,
               0,
               matchMd,
               mctx) >= 0;
  }
};
#endif // FOLLY_REGEX_BENCH_HAS_PCRE2

} // namespace folly::regex::bench

// ============================================================================
// Benchmark macros for regex engine comparisons.
//
// MATCH_BENCH(Name, Pattern, Input) — anchored full-match across all engines
// SEARCH_BENCH(Name, Pattern, Input) — unanchored search across all engines
//
// For patterns that cause exponential backtracking or stack overflow in
// std::regex and boost::regex, use the _NO_RECURSIVE variants which skip
// those engines and print a message explaining why:
//
// MATCH_BENCH_NO_RECURSIVE(Name, Pattern, Input, Reason)
// SEARCH_BENCH_NO_RECURSIVE(Name, Pattern, Input, Reason)
//
// Composable per-engine macros (BENCH_RE2_MATCH, BENCH_ALL_PCRE_MATCH, etc.)
// are also available for building custom benchmark groups.
// ============================================================================

// clang-format off

// Maximum input size for engines that use recursive backtracking internally.
// std::regex and boost::regex stack-overflow on large inputs with quantifiers
// like '+' because they recurse once per character matched.
inline constexpr std::size_t kRecursiveEngineMaxInput = 8192;

// --- Per-engine composable macros ---

#ifdef FOLLY_REGEX_BENCH_HAS_RE2

#define BENCH_RE2_MATCH(NAME, PATTERN, INPUT)                                  \
  BENCHMARK_RELATIVE(RE2_##NAME, n) {                                          \
    std::unique_ptr<re2::RE2> re;                                              \
    BENCHMARK_SUSPEND { re = std::make_unique<re2::RE2>(PATTERN); }            \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re2::RE2::FullMatch(INPUT, *re));               \
    }                                                                          \
  }

#define BENCH_RE2_SEARCH(NAME, PATTERN, INPUT)                                 \
  BENCHMARK_RELATIVE(RE2_##NAME, n) {                                          \
    std::unique_ptr<re2::RE2> re;                                              \
    BENCHMARK_SUSPEND { re = std::make_unique<re2::RE2>(PATTERN); }            \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re2::RE2::PartialMatch(INPUT, *re));            \
    }                                                                          \
  }

#else // !FOLLY_REGEX_BENCH_HAS_RE2

#define BENCH_RE2_MATCH(NAME, PATTERN, INPUT)
#define BENCH_RE2_SEARCH(NAME, PATTERN, INPUT)

#endif // FOLLY_REGEX_BENCH_HAS_RE2

#ifdef FOLLY_REGEX_BENCH_HAS_PCRE

#define BENCH_PCRE_MATCH(NAME, PATTERN, INPUT)                                 \
  BENCHMARK_RELATIVE(PCRE_##NAME, n) {                                         \
    folly::regex::bench::PcreCtx ctx;                                          \
    BENCHMARK_SUSPEND { ctx.compile(PATTERN, false); }                         \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctx.match(INPUT));                              \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(PCRE_JIT_##NAME, n) {                                     \
    folly::regex::bench::PcreCtx ctx;                                          \
    BENCHMARK_SUSPEND { ctx.compile(PATTERN, true); }                          \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctx.match(INPUT));                              \
    }                                                                          \
  }

#define BENCH_PCRE_SEARCH(NAME, PATTERN, INPUT)                                \
  BENCHMARK_RELATIVE(PCRE_##NAME, n) {                                         \
    folly::regex::bench::PcreCtx ctx;                                          \
    BENCHMARK_SUSPEND { ctx.compile(PATTERN, false); }                         \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctx.search(INPUT));                             \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(PCRE_JIT_##NAME, n) {                                     \
    folly::regex::bench::PcreCtx ctx;                                          \
    BENCHMARK_SUSPEND { ctx.compile(PATTERN, true); }                          \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctx.search(INPUT));                             \
    }                                                                          \
  }

#else // !FOLLY_REGEX_BENCH_HAS_PCRE

#define BENCH_PCRE_MATCH(NAME, PATTERN, INPUT)
#define BENCH_PCRE_SEARCH(NAME, PATTERN, INPUT)

#endif // FOLLY_REGEX_BENCH_HAS_PCRE

#ifdef FOLLY_REGEX_BENCH_HAS_PCRE2

#define BENCH_PCRE2_MATCH(NAME, PATTERN, INPUT)                                \
  BENCHMARK_RELATIVE(PCRE2_##NAME, n) {                                        \
    folly::regex::bench::Pcre2Ctx ctx;                                         \
    BENCHMARK_SUSPEND { ctx.compile(PATTERN, false); }                         \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctx.match(INPUT));                              \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(PCRE2_JIT_##NAME, n) {                                    \
    folly::regex::bench::Pcre2Ctx ctx;                                         \
    BENCHMARK_SUSPEND { ctx.compile(PATTERN, true); }                          \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctx.match(INPUT));                              \
    }                                                                          \
  }

#define BENCH_PCRE2_SEARCH(NAME, PATTERN, INPUT)                               \
  BENCHMARK_RELATIVE(PCRE2_##NAME, n) {                                        \
    folly::regex::bench::Pcre2Ctx ctx;                                         \
    BENCHMARK_SUSPEND { ctx.compile(PATTERN, false); }                         \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctx.search(INPUT));                             \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(PCRE2_JIT_##NAME, n) {                                    \
    folly::regex::bench::Pcre2Ctx ctx;                                         \
    BENCHMARK_SUSPEND { ctx.compile(PATTERN, true); }                          \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctx.search(INPUT));                             \
    }                                                                          \
  }

#else // !FOLLY_REGEX_BENCH_HAS_PCRE2

#define BENCH_PCRE2_MATCH(NAME, PATTERN, INPUT)
#define BENCH_PCRE2_SEARCH(NAME, PATTERN, INPUT)

#endif // FOLLY_REGEX_BENCH_HAS_PCRE2

#if defined(FOLLY_REGEX_BENCH_HAS_PCRE) && defined(FOLLY_REGEX_BENCH_HAS_PCRE2)
#define BENCH_ALL_PCRE_MATCH(NAME, PATTERN, INPUT)                             \
  BENCH_PCRE_MATCH(NAME, PATTERN, INPUT)                                       \
  BENCH_PCRE2_MATCH(NAME, PATTERN, INPUT)
#define BENCH_ALL_PCRE_SEARCH(NAME, PATTERN, INPUT)                            \
  BENCH_PCRE_SEARCH(NAME, PATTERN, INPUT)                                      \
  BENCH_PCRE2_SEARCH(NAME, PATTERN, INPUT)
#elif defined(FOLLY_REGEX_BENCH_HAS_PCRE)
#define BENCH_ALL_PCRE_MATCH(NAME, PATTERN, INPUT)                             \
  BENCH_PCRE_MATCH(NAME, PATTERN, INPUT)
#define BENCH_ALL_PCRE_SEARCH(NAME, PATTERN, INPUT)                            \
  BENCH_PCRE_SEARCH(NAME, PATTERN, INPUT)
#elif defined(FOLLY_REGEX_BENCH_HAS_PCRE2)
#define BENCH_ALL_PCRE_MATCH(NAME, PATTERN, INPUT)                             \
  BENCH_PCRE2_MATCH(NAME, PATTERN, INPUT)
#define BENCH_ALL_PCRE_SEARCH(NAME, PATTERN, INPUT)                            \
  BENCH_PCRE2_SEARCH(NAME, PATTERN, INPUT)
#else
#define BENCH_ALL_PCRE_MATCH(NAME, PATTERN, INPUT)
#define BENCH_ALL_PCRE_SEARCH(NAME, PATTERN, INPUT)
#endif

// --- Folly engine entries (shared between all macros) ---

#define BENCH_FOLLY_MATCH(NAME, PATTERN, INPUT)                                \
  BENCHMARK(FollyRegex_##NAME, n) {                                            \
    constexpr auto re = folly::regex::compile<PATTERN>();                       \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.match(INPUT));                               \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_Rev_##NAME, n) {                               \
    constexpr auto re = folly::regex::compile<                                 \
        PATTERN, folly::regex::Flags::ForceReverseExecution>();                 \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.match(INPUT));                               \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_Backtrack_##NAME, n) {                         \
    constexpr auto re = folly::regex::compile<                                 \
        PATTERN, folly::regex::Flags::ForceBacktracking>();                     \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.match(INPUT));                               \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_Backtrack_Rev_##NAME, n) {                     \
    constexpr auto re = folly::regex::compile<                                 \
        PATTERN,                                                               \
        folly::regex::Flags::ForceBacktracking |                               \
            folly::regex::Flags::ForceReverseExecution>();                      \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.match(INPUT));                               \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_NFA_##NAME, n) {                               \
    constexpr auto re =                                                        \
        folly::regex::compile<PATTERN, folly::regex::Flags::ForceNFA>();        \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.match(INPUT));                               \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_NFA_Rev_##NAME, n) {                           \
    constexpr auto re = folly::regex::compile<                                 \
        PATTERN,                                                               \
        folly::regex::Flags::ForceNFA |                                        \
            folly::regex::Flags::ForceReverseExecution>();                      \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.match(INPUT));                               \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_DFA_##NAME, n) {                               \
    constexpr auto re =                                                        \
        folly::regex::compile<PATTERN, folly::regex::Flags::ForceDFA>();        \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.match(INPUT));                               \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_DFA_Rev_##NAME, n) {                           \
    constexpr auto re = folly::regex::compile<                                 \
        PATTERN,                                                               \
        folly::regex::Flags::ForceDFA |                                        \
            folly::regex::Flags::ForceReverseExecution>();                      \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.match(INPUT));                               \
    }                                                                          \
  }

#define BENCH_FOLLY_SEARCH(NAME, PATTERN, INPUT)                               \
  BENCHMARK(FollyRegex_##NAME, n) {                                            \
    constexpr auto re = folly::regex::compile<PATTERN>();                       \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.search(INPUT));                              \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_Rev_##NAME, n) {                               \
    constexpr auto re = folly::regex::compile<                                 \
        PATTERN, folly::regex::Flags::ForceReverseExecution>();                 \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.search(INPUT));                              \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_Backtrack_##NAME, n) {                         \
    constexpr auto re = folly::regex::compile<                                 \
        PATTERN, folly::regex::Flags::ForceBacktracking>();                     \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.search(INPUT));                              \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_Backtrack_Rev_##NAME, n) {                     \
    constexpr auto re = folly::regex::compile<                                 \
        PATTERN,                                                               \
        folly::regex::Flags::ForceBacktracking |                               \
            folly::regex::Flags::ForceReverseExecution>();                      \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.search(INPUT));                              \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_NFA_##NAME, n) {                               \
    constexpr auto re =                                                        \
        folly::regex::compile<PATTERN, folly::regex::Flags::ForceNFA>();        \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.search(INPUT));                              \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_NFA_Rev_##NAME, n) {                           \
    constexpr auto re = folly::regex::compile<                                 \
        PATTERN,                                                               \
        folly::regex::Flags::ForceNFA |                                        \
            folly::regex::Flags::ForceReverseExecution>();                      \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.search(INPUT));                              \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_DFA_##NAME, n) {                               \
    constexpr auto re =                                                        \
        folly::regex::compile<PATTERN, folly::regex::Flags::ForceDFA>();        \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.search(INPUT));                              \
    }                                                                          \
  }                                                                            \
  BENCHMARK_RELATIVE(FollyRegex_DFA_Rev_##NAME, n) {                           \
    constexpr auto re = folly::regex::compile<                                 \
        PATTERN,                                                               \
        folly::regex::Flags::ForceDFA |                                        \
            folly::regex::Flags::ForceReverseExecution>();                      \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(re.search(INPUT));                              \
    }                                                                          \
  }

// --- std::regex and boost::regex entries with safety guards ---

#define BENCH_STDREGEX_MATCH(NAME, PATTERN, INPUT)                             \
  BENCHMARK_RELATIVE(StdRegex_##NAME, n) {                                     \
    std::regex re(PATTERN);                                                    \
    const auto& input = (INPUT);                                               \
    if (input.size() > kRecursiveEngineMaxInput) {                             \
      static bool printed = false;                                             \
      if (!printed) {                                                          \
        std::cerr << "StdRegex_" #NAME                                        \
                  << ": SKIPPED (input too large for recursive engine)"        \
                  << std::endl;                                                \
        printed = true;                                                        \
      }                                                                        \
      return;                                                                  \
    }                                                                          \
    for (unsigned i = 0; i < n; ++i) {                                         \
      try {                                                                    \
        folly::doNotOptimizeAway(std::regex_match(input, re));                 \
      } catch (const std::exception& e) {                                     \
        static bool catchPrinted = false;                                      \
        if (!catchPrinted) {                                                   \
          std::cerr << "StdRegex_" #NAME ": " << e.what() << std::endl;       \
          catchPrinted = true;                                                 \
        }                                                                      \
        return;                                                                \
      }                                                                        \
    }                                                                          \
  }

#ifdef FOLLY_REGEX_BENCH_HAS_BOOST_REGEX

#define BENCH_BOOSTREGEX_MATCH(NAME, PATTERN, INPUT)                           \
  BENCHMARK_RELATIVE(BoostRegex_##NAME, n) {                                   \
    boost::regex re(PATTERN);                                                  \
    const auto& input = (INPUT);                                               \
    if (input.size() > kRecursiveEngineMaxInput) {                             \
      static bool printed = false;                                             \
      if (!printed) {                                                          \
        std::cerr << "BoostRegex_" #NAME                                      \
                  << ": SKIPPED (input too large for recursive engine)"        \
                  << std::endl;                                                \
        printed = true;                                                        \
      }                                                                        \
      return;                                                                  \
    }                                                                          \
    for (unsigned i = 0; i < n; ++i) {                                         \
      try {                                                                    \
        folly::doNotOptimizeAway(boost::regex_match(input, re));               \
      } catch (const std::exception& e) {                                     \
        static bool catchPrinted = false;                                      \
        if (!catchPrinted) {                                                   \
          std::cerr << "BoostRegex_" #NAME ": " << e.what() << std::endl;     \
          catchPrinted = true;                                                 \
        }                                                                      \
        return;                                                                \
      }                                                                        \
    }                                                                          \
  }

#define BENCH_BOOSTREGEX_SEARCH(NAME, PATTERN, INPUT)                          \
  BENCHMARK_RELATIVE(BoostRegex_##NAME, n) {                                   \
    boost::regex re(PATTERN);                                                  \
    const auto& input = (INPUT);                                               \
    if (input.size() > kRecursiveEngineMaxInput) {                             \
      static bool printed = false;                                             \
      if (!printed) {                                                          \
        std::cerr << "BoostRegex_" #NAME                                      \
                  << ": SKIPPED (input too large for recursive engine)"        \
                  << std::endl;                                                \
        printed = true;                                                        \
      }                                                                        \
      return;                                                                  \
    }                                                                          \
    boost::smatch m;                                                           \
    for (unsigned i = 0; i < n; ++i) {                                         \
      try {                                                                    \
        folly::doNotOptimizeAway(boost::regex_search(input, m, re));           \
      } catch (const std::exception& e) {                                     \
        static bool catchPrinted = false;                                      \
        if (!catchPrinted) {                                                   \
          std::cerr << "BoostRegex_" #NAME ": " << e.what() << std::endl;     \
          catchPrinted = true;                                                 \
        }                                                                      \
        return;                                                                \
      }                                                                        \
    }                                                                          \
  }

#else // !FOLLY_REGEX_BENCH_HAS_BOOST_REGEX

#define BENCH_BOOSTREGEX_MATCH(NAME, PATTERN, INPUT)
#define BENCH_BOOSTREGEX_SEARCH(NAME, PATTERN, INPUT)

#endif // FOLLY_REGEX_BENCH_HAS_BOOST_REGEX

#define BENCH_STDREGEX_SEARCH(NAME, PATTERN, INPUT)                            \
  BENCHMARK_RELATIVE(StdRegex_##NAME, n) {                                     \
    std::regex re(PATTERN);                                                    \
    const auto& input = (INPUT);                                               \
    if (input.size() > kRecursiveEngineMaxInput) {                             \
      static bool printed = false;                                             \
      if (!printed) {                                                          \
        std::cerr << "StdRegex_" #NAME                                        \
                  << ": SKIPPED (input too large for recursive engine)"        \
                  << std::endl;                                                \
        printed = true;                                                        \
      }                                                                        \
      return;                                                                  \
    }                                                                          \
    std::smatch m;                                                             \
    for (unsigned i = 0; i < n; ++i) {                                         \
      try {                                                                    \
        folly::doNotOptimizeAway(std::regex_search(input, m, re));             \
      } catch (const std::exception& e) {                                     \
        static bool catchPrinted = false;                                      \
        if (!catchPrinted) {                                                   \
          std::cerr << "StdRegex_" #NAME ": " << e.what() << std::endl;       \
          catchPrinted = true;                                                 \
        }                                                                      \
        return;                                                                \
      }                                                                        \
    }                                                                          \
  }

// --- Skipped entries for engines that can't handle the pattern ---

#define BENCH_STDREGEX_MATCH_SKIP(NAME, REASON)                                \
  BENCHMARK_RELATIVE(StdRegex_##NAME, n) {                                     \
    (void)n;                                                                   \
    static bool printed = false;                                               \
    if (!printed) {                                                            \
      std::cerr << "StdRegex_" #NAME ": SKIPPED (" REASON ")" << std::endl;   \
      printed = true;                                                          \
    }                                                                          \
  }

#ifdef FOLLY_REGEX_BENCH_HAS_BOOST_REGEX

#define BENCH_BOOSTREGEX_MATCH_SKIP(NAME, REASON)                              \
  BENCHMARK_RELATIVE(BoostRegex_##NAME, n) {                                   \
    (void)n;                                                                   \
    static bool printed = false;                                               \
    if (!printed) {                                                            \
      std::cerr << "BoostRegex_" #NAME ": SKIPPED (" REASON ")" << std::endl; \
      printed = true;                                                          \
    }                                                                          \
  }

#define BENCH_BOOSTREGEX_SEARCH_SKIP(NAME, REASON)                             \
  BENCHMARK_RELATIVE(BoostRegex_##NAME, n) {                                   \
    (void)n;                                                                   \
    static bool printed = false;                                               \
    if (!printed) {                                                            \
      std::cerr << "BoostRegex_" #NAME ": SKIPPED (" REASON ")" << std::endl; \
      printed = true;                                                          \
    }                                                                          \
  }

#else // !FOLLY_REGEX_BENCH_HAS_BOOST_REGEX

#define BENCH_BOOSTREGEX_MATCH_SKIP(NAME, REASON)
#define BENCH_BOOSTREGEX_SEARCH_SKIP(NAME, REASON)

#endif // FOLLY_REGEX_BENCH_HAS_BOOST_REGEX

#define BENCH_STDREGEX_SEARCH_SKIP(NAME, REASON)                               \
  BENCHMARK_RELATIVE(StdRegex_##NAME, n) {                                     \
    (void)n;                                                                   \
    static bool printed = false;                                               \
    if (!printed) {                                                            \
      std::cerr << "StdRegex_" #NAME ": SKIPPED (" REASON ")" << std::endl;   \
      printed = true;                                                          \
    }                                                                          \
  }

// --- CTRE macros ---

#ifdef FOLLY_REGEX_BENCH_HAS_CTRE

#define BENCH_CTRE_MATCH(NAME, PATTERN, INPUT)                                 \
  BENCHMARK_RELATIVE(CTRE_##NAME, n) {                                         \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctre::match<PATTERN>(INPUT));                   \
    }                                                                          \
  }

#define BENCH_CTRE_SEARCH(NAME, PATTERN, INPUT)                                \
  BENCHMARK_RELATIVE(CTRE_##NAME, n) {                                         \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctre::search<PATTERN>(INPUT));                  \
    }                                                                          \
  }

#else // !FOLLY_REGEX_BENCH_HAS_CTRE

#define BENCH_CTRE_MATCH(NAME, PATTERN, INPUT)
#define BENCH_CTRE_SEARCH(NAME, PATTERN, INPUT)

#endif // FOLLY_REGEX_BENCH_HAS_CTRE

// --- Full benchmark macros: all engines ---

#define MATCH_BENCH(NAME, PATTERN, INPUT)                                      \
  BENCH_FOLLY_MATCH(NAME, PATTERN, INPUT)                                      \
  BENCH_STDREGEX_MATCH(NAME, PATTERN, INPUT)                                   \
  BENCH_BOOSTREGEX_MATCH(NAME, PATTERN, INPUT)                                 \
  BENCH_RE2_MATCH(NAME, PATTERN, INPUT)                                        \
  BENCH_CTRE_MATCH(NAME, PATTERN, INPUT)                                       \
  BENCH_ALL_PCRE_MATCH(NAME, PATTERN, INPUT)                                   \
  BENCHMARK_DRAW_LINE()

#define SEARCH_BENCH(NAME, PATTERN, INPUT)                                     \
  BENCH_FOLLY_SEARCH(NAME, PATTERN, INPUT)                                     \
  BENCH_STDREGEX_SEARCH(NAME, PATTERN, INPUT)                                  \
  BENCH_BOOSTREGEX_SEARCH(NAME, PATTERN, INPUT)                                \
  BENCH_RE2_SEARCH(NAME, PATTERN, INPUT)                                       \
  BENCH_CTRE_SEARCH(NAME, PATTERN, INPUT)                                      \
  BENCH_ALL_PCRE_SEARCH(NAME, PATTERN, INPUT)                                  \
  BENCHMARK_DRAW_LINE()

// Variants that skip std::regex and boost::regex with a printed reason.
// Use for patterns that cause exponential backtracking or stack overflow
// in these engines (e.g., ReDoS patterns like (a+)+b).

#define MATCH_BENCH_NO_RECURSIVE(NAME, PATTERN, INPUT, REASON)                 \
  BENCH_FOLLY_MATCH(NAME, PATTERN, INPUT)                                      \
  BENCH_STDREGEX_MATCH_SKIP(NAME, REASON)                                      \
  BENCH_BOOSTREGEX_MATCH_SKIP(NAME, REASON)                                    \
  BENCH_RE2_MATCH(NAME, PATTERN, INPUT)                                        \
  BENCH_CTRE_MATCH(NAME, PATTERN, INPUT)                                       \
  BENCH_ALL_PCRE_MATCH(NAME, PATTERN, INPUT)                                   \
  BENCHMARK_DRAW_LINE()

#define SEARCH_BENCH_NO_RECURSIVE(NAME, PATTERN, INPUT, REASON)                \
  BENCH_FOLLY_SEARCH(NAME, PATTERN, INPUT)                                     \
  BENCH_STDREGEX_SEARCH_SKIP(NAME, REASON)                                     \
  BENCH_BOOSTREGEX_SEARCH_SKIP(NAME, REASON)                                   \
  BENCH_RE2_SEARCH(NAME, PATTERN, INPUT)                                       \
  BENCH_CTRE_SEARCH(NAME, PATTERN, INPUT)                                      \
  BENCH_ALL_PCRE_SEARCH(NAME, PATTERN, INPUT)                                  \
  BENCHMARK_DRAW_LINE()

// clang-format on
