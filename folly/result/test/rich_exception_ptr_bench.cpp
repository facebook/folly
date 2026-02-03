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

#include <folly/Benchmark.h>
#include <folly/ExceptionWrapper.h>
#include <folly/result/immortal_rich_error.h>
#include <folly/result/rich_error.h>
#include <folly/result/rich_exception_ptr.h>
#include <folly/result/test/common.h>

#if FOLLY_HAS_RESULT

namespace folly {

TEST(RichExceptionPtrBench, benchmarksDoNotCrash) {
  runBenchmarksAsTest();
}

struct MyErr : rich_error_base {
  folly::source_location sl_;
  explicit constexpr MyErr(folly::source_location sl) : sl_{std::move(sl)} {}
  explicit constexpr MyErr(const folly::source_location* sl) : sl_{*sl} {}
  folly::source_location source_location() const noexcept override {
    return sl_;
  }
  // As noted in the `rich_error_with_partial_message` doc, using the default
  // `partial_message()` implementation makes `dynamic_cast` slower.  Since the
  // benchmark's goal is to model realistic usage, let's not pay that cost
  // here.  The above doc discusses available tradeoffs.
  const char* partial_message() const noexcept override { return "MyErr"; }
  using folly_get_exception_hint_types = rich_error_hints<MyErr>;
};

// Rich error unrelated to `MyErr` for testing `get_exception` "misses".
struct OtherErr : rich_error_base {
  using folly_get_exception_hint_types = rich_error_hints<OtherErr>;
  // Per above, speeds up some of the "falls back to RTTI" benchmarks.
  const char* partial_message() const noexcept override { return "OtherErr"; }
};

FOLLY_ALWAYS_INLINE auto bench_copy_and_compare(size_t iters, auto ep) {
  folly::BenchmarkSuspender suspender;
  bool all_same = true;
  suspender.dismissing([&] {
    while (iters--) {
      auto epCopy = ep;
      folly::compiler_must_not_predict(epCopy);
      all_same = all_same && (ep == epCopy);
    }
  });
  CHECK(all_same);
}

BENCHMARK(copyAndCompare_exceptionWrapper, n) {
  bench_copy_and_compare(
      n, make_exception_wrapper<rich_error<MyErr>>(source_location::current()));
}

BENCHMARK(copyAndCompare_dynamicRichExceptionPtr, n) {
  bench_copy_and_compare(
      n, rich_exception_ptr{rich_error<MyErr>{source_location::current()}});
}

BENCHMARK(copyAndCompare_immortalRichExceptionPtr, n) {
  static constexpr auto sl = source_location::current();
  bench_copy_and_compare(n, immortal_rich_error<MyErr, &sl>.ptr());
}

BENCHMARK_DRAW_LINE();

static constexpr auto src_loc = source_location::current();

const auto same_src_line = [](const auto& ex) {
  return ex->source_location().line() == src_loc.line();
};
const auto is_nullptr = [](const auto& ex) { return ex == nullptr; };
const auto has_what = [](const auto& ex) { return ex->what() != nullptr; };

BENCHMARK_DRAW_LINE();

template <typename QueryError, template <typename> typename GetExceptionFn>
void addQueryBenchmark(const std::string& name, auto checkFn, auto makeEptr) {
  addBenchmark(__FILE__, name, [=](size_t n) {
    auto contains_ex = makeEptr();
    folly::BenchmarkSuspender suspender;
    size_t numOkay = 0;
    suspender.dismissing([&] {
      for (size_t i = 0; i < n; ++i) {
        auto ex = GetExceptionFn<QueryError>{}(contains_ex);
        folly::compiler_must_not_predict(ex);
        numOkay += checkFn(ex);
      }
    });
    CHECK_EQ(n, numOkay);
    return n;
  });
}

template <template <typename> typename GetExceptionFn, typename QueryError>
void addQueryBenchmarkPair(
    const std::string& prefix,
    const std::string& suffix,
    auto checkFn,
    auto makeEptr) {
  addQueryBenchmark<QueryError, GetExceptionFn>(
      prefix + "_userErr_from_" + suffix, checkFn, makeEptr);
  addQueryBenchmark<rich_error<QueryError>, GetExceptionFn>(
      prefix + "_wrappedUserErr_from_" + suffix, checkFn, makeEptr);
}

template <template <typename> typename GetExceptionFn>
void addQueryBenchmarks(const std::string& getKind) {
  // `rich_error_base` hit benchmarks: successfully access it from rich errors
  addQueryBenchmark<rich_error_base, GetExceptionFn>(
      "get" + getKind + "_richErrorBase_from_exceptionWrapper",
      same_src_line,
      []() { return make_exception_wrapper<rich_error<MyErr>>(src_loc); });
  addQueryBenchmark<rich_error_base, GetExceptionFn>(
      "get" + getKind + "_richErrorBase_from_dynamicRichExceptionPtr",
      same_src_line,
      []() { return rich_exception_ptr{rich_error<MyErr>{src_loc}}; });
  addQueryBenchmark<rich_error_base, GetExceptionFn>(
      "get" + getKind + "_richErrorBase_from_immortalRichExceptionPtr",
      same_src_line,
      []() { return immortal_rich_error<MyErr, &src_loc>.ptr(); });
  addBenchmark(__FILE__, "-", []() { return 0; });

  // `rich_error_base` miss benchmarks: look for it in plain exceptions
  addQueryBenchmark<rich_error_base, GetExceptionFn>(
      "miss" + getKind + "_richErrorBase_from_exceptionWrapperPlain",
      is_nullptr,
      []() { return make_exception_wrapper<std::logic_error>("hi"); });
  addQueryBenchmark<rich_error_base, GetExceptionFn>(
      "miss" + getKind + "_richErrorBase_from_dynamicRichExceptionPtrPlain",
      is_nullptr,
      []() { return rich_exception_ptr{std::logic_error{"hi"}}; });
  // Future: Add `miss_richErrorBase_from_immortalRichExceptionPtr` here,
  // once C++ allows us to have immortals that aren't rich errors.
  addBenchmark(__FILE__, "-", []() { return 0; });

  // Hit benchmarks - successfully find the exception
  addQueryBenchmarkPair<GetExceptionFn, MyErr>(
      "get" + getKind, "exceptionWrapper", same_src_line, []() {
        return make_exception_wrapper<rich_error<MyErr>>(src_loc);
      });
  addQueryBenchmarkPair<GetExceptionFn, MyErr>(
      "get" + getKind, "dynamicRichExceptionPtr", same_src_line, []() {
        return rich_exception_ptr{rich_error<MyErr>{src_loc}};
      });
  addQueryBenchmarkPair<GetExceptionFn, MyErr>(
      "get" + getKind, "immortalRichExceptionPtr", same_src_line, []() {
        return immortal_rich_error<MyErr, &src_loc>.ptr();
      });
  addBenchmark(__FILE__, "-", []() { return 0; });

  // Miss benchmarks 1: Fail to find exception in plain (non-rich) exception
  addQueryBenchmarkPair<GetExceptionFn, MyErr>(
      "miss" + getKind, "exceptionWrapperPlain", is_nullptr, []() {
        return make_exception_wrapper<std::logic_error>("hi");
      });
  addQueryBenchmarkPair<GetExceptionFn, MyErr>(
      "miss" + getKind, "dynamicRichExceptionPtrPlain", is_nullptr, []() {
        return rich_exception_ptr{std::logic_error{"hi"}};
      });
  // Future: As above, add `immortalRichExceptionPtrPlain` counterpart.
  addBenchmark(__FILE__, "-", []() { return 0; });

  // Miss benchmarks 2: Different rich exceptions. These are slower than round 1
  // since the RTTI fallback needs to do a longer walk in the hierarchy.
  addQueryBenchmarkPair<GetExceptionFn, OtherErr>(
      "miss" + getKind, "exceptionWrapper", is_nullptr, []() {
        return make_exception_wrapper<rich_error<MyErr>>(src_loc);
      });
  addQueryBenchmarkPair<GetExceptionFn, OtherErr>(
      "miss" + getKind, "dynamicRichExceptionPtr", is_nullptr, []() {
        return rich_exception_ptr{rich_error<MyErr>{src_loc}};
      });
  addQueryBenchmarkPair<GetExceptionFn, OtherErr>(
      "miss" + getKind, "immortalRichExceptionPtr", is_nullptr, []() {
        return immortal_rich_error<MyErr, &src_loc>.ptr();
      });
  addBenchmark(__FILE__, "-", []() { return 0; });

  // `std::exception::what()` benchmarks
  addQueryBenchmark<std::exception, GetExceptionFn>(
      "get" + getKind + "_stdExceptionWhat_from_exceptionWrapper",
      has_what,
      []() { return make_exception_wrapper<rich_error<MyErr>>(src_loc); });
  addQueryBenchmark<std::exception, GetExceptionFn>(
      "get" + getKind + "_stdExceptionWhat_from_dynamicRichExceptionPtr",
      has_what,
      []() { return rich_exception_ptr{rich_error<MyErr>{src_loc}}; });
  addQueryBenchmark<std::exception, GetExceptionFn>(
      "get" + getKind + "_stdExceptionWhat_from_immortalRichExceptionPtr",
      has_what,
      []() { return immortal_rich_error<MyErr, &src_loc>.ptr(); });
}

void registerBenchmarks() {
  addBenchmark(__FILE__, "-", []() { return 0; });
  addQueryBenchmarks<get_exception_fn>("");
  addBenchmark(__FILE__, "-", []() { return 0; });
  addQueryBenchmarks<get_mutable_exception_fn>("Mutable");
  addBenchmark(__FILE__, "-", []() { return 0; });
}

} // namespace folly

int main(int argc, char** argv) {
  return folly::benchmarkMain(argc, argv, folly::registerBenchmarks);
}

#else // FOLLY_HAS_RESULT

int main() {
  return -1;
}

#endif // FOLLY_HAS_RESULT
