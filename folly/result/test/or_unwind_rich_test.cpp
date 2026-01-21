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

#include <folly/result/or_unwind_rich.h>

#include <folly/portability/GTest.h>
#include <folly/result/gtest_helpers.h>

// NB These tests are a bit over-elaborate / redundant with others, but the
// hope is they give a clear picture of the usage of `or_unwind_rich`.

#if FOLLY_HAS_RESULT

namespace folly {

const auto test_file_name = std::source_location::current().file_name();

// Verify enrichment was applied to the error.
void checkEnrichedError(folly::result<> res, std::string_view msg) {
  ASSERT_FALSE(res.has_value());
  ASSERT_FALSE(res.has_stopped());
  EXPECT_EQ(
      msg, fmt::format("{}", folly::get_exception<std::logic_error>(res)));
}

// Value path: returns the value without formatting.
RESULT_CO_TEST(OrUnwindRich, value) {
  EXPECT_EQ(42, co_await or_unwind_rich(result<int>{42}, "unused"));
  co_await or_unwind_rich(result<>{}, "unused");
}

// Errors propagate with enrichment -- message & source location.
TEST(OrUnwindRich, error) {
  std::uint_least32_t err_line = 0;
  auto expectedMsg = [&](std::string_view msgViaCtx) {
    return fmt::format(
        "std::logic_error: {} @ {}:{}", msgViaCtx, test_file_name, err_line);
  };

  checkEnrichedError( // `result` in non-value state
    [&]() -> result<> {
      result<int> r{non_value_result{std::logic_error{"err1"}}};
      err_line = std::source_location::current().line() + 1;
      (void)co_await or_unwind_rich(std::move(r), "ctx1");
    }(),
    expectedMsg("err1 [via] ctx1"));

  checkEnrichedError( // `non_value_result`
    [&]() -> result<> {
      non_value_result nvr{std::logic_error{"err2"}};
      err_line = std::source_location::current().line() + 1;
      co_await or_unwind_rich(std::move(nvr), "ctx2");
    }(),
    expectedMsg("err2 [via] ctx2"));

  checkEnrichedError( // `result` with format args
    [&]() -> result<> {
      result<int> r{non_value_result{std::logic_error{"err3"}}};
      err_line = std::source_location::current().line() + 1;
      (void)co_await or_unwind_rich(std::move(r), "x={} y={}", 10, 20);
    }(),
    expectedMsg("err3 [via] x=10 y=20"));
}

// Stopped state propagates with enrichment.
TEST(OrUnwindRich, stopped) {
  std::uint_least32_t err_line = 0;
  auto res = [&]() -> result<int> {
    err_line = std::source_location::current().line() + 1;
    co_return co_await or_unwind_rich(result<int>{stopped_result}, "ctx");
  }();
  EXPECT_TRUE(res.has_stopped());
  auto msg = "folly::OperationCancelled: coroutine operation cancelled";
  // Temporary workaround for `static_assert` against testing for OC in
  // `result.h`.  Future: Once `result` or `non_value_result` is formattable,
  // we can avoid this hackery.
  auto rep = std::move(res).non_value().release_rich_exception_ptr();
  EXPECT_EQ(
      fmt::format("{} [via] ctx @ {}:{}", msg, test_file_name, err_line),
      fmt::format("{}", get_exception<OperationCancelled>(rep)));
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
