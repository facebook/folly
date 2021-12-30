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

#include <folly/portability/GMock.h>
#include <folly/test/JsonTestUtil.h>

namespace folly {

namespace detail {
template <typename T>
class JsonEqMatcher : public ::testing::MatcherInterface<T> {
 public:
  explicit JsonEqMatcher(std::string expected, std::string prefixBeforeJson)
      : expected_(std::move(expected)),
        prefixBeforeJson_(std::move(prefixBeforeJson)) {}

  virtual bool MatchAndExplain(
      T actual, ::testing::MatchResultListener* /*listener*/) const override {
    StringPiece sp{actual};
    if (!sp.startsWith(prefixBeforeJson_)) {
      return false;
    }
    return compareJson(sp.subpiece(prefixBeforeJson_.size()), expected_);
  }

  virtual void DescribeTo(::std::ostream* os) const override {
    *os << "JSON is equivalent to " << expected_;
    if (prefixBeforeJson_.size() > 0) {
      *os << " after removing prefix '" << prefixBeforeJson_ << "'";
    }
  }

  virtual void DescribeNegationTo(::std::ostream* os) const override {
    *os << "JSON is not equivalent to " << expected_;
    if (prefixBeforeJson_.size() > 0) {
      *os << " after removing prefix '" << prefixBeforeJson_ << "'";
    }
  }

 private:
  std::string expected_;
  std::string prefixBeforeJson_;
};
} // namespace detail

/**
 * GMock matcher that uses compareJson, expecting exactly a type T as an
 * argument. Popular choices for T are std::string const&, StringPiece, and
 * std::string. prefixBeforeJson should not be present in expected.
 */
template <typename T>
::testing::Matcher<T> JsonEq(
    std::string expected, std::string prefixBeforeJson = "") {
  return ::testing::MakeMatcher(new detail::JsonEqMatcher<T>(
      std::move(expected), std::move(prefixBeforeJson)));
}

} // namespace folly
