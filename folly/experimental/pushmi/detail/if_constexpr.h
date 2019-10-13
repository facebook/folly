/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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


// Usage:
//
// PUSHMI_IF_CONSTEXPR((condition)(
//     stmt1;
//     stmt2;
// ) else (
//     stmt3;
//     stmt4;
// ))
//
// If the statements could potentially be ill-formed, you can give some
// part of the expression a dependent type by wrapping it in `id`. For

/**
 *  Maybe_unused indicates that a function, variable or parameter might or
 *  might not be used, e.g.
 *
 *  int foo(FOLLY_MAYBE_UNUSED int x) {
 *    #ifdef USE_X
 *      return x;
 *    #else
 *      return 0;
 *    #endif
 *  }
 */

#if defined(__GNUC__)
#define PUSHMI_CEXP_NOT_ON_WINDOWS 1
#else
#define PUSHMI_CEXP_NOT_ON_WINDOWS 0
#endif

 #ifndef __has_attribute
 #define PUSHMI_CEXP_HAS_ATTRIBUTE(x) 0
 #else
 #define PUSHMI_CEXP_HAS_ATTRIBUTE(x) __has_attribute(x)
 #endif

 #ifndef __has_cpp_attribute
 #define PUSHMI_CEXP_HAS_CPP_ATTRIBUTE(x) 0
 #else
 #define PUSHMI_CEXP_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
 #endif

#if PUSHMI_CEXP_HAS_CPP_ATTRIBUTE(maybe_unused)
#define PUSHMI_CEXP_MAYBE_UNUSED [[maybe_unused]]
#elif PUSHMI_CEXP_HAS_ATTRIBUTE(__unused__) || __GNUC__
#define PUSHMI_CEXP_MAYBE_UNUSED __attribute__((__unused__))
#else
#define PUSHMI_CEXP_MAYBE_UNUSED
#endif

// disable buggy compatibility warning about "requires" and "concept" being
// C++20 keywords.
#if PUSHMI_CEXP_NOT_ON_WINDOWS
#define PUSHMI_CEXP_IGNORE_SHADOW_BEGIN \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wshadow\"") \
    _Pragma("GCC diagnostic ignored \"-Wshadow-local\"") \
    /**/
#define PUSHMI_CEXP_IGNORE_SHADOW_END \
    _Pragma("GCC diagnostic pop")
#else

#define PUSHMI_CEXP_IGNORE_SHADOW_BEGIN
#define PUSHMI_CEXP_IGNORE_SHADOW_END
#endif

#define PUSHMI_CEXP_COMMA ,

#if PUSHMI_CEXP_NOT_ON_WINDOWS
#define PUSHMI_CEXP_EVAL(X, ...) X(__VA_ARGS__)

#define PUSHMI_CEXP_STRIP(...) __VA_ARGS__
#else

// https://stackoverflow.com/questions/5134523/msvc-doesnt-expand-va-args-correctly
#define PUSHMI_CEXP_VA_ARGS_EXPANDER_INNER(X) X
#define PUSHMI_CEXP_VA_ARGS_EXPANDER(X) PUSHMI_CEXP_VA_ARGS_EXPANDER_INNER(X)

#define PUSHMI_CEXP_EVAL(X, ...) PUSHMI_CEXP_VA_ARGS_EXPANDER(X(__VA_ARGS__))

#define PUSHMI_CEXP_STRIP(...) PUSHMI_CEXP_VA_ARGS_EXPANDER(__VA_ARGS__)
#endif

namespace folly {
namespace pushmi {
namespace detail {
struct id_fn {
  constexpr explicit operator bool() const noexcept {
      return false;
  }
  template <class T>
  constexpr T&& operator()(T&& t) const noexcept {
    return (T&&) t;
  }
};
} // namespace detail
} // namespace pushmi
} // namespace folly

#if __cpp_if_constexpr >= 201606

#define PUSHMI_IF_CONSTEXPR(LIST)                         \
  if constexpr (::folly::pushmi::detail::id_fn id = {}) { \
  } else if constexpr                                     \
  PUSHMI_CEXP_EVAL(PUSHMI_IF_CONSTEXPR_ELSE_, PUSHMI_IF_CONSTEXPR_IF_ LIST)

#define PUSHMI_IF_CONSTEXPR_RETURN(LIST)\
  PUSHMI_CEXP_IGNORE_SHADOW_BEGIN \
  PUSHMI_IF_CONSTEXPR(LIST)\
  PUSHMI_CEXP_IGNORE_SHADOW_END \
  /**/

#define PUSHMI_IF_CONSTEXPR_IF_(...) \
  (__VA_ARGS__) PUSHMI_CEXP_COMMA PUSHMI_IF_CONSTEXPR_THEN_

#define PUSHMI_IF_CONSTEXPR_THEN_(...) \
  ({__VA_ARGS__}) PUSHMI_CEXP_COMMA

#define PUSHMI_IF_CONSTEXPR_ELSE_(A, B, C) \
  A PUSHMI_CEXP_STRIP B PUSHMI_IF_CONSTEXPR_ ## C

#define PUSHMI_IF_CONSTEXPR_else(...) \
  else {__VA_ARGS__}

#else

#include <type_traits>

#define PUSHMI_IF_CONSTEXPR(LIST)\
  PUSHMI_CEXP_IGNORE_SHADOW_BEGIN \
  PUSHMI_CEXP_EVAL(PUSHMI_IF_CONSTEXPR_ELSE_, PUSHMI_IF_CONSTEXPR_IF_ LIST)\
  PUSHMI_CEXP_IGNORE_SHADOW_END \
  /**/

#define PUSHMI_IF_CONSTEXPR_RETURN(LIST)\
  PUSHMI_CEXP_IGNORE_SHADOW_BEGIN \
  return PUSHMI_CEXP_EVAL(PUSHMI_IF_CONSTEXPR_ELSE_, PUSHMI_IF_CONSTEXPR_IF_ LIST)\
  PUSHMI_CEXP_IGNORE_SHADOW_END \
  /**/

#define PUSHMI_IF_CONSTEXPR_IF_(...)                                                  \
  (::folly::pushmi::detail::select<bool(__VA_ARGS__)>() ->* PUSHMI_IF_CONSTEXPR_THEN_ \
  /**/

#define PUSHMI_IF_CONSTEXPR_THEN_(...) \
  ([&](PUSHMI_CEXP_MAYBE_UNUSED auto id)mutable->decltype(auto){__VA_ARGS__})) PUSHMI_CEXP_COMMA \
  /**/

#define PUSHMI_IF_CONSTEXPR_ELSE_(A, B) \
  A ->* PUSHMI_IF_CONSTEXPR_ ## B \
  /**/

#define PUSHMI_IF_CONSTEXPR_else(...) \
  ([&](PUSHMI_CEXP_MAYBE_UNUSED auto id)mutable->decltype(auto){__VA_ARGS__});\
  /**/

namespace folly {
namespace pushmi {
namespace detail {

template <bool>
struct select {
  template <class R, class = std::enable_if_t<!std::is_void<R>::value>>
  struct eat_return {
    R value_;
    template <class T>
    constexpr R operator->*(T&&) {
      return static_cast<R&&>(value_);
    }
  };
  struct eat {
    template <class T>
    constexpr void operator->*(T&&) {}
  };
  template <class T>
  constexpr auto operator->*(T&& t)
      -> eat_return<decltype(t(::folly::pushmi::detail::id_fn{}))> {
    return {t(::folly::pushmi::detail::id_fn{})};
  }
  template <class T>
  constexpr auto operator->*(T&& t) const -> eat {
    return t(::folly::pushmi::detail::id_fn{}), void(), eat{};
  }
};

template <>
struct select<false> {
  struct eat {
    template <class T>
    constexpr auto operator->*(T&& t) -> decltype(auto) {
      return t(::folly::pushmi::detail::id_fn{});
    }
  };
  template <class T>
  constexpr eat operator->*(T&&) {
    return {};
  }
};
} // namespace detail
} // namespace pushmi
} // namespace folly
#endif
