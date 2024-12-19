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

#include <fmt/args.h>

#include <folly/CppAttributes.h>

namespace folly {

/// fmt_make_format_args_from_map_fn
/// fmt_make_format_args_from_map
///
/// A helper function-object type and variable for making a format-args object
/// from a map.
///
/// May be useful for transitioning from legacy folly::svformat to fmt::vformat.
struct fmt_make_format_args_from_map_fn {
  template <typename Map>
  fmt::dynamic_format_arg_store<fmt::format_context> operator()(
      [[FOLLY_ATTR_CLANG_LIFETIMEBOUND]] Map const& map) const {
    fmt::dynamic_format_arg_store<fmt::format_context> ret;
    ret.reserve(map.size(), map.size());
    for (auto const& [key, val] : map) {
      ret.push_back(fmt::arg(key.c_str(), std::cref(val)));
    }
    return ret;
  }
};
inline constexpr fmt_make_format_args_from_map_fn
    fmt_make_format_args_from_map{};

/// fmt_vformat_mangle_name_fn
/// fmt_vformat_mangle_name
///
/// A helper function-object type and variable for mangling vformat named-arg
/// names which fmt::vformat might not otherwise permit.
struct fmt_vformat_mangle_name_fn {
  std::string operator()(std::string_view const str) const;
  void operator()(std::string& out, std::string_view const str) const;
};
inline constexpr fmt_vformat_mangle_name_fn fmt_vformat_mangle_name{};

/// fmt_vformat_mangle_format_string_fn
/// fmt_vformat_mangle_format_string
///
/// A helper function-object type and variable for mangling the content of
/// vformat format-strings containing named-arg names which fmt::vformat might
/// not otherwise permit.
struct fmt_vformat_mangle_format_string_fn {
  std::string operator()(std::string_view const str) const;
};
inline constexpr fmt_vformat_mangle_format_string_fn
    fmt_vformat_mangle_format_string{};

} // namespace folly
