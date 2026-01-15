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

#include <system_error>

#include <folly/result/coded_rich_error.h>

#if FOLLY_HAS_RESULT

namespace folly {

namespace detail {
// Wrapper to format `std::errc` as a rich error code. Avoids ODR issues from
// specializing `fmt::formatter<std::errc>`, which `fmt` may eventually provide.
struct errc_format_as {
  std::errc code;
  explicit errc_format_as(std::errc c) : code(c) {}
};
} // namespace detail

// Specialization of `rich_error_code` for `std::errc`.
// This allows using `std::errc` with `get_rich_error_code()`.
template <>
struct rich_error_code<std::errc> {
  // Do not change this random number, it's part of the ABI (see the docs).
  static constexpr uint64_t uuid = 14874560630508665473ULL;
  static constexpr const char* name = "std::errc";
  using format_as_t = detail::errc_format_as;
};

// This is the rich-error counterpart to `std::system_error`.  But, first get
// familiar with `docs/rich_error_code.md`, and `coded_rich_error.h`.
// Instantiate via:
//   make_coded_rich_error(std::errc::whatever, "message {}", arg)
//   errc_rich_error::make(...)
//
// Note that this is implementation is POSIX-centric, and that the first person
// wanting to deal with Windows codes may want to add an derived type that
// exports both `std::errc` and a Windows-centric enum.  Do get careful review
// of the design if you intend to add it to `folly/result`.
using errc_rich_error = coded_rich_error<std::errc>;

} // namespace folly

template <>
struct fmt::formatter<folly::detail::errc_format_as> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  fmt::appender format(
      folly::detail::errc_format_as ec, format_context& ctx) const;
};

#endif // FOLLY_HAS_RESULT
