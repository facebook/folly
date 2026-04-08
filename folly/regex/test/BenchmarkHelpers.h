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

#include <pcre.h>
#include <boost/regex.hpp> // NOLINT(facebook-hte-BadInclude-boost/regex.hpp) benchmark comparison
#include <ctre.hpp>
#include <re2/re2.h>
#include <folly/Benchmark.h>
#include <folly/BenchmarkUtil.h>
#include <folly/regex/Regex.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h> // @manual=fbsource//third-party/pcre2:pcre2-8

namespace folly {
namespace regex {
namespace bench {

struct PcreCtx {
  pcre* re = nullptr;
  pcre_extra* extra = nullptr;

  void compile(const char* pattern, bool jit) {
    const char* error;
    int erroffset;
    re = pcre_compile(pattern, 0, &error, &erroffset, nullptr);
    if (jit) {
      extra = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &error);
    }
  }

  ~PcreCtx() {
    if (extra) {
      pcre_free_study(extra);
    }
    if (re) {
      pcre_free(re);
    }
  }

  bool search(std::string_view input) const {
    int ov[30];
    return pcre_exec(
               re, extra, input.data(), static_cast<int>(input.size()), 0, 0, ov, 30) >= 0;
  }

  bool match(std::string_view input) const {
    int ov[30];
    int rc = pcre_exec(
        re, extra, input.data(), static_cast<int>(input.size()), 0, PCRE_ANCHORED, ov, 30);
    return rc >= 0 && static_cast<std::size_t>(ov[1]) == input.size();
  }
};

struct Pcre2Ctx {
  pcre2_code* re = nullptr;
  pcre2_match_data* md = nullptr;
  pcre2_match_context* mctx = nullptr;
  pcre2_jit_stack* jstack = nullptr;

  void compile(const char* pattern, bool jit) {
    int ec;
    PCRE2_SIZE eo;
    re = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(pattern),
        PCRE2_ZERO_TERMINATED,
        0,
        &ec,
        &eo,
        nullptr);
    md = pcre2_match_data_create_from_pattern(re, nullptr);
    if (jit && pcre2_jit_compile(re, PCRE2_JIT_COMPLETE) == 0) {
      jstack = pcre2_jit_stack_create(32768, 524288, nullptr);
      mctx = pcre2_match_context_create(nullptr);
      pcre2_jit_stack_assign(mctx, nullptr, jstack);
    }
  }

  ~Pcre2Ctx() {
    if (md) {
      pcre2_match_data_free(md);
    }
    if (mctx) {
      pcre2_match_context_free(mctx);
    }
    if (jstack) {
      pcre2_jit_stack_free(jstack);
    }
    if (re) {
      pcre2_code_free(re);
    }
  }

  bool search(std::string_view input) const {
    return pcre2_match(
               re,
               reinterpret_cast<PCRE2_SPTR>(input.data()),
               input.size(),
               0,
               0,
               md,
               mctx) >= 0;
  }

  bool match(std::string_view input) const {
    int rc = pcre2_match(
        re,
        reinterpret_cast<PCRE2_SPTR>(input.data()),
        input.size(),
        0,
        PCRE2_ANCHORED,
        md,
        mctx);
    if (rc < 0) {
      return false;
    }
    auto* ov = pcre2_get_ovector_pointer(md);
    return ov[1] == input.size();
  }
};

} // namespace bench
} // namespace regex
} // namespace folly

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

#define BENCH_ALL_PCRE_MATCH(NAME, PATTERN, INPUT)                             \
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
  }                                                                            \
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

#define BENCH_ALL_PCRE_SEARCH(NAME, PATTERN, INPUT)                            \
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
  }                                                                            \
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

// --- Folly engine entries (shared between all macros) ---

#define BENCH_FOLLY_MATCH(NAME, PATTERN, INPUT)                                \
  BENCHMARK(FollyRegex_##NAME, n) {                                            \
    constexpr auto re = folly::regex::compile<PATTERN>();                       \
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
  BENCHMARK_RELATIVE(FollyRegex_NFA_##NAME, n) {                               \
    constexpr auto re =                                                        \
        folly::regex::compile<PATTERN, folly::regex::Flags::ForceNFA>();        \
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
  }

#define BENCH_FOLLY_SEARCH(NAME, PATTERN, INPUT)                               \
  BENCHMARK(FollyRegex_##NAME, n) {                                            \
    constexpr auto re = folly::regex::compile<PATTERN>();                       \
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
  BENCHMARK_RELATIVE(FollyRegex_NFA_##NAME, n) {                               \
    constexpr auto re =                                                        \
        folly::regex::compile<PATTERN, folly::regex::Flags::ForceNFA>();        \
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

#define BENCH_BOOSTREGEX_MATCH_SKIP(NAME, REASON)                              \
  BENCHMARK_RELATIVE(BoostRegex_##NAME, n) {                                   \
    (void)n;                                                                   \
    static bool printed = false;                                               \
    if (!printed) {                                                            \
      std::cerr << "BoostRegex_" #NAME ": SKIPPED (" REASON ")" << std::endl; \
      printed = true;                                                          \
    }                                                                          \
  }

#define BENCH_STDREGEX_SEARCH_SKIP(NAME, REASON)                               \
  BENCHMARK_RELATIVE(StdRegex_##NAME, n) {                                     \
    (void)n;                                                                   \
    static bool printed = false;                                               \
    if (!printed) {                                                            \
      std::cerr << "StdRegex_" #NAME ": SKIPPED (" REASON ")" << std::endl;   \
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

// --- Full benchmark macros: all engines ---

#define MATCH_BENCH(NAME, PATTERN, INPUT)                                      \
  BENCH_FOLLY_MATCH(NAME, PATTERN, INPUT)                                      \
  BENCH_STDREGEX_MATCH(NAME, PATTERN, INPUT)                                   \
  BENCH_BOOSTREGEX_MATCH(NAME, PATTERN, INPUT)                                 \
  BENCH_RE2_MATCH(NAME, PATTERN, INPUT)                                        \
  BENCHMARK_RELATIVE(CTRE_##NAME, n) {                                         \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctre::match<PATTERN>(INPUT));                   \
    }                                                                          \
  }                                                                            \
  BENCH_ALL_PCRE_MATCH(NAME, PATTERN, INPUT)                                   \
  BENCHMARK_DRAW_LINE()

#define SEARCH_BENCH(NAME, PATTERN, INPUT)                                     \
  BENCH_FOLLY_SEARCH(NAME, PATTERN, INPUT)                                     \
  BENCH_STDREGEX_SEARCH(NAME, PATTERN, INPUT)                                  \
  BENCH_BOOSTREGEX_SEARCH(NAME, PATTERN, INPUT)                                \
  BENCH_RE2_SEARCH(NAME, PATTERN, INPUT)                                       \
  BENCHMARK_RELATIVE(CTRE_##NAME, n) {                                         \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctre::search<PATTERN>(INPUT));                  \
    }                                                                          \
  }                                                                            \
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
  BENCHMARK_RELATIVE(CTRE_##NAME, n) {                                         \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctre::match<PATTERN>(INPUT));                   \
    }                                                                          \
  }                                                                            \
  BENCH_ALL_PCRE_MATCH(NAME, PATTERN, INPUT)                                   \
  BENCHMARK_DRAW_LINE()

#define SEARCH_BENCH_NO_RECURSIVE(NAME, PATTERN, INPUT, REASON)                \
  BENCH_FOLLY_SEARCH(NAME, PATTERN, INPUT)                                     \
  BENCH_STDREGEX_SEARCH_SKIP(NAME, REASON)                                     \
  BENCH_BOOSTREGEX_SEARCH_SKIP(NAME, REASON)                                   \
  BENCH_RE2_SEARCH(NAME, PATTERN, INPUT)                                       \
  BENCHMARK_RELATIVE(CTRE_##NAME, n) {                                         \
    for (unsigned i = 0; i < n; ++i) {                                         \
      folly::doNotOptimizeAway(ctre::search<PATTERN>(INPUT));                  \
    }                                                                          \
  }                                                                            \
  BENCH_ALL_PCRE_SEARCH(NAME, PATTERN, INPUT)                                  \
  BENCHMARK_DRAW_LINE()

// clang-format on
