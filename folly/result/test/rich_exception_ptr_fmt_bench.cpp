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
#include <folly/result/epitaph.h>
#include <folly/result/rich_error.h>
#include <folly/result/rich_exception_ptr.h>
#include <folly/result/test/common.h>

#if FOLLY_HAS_RESULT

namespace folly {

TEST(RichExceptionPtrFmtBench, benchmarksDoNotCrash) {
  runBenchmarksAsTest();
}

struct PrintMe : rich_error_base {
  const char* partial_message() const noexcept override {
    return "hello, world!";
  }
  using folly_get_exception_hint_types = rich_error_hints<PrintMe>;
};

BENCHMARK(fmtSimpleRichError, iters) {
  folly::BenchmarkSuspender suspender;
  std::string buf;
  buf.reserve(100);
  rich_error<PrintMe> err;
  bool all_ok = true;
  suspender.dismissing([&] {
    while (iters--) {
      buf.clear();
      fmt::format_to(std::back_inserter(buf), "{}", err);
      folly::compiler_must_not_predict(buf);
      all_ok = all_ok && (buf.size() == 13);
    }
  });
  CHECK(all_ok);
}

struct MyBuf : std::streambuf {
  static constexpr size_t size_ = 128;
  char buf_[size_]{};
  MyBuf() { setp(buf_, buf_ + size_); }
  std::string_view written() const { return {pbase(), pptr()}; }
  int overflow(int) override { throw std::logic_error{"bad"}; }
  void clear() { setp(buf_, buf_ + size_); }
};

BENCHMARK(ostreamAppendSimpleRichError, iters) {
  folly::BenchmarkSuspender suspender;

  rich_error<PrintMe> err;
  MyBuf buf;
  std::ostream ss{&buf};
  CHECK_EQ(std::string(), buf.written());
  ss << err;
  CHECK_EQ(std::string("hello, world!"), buf.written());
  buf.clear();
  CHECK_EQ(std::string(), buf.written());

  bool all_ok = true;
  suspender.dismissing([&] {
    while (iters--) {
      buf.clear();
      ss << err;
      folly::compiler_must_not_predict(buf);
      all_ok = all_ok && (buf.written().size() == 13);
    }
  });
  CHECK(all_ok);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(fmtEpitaphRichError, iters) {
  folly::BenchmarkSuspender suspender;
  std::string buf;
  buf.reserve(200);
  auto eos = epitaph(error_or_stopped{rich_error<PrintMe>{}}, "context");
  auto ex = get_exception<rich_error_base>(eos);
  bool all_ok = true;
  suspender.dismissing([&] {
    while (iters--) {
      buf.clear();
      fmt::format_to(std::back_inserter(buf), "{}", ex);
      folly::compiler_must_not_predict(buf);
      all_ok = all_ok && (buf.size() > 29); // hello, world! [via] context @...
    }
  });
  CHECK(all_ok);
}

BENCHMARK(fmtEpitaphStdException, iters) {
  folly::BenchmarkSuspender suspender;
  std::string buf;
  buf.reserve(200);
  auto eos = epitaph(error_or_stopped{std::logic_error{"oops"}}, "context");
  auto ex = get_exception<std::exception>(eos);
  bool all_ok = true;
  suspender.dismissing([&] {
    while (iters--) {
      buf.clear();
      fmt::format_to(std::back_inserter(buf), "{}", ex);
      folly::compiler_must_not_predict(buf);
      // std::logic_error: oops [via] context @...
      all_ok = all_ok && (buf.size() > 38);
    }
  });
  CHECK(all_ok);
}

} // namespace folly

int main(int argc, char** argv) {
  return folly::benchmarkMain(argc, argv);
}

#else // FOLLY_HAS_RESULT

int main() {
  return -1;
}

#endif // FOLLY_HAS_RESULT
