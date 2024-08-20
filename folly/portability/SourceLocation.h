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

#include <version>

#include <fmt/format.h>

#if __has_include(<source_location>) && defined __cpp_lib_source_location
#include <source_location> // @manual
namespace folly {
using source_location = ::std::source_location;
#elif __has_include(<experimental/source_location>)
#include <experimental/source_location>
namespace folly {
using source_location = ::std::experimental::source_location;
#else
namespace folly {
struct source_location {
  static source_location current() noexcept { return source_location{}; }
  const char* function_name() const noexcept { return ""; }
  const char* file_name() const noexcept { return ""; }
  std::uint_least32_t line() const noexcept { return 0; }
  std::uint_least32_t column() const noexcept { return 0; }
};
#endif

inline auto sourceLocationToString(const source_location& location) {
  return fmt::format(
      "{}:{} [{}]",
      location.file_name(),
      location.line(),
      location.function_name());
}
}
