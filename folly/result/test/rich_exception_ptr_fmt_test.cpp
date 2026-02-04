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

#include <folly/result/test/common.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/result/epitaph.h>
#include <folly/result/immortal_rich_error.h>
#include <folly/result/rich_error.h>

#if FOLLY_HAS_RESULT

namespace folly {

const auto kTestFile = source_location::current().file_name();

// Checks REP formatting directly, and via get_exception<Queries...>.
template <typename... Queries>
void checkRepAndGetFormat(const auto& eos, const std::string& re) {
  checkFormatViaGet<Queries...>(eos, re);
  checkFormat(copy(eos).release_rich_exception_ptr(), re);
}

struct MsgErr : rich_error_base {
  using folly_get_exception_hint_types = rich_error_hints<MsgErr>;
  const char* partial_message() const noexcept override { return "msg"; }
};

TEST(RichExceptionPtrFormat, empty) {
  checkFormat(rich_exception_ptr{}, "\\[empty\\]");
}

TEST(RichExceptionPtrFormat, emptyTry) {
  checkFormat(
      rich_exception_ptr{detail::make_empty_try_t{}}, "\\[empty Try\\]");
}

TEST(RichExceptionPtrFormat, stopped) {
  constexpr auto mayThrowFmt =
      "folly::detail::StoppedMayThrow: operation stopped \\(cancelled\\)";

  checkFormat(
      rich_exception_ptr{detail::StoppedNoThrow{}},
      "folly::detail::StoppedNoThrow");
  checkFormat(rich_exception_ptr{detail::StoppedMayThrow{}}, mayThrowFmt);

  // StoppedNoThrow can be a regular (non-bit-optimized) eptr; works the same
  checkFormat(
      rich_exception_ptr::from_exception_ptr_slow(
          std::make_exception_ptr(detail::StoppedNoThrow{})),
      "folly::detail::StoppedNoThrow");

  auto checkWrappedStopped = [](auto eos, std::string_view expected_inner) {
    auto line = source_location::current().line() + 1;
    auto wrapped = epitaph(std::move(eos), "ctx");
    checkFormat(
        copy(wrapped).release_rich_exception_ptr(),
        fmt::format(
            "{} \\[via\\] ctx @ {}:{}", expected_inner, kTestFile, line));
  };

  checkWrappedStopped(error_or_stopped{detail::StoppedMayThrow{}}, mayThrowFmt);
  checkWrappedStopped(
      error_or_stopped{detail::StoppedNoThrow{}},
      "folly::detail::StoppedNoThrow");
}

TEST(RichExceptionPtrFormat, richError) {
  checkRepAndGetFormat<MsgErr, rich_error_base>(
      error_or_stopped{rich_error<MsgErr>{}}, "msg");
}

TEST(RichExceptionPtrFormat, immortalRichError) {
  auto rep = immortal_rich_error<MsgErr>.ptr();
  checkFormatViaGet<MsgErr, rich_error_base>(rep, "msg");
  checkFormat(rep, "msg");
}

// std::exception -- demangle + what
TEST(RichExceptionPtrFormat, stdException) {
  auto eos = error_or_stopped{std::logic_error{"oops"}};
  checkFormatViaGet<std::logic_error>(eos, "std::logic_error: oops");
  checkFormatViaGet<std::exception>(eos, "std::exception: oops");
  checkFormat(copy(eos).release_rich_exception_ptr(), "std::logic_error: oops");
}

struct NotAnException {};

// Non-std::exception -- demangle only
TEST(RichExceptionPtrFormat, nonStdException) {
  checkFormat(
      rich_exception_ptr::from_exception_ptr_slow(
          std::make_exception_ptr(NotAnException{})),
      "folly::NotAnException");
}

// Epitaph-wrapped rich error
TEST(RichExceptionPtrFormat, epitaphWrappedRichError) {
  auto line = source_location::current().line() + 1;
  auto eos = epitaph(error_or_stopped{rich_error<MsgErr>{}}, "ctx");
  checkRepAndGetFormat<MsgErr, rich_error_base>(
      eos, fmt::format("msg \\[via\\] ctx @ {}:{}", kTestFile, line));
}

// Epitaph-wrapped std::exception -- also check 2-epitaph chain
TEST(RichExceptionPtrFormat, epitaphWrappedStdException) {
  auto line1 = source_location::current().line() + 1;
  auto eos1 = epitaph(error_or_stopped{std::logic_error{"inner"}}, "ctx");
  checkRepAndGetFormat<std::logic_error, std::exception>(
      eos1,
      fmt::format(
          "std::logic_error: inner \\[via\\] ctx @ {}:{}", kTestFile, line1));

  auto line2 = source_location::current().line() + 1;
  auto eos2 = epitaph(std::move(eos1), "outer");
  checkRepAndGetFormat<std::logic_error, std::exception>(
      eos2,
      fmt::format(
          "std::logic_error: inner \\[via\\] outer @ {}:{} \\[after\\] ctx @ {}:{}",
          kTestFile,
          line2,
          kTestFile,
          line1));
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
