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

#pragma once

#include <sstream>
#include <string>
#include <type_traits>

#include <folly/Unit.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <folly/result/rich_exception_ptr.h>

namespace folly::test {

consteval void check(bool cond) {
  if (!cond) {
    // NOLINTNEXTLINE(facebook-hte-ThrowNonStdExceptionIssue)
    throw "check failed";
  }
}

void checkFormat(const auto& err, const std::string& re) {
  EXPECT_THAT(fmt::format("{}", err), ::testing::MatchesRegex(re));
  std::stringstream ss;
  ss << err;
  EXPECT_THAT(ss.str(), ::testing::MatchesRegex(re));
}

template <typename... Queries>
void checkFormatViaGet(const auto& container, const std::string& re) {
  (checkFormat(get_exception<Queries>(container), re), ...);
}

template <typename... Queries>
void checkFormatOfErrAndRep(const auto& err, const std::string& re) {
  checkFormat(err, re);
  checkFormatViaGet<Queries...>(rich_exception_ptr{err}, re);
}

} // namespace folly::test
