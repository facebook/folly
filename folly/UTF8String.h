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

#include <string>

#include <boost/regex/pending/unicode_iterator.hpp>

#include <folly/Range.h>

namespace folly {

class UTF8StringPiece {
 public:
  using iterator = boost::u8_to_u32_iterator<const char*>;
  using size_type = std::size_t;

  /* implicit */ UTF8StringPiece(const folly::StringPiece piece)
      : begin_{piece.begin(), piece.begin(), piece.end()},
        end_{piece.end(), piece.begin(), piece.end()} {}
  template <
      typename T,
      std::enable_if_t<std::is_convertible_v<T, folly::StringPiece>, int> = 0>
  /* implicit */ UTF8StringPiece(const T& t)
      : UTF8StringPiece(folly::StringPiece(t)) {}

  iterator begin() const noexcept { return begin_; }
  iterator cbegin() const noexcept { return begin_; }
  iterator end() const noexcept { return end_; }
  iterator cend() const noexcept { return end_; }

  bool empty() const noexcept { return begin_ == end_; }
  size_type walk_size() const {
    return static_cast<size_type>(std::distance(begin_, end_));
  }

 private:
  iterator begin_;
  iterator end_;
};

} // namespace folly
