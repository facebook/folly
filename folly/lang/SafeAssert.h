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

#include <cstdint>
#include <utility>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/Preprocessor.h>
#include <folly/lang/CArray.h>

#define FOLLY_DETAIL_SAFE_CHECK_IMPL(d, p, u, expr, ...)                  \
  do {                                                                    \
    if ((!d || ::folly::kIsDebug || ::folly::kIsSanitize) &&              \
        !static_cast<bool>(expr)) {                                       \
      static constexpr auto __folly_detail_safe_assert_fun = __func__;    \
      static constexpr ::folly::detail::safe_assert_arg                   \
          __folly_detail_safe_assert_arg{                                 \
              u ? nullptr : #expr,                                        \
              __FILE__,                                                   \
              __LINE__,                                                   \
              __folly_detail_safe_assert_fun,                             \
              ::folly::detail::safe_assert_msg_types<                     \
                  decltype(::folly::detail::safe_assert_msg_types_seq_of( \
                      __VA_ARGS__))>::value.data};                        \
      constexpr ::folly::detail::safe_assert_terminate_w<p>               \
          __folly_detail_safe_assert_terminate_w{                         \
              __folly_detail_safe_assert_arg};                            \
      __folly_detail_safe_assert_terminate_w(__VA_ARGS__);                \
    }                                                                     \
  } while (false)

//  FOLLY_SAFE_CHECK
//
//  If expr evaluates to false after explicit conversion to bool, prints context
//  information to stderr and aborts. Context information includes the remaining
//  variadic arguments.
//
//  When the remaining variadic arguments are printed to stderr, there are two
//  supported types after implicit conversions: char const* and uint64_t. This
//  facility is intentionally not extensible to custom types.
//
//  multi-thread-safe
//  async-signal-safe
#define FOLLY_SAFE_CHECK(expr, ...) \
  FOLLY_DETAIL_SAFE_CHECK_IMPL(0, 0, 0, expr, __VA_ARGS__)

//  FOLLY_SAFE_DCHECK
//
//  Equivalent to FOLLY_SAFE_CHECK when in debug or instrumented builds, where
//  debug builds are signalled by NDEBUG being undefined and instrumented builds
//  include sanitizer builds.
//
//  multi-thread-safe
//  async-signal-safe
#define FOLLY_SAFE_DCHECK(expr, ...) \
  FOLLY_DETAIL_SAFE_CHECK_IMPL(1, 0, 0, expr, __VA_ARGS__)

//  FOLLY_SAFE_PCHECK
//
//  Equivalent to FOLLY_SAFE_CHECK but includes errno in the contextual
//  information printed to stderr.
//
//  multi-thread-safe
//  async-signal-safe
#define FOLLY_SAFE_PCHECK(expr, ...) \
  FOLLY_DETAIL_SAFE_CHECK_IMPL(0, 1, 0, expr, __VA_ARGS__)

//  FOLLY_SAFE_DPCHECK
//
//  Equivalent to FOLLY_SAFE_DCHECK but includes errno in the contextual
//  information printed to stderr.
//
//  multi-thread-safe
//  async-signal-safe
#define FOLLY_SAFE_DPCHECK(expr, ...) \
  FOLLY_DETAIL_SAFE_CHECK_IMPL(1, 1, 0, expr, __VA_ARGS__)

//  FOLLY_SAFE_FATAL
//
//  Equivalent to FOLLY_SAFE_CHECK(false, ...) but excludes any failing
//  expression from the contextual information printed to stderr.
//
//  multi-thread-safe
//  async-signal-safe
#define FOLLY_SAFE_FATAL(...) \
  FOLLY_DETAIL_SAFE_CHECK_IMPL(0, 0, 1, false, __VA_ARGS__)

//  FOLLY_SAFE_DFATAL
//
//  Equivalent to FOLLY_SAFE_DCHECK(false, ...) but excludes any failing
//  expression from the contextual information printed to stderr.
//
//  multi-thread-safe
//  async-signal-safe
#define FOLLY_SAFE_DFATAL(...) \
  FOLLY_DETAIL_SAFE_CHECK_IMPL(1, 0, 1, false, __VA_ARGS__)

namespace folly {
namespace detail {

enum class safe_assert_msg_type : char { term, cstr, ui64 };

template <safe_assert_msg_type... A>
struct safe_assert_msg_type_s {};

struct safe_assert_msg_types_one_fn {
  template <safe_assert_msg_type A>
  using c = std::integral_constant<safe_assert_msg_type, A>;
  // only used in unevaluated contexts:
  c<safe_assert_msg_type::cstr> operator()(char const*) const;
  c<safe_assert_msg_type::ui64> operator()(uint64_t) const;
};
FOLLY_INLINE_VARIABLE constexpr safe_assert_msg_types_one_fn
    safe_assert_msg_types_one{}; // a function object to prevent extensions

template <typename... A>
safe_assert_msg_type_s<decltype(safe_assert_msg_types_one(A{}))::value...>
safe_assert_msg_types_seq_of(A...); // only used in unevaluated contexts

template <typename>
struct safe_assert_msg_types;
template <safe_assert_msg_type... A>
struct safe_assert_msg_types<safe_assert_msg_type_s<A...>> {
  using value_type = c_array<safe_assert_msg_type, sizeof...(A) + 1>;
  static constexpr value_type value = {{A..., safe_assert_msg_type::term}};
};
template <safe_assert_msg_type... A>
constexpr
    typename safe_assert_msg_types<safe_assert_msg_type_s<A...>>::value_type
        safe_assert_msg_types<safe_assert_msg_type_s<A...>>::value;

struct safe_assert_arg {
  char const* expr;
  char const* file;
  unsigned int line;
  char const* function;
  safe_assert_msg_type const* msg_types;
};

struct safe_assert_msg_cast_one_fn {
  FOLLY_ERASE auto operator()(char const* const a) const { return a; }
  FOLLY_ERASE auto operator()(uint64_t const a) const { return a; }
};
FOLLY_INLINE_VARIABLE constexpr safe_assert_msg_cast_one_fn
    safe_assert_msg_cast_one{}; // a function object to prevent extensions

template <bool P>
[[noreturn]] FOLLY_COLD FOLLY_NOINLINE void safe_assert_terminate(
    safe_assert_arg const* arg, ...) noexcept; // the true backing function

template <bool P>
struct safe_assert_terminate_w {
  safe_assert_arg const& arg;

  FOLLY_ERASE constexpr safe_assert_terminate_w(
      safe_assert_arg const& arg_) noexcept
      : arg{arg_} {}

  template <typename... A>
  [[noreturn]] FOLLY_ERASE void operator()(A... a) const noexcept {
    safe_assert_terminate<P>(&arg, safe_assert_msg_cast_one(a)...);
  }
};

} // namespace detail
} // namespace folly
