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

#include <folly/result/epitaph.h>

#include <folly/Benchmark.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/result/coro.h>
#include <folly/result/gtest_helpers.h>
#include <folly/result/test/common.h>

#if FOLLY_HAS_RESULT

namespace folly::test {

using namespace folly::detail;

// On 64-bit, the default stack epitaph should fit in 3 cache lines.
// Not checked on Windows where MSVC ABI widens `rich_exception_ptr`.
static_assert(
    sizeof(void*) != 8 || sizeof(rich_exception_ptr) != 8 ||
    sizeof(epitaph_impl<epitaph_stack_location<stack_epitaph_opts{}>>) == 192);

// Regex for a `[stack: #0 <sym>... @ <file>:NN ...]` block.
// The variadic form checks specific frames #0, #1, ... in order.
struct frame_re {
  std::string_view sym, file;
};
std::string stackRe(std::initializer_list<frame_re> frames) {
  std::string re = "\\[stack:";
  size_t i = 0;
  for (auto& [sym, file] : frames) {
    re +=
        fmt::format("\n  #{} [^\n]*{}[^\n]* @ [^\n]*{}:[0-9]+", i++, sym, file);
  }
  re += "(\n  #[0-9]+ [^\n]+)*\\]";
  return re;
}
std::string stackRe(std::string_view sym, std::string_view file) {
  return stackRe({{sym, file}});
}

// A named wrapper so `fullFormat`'s stack has two recognizable frames.
FOLLY_NOINLINE error_or_stopped captureStackHere(error_or_stopped eos) {
  return stack_epitaph(std::move(eos));
}

// A helper function that throws, to create a recognizable stack frame.
[[noreturn]] FOLLY_NOINLINE void throwFromHere() {
  throw std::runtime_error("boom");
}

// A coroutine that throws via a named function.
FOLLY_NOINLINE result<int> coroThatThrows() {
  throwFromHere();
  co_return 42;
}

// Assert the complete formatted output structure via regex.
// `[^\n]` ("not newline") keeps each frame pattern on one line.
TEST(StackEpitaphTest, fullFormat) {
  auto eos = captureStackHere(error_or_stopped{std::runtime_error{"oops"}});
  EXPECT_THAT(
      fmt::format("{}", get_exception<std::runtime_error>(eos)),
      ::testing::MatchesRegex(
          "std::runtime_error: oops \\[via\\] " +
          stackRe(
              {{"captureStackHere", "stack_epitaph_test\\.cpp"},
               {"fullFormat", "stack_epitaph_test\\.cpp"}})));
}

// Heap-spilled frames must match all-inline frames.
TEST(StackEpitaphTest, spillVsInline) {
  constexpr stack_epitaph_opts kSpill{.max_frames = 31, .inline_frames = 2};
  static_assert(epitaph_stack_location<kSpill>::kFramesMayUseHeap);
  auto spillLine = source_location::current().line() + 1;
  auto spill = stack_epitaph<kSpill>(error_or_stopped{std::logic_error{"doh"}});

  auto spillStr = fmt::format("{}", get_exception<std::logic_error>(spill));
  EXPECT_THAT(
      spillStr,
      ::testing::MatchesRegex(
          "std::logic_error: doh \\[via\\] " +
          stackRe("spillVsInline", "stack_epitaph_test\\.cpp")));

  constexpr stack_epitaph_opts kInline{.max_frames = 31, .inline_frames = 31};
  static_assert(!epitaph_stack_location<kInline>::kFramesMayUseHeap);
  auto inlLine = source_location::current().line() + 1;
  auto inl = stack_epitaph<kInline>(error_or_stopped{std::logic_error{"doh"}});

  // Stacks are identical modulo the call-site line number.
  auto spillLoc = fmt::format("stack_epitaph_test.cpp:{}", spillLine);
  spillStr.replace(
      spillStr.find(spillLoc),
      spillLoc.size(),
      fmt::format("stack_epitaph_test.cpp:{}", inlLine));
  EXPECT_EQ(spillStr, fmt::format("{}", get_exception<std::logic_error>(inl)));
}

// Like `fullFormat`, but with a message and only 1 checked frame.
TEST(StackEpitaphTest, messageFullFormat) {
  auto eos =
      stack_epitaph(error_or_stopped{std::logic_error{"inner"}}, "caught here");
  EXPECT_THAT(
      fmt::format("{}", get_exception<std::logic_error>(eos)),
      ::testing::MatchesRegex(
          "std::logic_error: inner \\[via\\] caught here " +
          stackRe("messageFullFormat", "stack_epitaph_test\\.cpp")));
}

// Like `messageFullFormat`, but wrapping a `result` that already has
// a stack epitaph from `unhandled_exception()`.
TEST(StackEpitaphTest, resultWrapsError) {
  auto r = stack_epitaph(coroThatThrows(), "result wrap");
  EXPECT_THAT(
      fmt::format("{}", get_exception<std::runtime_error>(r)),
      ::testing::MatchesRegex(
          "std::runtime_error: boom \\[via\\] result wrap " +
          stackRe("resultWrapsError", "stack_epitaph_test\\.cpp") +
          " \\[after\\] " + stackRe("coroThatThrows", "result_promise\\.h")));
}

// Like `fullFormat`, but for the `unhandled_exception()` path: a coroutine
// throws, and the stack epitaph is captured automatically.
// `unhandled_exception()` captures the stack AFTER throw has unwound to the
// coroutine frame, so we only see the coroutine name, not `throwFromHere`.
//
// Source location is `result_promise.h` because `unhandled_exception()` is
// `FOLLY_ALWAYS_INLINE` — DWARF attributes the inlined call site there.
TEST(StackEpitaphTest, thrownFullFormat) {
  auto r = coroThatThrows();
  EXPECT_THAT(
      fmt::format("{}", get_exception<std::runtime_error>(r)),
      ::testing::MatchesRegex(
          "std::runtime_error: boom \\[via\\] " +
          stackRe(
              {{"coroThatThrows", "result_promise\\.h"},
               {"thrownFullFormat", "stack_epitaph_test\\.cpp"}})));
}

// Regular `epitaph()` composes with the coro's stack epitaph:
// the source-location epitaph comes first (`[via]`), then the stack
// epitaph follows (`[after]`).
TEST(StackEpitaphTest, composesWithRegularEpitaph) {
  auto r = epitaph(coroThatThrows(), "extra context");
  EXPECT_THAT(
      fmt::format("{}", get_exception<std::runtime_error>(r)),
      ::testing::MatchesRegex(
          "std::runtime_error: boom \\[via\\] extra context"
          " @ [^\n]*stack_epitaph_test\\.cpp:[0-9]+"
          " \\[after\\] " +
          stackRe("coroThatThrows", "result_promise\\.h")));
}

TEST(StackEpitaphTest, valuePassthrough) {
  EXPECT_EQ(42, stack_epitaph(result<int>{42}).value_or_throw());
}

FOLLY_NOINLINE result<int> throwAtDepth(int depth) {
  if (depth > 0) {
    co_return co_await or_unwind(throwAtDepth(depth - 1));
  }
  throwFromHere();
  co_return 0;
}

// `or_unwind` does not add epitaphs, so the formatted output has one
// `[stack:]` block from the innermost `unhandled_exception()`.
// `throwAtDepth` recurses synchronously, so the stack includes
// all callers and frame count grows with depth (up to the capture limit).
TEST(StackEpitaphTest, variousDepths) {
  // `stack_epitaph_for_unhandled_exception` uses `max_frames = 19`.
  constexpr size_t kMaxVisibleFrames = 19;
  for (int depth : {1, 6, 11, 21}) {
    SCOPED_TRACE(depth);
    auto r = throwAtDepth(depth - 1);
    auto s = fmt::format("{}", get_exception<std::runtime_error>(r));
    EXPECT_THAT(
        s,
        ::testing::MatchesRegex(
            "std::runtime_error: boom \\[via\\] " +
            stackRe("throwAtDepth", "result_promise\\.h")));

    size_t tdFrames = 0;
    for (auto p = s.find("throwAtDepth"); p != std::string::npos;
         p = s.find("throwAtDepth", p + 1)) {
      ++tdFrames;
    }
    EXPECT_EQ(
        std::min(kMaxVisibleFrames, static_cast<size_t>(depth)), tdFrames);

    // Gtest/main frames exist below `throwAtDepth` (count varies with
    // inlining), unless recursion saturates the cap.
    if (tdFrames < kMaxVisibleFrames) {
      size_t frames = 0;
      for (auto p = s.find("\n  #"); p != std::string::npos;
           p = s.find("\n  #", p + 1)) {
        ++frames;
      }
      EXPECT_GT(frames, tdFrames);
    }
  }
}

// === Benchmarks ===

TEST(StackEpitaphBench, benchmarksDoNotCrash) {
  runBenchmarksAsTest();
}

FOLLY_NOINLINE void benchThrowAtDepth(int depth) {
  // compiler_must_not_predict: prevents proving noreturn by induction.
  // compiler_must_not_elide after the call: prevents tail-call optimization.
  // Without both, the compiler optimizes away all recursion and every
  // depth produces identical stacks.
  folly::compiler_must_not_predict(depth);
  if (depth > 0) {
    benchThrowAtDepth(depth - 1);
    folly::compiler_must_not_elide(depth);
    return;
  }
  throw std::runtime_error("bench");
}

template <int Depth, bool WithStack = false>
FOLLY_NOINLINE void benchDirect() {
  try {
    benchThrowAtDepth(Depth);
  } catch (...) {
    auto eos = error_or_stopped::from_current_exception();
    if constexpr (WithStack) {
      eos = stack_epitaph(std::move(eos));
    }
    folly::compiler_must_not_predict(eos);
  }
}

template <int Depth, bool WithStack = false>
FOLLY_NOINLINE result<int> benchCoro() {
  try {
    benchThrowAtDepth(Depth);
  } catch (...) {
    auto eos = error_or_stopped::from_current_exception();
    if constexpr (WithStack) {
      co_return stack_epitaph(std::move(eos));
    } else {
      co_return std::move(eos);
    }
  }
  co_return 0;
}

// Shows stack_epitaph overhead at increasing stack depths.
//
// "throw" group: measures overhead on top of throw+catch.  The throw
// dominates; stack capture adds a small fraction.
//
// "nothrow" group: isolates stack_epitaph cost by creating errors without
// throwing.  The baseline is just error_or_stopped construction.
// The delta is ~10ns per stack frame (pure libunwind walk cost).
//
// At depth 0 (~19 frames), most frames fit inline.  At depth 50 (~69
// frames), the tail spills to heap.

BENCHMARK(throw_depth0_baseline, n) {
  for (size_t i = 0; i < n; ++i) {
    benchDirect<0>();
  }
}
BENCHMARK_RELATIVE(throw_depth0_with_stack, n) {
  for (size_t i = 0; i < n; ++i) {
    benchDirect<0, true>();
  }
}
BENCHMARK_DRAW_LINE();

BENCHMARK(throw_depth10_baseline, n) {
  for (size_t i = 0; i < n; ++i) {
    benchDirect<10>();
  }
}
BENCHMARK_RELATIVE(throw_depth10_with_stack, n) {
  for (size_t i = 0; i < n; ++i) {
    benchDirect<10, true>();
  }
}
BENCHMARK_DRAW_LINE();

BENCHMARK(throw_depth50_baseline, n) {
  for (size_t i = 0; i < n; ++i) {
    benchDirect<50>();
  }
}
BENCHMARK_RELATIVE(throw_depth50_with_stack, n) {
  for (size_t i = 0; i < n; ++i) {
    benchDirect<50, true>();
  }
}
BENCHMARK_DRAW_LINE();

BENCHMARK(coro_depth10_baseline, n) {
  for (size_t i = 0; i < n; ++i) {
    auto r = benchCoro<10>();
    folly::compiler_must_not_predict(r);
  }
}
BENCHMARK_RELATIVE(coro_depth10_with_stack, n) {
  for (size_t i = 0; i < n; ++i) {
    auto r = benchCoro<10, true>();
    folly::compiler_must_not_predict(r);
  }
}
BENCHMARK_DRAW_LINE();

// -- No-throw group: isolates stack_epitaph cost --

template <bool WithStack>
FOLLY_NOINLINE void benchCaptureAtDepth(int depth) {
  folly::compiler_must_not_predict(depth);
  if (depth > 0) {
    benchCaptureAtDepth<WithStack>(depth - 1);
    folly::compiler_must_not_elide(depth);
    return;
  }
  auto eos = error_or_stopped{std::runtime_error{"bench"}};
  if constexpr (WithStack) {
    eos = stack_epitaph(std::move(eos));
  }
  folly::compiler_must_not_predict(eos);
}

BENCHMARK(nothrow_depth0_baseline, n) {
  for (size_t i = 0; i < n; ++i) {
    benchCaptureAtDepth<false>(0);
  }
}
BENCHMARK_RELATIVE(nothrow_depth0_with_stack, n) {
  for (size_t i = 0; i < n; ++i) {
    benchCaptureAtDepth<true>(0);
  }
}
BENCHMARK_DRAW_LINE();

BENCHMARK(nothrow_depth10_baseline, n) {
  for (size_t i = 0; i < n; ++i) {
    benchCaptureAtDepth<false>(10);
  }
}
BENCHMARK_RELATIVE(nothrow_depth10_with_stack, n) {
  for (size_t i = 0; i < n; ++i) {
    benchCaptureAtDepth<true>(10);
  }
}
BENCHMARK_DRAW_LINE();

BENCHMARK(nothrow_depth50_baseline, n) {
  for (size_t i = 0; i < n; ++i) {
    benchCaptureAtDepth<false>(50);
  }
}
BENCHMARK_RELATIVE(nothrow_depth50_with_stack, n) {
  for (size_t i = 0; i < n; ++i) {
    benchCaptureAtDepth<true>(50);
  }
}

} // namespace folly::test

#endif // FOLLY_HAS_RESULT

int main(int argc, char** argv) {
  return folly::test::benchmarkMain(argc, argv);
}
