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

#include <iterator>
#include <string>
#include <type_traits>

#include <fmt/args.h>
#include <fmt/format.h>

#include <folly/CppAttributes.h>
#include <folly/lang/Exception.h>
#include <folly/portability/FmtCompile.h>

namespace folly {

namespace detail {
template <typename T>
concept fmt_compiled_string =
#if FMT_VERSION >= 110104
    fmt::is_compiled_string<T>::value;
#else
    fmt::detail::is_compiled_string<T>::value;
#endif
} // namespace detail

/// fmt_sized_format_fn
/// fmt_sized_format
///
/// Two-pass formatting: measure the output, reserve the final std::string,
/// then format into it. Accepts checked, runtime, and FOLLY_FMT_COMPILE format
/// strings.
///
/// Custom formatters must produce the same output on both passes. A size
/// mismatch throws fmt::format_error. Values are passed by const reference,
/// without moves.
/// Fmt view wrappers are copied for fmt's argument bookkeeping.
///
/// Prefer compiled formats to reduce the cost of the sizing pass. Two-pass
/// formatting is not necessarily faster than fmt::format for every workload.
struct fmt_sized_format_fn {
 private:
  template <typename T>
  static constexpr bool format_arg_by_value =
      fmt::detail::is_named_arg<T>::value ||
      std::is_base_of_v<fmt::detail::view, T>;

  template <typename T>
  using format_arg_t = std::conditional_t<format_arg_by_value<T>, T, T const&>;

 public:
  template <typename... Args>
  std::string operator()(
      fmt::format_string<format_arg_t<Args>...> spec,
      Args const&... args) const {
    return format(spec, args...);
  }

  template <detail::fmt_compiled_string Format, typename... Args>
  std::string operator()(Format const& spec, Args const&... args) const {
    return format(spec, args...);
  }

 private:
  template <typename Format, typename... Args>
  static std::string format(Format const& spec, Args const&... args) {
    auto const size = fmt::formatted_size(spec, format_arg_t<Args>(args)...);
    std::string out;
    out.reserve(size);
    fmt::format_to(std::back_inserter(out), spec, format_arg_t<Args>(args)...);
    if (out.size() != size) {
      throw_exception<fmt::format_error>(
          "formatted size changed between counting and writing");
    }
    return out;
  }
};
inline constexpr fmt_sized_format_fn fmt_sized_format{};

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
  struct options {
    bool numeric_args_as_named = false;

    options& set_numeric_args_as_named(bool value) noexcept {
      numeric_args_as_named = value;
      return *this;
    }
  };

  std::string operator()(std::string_view const str) const;
  std::string operator()(options const& opts, std::string_view const str) const;
};
inline constexpr fmt_vformat_mangle_format_string_fn
    fmt_vformat_mangle_format_string{};

using fmt_vformat_mangle_format_string_options =
    fmt_vformat_mangle_format_string_fn::options;

} // namespace folly
