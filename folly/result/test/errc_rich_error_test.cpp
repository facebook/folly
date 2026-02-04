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

#include <folly/result/errc_rich_error.h>

#include <system_error>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

#include <folly/result/immortal_rich_error.h>
#include <folly/result/rich_error_code.h>
#include <folly/result/test/common.h>
#include <folly/result/test/rich_error_codes.h> // for `A1`

using namespace folly::string_literals;

namespace folly::test {

constexpr bool testImmortalErrcRichError() {
  // FYI, this immortal error is semanticaly equivalent to
  //
  //   rich_exception_ptr err_ptr{
  //       rich_error<errc_rich_error>{std::errc::io_error, "msg"}};
  //
  // But unlike the dynamic version, it uses no heap, and is > 10x faster.
  constexpr auto err_ptr =
      immortal_rich_error<errc_rich_error, std::errc::io_error, "msg"_litv>.ptr();
  auto& err = *get_rich_error(err_ptr);
  check(get_rich_error_code<std::errc>(err) == std::errc::io_error);
  check(std::string_view{"msg"} == std::string_view{err.partial_message()});
  check(get_rich_error_code<A1>(err) == std::nullopt);
  return true;
}
static_assert(testImmortalErrcRichError());

TEST(ErrcRichErrorTest, formatting) {
  auto err = make_coded_rich_error(std::errc::io_error, "message");
  static_assert(std::same_as<rich_error<errc_rich_error>, decltype(err)>);

  checkFormatOfErrAndRep<errc_rich_error, rich_error_base, std::exception>(
      err, "message - std::errc=[0-9]+ \\([^)]+\\).*");
}

} // namespace folly::test
