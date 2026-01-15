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
#include <folly/result/immortal_rich_error.h>
#include <folly/result/rich_error.h>
#include <folly/result/rich_error_code.h>
#include <folly/result/test/common.h>
#include <folly/result/test/rich_error_codes.h>

#if FOLLY_HAS_RESULT

namespace folly {

TEST(RichErrorCodeBench, benchmarksDoNotCrash) {
  runBenchmarksAsTest();
}

template <typename CodeType>
FOLLY_ALWAYS_INLINE void benchCodeExtraction(
    size_t iters, const auto& error, std::optional<CodeType> expectedCode) {
  folly::BenchmarkSuspender suspender;
  size_t numOkay = 0;
  suspender.dismissing([&] {
    for (size_t i = 0; i < iters; ++i) {
      auto code = get_rich_error_code<CodeType>(error);
      folly::compiler_must_not_predict(code);
      numOkay += (code == expectedCode);
    }
  });
  CHECK_EQ(iters, numOkay);
}

struct BenchRichError {
  template <typename CodeType, typename ErrorType, auto... Codes>
  static void run(size_t n, std::optional<CodeType> expected) {
    rich_error<ErrorType> err(Codes...);
    benchCodeExtraction<CodeType>(n, err, expected);
  }
};

struct BenchRichErrorViaBase {
  template <typename CodeType, typename ErrorType, auto... Codes>
  static void run(size_t n, std::optional<CodeType> expected) {
    rich_error<ErrorType> err(Codes...);
    const rich_error_base& base = err;
    benchCodeExtraction<CodeType>(n, base, expected);
  }
};

struct BenchImmortalRichError {
  template <typename CodeType, typename ErrorType, auto... Codes>
  static void run(size_t n, std::optional<CodeType> expected) {
    static constexpr auto rep = immortal_rich_error<ErrorType, Codes...>.ptr();
    benchCodeExtraction<CodeType>(n, *get_rich_error(rep), expected);
  }
};

template <typename Bench, typename QueryCode, typename Error, auto... ctorCodes>
void addCodeBenchmark(const std::string& nameSuffix) {
  std::optional<QueryCode> expectedCode = std::nullopt;
  ([&] {
    if constexpr (std::is_same_v<decltype(ctorCodes), QueryCode>) {
      expectedCode = ctorCodes;
      return true;
    }
    return false;
  }() ||
   ...);
  addBenchmark(
      __FILE__,
      (expectedCode.has_value() ? "get__" : "miss__") + nameSuffix,
      [expectedCode](size_t n) {
        Bench::template run<QueryCode, Error, ctorCodes...>(n, expectedCode);
        return n;
      });
}

template <typename Bench, typename QueryCode>
void addBenchmarkForErrorB(const std::string& nameSuffix) {
  addCodeBenchmark<
      Bench,
      QueryCode,
      ErrorB,
      A1::ONE_A1,
      B1{ImplB1::TWO_B1},
      B2::ONE_B2>(nameSuffix);
}

template <typename Bench, typename QueryCode>
void addBenchmarkForErrorC(const std::string& nameSuffix) {
  addCodeBenchmark<
      Bench,
      QueryCode,
      ErrorC,
      A1::ONE_A1,
      B1{ImplB1::TWO_B1},
      B2::ONE_B2,
      C1::TWO_C1>(nameSuffix);
}

template <typename Bench>
void addCodeBenchmarkSuite(const std::string& nameSuffix) {
  addCodeBenchmark<Bench, A1, ErrorA, A1::ONE_A1>("A1__ErrorA__" + nameSuffix);
  addCodeBenchmark<Bench, B1, ErrorA, A1::ONE_A1>( // miss
      "b1__ErrorA__" + nameSuffix);

  addBenchmarkForErrorB<Bench, A1>("A1__ErrorB__" + nameSuffix);
  addBenchmarkForErrorB<Bench, B1>("B1__ErrorB__" + nameSuffix);
  addBenchmarkForErrorB<Bench, B2>("B2__ErrorB__" + nameSuffix);
  addBenchmarkForErrorB<Bench, C1>("C1__ErrorB__" + nameSuffix); // miss

  addBenchmarkForErrorC<Bench, A1>("A1__ErrorC__" + nameSuffix);
  addBenchmarkForErrorC<Bench, B1>("B1__ErrorC__" + nameSuffix);
  addBenchmarkForErrorC<Bench, B2>("B2__ErrorC__" + nameSuffix);
  addBenchmarkForErrorC<Bench, C1>("C1__ErrorC__" + nameSuffix);
}

void registerBenchmarks() {
  addCodeBenchmarkSuite<BenchRichError>("rich_error");
  addBenchmark(__FILE__, "-", [](unsigned int n) { return n; });

  addCodeBenchmarkSuite<BenchRichErrorViaBase>("rich_error_via_base");
  addBenchmark(__FILE__, "-", [](unsigned int n) { return n; });

  addCodeBenchmarkSuite<BenchImmortalRichError>("immortal_error");
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
