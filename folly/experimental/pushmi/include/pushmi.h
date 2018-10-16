#pragma once
#ifndef PUSHMI_SINGLE_HEADER
#define PUSHMI_SINGLE_HEADER

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <cstddef>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <exception>
#include <functional>
#include <algorithm>
#include <utility>
#include <memory>
#include <type_traits>
#include <initializer_list>

#include <thread>
#include <future>
#include <tuple>
#include <deque>
#include <vector>
#include <queue>

#if __cpp_lib_optional >= 201606
#include <optional>
#endif
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.


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

#define PUSHMI_COMMA ,

#define PUSHMI_EVAL(F, ...) F(__VA_ARGS__)

#define PUSHMI_STRIP(...) __VA_ARGS__

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
}
}

#if __cpp_if_constexpr >= 201606

#define PUSHMI_IF_CONSTEXPR(LIST)\
  if constexpr (::pushmi::detail::id_fn id = {}) {} \
  else if constexpr PUSHMI_EVAL(PUSHMI_IF_CONSTEXPR_ELSE_, PUSHMI_IF_CONSTEXPR_IF_ LIST)

#define PUSHMI_IF_CONSTEXPR_RETURN(LIST)\
  PUSHMI_IF_CONSTEXPR(LIST)\
  /**/

#define PUSHMI_IF_CONSTEXPR_IF_(...) \
  (__VA_ARGS__) PUSHMI_COMMA PUSHMI_IF_CONSTEXPR_THEN_

#define PUSHMI_IF_CONSTEXPR_THEN_(...) \
  ({__VA_ARGS__}) PUSHMI_COMMA

#define PUSHMI_IF_CONSTEXPR_ELSE_(A, B, C) \
  A PUSHMI_STRIP B PUSHMI_IF_CONSTEXPR_ ## C

#define PUSHMI_IF_CONSTEXPR_else(...) \
  else {__VA_ARGS__}

#else

//#include <type_traits>

#define PUSHMI_IF_CONSTEXPR(LIST)\
  PUSHMI_EVAL(PUSHMI_IF_CONSTEXPR_ELSE_, PUSHMI_IF_CONSTEXPR_IF_ LIST)\
  /**/

#define PUSHMI_IF_CONSTEXPR_RETURN(LIST)\
  return PUSHMI_EVAL(PUSHMI_IF_CONSTEXPR_ELSE_, PUSHMI_IF_CONSTEXPR_IF_ LIST)\
  /**/

#define PUSHMI_IF_CONSTEXPR_IF_(...) \
  (::pushmi::detail::select<bool(__VA_ARGS__)>() ->* PUSHMI_IF_CONSTEXPR_THEN_ \
  /**/

#define PUSHMI_IF_CONSTEXPR_THEN_(...) \
  ([&](auto id)mutable->decltype(auto){__VA_ARGS__})) PUSHMI_COMMA \
  /**/

#define PUSHMI_IF_CONSTEXPR_ELSE_(A, B) \
  A ->* PUSHMI_IF_CONSTEXPR_ ## B \
  /**/

#define PUSHMI_IF_CONSTEXPR_else(...) \
  ([&](auto id)mutable->decltype(auto){__VA_ARGS__});\
  /**/

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
    constexpr auto operator->*(T&& t) -> eat_return<decltype(t(::pushmi::detail::id_fn{}))> {
        return {t(::pushmi::detail::id_fn{})};
    }
    template <class T>
    constexpr auto operator->*(T&& t) const -> eat {
        return t(::pushmi::detail::id_fn{}), void(), eat{};
    }
};

template <>
struct select<false> {
    struct eat {
        template <class T>
        constexpr auto operator->*(T&& t) -> decltype(auto) {
            return t(::pushmi::detail::id_fn{});
        }
    };
    template <class T>
    constexpr eat operator->*(T&& t) {
        return {};
    }
};
}
}
#endif
// clang-format off
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <type_traits>

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

// disable buggy compatibility warning about "requires" and "concept" being
// C++20 keywords.
#if defined(__clang__) && not defined(__APPLE__)
#define PUSHMI_PP_IGNORE_CXX2A_COMPAT_BEGIN \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wunknown-pragmas\"") \
    _Pragma("GCC diagnostic ignored \"-Wpragmas\"") \
    _Pragma("GCC diagnostic ignored \"-Wc++2a-compat\"") \
    _Pragma("GCC diagnostic ignored \"-Wfloat-equal\"") \
    /**/
#define PUSHMI_PP_IGNORE_CXX2A_COMPAT_END \
    _Pragma("GCC diagnostic pop")
#elif defined(__clang__) && defined(__APPLE__)
#define PUSHMI_PP_IGNORE_CXX2A_COMPAT_BEGIN \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wunknown-pragmas\"") \
    _Pragma("GCC diagnostic ignored \"-Wpragmas\"") \
    _Pragma("GCC diagnostic ignored \"-Wfloat-equal\"") \
    /**/
#define PUSHMI_PP_IGNORE_CXX2A_COMPAT_END \
    _Pragma("GCC diagnostic pop")
#else

#define PUSHMI_PP_IGNORE_CXX2A_COMPAT_BEGIN
#define PUSHMI_PP_IGNORE_CXX2A_COMPAT_END
// #pragma GCC diagnostic push
// #pragma GCC diagnostic ignored "-Wunknown-pragmas"
// #pragma GCC diagnostic ignored "-Wpragmas"
// #pragma GCC diagnostic ignored "-Wc++2a-compat"
#endif

PUSHMI_PP_IGNORE_CXX2A_COMPAT_BEGIN

#if __cpp_inline_variables >= 201606
#define PUSHMI_INLINE_VAR inline
#else
#define PUSHMI_INLINE_VAR
#endif

#ifdef __clang__
#define PUSHMI_PP_IS_SAME(...) __is_same(__VA_ARGS__)
#elif defined(__GNUC__) && __GNUC__ >= 6 && !defined(__NVCC__)
#define PUSHMI_PP_IS_SAME(...) __is_same_as(__VA_ARGS__)
#else
#define PUSHMI_PP_IS_SAME(...) std::is_same<__VA_ARGS__>::value
#endif

#ifdef __clang__
#define PUSHMI_PP_IS_CONSTRUCTIBLE(...)  __is_constructible(__VA_ARGS__)
#elif defined(__GNUC__) && __GNUC__ >= 8
#define PUSHMI_PP_IS_CONSTRUCTIBLE(...)  __is_constructible(__VA_ARGS__)
#else
#define PUSHMI_PP_IS_CONSTRUCTIBLE(...) std::is_constructible<__VA_ARGS__>::value
#endif

#if __COUNTER__ != __COUNTER__
#define PUSHMI_COUNTER __COUNTER__
#else
#define PUSHMI_COUNTER __LINE__
#endif

#define PUSHMI_PP_CHECK(...) PUSHMI_PP_CHECK_N(__VA_ARGS__, 0,)
#define PUSHMI_PP_CHECK_N(x, n, ...) n
#define PUSHMI_PP_PROBE(x) x, 1,

// PUSHMI_CXX_VA_OPT
#ifndef PUSHMI_CXX_VA_OPT
#if __cplusplus > 201703L
#define PUSHMI_CXX_VA_OPT_(...) PUSHMI_PP_CHECK(__VA_OPT__(,) 1)
#define PUSHMI_CXX_VA_OPT PUSHMI_CXX_VA_OPT_(~)
#else
#define PUSHMI_CXX_VA_OPT 0
#endif
#endif // PUSHMI_CXX_VA_OPT

#define PUSHMI_PP_CAT_(X, ...)  X ## __VA_ARGS__
#define PUSHMI_PP_CAT(X, ...)   PUSHMI_PP_CAT_(X, __VA_ARGS__)
#define PUSHMI_PP_CAT2_(X, ...) X ## __VA_ARGS__
#define PUSHMI_PP_CAT2(X, ...)  PUSHMI_PP_CAT2_(X, __VA_ARGS__)

#define PUSHMI_PP_EVAL(X, ...) X(__VA_ARGS__)
#define PUSHMI_PP_EVAL2(X, ...) X(__VA_ARGS__)

#define PUSHMI_PP_EXPAND(...) __VA_ARGS__
#define PUSHMI_PP_EAT(...)

#define PUSHMI_PP_IS_PAREN(x) PUSHMI_PP_CHECK(PUSHMI_PP_IS_PAREN_PROBE x)
#define PUSHMI_PP_IS_PAREN_PROBE(...) PUSHMI_PP_PROBE(~)

#define PUSHMI_PP_COUNT(...)                                                   \
    PUSHMI_PP_COUNT_(__VA_ARGS__,                                              \
        50,49,48,47,46,45,44,43,42,41,40,39,38,37,36,35,34,33,32,31,           \
        30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,           \
        10,9,8,7,6,5,4,3,2,1,)                                                 \
        /**/
#define PUSHMI_PP_COUNT_(                                                      \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,                                   \
    _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,                          \
    _21, _22, _23, _24, _25, _26, _27, _28, _29, _30,                          \
    _31, _32, _33, _34, _35, _36, _37, _38, _39, _40,                          \
    _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, N, ...)                  \
    N                                                                          \
    /**/

#define PUSHMI_PP_IIF(BIT) PUSHMI_PP_CAT_(PUSHMI_PP_IIF_, BIT)
#define PUSHMI_PP_IIF_0(TRUE, ...) __VA_ARGS__
#define PUSHMI_PP_IIF_1(TRUE, ...) TRUE

#define PUSHMI_PP_EMPTY()
#define PUSHMI_PP_COMMA() ,
#define PUSHMI_PP_COMMA_IIF(X)                                                 \
    PUSHMI_PP_IIF(X)(PUSHMI_PP_EMPTY, PUSHMI_PP_COMMA)()

#define PUSHMI_CONCEPT_ASSERT(...)                                             \
    static_assert((bool) (__VA_ARGS__),                                        \
        "Concept assertion failed : " #__VA_ARGS__)

////////////////////////////////////////////////////////////////////////////////
// PUSHMI_CONCEPT_DEF
//   For defining concepts with a syntax symilar to C++20. For example:
//
//     PUSHMI_CONCEPT_DEF(
//         // The Assignable concept from the C++20
//         template(class T, class U)
//         concept Assignable,
//             requires (T t, U &&u) (
//                 t = (U &&) u,
//                 ::pushmi::concepts::requires_<Same<decltype(t = (U &&) u), T>>
//             ) &&
//             std::is_lvalue_reference_v<T>
//     );
#define PUSHMI_CONCEPT_DEF(DECL, ...)                                          \
    PUSHMI_PP_EVAL(                                                            \
        PUSHMI_PP_DECL_DEF,                                                    \
        PUSHMI_PP_CAT(PUSHMI_PP_DEF_DECL_, DECL),                              \
        __VA_ARGS__)                                                           \
    /**/
#define PUSHMI_PP_DECL_DEF_NAME(...)                                           \
    PUSHMI_PP_CAT(PUSHMI_PP_DEF_, __VA_ARGS__),                                \
    /**/
#define PUSHMI_PP_DECL_DEF(TPARAM, NAME, ...)                                  \
    PUSHMI_PP_CAT(PUSHMI_PP_DECL_DEF_, PUSHMI_PP_IS_PAREN(NAME))(              \
        TPARAM,                                                                \
        NAME,                                                                  \
        __VA_ARGS__)                                                           \
    /**/
// The defn is of the form:
//   template(class A, class B = void, class... Rest)
//   (concept Name)(A, B, Rest...),
//      // requirements...
#define PUSHMI_PP_DECL_DEF_1(TPARAM, NAME, ...)                                \
    PUSHMI_PP_EVAL2(                                                           \
        PUSHMI_PP_DECL_DEF_IMPL,                                               \
        TPARAM,                                                                \
        PUSHMI_PP_DECL_DEF_NAME NAME,                                          \
        __VA_ARGS__)                                                           \
    /**/
// The defn is of the form:
//   template(class A, class B)
//   concept Name,
//      // requirements...
// Compute the template arguments (A, B) from the template introducer.
#define PUSHMI_PP_DECL_DEF_0(TPARAM, NAME, ...)                                \
    PUSHMI_PP_DECL_DEF_IMPL(                                                   \
        TPARAM,                                                                \
        PUSHMI_PP_CAT(PUSHMI_PP_DEF_, NAME),                                   \
        (PUSHMI_PP_CAT(PUSHMI_PP_AUX_, TPARAM)),                               \
        __VA_ARGS__)                                                           \
    /**/
// Expand the template definition into a struct and template alias like:
//    struct NameConcept {
//      template<class A, class B>
//      static auto Requires_(/* args (optional)*/) ->
//          decltype(/*requirements...*/);
//      template<class A, class B>
//      static constexpr auto is_satisfied_by(int) ->
//          decltype(bool(&Requires_<A,B>)) { return true; }
//      template<class A, class B>
//      static constexpr bool is_satisfied_by(long) { return false; }
//    };
//    template<class A, class B>
//    inline constexpr bool Name = NameConcept::is_satisfied_by<A, B>(0);
#if __cpp_concepts
// No requires expression
#define PUSHMI_PP_DEF_IMPL_0(...)                                              \
    __VA_ARGS__                                                                \
    /**/
// Requires expression
#define PUSHMI_PP_DEF_IMPL_1(...)                                              \
    PUSHMI_PP_CAT(PUSHMI_PP_DEF_IMPL_1_, __VA_ARGS__)                          \
    /**/
#define PUSHMI_PP_DEF_IMPL_1_requires                                          \
    requires PUSHMI_PP_DEF_IMPL_1_REQUIRES                                     \
    /**/
#define PUSHMI_PP_DEF_IMPL_1_REQUIRES(...)                                     \
    (__VA_ARGS__) PUSHMI_PP_DEF_IMPL_1_REQUIRES_BODY                           \
    /**/
#define PUSHMI_PP_DEF_IMPL_1_REQUIRES_BODY(...)                                \
    { __VA_ARGS__; }                                                           \
    /**/
#define PUSHMI_PP_DECL_DEF_IMPL(TPARAM, NAME, ARGS, ...)                       \
    inline namespace _eager_ {                                                 \
        PUSHMI_PP_CAT(PUSHMI_PP_DEF_, TPARAM)                                  \
        concept bool NAME = PUSHMI_PP_DEF_IMPL(__VA_ARGS__)(__VA_ARGS__);      \
    }                                                                          \
    namespace lazy = _eager_;                                                  \
    /**/
#else
// No requires expression:
#define PUSHMI_PP_DEF_IMPL_0(...)                                              \
    () -> std::enable_if_t<bool(__VA_ARGS__), int>                             \
    /**/
// Requires expression:
#define PUSHMI_PP_DEF_IMPL_1(...)                                              \
    PUSHMI_PP_CAT(PUSHMI_PP_DEF_IMPL_1_, __VA_ARGS__) ), int>                  \
    /**/
#define PUSHMI_PP_DEF_IMPL_1_requires                                          \
    PUSHMI_PP_DEF_IMPL_1_REQUIRES                                              \
    /**/
#define PUSHMI_PP_DEF_IMPL_1_REQUIRES(...)                                     \
    (__VA_ARGS__) -> std::enable_if_t<bool(                                    \
        ::pushmi::concepts::detail::requires_  PUSHMI_PP_DEF_REQUIRES_BODY     \
    /**/
 #define PUSHMI_PP_DEF_REQUIRES_BODY(...)                                      \
    <decltype(__VA_ARGS__, void())>()                                          \
    /**/
#define PUSHMI_PP_DECL_DEF_IMPL(TPARAM, NAME, ARGS, ...)                       \
    struct PUSHMI_PP_CAT(NAME, Concept) {                                      \
        using Concept = PUSHMI_PP_CAT(NAME, Concept);                          \
        PUSHMI_PP_IGNORE_CXX2A_COMPAT_BEGIN                                    \
        PUSHMI_PP_CAT(PUSHMI_PP_DEF_, TPARAM)                                  \
        static auto Requires_ PUSHMI_PP_DEF_IMPL(__VA_ARGS__)(__VA_ARGS__) {   \
            return 0;                                                          \
        }                                                                      \
        PUSHMI_PP_IGNORE_CXX2A_COMPAT_END                                      \
        PUSHMI_PP_CAT(PUSHMI_PP_DEF_, TPARAM)                                  \
        struct Eval {                                                          \
            template <class C_ = Concept>                                      \
            static constexpr decltype(                                         \
                ::pushmi::concepts::detail::gcc_bugs(                          \
                    &C_::template Requires_<PUSHMI_PP_EXPAND ARGS>))           \
            impl(int) noexcept { return true; }                                \
            static constexpr bool impl(long) noexcept { return false; }        \
            explicit constexpr operator bool() const noexcept {                \
                return Eval::impl(0);                                          \
            }                                                                  \
            template <class PMThis = Concept, bool PMB,                        \
              class = std::enable_if_t<PMB == (bool) PMThis{}>>                \
            constexpr operator std::integral_constant<bool, PMB>() const noexcept {\
                return {};                                                     \
            }                                                                  \
            constexpr auto operator!() const noexcept {                        \
                return ::pushmi::concepts::detail::Not<Eval>{};                \
            }                                                                  \
            template <class That>                                              \
            constexpr auto operator&&(That) const noexcept {                   \
                return ::pushmi::concepts::detail::And<Eval, That>{};          \
            }                                                                  \
            template <class That>                                              \
            constexpr auto operator||(That) const noexcept {                   \
                return ::pushmi::concepts::detail::Or<Eval, That>{};           \
            }                                                                  \
        };                                                                     \
    };                                                                         \
    PUSHMI_PP_CAT(PUSHMI_PP_DEF_, TPARAM)                                      \
    PUSHMI_INLINE_VAR constexpr bool NAME =                                    \
        (bool)PUSHMI_PP_CAT(NAME, Concept)::Eval<PUSHMI_PP_EXPAND ARGS>{};     \
    namespace lazy {                                                           \
        PUSHMI_PP_CAT(PUSHMI_PP_DEF_, TPARAM)                                  \
        PUSHMI_INLINE_VAR constexpr auto NAME =                                \
            PUSHMI_PP_CAT(NAME, Concept)::Eval<PUSHMI_PP_EXPAND ARGS>{};       \
    }                                                                          \
    /**/
#endif

#define PUSHMI_PP_REQUIRES_PROBE_requires                                      \
    PUSHMI_PP_PROBE(~)                                                         \
    /**/
#define PUSHMI_PP_DEF_IMPL(REQUIRES, ...)                                      \
    PUSHMI_PP_CAT(                                                             \
        PUSHMI_PP_DEF_IMPL_,                                                   \
        PUSHMI_PP_CHECK(PUSHMI_PP_CAT(PUSHMI_PP_REQUIRES_PROBE_, REQUIRES)))   \
    /**/
#define PUSHMI_PP_DEF_DECL_template(...)                                       \
    template(__VA_ARGS__),                                                     \
    /**/
#define PUSHMI_PP_DEF_template(...)                                            \
    template<__VA_ARGS__>                                                      \
    /**/
#define PUSHMI_PP_DEF_concept
#define PUSHMI_PP_DEF_class
#define PUSHMI_PP_DEF_typename
#define PUSHMI_PP_DEF_int
#define PUSHMI_PP_DEF_bool
#define PUSHMI_PP_DEF_size_t
#define PUSHMI_PP_DEF_unsigned
#define PUSHMI_PP_AUX_template(...)                                            \
    PUSHMI_PP_CAT2(                                                            \
        PUSHMI_PP_TPARAM_,                                                     \
        PUSHMI_PP_COUNT(__VA_ARGS__))(__VA_ARGS__)                             \
    /**/
#define PUSHMI_PP_TPARAM_1(_1)                                                 \
    PUSHMI_PP_CAT2(PUSHMI_PP_DEF_, _1)
#define PUSHMI_PP_TPARAM_2(_1, ...)                                            \
    PUSHMI_PP_CAT2(PUSHMI_PP_DEF_, _1), PUSHMI_PP_TPARAM_1(__VA_ARGS__)
#define PUSHMI_PP_TPARAM_3(_1, ...)                                            \
    PUSHMI_PP_CAT2(PUSHMI_PP_DEF_, _1), PUSHMI_PP_TPARAM_2(__VA_ARGS__)
#define PUSHMI_PP_TPARAM_4(_1, ...)                                            \
    PUSHMI_PP_CAT2(PUSHMI_PP_DEF_, _1), PUSHMI_PP_TPARAM_3(__VA_ARGS__)
#define PUSHMI_PP_TPARAM_5(_1, ...)                                            \
    PUSHMI_PP_CAT2(PUSHMI_PP_DEF_, _1), PUSHMI_PP_TPARAM_4(__VA_ARGS__)

////////////////////////////////////////////////////////////////////////////////
// PUSHMI_TEMPLATE
// Usage:
//   PUSHMI_TEMPLATE (class A, class B)
//     (requires Concept1<A> && Concept2<B>)
//   void foo(A a, B b)
//   {}
// or
//   PUSHMI_TEMPLATE (class A, class B)
//     (requires requires (expr1, expr2, expr3) && Concept1<A> && Concept2<B>)
//   void foo(A a, B b)
//   {}
#if __cpp_concepts
#define PUSHMI_TEMPLATE(...)                                                   \
    template<__VA_ARGS__> PUSHMI_TEMPLATE_AUX_                                 \
    /**/
#define PUSHMI_TEMPLATE_AUX_(...)                                              \
    PUSHMI_TEMPLATE_AUX_4(PUSHMI_PP_CAT(PUSHMI_TEMPLATE_AUX_3_, __VA_ARGS__))  \
    /**/
#define PUSHMI_TEMPLATE_AUX_3_requires
#define PUSHMI_TEMPLATE_AUX_4(...)                                             \
    PUSHMI_TEMPLATE_AUX_5(__VA_ARGS__)(__VA_ARGS__)                            \
    /**/
#define PUSHMI_TEMPLATE_AUX_5(REQUIRES, ...)                                   \
    PUSHMI_PP_CAT(                                                             \
        PUSHMI_TEMPLATE_AUX_5_,                                                \
        PUSHMI_PP_CHECK(PUSHMI_PP_CAT(PUSHMI_PP_REQUIRES_PROBE_, REQUIRES)))   \
    /**/
// No requires expression:
#define PUSHMI_TEMPLATE_AUX_5_0(...)                                           \
    requires __VA_ARGS__                                                       \
    /**/
// Requires expression
#define PUSHMI_TEMPLATE_AUX_5_1(...)                                           \
    PUSHMI_PP_CAT(PUSHMI_TEMPLATE_AUX_6_, __VA_ARGS__)                         \
    /**/
#define PUSHMI_TEMPLATE_AUX_6_requires(...)\
    requires requires { __VA_ARGS__; }
#else
#define PUSHMI_TEMPLATE(...)                                                   \
    template<__VA_ARGS__ PUSHMI_TEMPLATE_AUX_
#define PUSHMI_TEMPLATE_AUX_(...) ,                                            \
    int (*PUSHMI_PP_CAT(_pushmi_concept_unique_, __LINE__))[                   \
        PUSHMI_COUNTER] = nullptr,                                             \
    std::enable_if_t<PUSHMI_PP_CAT(_pushmi_concept_unique_, __LINE__) ||       \
        bool(PUSHMI_TEMPLATE_AUX_4(PUSHMI_PP_CAT(                              \
            PUSHMI_TEMPLATE_AUX_3_, __VA_ARGS__))), int> = 0>                  \
    /**/
#define PUSHMI_TEMPLATE_AUX_3_requires
#define PUSHMI_TEMPLATE_AUX_4(...)                                             \
    PUSHMI_TEMPLATE_AUX_5(__VA_ARGS__)(__VA_ARGS__)                            \
    /**/
#define PUSHMI_TEMPLATE_AUX_5(REQUIRES, ...)                                   \
    PUSHMI_PP_CAT(                                                             \
        PUSHMI_TEMPLATE_AUX_5_,                                                \
        PUSHMI_PP_CHECK(PUSHMI_PP_CAT(PUSHMI_PP_REQUIRES_PROBE_, REQUIRES)))   \
    /**/
// No requires expression:
#define PUSHMI_TEMPLATE_AUX_5_0(...)                                           \
    __VA_ARGS__                                                                \
    /**/
#define PUSHMI_TEMPLATE_AUX_5_1(...)                                           \
    PUSHMI_PP_CAT(PUSHMI_TEMPLATE_AUX_6_, __VA_ARGS__)                         \
    /**/
#define PUSHMI_TEMPLATE_AUX_6_requires(...)                                    \
    ::pushmi::concepts::detail::requires_<decltype(__VA_ARGS__)>()             \
    /**/
#endif


#if __cpp_concepts
#define PUSHMI_BROKEN_SUBSUMPTION(...)
#define PUSHMI_TYPE_CONSTRAINT(...) __VA_ARGS__
#define PUSHMI_EXP(...) __VA_ARGS__
#define PUSHMI_AND &&
#else
#define PUSHMI_BROKEN_SUBSUMPTION(...) __VA_ARGS__
#define PUSHMI_TYPE_CONSTRAINT(...) class
// bool() is used to prevent 'error: pasting "PUSHMI_PP_REQUIRES_PROBE_" and "::" does not give a valid preprocessing token'
#define PUSHMI_EXP(...) bool(::pushmi::expAnd(__VA_ARGS__))
#define PUSHMI_AND ,
#endif


#if __cpp_concepts
#define PUSHMI_PP_CONSTRAINED_USING(REQUIRES, NAME, ...)                       \
    requires REQUIRES                                                          \
  using NAME __VA_ARGS__;                                                      \
  /**/
#else
#define PUSHMI_PP_CONSTRAINED_USING(REQUIRES, NAME, ...)                       \
  using NAME std::enable_if_t<bool(REQUIRES), __VA_ARGS__>;                    \
  /**/
#endif

namespace pushmi {

template <bool B>
using bool_ = std::integral_constant<bool, B>;

namespace concepts {
namespace detail {
bool gcc_bugs(...);

template <class>
inline constexpr bool requires_() {
  return true;
}

template <class T, class U>
struct And;
template <class T, class U>
struct Or;

template <class T>
struct Not {
    explicit constexpr operator bool() const noexcept {
        return !(bool) T{};
    }
    PUSHMI_TEMPLATE (class This = Not, bool B)
        (requires B == (bool) This{})
    constexpr operator std::integral_constant<bool, B>() const noexcept {
        return {};
    }
    constexpr auto operator!() const noexcept {
        return T{};
    }
    template <class That>
    constexpr auto operator&&(That) const noexcept {
        return And<Not, That>{};
    }
    template <class That>
    constexpr auto operator||(That) const noexcept {
        return Or<Not, That>{};
    }
};

template <class T, class U>
struct And {
    explicit constexpr operator bool() const noexcept {
        return (bool) std::conditional_t<(bool) T{}, U, std::false_type>{};
    }
    PUSHMI_TEMPLATE (class This = And, bool B)
        (requires B == (bool) This{})
    constexpr operator std::integral_constant<bool, B>() const noexcept {
        return {};
    }
    constexpr auto operator!() const noexcept {
        return Not<And>{};
    }
    template <class That>
    constexpr auto operator&&(That) const noexcept {
        return And<And, That>{};
    }
    template <class That>
    constexpr auto operator||(That) const noexcept {
        return Or<And, That>{};
    }
};

template <class T, class U>
struct Or {
    explicit constexpr operator bool() const noexcept {
        return (bool) std::conditional_t<(bool) T{}, std::true_type, U>{};
    }
    PUSHMI_TEMPLATE (class This = Or, bool B)
        (requires B == (bool) This{})
    constexpr operator std::integral_constant<bool, B>() const noexcept {
        return {};
    }
    constexpr auto operator!() const noexcept {
        return Not<Or>{};
    }
    template <class That>
    constexpr auto operator&&(That) const noexcept {
        return And<Or, That>{};
    }
    template <class That>
    constexpr auto operator||(That) const noexcept {
        return Or<Or, That>{};
    }
};
} // namespace detail
} // namespace concepts

namespace isolated {

template<class T0>
constexpr auto expAnd(T0&& t0) {
  return (T0&&)t0;
}
template<class T0, class... TN>
constexpr auto expAnd(T0&& t0, TN&&... tn) {
  return concepts::detail::And<T0, decltype(isolated::expAnd((TN&&)tn...))>{};
}

}

template<class... TN>
constexpr auto expAnd(TN&&... tn) {
  return isolated::expAnd((TN&&)tn...);
}

template <class T>
constexpr bool implicitly_convertible_to(T) {
  return true;
}
#ifdef __clang__
template <bool B>
std::enable_if_t<B> requires_()
{}
#else
template <bool B>
PUSHMI_INLINE_VAR constexpr std::enable_if_t<B, int> requires_ = 0;
#endif
} // namespace pushmi

PUSHMI_PP_IGNORE_CXX2A_COMPAT_END
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <functional>
//#include <type_traits>

//#include "detail/concept_def.h"

#define PUSHMI_NOEXCEPT_AUTO(...) \
  noexcept(noexcept(static_cast<decltype((__VA_ARGS__))>(__VA_ARGS__)))\
  /**/
#define PUSHMI_NOEXCEPT_RETURN(...) \
  PUSHMI_NOEXCEPT_AUTO(__VA_ARGS__) {\
    return (__VA_ARGS__);\
  }\
  /**/

namespace pushmi {
#if __cpp_fold_expressions >= 201603
template <bool...Bs>
PUSHMI_INLINE_VAR constexpr bool and_v = (Bs &&...);

template <bool...Bs>
PUSHMI_INLINE_VAR constexpr bool or_v = (Bs ||...);

template <int...Is>
PUSHMI_INLINE_VAR constexpr int sum_v = (Is +...);
#else
namespace detail {
  template <bool...>
  struct bools;

  template <std::size_t N>
  constexpr int sum_impl(int const (&rgi)[N], int i = 0, int state = 0) noexcept {
    return i == N ? state : sum_impl(rgi, i+1, state + rgi[i]);
  }
  template <int... Is>
  constexpr int sum_impl() noexcept {
    using RGI = int[sizeof...(Is)];
    return sum_impl(RGI{Is...});
  }
} // namespace detail

template <bool...Bs>
PUSHMI_INLINE_VAR constexpr bool and_v =
  PUSHMI_PP_IS_SAME(detail::bools<Bs..., true>, detail::bools<true, Bs...>);

template <bool...Bs>
PUSHMI_INLINE_VAR constexpr bool or_v =
  !PUSHMI_PP_IS_SAME(detail::bools<Bs..., false>, detail::bools<false, Bs...>);

template <int...Is>
PUSHMI_INLINE_VAR constexpr int sum_v = detail::sum_impl<Is...>();
#endif

template <class...>
struct typelist;

template <class...>
using void_t = void;

template <class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

PUSHMI_CONCEPT_DEF(
  template(class... Args)
  (concept True)(Args...),
    true
);

PUSHMI_CONCEPT_DEF(
  template(class T, template<class...> class C)
  (concept Valid)(T, C),
    True< C<T> >
);

PUSHMI_CONCEPT_DEF(
  template (class T, template<class...> class Trait, class... Args)
  (concept Satisfies)(T, Trait, Args...),
    static_cast<bool>(Trait<T>::type::value)
);

PUSHMI_CONCEPT_DEF(
  template (class T, class U)
  concept Same,
    PUSHMI_PP_IS_SAME(T, U) && PUSHMI_PP_IS_SAME(U, T)
);

PUSHMI_CONCEPT_DEF(
  template (bool...Bs)
  (concept And)(Bs...),
    and_v<Bs...>
);

PUSHMI_CONCEPT_DEF(
  template (bool...Bs)
  (concept Or)(Bs...),
    or_v<Bs...>
);

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept Object,
    requires (T* p) (
      *p,
      implicitly_convertible_to<const volatile void*>(p)
    )
);

PUSHMI_CONCEPT_DEF(
  template (class T, class... Args)
  (concept Constructible)(T, Args...),
    PUSHMI_PP_IS_CONSTRUCTIBLE(T, Args...)
);

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept MoveConstructible,
    Constructible<T, T>
);

PUSHMI_CONCEPT_DEF(
  template (class From, class To)
  concept ConvertibleTo,
    requires (From (&f)()) (
      static_cast<To>(f())
    ) && std::is_convertible<From, To>::value
);

PUSHMI_CONCEPT_DEF(
  template (class A, class B)
  concept DerivedFrom,
    __is_base_of(B, A)
);

PUSHMI_CONCEPT_DEF(
  template (class A)
  concept Decayed,
    Same<A, std::decay_t<A>>
);

PUSHMI_CONCEPT_DEF(
  template (class T, class U)
  concept Assignable,
    requires(T t, U&& u) (
      t = (U &&) u,
      requires_<Same<decltype(t = (U &&) u), T>>
    ) && Same<T, T&>
);

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept EqualityComparable,
    requires(remove_cvref_t<T> const & t) (
      implicitly_convertible_to<bool>( t == t ),
      implicitly_convertible_to<bool>( t != t )
    )
);

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept SemiMovable,
    Object<T> && Constructible<T, T> && ConvertibleTo<T, T>
);

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept Movable,
    SemiMovable<T> && Assignable<T&, T>
);

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept Copyable,
    Movable<T> &&
    Assignable<T&, const T&> &&
    ConvertibleTo<const T&, T>
);

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept Semiregular,
    Copyable<T> && Constructible<T>
);

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept Regular,
    Semiregular<T> && EqualityComparable<T>
);

namespace detail {
// is_ taken from meta library

template <typename, template <typename...> class>
struct is_ : std::false_type {};

template <typename... Ts, template <typename...> class C>
struct is_<C<Ts...>, C> : std::true_type {};

template <typename T, template <typename...> class C>
constexpr bool is_v = is_<T, C>::value;

template <bool B, class T = void>
using requires_ = std::enable_if_t<B, T>;

PUSHMI_INLINE_VAR constexpr struct as_const_fn {
  template <class T>
  constexpr const T& operator()(T& t) const noexcept {
    return t;
  }
} const as_const {};

} // namespace detail

} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <functional>

//#include "concept_def.h"

namespace pushmi {

PUSHMI_INLINE_VAR constexpr struct invoke_fn {
private:
  template <class F>
  using mem_fn_t = decltype(std::mem_fn(std::declval<F>()));
public:
  template <class F, class... As>
  auto operator()(F&& f, As&&...as) const
      noexcept(noexcept(((F&&) f)((As&&) as...))) ->
      decltype(((F&&) f)((As&&) as...)) {
    return ((F&&) f)((As&&) as...);
  }
  template <class F, class... As>
  auto operator()(F&& f, As&&...as) const
      noexcept(noexcept(std::declval<mem_fn_t<F>>()((As&&) as...))) ->
      decltype(std::mem_fn(f)((As&&) as...)) {
    return std::mem_fn(f)((As&&) as...);
  }
} invoke {};

template <class F, class...As>
using invoke_result_t =
  decltype(pushmi::invoke(std::declval<F>(), std::declval<As>()...));

PUSHMI_CONCEPT_DEF(
  template (class F, class... Args)
  (concept Invocable)(F, Args...),
    requires(F&& f) (
      pushmi::invoke((F &&) f, std::declval<Args>()...)
    )
);

PUSHMI_CONCEPT_DEF(
  template (class F, class... Args)
  (concept NothrowInvocable)(F, Args...),
    requires(F&& f) (
      requires_<noexcept(pushmi::invoke((F &&) f, std::declval<Args>()...))>
    ) &&
    Invocable<F, Args...>
);

} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#if __cpp_lib_optional >= 201606
//#include <optional>
#endif
//#include <type_traits>

namespace pushmi {
namespace detail {
#if __cpp_lib_optional >= 201606
template <class T>
struct opt : private std::optional<T> {
   opt() = default;
   opt& operator=(T&& t) {
     this->std::optional<T>::operator=(std::move(t));
     return *this;
   }
   using std::optional<T>::operator*;
   using std::optional<T>::operator bool;
};
#else
template <class T>
struct opt {
private:
  bool empty_ = true;
  std::aligned_union_t<0, T> data_;
  T* ptr() {
    return static_cast<T*>((void*)&data_);
  }
  const T* ptr() const {
    return static_cast<const T*>((const void*)&data_);
  }
  void reset() {
    if (!empty_) {
      ptr()->~T();
      empty_ = true;
    }
  }
public:
  opt() = default;
  opt(T&& t) noexcept(std::is_nothrow_move_constructible<T>::value) {
    ::new(ptr()) T(std::move(t));
    empty_ = false;
  }
  opt(const T& t) {
    ::new(ptr()) T(t);
    empty_ = false;
  }
  opt(opt&& that) noexcept(std::is_nothrow_move_constructible<T>::value) {
    if (that) {
      ::new(ptr()) T(std::move(*that));
      empty_ = false;
      that.reset();
    }
  }
  opt(const opt& that) {
    if (that) {
      ::new(ptr()) T(*that);
      empty_ = false;
    }
  }
  ~opt() { reset(); }
  opt& operator=(opt&& that)
    noexcept(std::is_nothrow_move_constructible<T>::value &&
             std::is_nothrow_move_assignable<T>::value) {
    if (*this && that) {
      **this = std::move(*that);
      that.reset();
    } else if (*this) {
      reset();
    } else if (that) {
      ::new(ptr()) T(std::move(*that));
      empty_ = false;
    }
    return *this;
  }
  opt& operator=(const opt& that) {
    if (*this && that) {
      **this = *that;
    } else if (*this) {
      reset();
    } else if (that) {
      ::new(ptr()) T(*that);
      empty_ = false;
    }
    return *this;
  }
  opt& operator=(T&& t) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                 std::is_nothrow_move_assignable<T>::value) {
    if (*this)
      **this = std::move(t);
    else {
      ::new(ptr()) T(std::move(t));
      empty_ = false;
    }
    return *this;
  }
  opt& operator=(const T& t) {
    if (*this)
      **this = t;
    else {
      ::new(ptr()) T(t);
      empty_ = false;
    }
    return *this;
  }
  explicit operator bool() const noexcept {
    return !empty_;
  }
  T& operator*() noexcept {
    return *ptr();
  }
  const T& operator*() const noexcept {
    return *ptr();
  }
};
#endif

} // namespace detail
} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <exception>
//#include <chrono>
//#include "traits.h"

namespace pushmi {

// property_set

template <class T, class = void>
struct property_traits;

template <class T>
struct property_set_traits;

template<class... PropertyN>
struct property_set;

// trait & tag types
template<class...TN>
struct is_single;
template<class...TN>
struct is_many;

template<class...TN>
struct is_flow;

template<class...TN>
struct is_receiver;

template<class...TN>
struct is_sender;

template<class... TN>
struct is_executor;

template<class...TN>
struct is_time;
template<class...TN>
struct is_constrained;

template<class... TN>
struct is_always_blocking;

template<class... TN>
struct is_never_blocking;

template<class... TN>
struct is_maybe_blocking;

template<class... TN>
struct is_fifo_sequence;

template<class... TN>
struct is_concurrent_sequence;

// implementation types

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class receiver;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_receiver;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class many_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class constrained_single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class time_single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_many_sender;

template <
  class E = std::exception_ptr,
  class... VN>
class any_receiver;

template <
  class PE=std::exception_ptr,
  class PV=std::ptrdiff_t,
  class E=PE,
  class... VN>
class any_flow_receiver;

template<
  class E = std::exception_ptr,
  class... VN>
struct any_single_sender;

template<
  class E = std::exception_ptr,
  class... VN>
struct any_many_sender;

template <
  class PE=std::exception_ptr,
  class E=PE,
  class... VN>
class any_flow_single_sender;

template <
  class PE=std::exception_ptr,
  class PV=std::ptrdiff_t,
  class E=PE,
  class... VN>
class any_flow_many_sender;

template<
  class E = std::exception_ptr,
  class C = std::ptrdiff_t,
  class... VN>
struct any_constrained_single_sender;

template<
  class E = std::exception_ptr,
  class TP = std::chrono::system_clock::time_point,
  class... VN>
class any_time_single_sender;

template<
  class E = std::exception_ptr>
struct any_executor;

template<
  class E = std::exception_ptr>
struct any_executor_ref;

template<
  class E = std::exception_ptr,
  class CV = std::ptrdiff_t>
struct any_constrained_executor;

template<
  class E = std::exception_ptr,
  class TP = std::ptrdiff_t>
struct any_constrained_executor_ref;

template<
  class E = std::exception_ptr,
  class TP = std::chrono::system_clock::time_point>
struct any_time_executor;

template<
  class E = std::exception_ptr,
  class TP = std::chrono::system_clock::time_point>
struct any_time_executor_ref;

namespace operators {}
namespace extension_operators {}
namespace aliases {
    namespace v = ::pushmi;
    namespace mi = ::pushmi;
    namespace op = ::pushmi::operators;
    namespace ep = ::pushmi::extension_operators;
}

namespace detail {
  struct any {
    template <class T>
    constexpr any(T&&) noexcept {}
  };
}

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "forwards.h"

namespace pushmi {


// template <class T, class Dual>
// struct entangled {
//   T t;
//   entangled<Dual, T>* dual;
//
//   ~entangled() {
//     if (!!dual) {
//       dual->dual = nullptr;
//     }
//   }
//   explicit entangled(T t) : t(std::move(t)), dual(nullptr) {}
//   entangled(entangled&& o) : t(std::move(o.t)), dual(o.dual) {
//     o.dual = nullptr;
//     if (!!dual) {
//       dual->dual = this;
//     }
//   }
//
//   entangled() = delete;
//   entangled(const entangled&) = delete;
//   entangled& operator=(const entangled&) = delete;
//   entangled& operator=(entangled&&) = delete;
//
//   Dual* lockPointerToDual() {
//     if (!!dual) {
//       return std::addressof(dual->t);
//     }
//     return nullptr;
//   }
//
//   void unlockPointerToDual() {
//   }
// };

// This class can be used to keep a pair of values with pointers to each other
// in sync, even when both objects are allowed to move. Ordinarily you'd do this
// with a heap-allocated, refcounted, control block (or do something else using
// external storage, like a lock table chosen by the current addresses of both
// objects).
// Another thing you could do is have locks, and a backoff strategy for dealing
// with deadlock: lock locally, trylock your dual, if the trylock fails,
// unlock yourself, wait a little while (giving a thread touching the dual a
// chance to acquire the local lock), and repeat. That's kind of ugly.
// This algorithm (which, to be clear, is untested and I haven't really even
// thought through carefully) provides the same guarantees, but without using
// external storage or backoff-based deadlock recovery.

template <class T, class Dual>
struct entangled {
  // must be constructed first so that the other.lockBoth() in the move
  // constructor is run before moving other.t and other.dual
  std::atomic<int> stateMachine;

  T t;
  // In a couple places, we can save on some atomic ops by making this atomic,
  // and adding a "dual == null" fast-path without locking.
  entangled<Dual, T>* dual;

  const static int kUnlocked = 0;
  const static int kLocked = 1;
  const static int kLockedAndLossAcknowledged = 2;

  // Note: *not* thread-safe; it's a bug for two threads to concurrently call
  // lockBoth() on the same entangled (just as it's a bug for two threads to
  // concurrently move from the same object).
  // However, calling lockBoth() on two entangled objects at once is
  // thread-safe.
  // Note also that this may wait indefinitely; it's not the usual non-blocking
  // tryLock().
  bool tryLockBoth() {
    // Try to acquire the local lock. We have to start locally, since local
    // addresses are the only ones we know are safe at first. The rule is, you
    // have to hold *both* locks to write any of either entangled object's
    // metadata, but need only one to read it.
    int expected = kUnlocked;
    if (!stateMachine.compare_exchange_weak(expected, kLocked, std::memory_order_seq_cst, std::memory_order_relaxed)) {
      return false;
    }
    // Having *either* object local-locked protects the data in both objects.
    // Once we hold our lock, no control data can change, in either object.
    if (dual == nullptr) {
      return true;
    }
    expected = kUnlocked;
    if (dual->stateMachine.compare_exchange_strong(expected, kLocked, std::memory_order_seq_cst)) {
      return true;
    }
    // We got here, and so hit the race; we're deadlocked if we stick to
    // locking. Revert to address-ordering. Note that address-ordering would
    // not be safe on its own, because of the lifetime issues involved; the
    // addresses here are only stable *because* we know both sides are locked,
    // and because of the invariant that you must hold both locks to modify
    // either piece of data.
    if ((uintptr_t)this < (uintptr_t)dual) {
      // I get to win the race. I'll acquire the locks, but have to make sure
      // my memory stays valid until the other thread acknowledges its loss.
      while (stateMachine.load(std::memory_order_relaxed) != kLockedAndLossAcknowledged) {
        // Spin.
      }
      stateMachine.store(kLocked, std::memory_order_relaxed);
      return true;
    } else {
      // I lose the race, but have to coordinate with the winning thread, so
      // that it knows that I'm not about to try to touch it's data
      dual->stateMachine.store(kLockedAndLossAcknowledged, std::memory_order_relaxed);
      return false;
    }
  }

  void lockBoth() {
    while (!tryLockBoth()) {
      // Spin. But, note that all the unbounded spinning in tryLockBoth can be
      // straightforwardly futex-ified. There's a potentialy starvation issue
      // here, but note that it can be dealt with by adding a "priority" bit to
      // the state machine (i.e. if my priority bit is set, the thread for whom
      // I'm the local member of the pair gets to win the race, rather than
      // using address-ordering).
    }
  }

  void unlockBoth() {
    // Note that unlocking locally and then remotely is the right order. There
    // are no concurrent accesses to this object (as an API constraint -- lock
    // and unlock are not thread safe!), and no other thread will touch the
    // other object so long as its locked. Going in the other order could let
    // another thread incorrectly think we're going down the deadlock-avoidance
    // path in tryLock().
    stateMachine.store(kUnlocked, std::memory_order_release);
    if (dual != nullptr) {
      dual->stateMachine.store(kUnlocked, std::memory_order_release);
    }
  }

  entangled() = delete;
  entangled(const entangled&) = delete;
  entangled& operator=(const entangled&) = delete;
  entangled& operator=(entangled&&) = delete;

  explicit entangled(T t)
      : t(std::move(t)), dual(nullptr), stateMachine(kUnlocked) {}
  entangled(entangled&& other)
      : stateMachine((other.lockBoth(), kLocked)),
        t(std::move(other.t)),
        dual(std::move(other.dual)) {
    // Note that, above, we initialized stateMachine to the locked state; the
    // address of this object hasn't escaped yet, and won't (until we unlock
    // the dual), so it doesn't *really* matter, but it's conceptually helpful
    // to maintain that invariant.

    // Update our dual's data.
    if (dual != nullptr) {
      dual->dual = this;
    }

    // Update other's data.
    other.dual = nullptr;
    // unlock other so that its destructor can complete
    other.stateMachine.store(kUnlocked);

    // We locked on other, but will unlock on *this. The locking protocol
    // ensured that no accesses to other will occur after lock() returns, and
    // since then we updated dual's dual to be us.
    unlockBoth();
  }

  ~entangled() {
    lockBoth();
    if (dual != nullptr) {
      dual->dual = nullptr;
    }
    unlockBoth();
  }

  // Must unlock later even if dual is nullptr. This is fixable.
  Dual* lockPointerToDual() {
    lockBoth();
    return !!dual ? std::addressof(dual->t) : nullptr;
  }

  void unlockPointerToDual() {
    unlockBoth();
  }
};

template <class First, class Second>
using entangled_pair = std::pair<entangled<First, Second>, entangled<Second, First>>;

template <class First, class Second>
auto entangle(First f, Second s)
    -> entangled_pair<First, Second> {
  entangled<First, Second> ef(std::move(f));
  entangled<Second, First> es(std::move(s));
  ef.dual = std::addressof(es);
  es.dual = std::addressof(ef);
  return {std::move(ef), std::move(es)};
}

template <class T, class Dual>
struct locked_entangled_pair : std::pair<T*, Dual*> {
  entangled<T, Dual>* e;
  ~locked_entangled_pair(){if (!!e) {e->unlockBoth();}}
  explicit locked_entangled_pair(entangled<T, Dual>& e) : e(std::addressof(e)){
    this->e->lockBoth();
    this->first = std::addressof(this->e->t);
    this->second = !!this->e->dual ? std::addressof(this->e->dual->t) : nullptr;
  }
  locked_entangled_pair() = delete;
  locked_entangled_pair(const locked_entangled_pair&) = delete;
  locked_entangled_pair& operator=(const locked_entangled_pair&) = delete;
  locked_entangled_pair(locked_entangled_pair&& o) : std::pair<T*, Dual*>(o), e(o.e) {o.e = nullptr;};
  locked_entangled_pair& operator=(locked_entangled_pair&& o){
    static_cast<std::pair<T*, Dual*>&>(*this) = static_cast<std::pair<T*, Dual*>&&>(o);
    e = o.e;
    o.e = nullptr;
    return *this;
  }
};

template <class T, class Dual>
locked_entangled_pair<T, Dual> lock_both(entangled<T, Dual>& e){
  return locked_entangled_pair<T, Dual>{e};
}

template <class T, class Dual>
struct shared_entangled : std::shared_ptr<T> {
  Dual* dual;
  std::mutex* lock;

  template<class P>
  explicit shared_entangled(std::shared_ptr<P>& p, T& t, Dual& d, std::mutex& l) :
    std::shared_ptr<T>(p, std::addressof(t)), dual(std::addressof(d)), lock(std::addressof(l)){
  }
  shared_entangled() = delete;
};

template <class First, class Second>
using shared_entangled_pair = std::pair<shared_entangled<First, Second>, shared_entangled<Second, First>>;

template <class First, class Second>
auto shared_entangle(First f, Second s)
    -> shared_entangled_pair<First, Second> {
  struct storage {
    storage(First&& f, Second&& s) : p((First&&) f, (Second&&) s) {}
    std::tuple<First, Second> p;
    std::mutex lock;
  };
  auto p = std::make_shared<storage>(std::move(f), std::move(s));
  shared_entangled<First, Second> ef(p, std::get<0>(p->p), std::get<1>(p->p), p->lock);
  shared_entangled<Second, First> es(p, std::get<1>(p->p), std::get<0>(p->p), p->lock);
  return {std::move(ef), std::move(es)};
}

template <class T, class Dual>
struct locked_shared_entangled_pair : std::pair<T*, Dual*> {
  shared_entangled<T, Dual> e;
  ~locked_shared_entangled_pair() {
    if (!!e && !!e.lock) {
      e.lock->unlock();
    }
  }
  explicit locked_shared_entangled_pair(shared_entangled<T, Dual>& e) : e(std::move(e)){
    this->e.lock->lock();
    this->first = this->e.get();
    this->second = this->e.dual;
  }
  locked_shared_entangled_pair() = delete;
};


template <class T, class Dual>
locked_shared_entangled_pair<T, Dual> lock_both(shared_entangled<T, Dual>& e){
  return locked_shared_entangled_pair<T, Dual>{e};
}

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <future>
//#include <functional>

//#include "traits.h"

namespace pushmi {
namespace __adl {

//
// support methods on a class reference
//

PUSHMI_TEMPLATE (class S)
  (requires requires (std::declval<S&>().done()))
void set_done(S& s) noexcept(noexcept(s.done())) {
  s.done();
}
PUSHMI_TEMPLATE (class S, class E)
  (requires requires (std::declval<S&>().error(std::declval<E>())))
void set_error(S& s, E e) noexcept(noexcept(s.error(std::move(e)))) {
  s.error(std::move(e));
}
PUSHMI_TEMPLATE (class S, class... VN)
  (requires requires (std::declval<S&>().value(std::declval<VN&&>()...)))
void set_value(S& s, VN&&... vn) noexcept(noexcept(s.value((VN&&) vn...))) {
  s.value((VN&&) vn...);
}

PUSHMI_TEMPLATE (class S, class Up)
  (requires requires (std::declval<S&>().starting(std::declval<Up&&>())))
void set_starting(S& s, Up&& up) noexcept(noexcept(s.starting((Up&&) up))) {
  s.starting((Up&&) up);
}

PUSHMI_TEMPLATE (class SD)
  (requires requires (std::declval<SD&>().executor()))
auto executor(SD& sd) noexcept(noexcept(sd.executor())) {
  return sd.executor();
}

PUSHMI_TEMPLATE (class SD, class Out)
  (requires requires (
    std::declval<SD&>().submit(std::declval<Out>())
  ))
void submit(SD& sd, Out out) noexcept(noexcept(sd.submit(std::move(out)))) {
  sd.submit(std::move(out));
}

PUSHMI_TEMPLATE (class SD)
  (requires requires (std::declval<SD&>().top()))
auto top(SD& sd) noexcept(noexcept(sd.top())) {
  return sd.top();
}

PUSHMI_TEMPLATE (class SD, class TP, class Out)
  (requires requires (
    std::declval<SD&>().submit(
        std::declval<TP(&)(TP)>()(top(std::declval<SD&>())),
        std::declval<Out>())
  ))
void submit(SD& sd, TP tp, Out out)
  noexcept(noexcept(sd.submit(std::move(tp), std::move(out)))) {
  sd.submit(std::move(tp), std::move(out));
}

//
// support methods on a class pointer
//

PUSHMI_TEMPLATE (class S)
  (requires requires (std::declval<S&>()->done()))
void set_done(S& s) noexcept(noexcept(s->done())) {
  s->done();
}
PUSHMI_TEMPLATE (class S, class E)
  (requires requires (std::declval<S&>()->error(std::declval<E>())))
void set_error(S& s, E e) noexcept(noexcept(s->error(std::move(e)))) {
  s->error(std::move(e));
}
PUSHMI_TEMPLATE (class S, class... VN)
  (requires requires (std::declval<S&>()->value(std::declval<VN&&>()...)))
void set_value(S& s, VN&&... vn) noexcept(noexcept(s->value((VN&&) vn...))) {
  s->value((VN&&) vn...);
}

PUSHMI_TEMPLATE (class S, class Up)
  (requires requires (std::declval<S&>()->starting(std::declval<Up&&>())))
void set_starting(S& s, Up&& up) noexcept(noexcept(s->starting((Up&&) up))) {
  s->starting((Up&&) up);
}

PUSHMI_TEMPLATE (class SD)
  (requires requires (std::declval<SD&>()->executor()))
auto executor(SD& sd) noexcept(noexcept(sd->executor())) {
  return sd->executor();
}

PUSHMI_TEMPLATE (class SD, class Out)
  (requires requires (std::declval<SD&>()->submit(std::declval<Out>())))
void submit(SD& sd, Out out) noexcept(noexcept(sd->submit(std::move(out)))) {
  sd->submit(std::move(out));
}

PUSHMI_TEMPLATE (class SD)
  (requires requires (std::declval<SD&>()->top()))
auto top(SD& sd) noexcept(noexcept(sd->top())) {
  return sd->top();
}

PUSHMI_TEMPLATE (class SD, class TP, class Out)
  (requires requires (
    std::declval<SD&>()->submit(
        std::declval<TP(&)(TP)>()(top(std::declval<SD&>())),
        std::declval<Out>())
  ))
void submit(SD& sd, TP tp, Out out)
  noexcept(noexcept(sd->submit(std::move(tp), std::move(out)))) {
  sd->submit(std::move(tp), std::move(out));
}

//
// add support for std::promise externally
//

// std::promise does not support the done signal.
// either set_value or set_error must be called
template <class T>
void set_done(std::promise<T>& p) noexcept {}

template <class T>
void set_error(std::promise<T>& p, std::exception_ptr e) noexcept {
  p.set_exception(std::move(e));
}
template <class T, class E>
void set_error(std::promise<T>& p, E e) noexcept {
  p.set_exception(std::make_exception_ptr(std::move(e)));
}
template <class T>
void set_value(std::promise<T>& p, T t) noexcept(noexcept(p.set_value(std::move(t)))) {
  p.set_value(std::move(t));
}
inline void set_value(std::promise<void>& p) noexcept(noexcept(p.set_value())) {
  p.set_value();
}

//
// support reference_wrapper
//

PUSHMI_TEMPLATE (class S)
  (requires requires ( set_done(std::declval<S&>()) ))
void set_done(std::reference_wrapper<S> s) noexcept(
  noexcept(set_done(s.get()))) {
  set_done(s.get());
}
PUSHMI_TEMPLATE (class S, class E)
  (requires requires ( set_error(std::declval<S&>(), std::declval<E>()) ))
void set_error(std::reference_wrapper<S> s, E e) noexcept {
  set_error(s.get(), std::move(e));
}
PUSHMI_TEMPLATE (class S, class... VN)
  (requires requires ( set_value(std::declval<S&>(), std::declval<VN&&>()...) ))
void set_value(std::reference_wrapper<S> s, VN&&... vn) noexcept(
  noexcept(set_value(s.get(), (VN&&) vn...))) {
  set_value(s.get(), (VN&&) vn...);
}
PUSHMI_TEMPLATE (class S, class Up)
  (requires requires ( set_starting(std::declval<S&>(), std::declval<Up&&>()) ))
void set_starting(std::reference_wrapper<S> s, Up&& up) noexcept(
  noexcept(set_starting(s.get(), (Up&&) up))) {
  set_starting(s.get(), (Up&&) up);
}
PUSHMI_TEMPLATE (class SD)
  (requires requires ( executor(std::declval<SD&>()) ))
auto executor(std::reference_wrapper<SD> sd) noexcept(noexcept(executor(sd.get()))) {
  return executor(sd.get());
}
PUSHMI_TEMPLATE (class SD, class Out)
  (requires requires ( submit(std::declval<SD&>(), std::declval<Out>()) ))
void submit(std::reference_wrapper<SD> sd, Out out) noexcept(
  noexcept(submit(sd.get(), std::move(out)))) {
  submit(sd.get(), std::move(out));
}

PUSHMI_TEMPLATE (class SD)
  (requires requires ( top(std::declval<SD&>()) ))
auto top(std::reference_wrapper<SD> sd) noexcept(noexcept(top(sd.get()))) {
  return top(sd.get());
}
PUSHMI_TEMPLATE (class SD, class TP, class Out)
  (requires requires (
    submit(
      std::declval<SD&>(),
      std::declval<TP(&)(TP)>()(top(std::declval<SD&>())),
      std::declval<Out>())
  ))
void submit(std::reference_wrapper<SD> sd, TP tp, Out out)
  noexcept(noexcept(submit(sd.get(), std::move(tp), std::move(out)))) {
  submit(sd.get(), std::move(tp), std::move(out));
}

//
// accessors for free functions in this namespace
//

struct set_done_fn {
  PUSHMI_TEMPLATE (class S)
    (requires requires (
      set_done(std::declval<S&>()),
      set_error(std::declval<S&>(), std::current_exception())
    ))
  void operator()(S&& s) const noexcept(noexcept(set_done(s))) {
    try {
      set_done(s);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};
struct set_error_fn {
  PUSHMI_TEMPLATE (class S, class E)
    (requires requires (
      set_error(std::declval<S&>(), std::declval<E>())
    ))
  void operator()(S&& s, E e) const
      noexcept(noexcept(set_error(s, std::move(e)))) {
    set_error(s, std::move(e));
  }
};
struct set_value_fn {
  PUSHMI_TEMPLATE (class S, class... VN)
    (requires requires (
      set_value(std::declval<S&>(), std::declval<VN&&>()...),
      set_error(std::declval<S&>(), std::current_exception())
    ))
  void operator()(S&& s, VN&&... vn) const
      noexcept(noexcept(set_value(s, (VN&&) vn...))) {
    try {
      set_value(s, (VN&&) vn...);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};

struct set_starting_fn {
  PUSHMI_TEMPLATE (class S, class Up)
    (requires requires (
      set_starting(std::declval<S&>(), std::declval<Up&&>()),
      set_error(std::declval<S&>(), std::current_exception())
    ))
  void operator()(S&& s, Up&& up) const
    noexcept(noexcept(set_starting(s, (Up&&) up))) {
    try {
      set_starting(s, (Up&&) up);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};

struct get_executor_fn {
  PUSHMI_TEMPLATE (class SD)
    (requires requires (
      executor(std::declval<SD&>())
    ))
  auto operator()(SD&& sd) const noexcept(noexcept(executor(sd))) {
    return executor(sd);
  }
};

struct do_submit_fn {
  PUSHMI_TEMPLATE (class SD, class Out)
    (requires requires (
      submit(std::declval<SD&>(), std::declval<Out>())
    ))
  void operator()(SD&& s, Out out) const
      noexcept(noexcept(submit(s, std::move(out)))) {
    submit(s, std::move(out));
  }

  PUSHMI_TEMPLATE (class SD, class Out)
    (requires requires (
      submit(std::declval<SD&>(), top(std::declval<SD&>()), std::declval<Out>())
    ))
  void operator()(SD&& s, Out out) const
      noexcept(noexcept(submit(s, top(s), std::move(out)))) {
    submit(s, top(s), std::move(out));
  }

  PUSHMI_TEMPLATE (class SD, class TP, class Out)
    (requires requires (
      submit(
        std::declval<SD&>(),
        std::declval<TP>(),
        std::declval<Out>())
    ))
  void operator()(SD&& s, TP tp, Out out) const
      noexcept(noexcept(submit(s, std::move(tp), std::move(out)))) {
    submit(s, std::move(tp), std::move(out));
  }
};

struct get_top_fn {
  PUSHMI_TEMPLATE (class SD)
    (requires requires (
      top(std::declval<SD&>())
    ))
  auto operator()(SD&& sd) const noexcept(noexcept(top(sd))) {
    return top(sd);
  }
};

} // namespace __adl

PUSHMI_INLINE_VAR constexpr __adl::set_done_fn set_done{};
PUSHMI_INLINE_VAR constexpr __adl::set_error_fn set_error{};
PUSHMI_INLINE_VAR constexpr __adl::set_value_fn set_value{};
PUSHMI_INLINE_VAR constexpr __adl::set_starting_fn set_starting{};
PUSHMI_INLINE_VAR constexpr __adl::get_executor_fn executor{};
PUSHMI_INLINE_VAR constexpr __adl::do_submit_fn submit{};
PUSHMI_INLINE_VAR constexpr __adl::get_top_fn now{};
PUSHMI_INLINE_VAR constexpr __adl::get_top_fn top{};

template <class T>
struct property_set_traits<std::promise<T>> {
  using properties = property_set<is_receiver<>>;
};
template <>
struct property_set_traits<std::promise<void>> {
  using properties = property_set<is_receiver<>>;
};

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "traits.h"

namespace pushmi {

// property_set implements a map of category-type to property-type.
// for each category only one property in that category is allowed in the set.

// customization point for a property with a category

template <class T>
using __property_category_t = typename T::property_category;

template <class T, class>
struct property_traits : property_traits<std::decay_t<T>> {
};
template <class T>
struct property_traits<T,
    std::enable_if_t<Decayed<T> && not Valid<T, __property_category_t>>> {
};
template <class T>
struct property_traits<T,
    std::enable_if_t<Decayed<T> && Valid<T, __property_category_t>>> {
  using property_category = __property_category_t<T>;
};

template <class T>
using property_category_t = __property_category_t<property_traits<T>>;

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept Property,
    Valid<T, property_category_t>
);

// in cases where Set contains T, allow T to find itself only once
PUSHMI_CONCEPT_DEF(
  template (class T, class... Set)
  (concept FoundExactlyOnce)(T, Set...),
    sum_v<(PUSHMI_PP_IS_SAME(T, Set) ? 1 : 0)...> == 1
);

PUSHMI_CONCEPT_DEF(
  template (class... PropertyN)
  (concept UniqueCategory)(PropertyN...),
    And<FoundExactlyOnce<property_category_t<PropertyN>,
                         property_category_t<PropertyN>...>...> &&
    And<Property<PropertyN>...>
);

namespace detail {
template <PUSHMI_TYPE_CONSTRAINT(Property) P, class = property_category_t<P>>
struct property_set_element {};
}

template<class... PropertyN>
struct property_set : detail::property_set_element<PropertyN>... {
  static_assert(and_v<Property<PropertyN>...>, "property_set only supports types that match the Property concept");
  static_assert(UniqueCategory<PropertyN...>, "property_set has multiple properties from the same category");
  using properties = property_set;
};

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept PropertySet,
    detail::is_v<T, property_set>
);

// customization point for a type with properties

template <class T>
using __properties_t = typename T::properties;

namespace detail {
template <class T, class = void>
struct property_set_traits_impl : property_traits<std::decay_t<T>> {
};
template <class T>
struct property_set_traits_impl<T,
    std::enable_if_t<Decayed<T> && not Valid<T, __properties_t>>> {
};
template <class T>
struct property_set_traits_impl<T,
    std::enable_if_t<Decayed<T> && Valid<T, __properties_t>>> {
  using properties = __properties_t<T>;
};
} // namespace detail

template <class T>
struct property_set_traits : detail::property_set_traits_impl<T> {
};

template <class T>
using properties_t =
    std::enable_if_t<
        PropertySet<__properties_t<property_set_traits<T>>>,
        __properties_t<property_set_traits<T>>>;

PUSHMI_CONCEPT_DEF(
  template (class T)
  concept Properties,
    Valid<T, properties_t>
);

// find property in the specified set that matches the category of the property specified.
namespace detail {
template <class PIn, class POut>
POut __property_set_index_fn(property_set_element<POut, property_category_t<PIn>>);

template <class PIn, class POut, class...Ps>
property_set<std::conditional_t<PUSHMI_PP_IS_SAME(Ps, PIn), POut, Ps>...>
__property_set_insert_fn(property_set<Ps...>, property_set_element<POut, property_category_t<PIn>>);

template <class PIn, class...Ps>
property_set<Ps..., PIn> __property_set_insert_fn(property_set<Ps...>, ...);

template <class PS, class P>
using property_set_insert_one_t =
  decltype(detail::__property_set_insert_fn<P>(PS{}, PS{}));

template <class PS0, class>
struct property_set_insert {
  using type = PS0;
};

template <class PS0, class P, class... P1>
struct property_set_insert<PS0, property_set<P, P1...>>
  : property_set_insert<property_set_insert_one_t<PS0, P>, property_set<P1...>>
{};
} // namespace detail

template <class PS, class P>
using property_set_index_t =
  std::enable_if_t<
    PropertySet<PS> && Property<P>,
    decltype(detail::__property_set_index_fn<P>(PS{}))>;

template <class PS0, class PS1>
using property_set_insert_t =
  typename std::enable_if_t<
    PropertySet<PS0> && PropertySet<PS1>,
    detail::property_set_insert<PS0, PS1>>::type;

// query for properties on types with properties.

namespace detail {
template<class PIn, class POut>
std::is_base_of<PIn, POut>
property_query_fn(property_set_element<POut, property_category_t<PIn>>*);
template<class PIn>
std::false_type property_query_fn(void*);

template<class PS, class... ExpectedN>
struct property_query_impl : bool_<
  and_v<decltype(property_query_fn<ExpectedN>(
      (properties_t<PS>*)nullptr))::value...>> {};
} //namespace detail

template<class PS, class... ExpectedN>
struct property_query
  : std::conditional_t<
      Properties<PS> && And<Property<ExpectedN>...>,
      detail::property_query_impl<PS, ExpectedN...>,
      std::false_type> {};

template<class PS, class... ExpectedN>
PUSHMI_INLINE_VAR constexpr bool property_query_v =
  property_query<PS, ExpectedN...>::value;


// query for categories on types with properties.

namespace detail {
template<class CIn, class POut>
std::true_type category_query_fn(property_set_element<POut, CIn>*);
template<class C>
std::false_type category_query_fn(void*);

template<class PS, class... ExpectedN>
struct category_query_impl : bool_<
  and_v<decltype(category_query_fn<ExpectedN>(
      (properties_t<PS>*)nullptr))::value...>> {};
} //namespace detail

template<class PS, class... ExpectedN>
struct category_query
  : std::conditional_t<
      Properties<PS> && not Or<Property<ExpectedN>...>,
      detail::category_query_impl<PS, ExpectedN...>,
      std::false_type> {};

template<class PS, class... ExpectedN>
PUSHMI_INLINE_VAR constexpr bool category_query_v =
  category_query<PS, ExpectedN...>::value;

} // namespace pushmi
// clang-format off
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "forwards.h"
//#include "extension_points.h"
//#include "properties.h"

namespace pushmi {

// traits & tags

// cardinality affects both sender and receiver

struct cardinality_category {};

// Trait
template<class PS>
struct has_cardinality : category_query<PS, cardinality_category> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool has_cardinality_v = has_cardinality<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Cardinality,
    has_cardinality_v<PS>
);

// flow affects both sender and receiver

struct flow_category {};

// sender and receiver are mutually exclusive

struct receiver_category {};

struct sender_category {};

// for senders that are executors

struct executor_category {};

// time and constrained are mutually exclusive refinements of sender (time is a special case of constrained and may be folded in later)

// blocking affects senders

struct blocking_category {};

// sequence affects senders

struct sequence_category {};

// Single trait and tag
template<class... TN>
struct is_single;
// Tag
template<>
struct is_single<> { using property_category = cardinality_category; };
// Trait
template<class PS>
struct is_single<PS> : property_query<PS, is_single<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_single_v = is_single<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Single,
    is_single_v<PS>
);

// Many trait and tag
template<class... TN>
struct is_many;
// Tag
template<>
struct is_many<> { using property_category = cardinality_category; }; // many::value() does not terminate, so it is not a refinement of single
// Trait
template<class PS>
struct is_many<PS> : property_query<PS, is_many<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_many_v = is_many<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Many,
    is_many_v<PS>
);

// Flow trait and tag
template<class... TN>
struct is_flow;
// Tag
template<>
struct is_flow<> { using property_category = flow_category; };
// Trait
template<class PS>
struct is_flow<PS> : property_query<PS, is_flow<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_flow_v = is_flow<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Flow,
    is_flow_v<PS>
);

// Receiver trait and tag
template<class... TN>
struct is_receiver;
// Tag
template<>
struct is_receiver<> { using property_category = receiver_category; };
// Trait
template<class PS>
struct is_receiver<PS> : property_query<PS, is_receiver<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_receiver_v = is_receiver<PS>::value;
// PUSHMI_CONCEPT_DEF(
//   template (class PS)
//   concept Receiver,
//     is_receiver_v<PS>
// );

// Sender trait and tag
template<class... TN>
struct is_sender;
// Tag
template<>
struct is_sender<> { using property_category = sender_category; };
// Trait
template<class PS>
struct is_sender<PS> : property_query<PS, is_sender<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_sender_v = is_sender<PS>::value;
// PUSHMI_CONCEPT_DEF(
//   template (class PS)
//   concept Sender,
//     is_sender_v<PS>
// );

// Executor trait and tag
template<class... TN>
struct is_executor;
// Tag
template<>
struct is_executor<> { using property_category = executor_category; };
// Trait
template<class PS>
struct is_executor<PS> : property_query<PS, is_executor<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_executor_v = is_executor<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Executor,
    is_executor_v<PS> && is_sender_v<PS> && is_single_v<PS>
);

// Constrained trait and tag
template<class... TN>
struct is_constrained;
// Tag
template<>
struct is_constrained<> : is_sender<> {};
// Trait
template<class PS>
struct is_constrained<PS> : property_query<PS, is_constrained<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_constrained_v = is_constrained<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Constrained,
    is_constrained_v<PS> && is_sender_v<PS>
);

// Time trait and tag
template<class... TN>
struct is_time;
// Tag
template<>
struct is_time<> : is_constrained<> {};
// Trait
template<class PS>
struct is_time<PS> : property_query<PS, is_time<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_time_v = is_time<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Time,
    is_time_v<PS> && is_constrained_v<PS> && is_sender_v<PS>
);

// AlwaysBlocking trait and tag
template<class... TN>
struct is_always_blocking;
// Tag
template<>
struct is_always_blocking<> { using property_category = blocking_category; };
// Trait
template<class PS>
struct is_always_blocking<PS> : property_query<PS, is_always_blocking<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_always_blocking_v = is_always_blocking<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept AlwaysBlocking,
    is_always_blocking_v<PS> && is_sender_v<PS>
);

// NeverBlocking trait and tag
template<class... TN>
struct is_never_blocking;
// Tag
template<>
struct is_never_blocking<> { using property_category = blocking_category; };
// Trait
template<class PS>
struct is_never_blocking<PS> : property_query<PS, is_never_blocking<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_never_blocking_v = is_never_blocking<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept NeverBlocking,
    is_never_blocking_v<PS> && is_sender_v<PS>
);

// MaybeBlocking trait and tag
template<class... TN>
struct is_maybe_blocking;
// Tag
template<>
struct is_maybe_blocking<> { using property_category = blocking_category; };
// Trait
template<class PS>
struct is_maybe_blocking<PS> : property_query<PS, is_maybe_blocking<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_maybe_blocking_v = is_maybe_blocking<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept MaybeBlocking,
    is_maybe_blocking_v<PS> && is_sender_v<PS>
);

// FifoSequence trait and tag
template<class... TN>
struct is_fifo_sequence;
// Tag
template<>
struct is_fifo_sequence<> { using property_category = sequence_category; };
// Trait
template<class PS>
struct is_fifo_sequence<PS> : property_query<PS, is_fifo_sequence<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_fifo_sequence_v = is_fifo_sequence<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept FifoSequence,
    is_fifo_sequence_v<PS> && is_sender_v<PS>
);

// ConcurrentSequence trait and tag
template<class... TN>
struct is_concurrent_sequence;
// Tag
template<>
struct is_concurrent_sequence<> { using property_category = sequence_category; };
// Trait
template<class PS>
struct is_concurrent_sequence<PS> : property_query<PS, is_concurrent_sequence<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_concurrent_sequence_v = is_concurrent_sequence<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept ConcurrentSequence,
    is_concurrent_sequence_v<PS> && is_sender_v<PS>
);


PUSHMI_CONCEPT_DEF(
  template (class R, class... PropertyN)
  (concept Receiver)(R, PropertyN...),
    requires (R& r) (
      ::pushmi::set_done(r),
      ::pushmi::set_error(r, std::exception_ptr{})
    ) &&
    SemiMovable<R> &&
    property_query_v<R, PropertyN...> &&
    is_receiver_v<R> &&
    !is_sender_v<R>
);

PUSHMI_CONCEPT_DEF(
  template (class R, class... VN)
  (concept ReceiveValue)(R, VN...),
    requires(R& r) (
      ::pushmi::set_value(r, std::declval<VN &&>()...)
    ) &&
    Receiver<R> &&
    // GCC w/-fconcepts ICE on SemiMovable<VN>...
    True<> // And<SemiMovable<VN>...>
);

PUSHMI_CONCEPT_DEF(
  template (class R, class E = std::exception_ptr)
  (concept ReceiveError)(R, E),
    requires(R& r, E&& e) (
      ::pushmi::set_error(r, (E &&) e)
    ) &&
    Receiver<R> &&
    SemiMovable<E>
);


PUSHMI_CONCEPT_DEF(
  template (class D, class... PropertyN)
  (concept Sender)(D, PropertyN...),
    requires(D& d) (
      ::pushmi::executor(d),
      requires_<Executor<decltype(::pushmi::executor(d))>>
    ) &&
    SemiMovable<D> &&
    Cardinality<D> &&
    property_query_v<D, PropertyN...> &&
    is_sender_v<D> &&
    !is_receiver_v<D>
);

PUSHMI_CONCEPT_DEF(
  template (class D, class S, class... PropertyN)
  (concept SenderTo)(D, S, PropertyN...),
    requires(D& d, S&& s) (
      ::pushmi::submit(d, (S &&) s)
    ) &&
    Sender<D> &&
    Receiver<S> &&
    property_query_v<D, PropertyN...>
);

template <class D>
PUSHMI_PP_CONSTRAINED_USING(
  Sender<D>,
  executor_t =, decltype(::pushmi::executor(std::declval<D&>())));

// add concepts to support cancellation
//

PUSHMI_CONCEPT_DEF(
  template (class S, class... PropertyN)
  (concept FlowReceiver)(S, PropertyN...),
    Receiver<S> &&
    property_query_v<S, PropertyN...> &&
    Flow<S>
);

PUSHMI_CONCEPT_DEF(
  template (class R, class... VN)
  (concept FlowReceiveValue)(R, VN...),
    Flow<R> &&
    ReceiveValue<R, VN...>
);

PUSHMI_CONCEPT_DEF(
  template (class R, class E = std::exception_ptr)
  (concept FlowReceiveError)(R, E),
    Flow<R> &&
    ReceiveError<R, E>
);

PUSHMI_CONCEPT_DEF(
  template (class R, class Up)
  (concept FlowUpTo)(R, Up),
    requires(R& r, Up&& up) (
      ::pushmi::set_starting(r, (Up &&) up)
    ) &&
    Flow<R>
);

PUSHMI_CONCEPT_DEF(
  template (class S, class... PropertyN)
  (concept FlowSender)(S, PropertyN...),
    Sender<S> &&
    property_query_v<S, PropertyN...> &&
    Flow<S>
);

PUSHMI_CONCEPT_DEF(
  template (class D, class S, class... PropertyN)
  (concept FlowSenderTo)(D, S, PropertyN...),
    FlowSender<D> &&
    property_query_v<D, PropertyN...> &&
    FlowReceiver<S>
);

// add concepts for constraints
//
// the constraint could be time or priority enum or any other
// ordering constraint value-type.
//
// top() returns the constraint value that will cause the item to run asap.
// So now() for time and NORMAL for priority.
//

PUSHMI_CONCEPT_DEF(
  template (class D, class... PropertyN)
  (concept ConstrainedSender)(D, PropertyN...),
    requires(D& d) (
      ::pushmi::top(d),
      requires_<Regular<decltype(::pushmi::top(d))>>
    ) &&
    Sender<D> &&
    property_query_v<D, PropertyN...> &&
    Constrained<D>
);

PUSHMI_CONCEPT_DEF(
  template (class D, class S, class... PropertyN)
  (concept ConstrainedSenderTo)(D, S, PropertyN...),
    requires(D& d, S&& s) (
      ::pushmi::submit(d, ::pushmi::top(d), (S &&) s)
    ) &&
    ConstrainedSender<D> &&
    property_query_v<D, PropertyN...> &&
    Receiver<S>
);

template <class D>
PUSHMI_PP_CONSTRAINED_USING(
  ConstrainedSender<D>,
  constraint_t =, decltype(::pushmi::top(std::declval<D&>())));


PUSHMI_CONCEPT_DEF(
  template (class D, class... PropertyN)
  (concept TimeSender)(D, PropertyN...),
    requires(D& d) (
      ::pushmi::now(d),
      requires_<Regular<decltype(::pushmi::now(d) + std::chrono::seconds(1))>>
    ) &&
    ConstrainedSender<D, PropertyN...> &&
    Time<D>
);

PUSHMI_CONCEPT_DEF(
  template (class D, class S, class... PropertyN)
  (concept TimeSenderTo)(D, S, PropertyN...),
    ConstrainedSenderTo<D, S, PropertyN...> &&
    TimeSender<D>
);

template <class D>
PUSHMI_PP_CONSTRAINED_USING(
  TimeSender<D>,
  time_point_t =, decltype(::pushmi::now(std::declval<D&>())));

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <chrono>
//#include <cstdint>
//#include <cstdio>
//#include <exception>
//#include <functional>
//#include <utility>

//#include "concepts.h"
//#include "traits.h"
//#include "detail/functional.h"

namespace pushmi {

template<class T>
struct construct {
  PUSHMI_TEMPLATE(class... AN)
    (requires Constructible<T, AN...>)
  auto operator()(AN&&... an) const {
    return T{std::forward<AN>(an)...};
  }
};

template<template <class...> class T>
struct construct_deduced;

template<>
struct construct_deduced<receiver>;

template<>
struct construct_deduced<flow_receiver>;

template<>
struct construct_deduced<sender>;

template<>
struct construct_deduced<single_sender>;

template<>
struct construct_deduced<many_sender>;

template<>
struct construct_deduced<flow_single_sender>;

template<>
struct construct_deduced<constrained_single_sender>;

template<>
struct construct_deduced<time_single_sender>;

template<>
struct construct_deduced<flow_many_sender>;

template <template <class...> class T, class... AN>
using deduced_type_t = pushmi::invoke_result_t<construct_deduced<T>, AN...>;

struct ignoreVF {
  PUSHMI_TEMPLATE(class... VN)
    (requires And<ConvertibleTo<VN&&, detail::any>...>)
  void operator()(VN&&...) {}
};

struct abortEF {
  void operator()(detail::any) noexcept {
    std::abort();
  }
};

struct ignoreDF {
  void operator()() {}
};

struct ignoreNF {
  void operator()(detail::any) {}
};

struct ignoreStrtF {
  void operator()(detail::any) {}
};

struct trampolineEXF;
// see trampoline.h
// struct trampolineEXF {
//   auto operator()() { return trampoline(); }
// };

struct ignoreSF {
  void operator()(detail::any) {}
  void operator()(detail::any, detail::any) {}
};

struct systemNowF {
  auto operator()() { return std::chrono::system_clock::now(); }
};

struct priorityZeroF {
  auto operator()(){ return 0; }
};

struct passDVF {
  PUSHMI_TEMPLATE(class Data, class... VN)
    (requires requires (
      ::pushmi::set_value(std::declval<Data&>(), std::declval<VN>()...)
    ) && Receiver<Data>)
  void operator()(Data& out, VN&&... vn) const {
    ::pushmi::set_value(out, (VN&&) vn...);
  }
};

struct passDEF {
  PUSHMI_TEMPLATE(class E, class Data)
    (requires ReceiveError<Data, E>)
  void operator()(Data& out, E e) const noexcept {
    ::pushmi::set_error(out, e);
  }
};

struct passDDF {
  PUSHMI_TEMPLATE(class Data)
    (requires Receiver<Data>)
  void operator()(Data& out) const {
    ::pushmi::set_done(out);
  }
};

struct passDStrtF {
  PUSHMI_TEMPLATE(class Up, class Data)
    (requires requires (
      ::pushmi::set_starting(std::declval<Data&>(), std::declval<Up>())
    ) && Receiver<Data>)
  void operator()(Data& out, Up&& up) const {
    ::pushmi::set_starting(out, (Up&&) up);
  }
};

struct passDEXF {
  PUSHMI_TEMPLATE(class Data)
    (requires Sender<Data>)
  auto operator()(Data& in) const noexcept {
    return ::pushmi::executor(in);
  }
};

struct passDSF {
  template <class Data, class Out>
  void operator()(Data& in, Out out) {
    ::pushmi::submit(in, std::move(out));
  }
  template <class Data, class TP, class Out>
  void operator()(Data& in, TP at, Out out) {
    ::pushmi::submit(in, std::move(at), std::move(out));
  }
};

struct passDNF {
  PUSHMI_TEMPLATE(class Data)
    (requires TimeSender<Data>)
  auto operator()(Data& in) const noexcept {
    return ::pushmi::now(in);
  }
};

struct passDZF {
  PUSHMI_TEMPLATE(class Data)
    (requires ConstrainedSender<Data>)
  auto operator()(Data& in) const noexcept {
    return ::pushmi::top(in);
  }
};

// inspired by Ovrld - shown in a presentation by Nicolai Josuttis
#if __cpp_variadic_using >= 201611 && __cpp_concepts
template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... Fns>
  requires sizeof...(Fns) > 0
struct overload_fn : Fns... {
  constexpr overload_fn() = default;
  constexpr explicit overload_fn(Fns... fns) requires sizeof...(Fns) == 1
      : Fns(std::move(fns))... {}
  constexpr overload_fn(Fns... fns) requires sizeof...(Fns) > 1
      : Fns(std::move(fns))... {}
  using Fns::operator()...;
};
#else
template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... Fns>
#if __cpp_concepts
  requires sizeof...(Fns) > 0
#endif
struct overload_fn;
template <class Fn>
struct overload_fn<Fn> : Fn {
  constexpr overload_fn() = default;
  constexpr explicit overload_fn(Fn fn)
      : Fn(std::move(fn)) {}
  using Fn::operator();
};
#if !defined(__GNUC__) || __GNUC__ >= 8
template <class Fn, class... Fns>
struct overload_fn<Fn, Fns...> : Fn, overload_fn<Fns...> {
  constexpr overload_fn() = default;
  constexpr overload_fn(Fn fn, Fns... fns)
      : Fn(std::move(fn)), overload_fn<Fns...>{std::move(fns)...} {}
  using Fn::operator();
  using overload_fn<Fns...>::operator();
};
#else
template <class Fn, class... Fns>
struct overload_fn<Fn, Fns...> {
private:
  std::pair<Fn, overload_fn<Fns...>> fns_;
  template <bool B>
  using _which_t = std::conditional_t<B, Fn, overload_fn<Fns...>>;
public:
  constexpr overload_fn() = default;
  constexpr overload_fn(Fn fn, Fns... fns)
      : fns_{std::move(fn), overload_fn<Fns...>{std::move(fns)...}} {}
  PUSHMI_TEMPLATE (class... Args)
    (requires lazy::Invocable<Fn&, Args...> ||
      lazy::Invocable<overload_fn<Fns...>&, Args...>)
  decltype(auto) operator()(Args &&... args) PUSHMI_NOEXCEPT_AUTO(
      std::declval<_which_t<Invocable<Fn&, Args...>>&>()(std::declval<Args>()...)) {
    return std::get<!Invocable<Fn&, Args...>>(fns_)((Args &&) args...);
  }
  PUSHMI_TEMPLATE (class... Args)
    (requires lazy::Invocable<const Fn&, Args...> ||
      lazy::Invocable<const overload_fn<Fns...>&, Args...>)
  decltype(auto) operator()(Args &&... args) const PUSHMI_NOEXCEPT_AUTO(
      std::declval<const _which_t<Invocable<const Fn&, Args...>>&>()(std::declval<Args>()...)) {
    return std::get<!Invocable<const Fn&, Args...>>(fns_)((Args &&) args...);
  }
};
#endif
#endif

template <class... Fns>
auto overload(Fns... fns) -> overload_fn<Fns...> {
  return overload_fn<Fns...>{std::move(fns)...};
}

template <class... Fns>
struct on_value_fn : overload_fn<Fns...> {
  constexpr on_value_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_value(Fns... fns) -> on_value_fn<Fns...> {
  return on_value_fn<Fns...>{std::move(fns)...};
}

template <class... Fns>
struct on_error_fn : overload_fn<Fns...> {
  constexpr on_error_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_error(Fns... fns) -> on_error_fn<Fns...> {
  return on_error_fn<Fns...>{std::move(fns)...};
}

template <class Fn>
struct on_done_fn : Fn {
  constexpr on_done_fn() = default;
  constexpr explicit on_done_fn(Fn fn) : Fn(std::move(fn)) {}
  using Fn::operator();
};

template <class Fn>
auto on_done(Fn fn) -> on_done_fn<Fn> {
  return on_done_fn<Fn>{std::move(fn)};
}

template <class... Fns>
struct on_starting_fn : overload_fn<Fns...> {
  constexpr on_starting_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_starting(Fns... fns) -> on_starting_fn<Fns...> {
  return on_starting_fn<Fns...>{std::move(fns)...};
}

template <class Fn>
struct on_executor_fn : overload_fn<Fn> {
  constexpr on_executor_fn() = default;
  using overload_fn<Fn>::overload_fn;
};

template <class Fn>
auto on_executor(Fn fn) -> on_executor_fn<Fn> {
  return on_executor_fn<Fn>{std::move(fn)};
}

template <class... Fns>
struct on_submit_fn : overload_fn<Fns...> {
  constexpr on_submit_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_submit(Fns... fns) -> on_submit_fn<Fns...> {
  return on_submit_fn<Fns...>{std::move(fns)...};
}

template <class Fn>
struct on_now_fn : overload_fn<Fn> {
  constexpr on_now_fn() = default;
  using overload_fn<Fn>::overload_fn;
};

template <class Fn>
auto on_now(Fn fn) -> on_now_fn<Fn> {
  return on_now_fn<Fn>{std::move(fn)};
}

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "traits.h"

namespace pushmi {

PUSHMI_TEMPLATE (class In, class Op)
  (requires lazy::Sender<std::decay_t<In>> && lazy::Invocable<Op&, In>)
decltype(auto) operator|(In&& in, Op op) {
  return op((In&&) in);
}

PUSHMI_INLINE_VAR constexpr struct pipe_fn {
#if __cpp_fold_expressions >= 201603
  template<class T, class... FN>
  auto operator()(T t, FN... fn) const -> decltype((t | ... | fn)) {
    return (t | ... | fn);
  }
#else
  template<class T, class F>
  auto operator()(T t, F f) const -> decltype(t | f) {
    return t | f;
  }
  template<class T, class F, class... FN, class This = pipe_fn>
  auto operator()(T t, F f, FN... fn) const -> decltype(This()((t | f), fn...)) {
    return This()((t | f), fn...);
  }
#endif
} const pipe {};

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <future>
//#include "boosters.h"

namespace pushmi {

template <class E, class... VN>
class any_receiver {
  bool done_ = false;
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<int>)]; // can hold a std::promise in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() noexcept {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_done(data&) {}
    static void s_error(data&, E) noexcept { std::terminate(); }
    static void s_value(data&, VN...) {}
    void (*op_)(data&, data*) = vtable::s_op;
    void (*done_)(data&) = vtable::s_done;
    void (*error_)(data&, E) noexcept = vtable::s_error;
    void (*value_)(data&, VN...) = vtable::s_value;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_receiver>::value, U>;
  template <class Wrapped>
  static void check() {
    static_assert(ReceiveValue<Wrapped, VN...>,
      "Wrapped receiver must support values of type VN...");
    static_assert(ReceiveError<Wrapped, std::exception_ptr>,
      "Wrapped receiver must support std::exception_ptr and be noexcept");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, E>,
      "Wrapped receiver must support E and be noexcept");
  }
  template<class Wrapped>
  any_receiver(Wrapped obj, std::false_type) : any_receiver() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static void done(data& src) {
        ::pushmi::set_done(*static_cast<Wrapped*>(src.pobj_));
      }
      static void error(data& src, E e) noexcept {
        ::pushmi::set_error(*static_cast<Wrapped*>(src.pobj_), std::move(e));
      }
      static void value(data& src, VN... vn) {
        ::pushmi::set_value(*static_cast<Wrapped*>(src.pobj_), std::move(vn)...);
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::value};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template<class Wrapped>
  any_receiver(Wrapped obj, std::true_type) noexcept : any_receiver() {
    struct s {
      static void op(data& src, data* dst) {
          if (dst)
            new (dst->buffer_) Wrapped(
                std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
          static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static void done(data& src) {
        ::pushmi::set_done(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static void error(data& src, E e) noexcept {
        ::pushmi::set_error(
          *static_cast<Wrapped*>((void*)src.buffer_),
          std::move(e));
      }
      static void value(data& src, VN... vn) {
        ::pushmi::set_value(*static_cast<Wrapped*>((void*)src.buffer_), std::move(vn)...);
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::value};
    new ((void*)data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
public:
  using properties = property_set<is_receiver<>>;

  any_receiver() = default;
  any_receiver(any_receiver&& that) noexcept : any_receiver() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires ReceiveValue<wrapped_t<Wrapped>, VN...> && ReceiveError<Wrapped, E>)
  explicit any_receiver(Wrapped obj) noexcept(insitu<Wrapped>())
    : any_receiver{std::move(obj), bool_<insitu<Wrapped>()>{}} {
    check<Wrapped>();
  }
  ~any_receiver() {
    vptr_->op_(data_, nullptr);
  }
  any_receiver& operator=(any_receiver&& that) noexcept {
    this->~any_receiver();
    new ((void*)this) any_receiver(std::move(that));
    return *this;
  }
  void value(VN&&... vn) {
    if (!done_) {
      // done_ = true;
      vptr_->value_(data_, (VN&&) vn...);
    }
  }
  void error(E e) noexcept {
    if (!done_) {
      done_ = true;
      vptr_->error_(data_, std::move(e));
    }
  }
  void done() {
    if (!done_) {
      done_ = true;
      vptr_->done_(data_);
    }
  }
};

// Class static definitions:
template <class E, class... VN>
constexpr typename any_receiver<E, VN...>::vtable const any_receiver<E, VN...>::noop_;

template <class VF, class EF, class DF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class receiver<VF, EF, DF> {
  bool done_ = false;
  VF vf_;
  EF ef_;
  DF df_;

  static_assert(
      !detail::is_v<VF, on_error_fn>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<EF, on_value_fn>,
      "the second parameter is the error implementation, but on_value{} was passed");
  static_assert(NothrowInvocable<EF&, std::exception_ptr>,
      "error function must be noexcept and support std::exception_ptr");
 public:
  using properties = property_set<is_receiver<>>;

  receiver() = default;
  constexpr explicit receiver(VF vf) : receiver(std::move(vf), EF{}, DF{}) {}
  constexpr explicit receiver(EF ef) : receiver(VF{}, std::move(ef), DF{}) {}
  constexpr explicit receiver(DF df) : receiver(VF{}, EF{}, std::move(df)) {}
  constexpr receiver(EF ef, DF df)
      : done_(false), vf_(), ef_(std::move(ef)), df_(std::move(df))
  {}
  constexpr receiver(VF vf, EF ef, DF df = DF{})
      : done_(false), vf_(std::move(vf)), ef_(std::move(ef)), df_(std::move(df))
  {}

  PUSHMI_TEMPLATE (class... VN)
    (requires Invocable<VF&, VN...>)
  void value(VN&&... vn) {
    if (done_) {return;}
    // done_ = true;
    vf_((VN&&) vn...);
  }
  PUSHMI_TEMPLATE (class E)
    (requires Invocable<EF&, E>)
  void error(E e) noexcept {
    static_assert(NothrowInvocable<EF&, E>, "error function must be noexcept");
    if (!done_) {
      done_ = true;
      ef_(std::move(e));
    }
  }
  void done() {
    if (!done_) {
      done_ = true;
      df_();
    }
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Receiver) Data, class DVF, class DEF, class DDF>
#if __cpp_concepts
  requires Invocable<DDF&, Data&>
#endif
class receiver<Data, DVF, DEF, DDF> {
  bool done_ = false;
  Data data_;
  DVF vf_;
  DEF ef_;
  DDF df_;

  static_assert(
      !detail::is_v<DVF, on_error_fn>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<DEF, on_value_fn>,
      "the second parameter is the error implementation, but on_value{} was passed");
  static_assert(Invocable<DEF, Data&, std::exception_ptr>,
      "error function must support std::exception_ptr");
  static_assert(NothrowInvocable<DEF, Data&, std::exception_ptr>,
      "error function must be noexcept");

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_receiver<>>>;

  constexpr explicit receiver(Data d)
      : receiver(std::move(d), DVF{}, DEF{}, DDF{}) {}
  constexpr receiver(Data d, DDF df)
      : done_(false), data_(std::move(d)), vf_(), ef_(), df_(df) {}
  constexpr receiver(Data d, DEF ef, DDF df = DDF{})
      : done_(false), data_(std::move(d)), vf_(), ef_(ef), df_(df) {}
  constexpr receiver(Data d, DVF vf, DEF ef = DEF{}, DDF df = DDF{})
      : done_(false), data_(std::move(d)), vf_(vf), ef_(ef), df_(df) {}

  Data& data() { return data_; }

  PUSHMI_TEMPLATE(class... VN)
    (requires Invocable<DVF&, Data&, VN...>)
  void value(VN&&... vn) {
    if (!done_) {
      // done_ = true;
      vf_(data_, (VN&&) vn...);
    }
  }
  PUSHMI_TEMPLATE(class E)
    (requires Invocable<DEF&, Data&, E>)
  void error(E e) noexcept {
    static_assert(
        NothrowInvocable<DEF&, Data&, E>, "error function must be noexcept");
    if (!done_) {
      done_ = true;
      ef_(data_, std::move(e));
    }
  }
  void done() {
    if (!done_) {
      done_ = true;
      df_(data_);
    }
  }
};

template <>
class receiver<>
    : public receiver<ignoreVF, abortEF, ignoreDF> {
public:
  receiver() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_receiver
PUSHMI_INLINE_VAR constexpr struct make_receiver_fn {
  inline auto operator()() const {
    return receiver<>{};
  }
  PUSHMI_TEMPLATE(class VF)
    (requires PUSHMI_EXP(
      lazy::True<>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF>)))
  auto operator()(VF vf) const {
    return receiver<VF, abortEF, ignoreDF>{std::move(vf)};
  }
  template <class... EFN>
  auto operator()(on_error_fn<EFN...> ef) const {
    return receiver<ignoreVF, on_error_fn<EFN...>, ignoreDF>{std::move(ef)};
  }
  template <class... DFN>
  auto operator()(on_done_fn<DFN...> df) const {
    return receiver<ignoreVF, abortEF, on_done_fn<DFN...>>{std::move(df)};
  }
  PUSHMI_TEMPLATE(class VF, class EF)
    (requires PUSHMI_EXP(
      lazy::True<>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
        not lazy::Receiver<VF> PUSHMI_AND
        not lazy::Invocable<EF&>)))
  auto operator()(VF vf, EF ef) const {
    return receiver<VF, EF, ignoreDF>{std::move(vf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class EF, class DF)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Invocable<DF&>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
  auto operator()(EF ef, DF df) const {
    return receiver<ignoreVF, EF, DF>{std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class VF, class EF, class DF)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Invocable<DF&>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF>)))
  auto operator()(VF vf, EF ef, DF df) const {
    return receiver<VF, EF, DF>{std::move(vf), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Receiver<Data>))
  auto operator()(Data d) const {
    return receiver<Data, passDVF, passDEF, passDDF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Receiver<Data>))
  auto operator()(Data d, DVF vf) const {
    return receiver<Data, DVF, passDEF, passDDF>{std::move(d), std::move(vf)};
  }
  PUSHMI_TEMPLATE(class Data, class... DEFN)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Receiver<Data>))
  auto operator()(Data d, on_error_fn<DEFN...> ef) const {
    return receiver<Data, passDVF, on_error_fn<DEFN...>, passDDF>{std::move(d), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class... DDFN)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Receiver<Data>))
  auto operator()(Data d, on_done_fn<DDFN...> df) const {
    return receiver<Data, passDVF, passDEF, on_done_fn<DDFN...>>{std::move(d), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF, class... DEFN)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Receiver<Data>))
  auto operator()(Data d, DVF vf, on_error_fn<DEFN...> ef) const {
    return receiver<Data, DVF, on_error_fn<DEFN...>, passDDF>{std::move(d), std::move(vf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DEF, class... DDFN)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Receiver<Data>))
  auto operator()(Data d, DEF ef, on_done_fn<DDFN...> df) const {
    return receiver<Data, passDVF, DEF, on_done_fn<DDFN...>>{std::move(d), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Receiver<Data> PUSHMI_AND
      lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DVF vf, DEF ef, DDF df) const {
    return receiver<Data, DVF, DEF, DDF>{std::move(d), std::move(vf), std::move(ef), std::move(df)};
  }
} const make_receiver {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
receiver() -> receiver<>;

PUSHMI_TEMPLATE(class VF)
  (requires PUSHMI_EXP(
    True<>
    // lazy::Callable<VF>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF>)))
receiver(VF) -> receiver<VF, abortEF, ignoreDF>;

template <class... EFN>
receiver(on_error_fn<EFN...>) -> receiver<ignoreVF, on_error_fn<EFN...>, ignoreDF>;

template <class... DFN>
receiver(on_done_fn<DFN...>) -> receiver<ignoreVF, abortEF, on_done_fn<DFN...>>;

PUSHMI_TEMPLATE(class VF, class EF)
  (requires PUSHMI_EXP(
    lazy::True<>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
      not lazy::Receiver<VF> PUSHMI_AND
      not lazy::Invocable<EF&>)))
receiver(VF, EF) -> receiver<VF, EF, ignoreDF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Invocable<DF&>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
receiver(EF, DF) -> receiver<ignoreVF, EF, DF>;

PUSHMI_TEMPLATE(class VF, class EF, class DF)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Invocable<DF&>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF>)))
receiver(VF, EF, DF) -> receiver<VF, EF, DF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Receiver<Data>))
receiver(Data d) -> receiver<Data, passDVF, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DVF)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Receiver<Data>))
receiver(Data d, DVF vf) -> receiver<Data, DVF, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class... DEFN)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Receiver<Data>))
receiver(Data d, on_error_fn<DEFN...>) ->
    receiver<Data, passDVF, on_error_fn<DEFN...>, passDDF>;

PUSHMI_TEMPLATE(class Data, class... DDFN)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Receiver<Data>))
receiver(Data d, on_done_fn<DDFN...>) ->
    receiver<Data, passDVF, passDEF, on_done_fn<DDFN...>>;

PUSHMI_TEMPLATE(class Data, class DVF, class... DEFN)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Receiver<Data>))
receiver(Data d, DVF vf, on_error_fn<DEFN...> ef) -> receiver<Data, DVF, on_error_fn<DEFN...>, passDDF>;

PUSHMI_TEMPLATE(class Data, class DEF, class... DDFN)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Receiver<Data>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
receiver(Data d, DEF, on_done_fn<DDFN...>) -> receiver<Data, passDVF, DEF, on_done_fn<DDFN...>>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Receiver<Data> PUSHMI_AND
    lazy::Invocable<DDF&, Data&>))
receiver(Data d, DVF vf, DEF ef, DDF df) -> receiver<Data, DVF, DEF, DDF>;
#endif

template<>
struct construct_deduced<receiver> : make_receiver_fn {};

PUSHMI_TEMPLATE (class T, class In)
  (requires SenderTo<In, std::promise<T>, is_single<>>)
std::future<T> future_from(In in) {
  std::promise<T> p;
  auto result = p.get_future();
  submit(in, std::move(p));
  return result;
}
PUSHMI_TEMPLATE (class In)
  (requires SenderTo<In, std::promise<void>, is_single<>>)
std::future<void> future_from(In in) {
  std::promise<void> p;
  auto result = p.get_future();
  submit(in, std::move(p));
  return result;
}

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "receiver.h"

namespace pushmi {

template <class PE, class PV, class E, class... VN>
class any_flow_receiver {
  bool done_ = false;
  bool started_ = false;
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::tuple<VN...>)]; // can hold V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_done(data&) {}
    static void s_error(data&, E) noexcept { std::terminate(); }
    static void s_value(data&, VN...) {}
    static void s_starting(data&, any_receiver<PE, PV>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    void (*done_)(data&) = vtable::s_done;
    void (*error_)(data&, E) noexcept = vtable::s_error;
    void (*value_)(data&, VN...) = vtable::s_value;
    void (*starting_)(data&, any_receiver<PE, PV>) = vtable::s_starting;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_flow_receiver(Wrapped obj, std::false_type) : any_flow_receiver() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static void done(data& src) {
        ::pushmi::set_done(*static_cast<Wrapped*>(src.pobj_));
      }
      static void error(data& src, E e) noexcept {
        ::pushmi::set_error(*static_cast<Wrapped*>(src.pobj_), std::move(e));
      }
      static void value(data& src, VN... vn) {
        ::pushmi::set_value(*static_cast<Wrapped*>(src.pobj_), std::move(vn)...);
      }
      static void starting(data& src, any_receiver<PE, PV> up) {
        ::pushmi::set_starting(*static_cast<Wrapped*>(src.pobj_), std::move(up));
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::value, s::starting};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_flow_receiver(Wrapped obj, std::true_type) noexcept : any_flow_receiver() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static void done(data& src) {
        ::pushmi::set_done(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static void error(data& src, E e) noexcept {::pushmi::set_error(
          *static_cast<Wrapped*>((void*)src.buffer_),
          std::move(e));
      }
      static void value(data& src, VN... vn) {
        ::pushmi::set_value(
            *static_cast<Wrapped*>((void*)src.buffer_), std::move(vn)...);
      }
      static void starting(data& src, any_receiver<PE, PV> up) {
        ::pushmi::set_starting(*static_cast<Wrapped*>((void*)src.buffer_), std::move(up));
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::value, s::starting};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_flow_receiver>::value, U>;
public:
  using properties = property_set<is_receiver<>, is_flow<>>;

  any_flow_receiver() = default;
  any_flow_receiver(any_flow_receiver&& that) noexcept : any_flow_receiver() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires FlowUpTo<wrapped_t<Wrapped>, any_receiver<PE, PV>> &&
      ReceiveValue<wrapped_t<Wrapped>, VN...> &&
      ReceiveError<wrapped_t<Wrapped>, E>)
  explicit any_flow_receiver(Wrapped obj) noexcept(insitu<Wrapped>())
    : any_flow_receiver{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~any_flow_receiver() {
    vptr_->op_(data_, nullptr);
  }
  any_flow_receiver& operator=(any_flow_receiver&& that) noexcept {
    this->~any_flow_receiver();
    new ((void*)this) any_flow_receiver(std::move(that));
    return *this;
  }
  void value(VN... vn) {
    if (!started_) {std::abort();}
    if (done_){ return; }
    vptr_->value_(data_, std::move(vn)...);
  }
  void error(E e) noexcept {
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    vptr_->error_(data_, std::move(e));
  }
  void done() {
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    vptr_->done_(data_);
  }

  void starting(any_receiver<PE, PV> up) {
    if (started_) {std::abort();}
    started_ = true;
    vptr_->starting_(data_, std::move(up));
  }
};

// Class static definitions:
template <class PE, class PV, class E, class... VN>
constexpr typename any_flow_receiver<PE, PV, E, VN...>::vtable const
  any_flow_receiver<PE, PV, E, VN...>::noop_;

template <class VF, class EF, class DF, class StrtF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class flow_receiver<VF, EF, DF, StrtF> {
  bool done_ = false;
  bool started_ = false;
  VF nf_;
  EF ef_;
  DF df_;
  StrtF strtf_;

 public:
  using properties = property_set<is_receiver<>, is_flow<>>;

  static_assert(
      !detail::is_v<VF, on_error_fn>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<EF, on_value_fn>,
      "the second parameter is the error implementation, but on_value{} was passed");

  flow_receiver() = default;
  constexpr explicit flow_receiver(VF nf)
      : flow_receiver(std::move(nf), EF{}, DF{}) {}
  constexpr explicit flow_receiver(EF ef)
      : flow_receiver(VF{}, std::move(ef), DF{}) {}
  constexpr explicit flow_receiver(DF df)
      : flow_receiver(VF{}, EF{}, std::move(df)) {}
  constexpr flow_receiver(EF ef, DF df)
      : nf_(), ef_(std::move(ef)), df_(std::move(df)) {}
  constexpr flow_receiver(
      VF nf,
      EF ef,
      DF df = DF{},
      StrtF strtf = StrtF{})
      : nf_(std::move(nf)),
        ef_(std::move(ef)),
        df_(std::move(df)),
        strtf_(std::move(strtf)) {}
  PUSHMI_TEMPLATE (class V)
    (requires Invocable<VF&, V>)
  void value(V&& v) {
    if (!started_) {std::abort();}
    if (done_){ return; }
    nf_((V&&) v);
  }
  PUSHMI_TEMPLATE (class E)
    (requires Invocable<EF&, E>)
  void error(E e) noexcept {
    static_assert(NothrowInvocable<EF&, E>, "error function must be noexcept");
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    ef_(std::move(e));
  }
  void done() {
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    df_();
  }
  PUSHMI_TEMPLATE(class Up)
    (requires Invocable<StrtF&, Up&&>)
  void starting(Up&& up) {
    if (started_) {std::abort();}
    started_ = true;
    strtf_( (Up &&) up);
  }
};

template<
    PUSHMI_TYPE_CONSTRAINT(Receiver) Data,
    class DVF,
    class DEF,
    class DDF,
    class DStrtF>
#if __cpp_concepts
  requires Invocable<DDF&, Data&>
#endif
class flow_receiver<Data, DVF, DEF, DDF, DStrtF> {
  bool done_ = false;
  bool started_ = false;
  Data data_;
  DVF nf_;
  DEF ef_;
  DDF df_;
  DStrtF strtf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_receiver<>, is_flow<>>>;

  static_assert(
      !detail::is_v<DVF, on_error_fn>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<DEF, on_value_fn>,
      "the second parameter is the error implementation, but on_value{} was passed");

  constexpr explicit flow_receiver(Data d)
      : flow_receiver(std::move(d), DVF{}, DEF{}, DDF{}) {}
  constexpr flow_receiver(Data d, DDF df)
      : data_(std::move(d)), nf_(), ef_(), df_(df) {}
  constexpr flow_receiver(Data d, DEF ef, DDF df = DDF{})
      : data_(std::move(d)), nf_(), ef_(ef), df_(df) {}
  constexpr flow_receiver(
      Data d,
      DVF nf,
      DEF ef = DEF{},
      DDF df = DDF{},
      DStrtF strtf = DStrtF{})
      : data_(std::move(d)),
        nf_(nf),
        ef_(ef),
        df_(df),
        strtf_(std::move(strtf)) {}


  Data& data() { return data_; }

  PUSHMI_TEMPLATE (class V)
    (requires Invocable<DVF&, Data&, V>)
  void value(V&& v) {
    if (!started_) {std::abort();}
    if (done_){ return; }
    nf_(data_, (V&&) v);
  }
  PUSHMI_TEMPLATE (class E)
    (requires Invocable<DEF&, Data&, E>)
  void error(E&& e) noexcept {
    static_assert(
        NothrowInvocable<DEF&, Data&, E>, "error function must be noexcept");
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    ef_(data_, (E&&) e);
  }
  void done() {
    if (!started_) {std::abort();}
    if (done_){ return; }
    done_ = true;
    df_(data_);
  }
  PUSHMI_TEMPLATE (class Up)
    (requires Invocable<DStrtF&, Data&, Up&&>)
  void starting(Up&& up) {
    if (started_) {std::abort();}
    started_ = true;
    strtf_(data_, (Up &&) up);
  }
};

template <>
class flow_receiver<>
    : public flow_receiver<ignoreVF, abortEF, ignoreDF, ignoreStrtF> {
};

// TODO winnow down the number of make_flow_receiver overloads and deduction
// guides here, as was done for make_many.

////////////////////////////////////////////////////////////////////////////////
// make_flow_receiver
PUSHMI_INLINE_VAR constexpr struct make_flow_receiver_fn {
  inline auto operator()() const {
    return flow_receiver<>{};
  }
  PUSHMI_TEMPLATE (class VF)
    (requires PUSHMI_EXP(
      lazy::True<>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
        not lazy::Receiver<VF>)))
  auto operator()(VF nf) const {
    return flow_receiver<VF, abortEF, ignoreDF, ignoreStrtF>{
      std::move(nf)};
  }
  template <class... EFN>
  auto operator()(on_error_fn<EFN...> ef) const {
    return flow_receiver<ignoreVF, on_error_fn<EFN...>, ignoreDF, ignoreStrtF>{
      std::move(ef)};
  }
  template <class... DFN>
  auto operator()(on_done_fn<DFN...> df) const {
    return flow_receiver<ignoreVF, abortEF, on_done_fn<DFN...>, ignoreStrtF>{
      std::move(df)};
  }
  PUSHMI_TEMPLATE (class VF, class EF)
    (requires PUSHMI_EXP(
      lazy::True<>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
        not lazy::Receiver<VF> PUSHMI_AND
        not lazy::Invocable<EF&>)))
  auto operator()(VF nf, EF ef) const {
    return flow_receiver<VF, EF, ignoreDF, ignoreStrtF>{std::move(nf),
      std::move(ef)};
  }
  PUSHMI_TEMPLATE(class EF, class DF)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Invocable<DF&>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
        not lazy::Receiver<EF>)))
  auto operator()(EF ef, DF df) const {
    return flow_receiver<ignoreVF, EF, DF, ignoreStrtF>{std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE (class VF, class EF, class DF)
    (requires PUSHMI_EXP(
      lazy::Invocable<DF&>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
        not lazy::Receiver<VF>)))
  auto operator()(VF nf, EF ef, DF df) const {
    return flow_receiver<VF, EF, DF, ignoreStrtF>{std::move(nf),
      std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE (class VF, class EF, class DF, class StrtF)
    (requires PUSHMI_EXP(
      lazy::Invocable<DF&>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
        not lazy::Receiver<VF>)))
  auto operator()(VF nf, EF ef, DF df, StrtF strtf) const {
    return flow_receiver<VF, EF, DF, StrtF>{std::move(nf), std::move(ef),
      std::move(df), std::move(strtf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Receiver<Data>))
  auto operator()(Data d) const {
    return flow_receiver<Data, passDVF, passDEF, passDDF, passDStrtF>{
        std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF)
    (requires PUSHMI_EXP(
      lazy::True<> PUSHMI_AND
      lazy::Receiver<Data>))
  auto operator()(Data d, DVF nf) const {
    return flow_receiver<Data, DVF, passDEF, passDDF, passDStrtF>{
      std::move(d), std::move(nf)};
  }
  PUSHMI_TEMPLATE(class Data, class... DEFN)
    (requires PUSHMI_EXP(
      lazy::Receiver<Data>))
  auto operator()(Data d, on_error_fn<DEFN...> ef) const {
    return flow_receiver<Data, passDVF, on_error_fn<DEFN...>, passDDF, passDStrtF>{
      std::move(d), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class... DDFN)
    (requires PUSHMI_EXP(
      lazy::Receiver<Data>))
  auto operator()(Data d, on_done_fn<DDFN...> df) const {
    return flow_receiver<Data, passDVF, passDEF, on_done_fn<DDFN...>, passDStrtF>{
      std::move(d), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF, class DEF)
    (requires PUSHMI_EXP(
      lazy::Receiver<Data>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
        not lazy::Invocable<DEF&, Data&>)))
  auto operator()(Data d, DVF nf, DEF ef) const {
    return flow_receiver<Data, DVF, DEF, passDDF, passDStrtF>{std::move(d), std::move(nf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
    (requires PUSHMI_EXP(
      lazy::Receiver<Data> PUSHMI_AND
      lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DEF ef, DDF df) const {
    return flow_receiver<Data, passDVF, DEF, DDF, passDStrtF>{
      std::move(d), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF)
    (requires PUSHMI_EXP(
      lazy::Receiver<Data> PUSHMI_AND
      lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DVF nf, DEF ef, DDF df) const {
    return flow_receiver<Data, DVF, DEF, DDF, passDStrtF>{std::move(d),
      std::move(nf), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF, class DStrtF)
    (requires PUSHMI_EXP(
      lazy::Receiver<Data> PUSHMI_AND
      lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DVF nf, DEF ef, DDF df, DStrtF strtf) const {
    return flow_receiver<Data, DVF, DEF, DDF, DStrtF>{std::move(d),
      std::move(nf), std::move(ef), std::move(df), std::move(strtf)};
  }
} const make_flow_receiver {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
flow_receiver() -> flow_receiver<>;

PUSHMI_TEMPLATE(class VF)
  (requires PUSHMI_EXP(
    lazy::True<>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
      not lazy::Receiver<VF>)))
flow_receiver(VF) ->
  flow_receiver<VF, abortEF, ignoreDF, ignoreStrtF>;

template <class... EFN>
flow_receiver(on_error_fn<EFN...>) ->
  flow_receiver<ignoreVF, on_error_fn<EFN...>, ignoreDF, ignoreStrtF>;

template <class... DFN>
flow_receiver(on_done_fn<DFN...>) ->
  flow_receiver<ignoreVF, abortEF, on_done_fn<DFN...>, ignoreStrtF>;

PUSHMI_TEMPLATE(class VF, class EF)
  (requires PUSHMI_EXP(
    lazy::True<>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
      not lazy::Receiver<VF> PUSHMI_AND
      not lazy::Invocable<EF&>)))
flow_receiver(VF, EF) ->
  flow_receiver<VF, EF, ignoreDF, ignoreStrtF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Invocable<DF&>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
      not lazy::Receiver<EF>)))
flow_receiver(EF, DF) ->
  flow_receiver<ignoreVF, EF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class VF, class EF, class DF)
  (requires PUSHMI_EXP(
    lazy::Invocable<DF&>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
      not lazy::Receiver<VF>)))
flow_receiver(VF, EF, DF) ->
  flow_receiver<VF, EF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class VF, class EF, class DF, class StrtF)
  (requires PUSHMI_EXP(
    lazy::Invocable<DF&>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
      not lazy::Receiver<VF>)))
flow_receiver(VF, EF, DF, StrtF) ->
  flow_receiver<VF, EF, DF, StrtF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Receiver<Data>))
flow_receiver(Data d) ->
  flow_receiver<Data, passDVF, passDEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DVF)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Receiver<Data>))
flow_receiver(Data d, DVF nf) ->
  flow_receiver<Data, DVF, passDEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class... DEFN)
  (requires PUSHMI_EXP(
    lazy::Receiver<Data>))
flow_receiver(Data d, on_error_fn<DEFN...>) ->
  flow_receiver<Data, passDVF, on_error_fn<DEFN...>, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class... DDFN)
  (requires PUSHMI_EXP(
    lazy::Receiver<Data>))
flow_receiver(Data d, on_done_fn<DDFN...>) ->
  flow_receiver<Data, passDVF, passDEF, on_done_fn<DDFN...>, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(
    lazy::True<> PUSHMI_AND
    lazy::Receiver<Data> PUSHMI_AND
    lazy::Invocable<DDF&, Data&>))
flow_receiver(Data d, DDF) ->
    flow_receiver<Data, passDVF, passDEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF)
  (requires PUSHMI_EXP(
    lazy::Receiver<Data>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND
      not lazy::Invocable<DEF&, Data&>)))
flow_receiver(Data d, DVF nf, DEF ef) ->
  flow_receiver<Data, DVF, DEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(
    lazy::Receiver<Data> PUSHMI_AND
    lazy::Invocable<DDF&, Data&>))
flow_receiver(Data d, DEF, DDF) ->
  flow_receiver<Data, passDVF, DEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF)
  (requires PUSHMI_EXP(
    lazy::Receiver<Data> PUSHMI_AND
    lazy::Invocable<DDF&, Data&>))
flow_receiver(Data d, DVF nf, DEF ef, DDF df) ->
  flow_receiver<Data, DVF, DEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF, class DStrtF)
  (requires PUSHMI_EXP(
    lazy::Receiver<Data> PUSHMI_AND
    lazy::Invocable<DDF&, Data&> ))
flow_receiver(Data d, DVF nf, DEF ef, DDF df, DStrtF strtf) ->
  flow_receiver<Data, DVF, DEF, DDF, DStrtF>;
#endif

template<>
struct construct_deduced<flow_receiver> : make_flow_receiver_fn {};


} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <chrono>
//#include <functional>
//#include "receiver.h"

namespace pushmi {
namespace detail {
template <class T, template <class...> class C>
using not_is_t = std::enable_if_t<!is_v<std::decay_t<T>, C>, std::decay_t<T>>;
} // namespace detail

//
// define types for executors

namespace detail {
template <class T>
using not_any_executor_ref_t = not_is_t<T, any_executor_ref>;
} // namespace detail

template<class E>
struct any_executor_ref {
private:
  using This = any_executor_ref;
  void* pobj_;
  struct vtable {
    void (*submit_)(void*, void*);
  } const *vptr_;
  template <class T>
  using wrapped_t = detail::not_any_executor_ref_t<T>;
public:
  using properties = property_set<is_sender<>, is_executor<>, is_single<>>;

  any_executor_ref() = delete;
  any_executor_ref(const any_executor_ref&) = default;

  PUSHMI_TEMPLATE (class Wrapped)
    (requires Sender<wrapped_t<Wrapped>, is_executor<>, is_single<>>)
    // (requires SenderTo<wrapped_t<Wrapped>, any_receiver<E, This>>)
  any_executor_ref(Wrapped& w) {
    // This can't be a requirement because it asks if submit(w, any_receiver<E,T>)
    // is well-formed (where T is an alias for any_executor_ref). If w
    // has a submit that is constrained with SingleReceiver<any_receiver<E,T>, T'&, E'>, that
    // will ask whether value(any_receiver<E,T>, T'&) is well-formed. And *that* will
    // ask whether T'& is convertible to T. That brings us right back to this
    // constructor. Constraint recursion!
    static_assert(
      SenderTo<Wrapped, any_receiver<E,This>>,
      "Expecting to be passed a Sender that can send to a SingleReceiver"
      " that accpets a value of type This and an error of type E");
    struct s {
      static void submit(void* pobj, void* s) {
        return ::pushmi::submit(
          *static_cast<Wrapped*>(pobj),
          std::move(*static_cast<any_receiver<E,This>*>(s)));
      }
    };
    static const vtable vtbl{s::submit};
    pobj_ = std::addressof(w);
    vptr_ = &vtbl;
  }
  any_executor_ref executor() { return *this; }
  template<class SingleReceiver>
  void submit(SingleReceiver&& sa) {
    // static_assert(
    //   ConvertibleTo<SingleReceiver, any_receiver<E, This>>,
    //   "requires any_receiver<E, any_executor_ref<E>>");
    any_receiver<E, This> s{(SingleReceiver&&) sa};
    vptr_->submit_(pobj_, &s);
  }
};

////////////////////////////////////////////////////////////////////////////////
// make_any_executor_ref
template <
    class E = std::exception_ptr>
auto make_any_executor_ref() {
  return any_executor_ref<E>{};
}

PUSHMI_TEMPLATE (
    class E = std::exception_ptr,
    class Wrapped)
  (requires Sender<detail::not_any_executor_ref_t<Wrapped>, is_executor<>, is_single<>>)
auto make_any_executor_ref(Wrapped& w) {
  return any_executor_ref<E>{w};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
any_executor_ref() ->
    any_executor_ref<
        std::exception_ptr>;

PUSHMI_TEMPLATE (class Wrapped)
  (requires Sender<detail::not_any_executor_ref_t<Wrapped>, is_executor<>, is_single<>>)
any_executor_ref(Wrapped&) ->
    any_executor_ref<
        std::exception_ptr>;
#endif

namespace detail {
template<class E>
using any_executor_base =
  any_single_sender<E, any_executor_ref<E>>;

template<class T, class E>
using not_any_executor =
  std::enable_if_t<
    !std::is_base_of<any_executor_base<E>, std::decay_t<T>>::value,
    std::decay_t<T>>;
} // namespace detail

template <class E>
struct any_executor : detail::any_executor_base<E> {
  constexpr any_executor() = default;
  using properties = property_set<is_sender<>, is_executor<>, is_single<>>;
  using detail::any_executor_base<E>::any_executor_base;
};

////////////////////////////////////////////////////////////////////////////////
// make_any_executor
template <
    class E = std::exception_ptr>
auto make_any_executor() -> any_executor<E> {
  return any_executor<E>{};
}

PUSHMI_TEMPLATE(
    class E = std::exception_ptr,
    class Wrapped)
  (requires SenderTo<
      detail::not_any_executor<Wrapped, E>,
      any_receiver<E, any_executor_ref<E>>>)
auto make_any_executor(Wrapped w) -> any_executor<E> {
  return any_executor<E>{std::move(w)};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
any_executor() ->
    any_executor<
        std::exception_ptr>;

PUSHMI_TEMPLATE(class Wrapped)
  (requires SenderTo<
      detail::not_any_executor<
          Wrapped,
          std::exception_ptr>,
      any_receiver<
          std::exception_ptr,
          any_executor_ref<
              std::exception_ptr>>>)
any_executor(Wrapped) ->
    any_executor<
        std::exception_ptr>;
#endif


//
// define types for constrained executors

namespace detail {
template <class T>
using not_any_constrained_executor_ref_t = not_is_t<T, any_constrained_executor_ref>;
} // namespace detail

template<class E, class CV>
struct any_constrained_executor_ref {
private:
  using This = any_constrained_executor_ref;
  void* pobj_;
  struct vtable {
    CV (*top_)(void*);
    void (*submit_)(void*, CV, void*);
  } const *vptr_;
  template <class T>
  using wrapped_t = detail::not_any_constrained_executor_ref_t<T>;
public:
  using properties = property_set<is_constrained<>, is_executor<>, is_single<>>;

  any_constrained_executor_ref() = delete;
  any_constrained_executor_ref(const any_constrained_executor_ref&) = default;

  PUSHMI_TEMPLATE (class Wrapped)
    (requires ConstrainedSender<wrapped_t<Wrapped>, is_single<>>)
    // (requires ConstrainedSenderTo<wrapped_t<Wrapped>, any_receiver<E,This>>)
  any_constrained_executor_ref(Wrapped& w) {
    // This can't be a requirement because it asks if submit(w, top(w), any_receiver<E,T>)
    // is well-formed (where T is an alias for any_constrained_executor_ref). If w
    // has a submit that is constrained with ReceiveValue<any_receiver<E,T>, T'&>, that
    // will ask whether value(any_receiver<E,T>, T'&) is well-formed. And *that* will
    // ask whether T'& is convertible to T. That brings us right back to this
    // constructor. Constraint recursion!
    static_assert(
      ConstrainedSenderTo<Wrapped, any_receiver<E,This>>,
      "Expecting to be passed a ConstrainedSender that can send to a SingleReceiver"
      " that accpets a value of type This and an error of type E");
    struct s {
      static CV top(void* pobj) {
        return ::pushmi::top(*static_cast<Wrapped*>(pobj));
      }
      static void submit(void* pobj, CV cv, void* s) {
        return ::pushmi::submit(
          *static_cast<Wrapped*>(pobj),
          cv,
          std::move(*static_cast<any_receiver<E,This>*>(s)));
      }
    };
    static const vtable vtbl{s::top, s::submit};
    pobj_ = std::addressof(w);
    vptr_ = &vtbl;
  }
  CV top() {
    return vptr_->top_(pobj_);
  }
  any_constrained_executor_ref executor() { return *this; }
  template<class SingleReceiver>
  void submit(CV cv, SingleReceiver&& sa) {
    // static_assert(
    //   ConvertibleTo<SingleReceiver, any_receiver<E, This>>,
    //   "requires any_receiver<E, any_constrained_executor_ref<E, TP>>");
    any_receiver<E, This> s{(SingleReceiver&&) sa};
    vptr_->submit_(pobj_, cv, &s);
  }
};

////////////////////////////////////////////////////////////////////////////////
// make_any_constrained_executor_ref
template <
    class E = std::exception_ptr,
    class CV = std::ptrdiff_t>
auto make_any_constrained_executor_ref() {
  return any_constrained_executor_ref<E, CV>{};
}

PUSHMI_TEMPLATE (
    class E = std::exception_ptr,
    class Wrapped)
  (requires ConstrainedSender<detail::not_any_constrained_executor_ref_t<Wrapped>, is_single<>>)
auto make_any_constrained_executor_ref(Wrapped& w) {
  return any_constrained_executor_ref<E, constraint_t<Wrapped>>{w};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
any_constrained_executor_ref() ->
    any_constrained_executor_ref<
        std::exception_ptr,
        std::ptrdiff_t>;

PUSHMI_TEMPLATE (class Wrapped)
  (requires ConstrainedSender<detail::not_any_constrained_executor_ref_t<Wrapped>, is_single<>>)
any_constrained_executor_ref(Wrapped&) ->
    any_constrained_executor_ref<
        std::exception_ptr,
        constraint_t<Wrapped>>;
#endif

namespace detail {
template<class E, class CV>
using any_constrained_executor_base =
  any_constrained_single_sender<E, CV, any_constrained_executor_ref<E, CV>>;

template<class T, class E, class CV>
using not_any_constrained_executor =
  std::enable_if_t<
    !std::is_base_of<any_constrained_executor_base<E, CV>, std::decay_t<T>>::value,
    std::decay_t<T>>;
} // namespace detail

template <class E, class CV>
struct any_constrained_executor : detail::any_constrained_executor_base<E, CV> {
  using properties = property_set<is_constrained<>, is_executor<>, is_single<>>;
  constexpr any_constrained_executor() = default;
  using detail::any_constrained_executor_base<E, CV>::any_constrained_executor_base;
};

////////////////////////////////////////////////////////////////////////////////
// make_any_constrained_executor
template <
    class E = std::exception_ptr,
    class CV = std::ptrdiff_t>
auto make_any_constrained_executor() -> any_constrained_executor<E, CV> {
  return any_constrained_executor<E, CV>{};
}

PUSHMI_TEMPLATE(
    class E = std::exception_ptr,
    class Wrapped)
  (requires ConstrainedSenderTo<
      detail::not_any_constrained_executor<Wrapped, E, constraint_t<Wrapped>>,
      any_receiver<E, any_constrained_executor_ref<E, constraint_t<Wrapped>>>>)
auto make_any_constrained_executor(Wrapped w) -> any_constrained_executor<E, constraint_t<Wrapped>> {
  return any_constrained_executor<E, constraint_t<Wrapped>>{std::move(w)};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
any_constrained_executor() ->
    any_constrained_executor<
        std::exception_ptr,
        std::ptrdiff_t>;

PUSHMI_TEMPLATE(class Wrapped)
  (requires ConstrainedSenderTo<
      detail::not_any_constrained_executor<
          Wrapped,
          std::exception_ptr,
          constraint_t<Wrapped>>,
      any_receiver<
          std::exception_ptr,
          any_constrained_executor_ref<
              std::exception_ptr,
              constraint_t<Wrapped>>>>)
any_constrained_executor(Wrapped) ->
    any_constrained_executor<
        std::exception_ptr,
        constraint_t<Wrapped>>;
#endif

//
// define types for time executors

namespace detail {
template <class T>
using not_any_time_executor_ref_t = not_is_t<T, any_time_executor_ref>;
} // namespace detail

template<class E, class TP>
struct any_time_executor_ref {
private:
  using This = any_time_executor_ref;
  void* pobj_;
  struct vtable {
    TP (*now_)(void*);
    void (*submit_)(void*, TP, void*);
  } const *vptr_;
  template <class T>
  using wrapped_t = detail::not_any_time_executor_ref_t<T>;
public:
  using properties = property_set<is_time<>, is_executor<>, is_single<>>;

  any_time_executor_ref() = delete;
  any_time_executor_ref(const any_time_executor_ref&) = default;

  PUSHMI_TEMPLATE (class Wrapped)
    (requires TimeSender<wrapped_t<Wrapped>, is_single<>>)
    // (requires TimeSenderTo<wrapped_t<Wrapped>, receiver<E, This>>)
  any_time_executor_ref(Wrapped& w) {
    // This can't be a requirement because it asks if submit(w, now(w), any_receiver<E, T>)
    // is well-formed (where T is an alias for any_time_executor_ref). If w
    // has a submit that is constrained with ReceiverValue<any_receiver<E, T>, T'&>, that
    // will ask whether value(any_receiver<E, T>, T'&) is well-formed. And *that* will
    // ask whether T'& is convertible to T. That brings us right back to this
    // constructor. Constraint recursion!
    static_assert(
      TimeSenderTo<Wrapped, any_receiver<E, This>>,
      "Expecting to be passed a TimeSender that can send to a SingleReceiver"
      " that accpets a value of type This and an error of type E");
    struct s {
      static TP now(void* pobj) {
        return ::pushmi::now(*static_cast<Wrapped*>(pobj));
      }
      static void submit(void* pobj, TP tp, void* s) {
        return ::pushmi::submit(
          *static_cast<Wrapped*>(pobj),
          tp,
          std::move(*static_cast<any_receiver<E,This>*>(s)));
      }
    };
    static const vtable vtbl{s::now, s::submit};
    pobj_ = std::addressof(w);
    vptr_ = &vtbl;
  }
  std::chrono::system_clock::time_point top() {
    return vptr_->now_(pobj_);
  }
  any_time_executor_ref executor() { return *this; }
  template<class SingleReceiver>
  void submit(TP tp, SingleReceiver&& sa) {
    // static_assert(
    //   ConvertibleTo<SingleReceiver, any_receiver<E, This>>,
    //   "requires any_receiver<E, any_time_executor_ref<E, TP>>");
    any_receiver<E, This> s{(SingleReceiver&&) sa};
    vptr_->submit_(pobj_, tp, &s);
  }
};

////////////////////////////////////////////////////////////////////////////////
// make_any_time_executor_ref
template <
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point>
auto make_any_time_executor_ref() {
  return any_time_executor_ref<E, TP>{};
}

PUSHMI_TEMPLATE (
    class E = std::exception_ptr,
    class Wrapped)
  (requires TimeSender<detail::not_any_time_executor_ref_t<Wrapped>, is_single<>>)
auto make_any_time_executor_ref(Wrapped& w) {
  return any_time_executor_ref<E, time_point_t<Wrapped>>{w};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
any_time_executor_ref() ->
    any_time_executor_ref<
        std::exception_ptr,
        std::chrono::system_clock::time_point>;

PUSHMI_TEMPLATE (class Wrapped)
  (requires TimeSender<detail::not_any_time_executor_ref_t<Wrapped>, is_single<>>)
any_time_executor_ref(Wrapped&) ->
    any_time_executor_ref<
        std::exception_ptr,
        time_point_t<Wrapped>>;
#endif

namespace detail {
template<class E, class TP>
using any_time_executor_base =
  any_time_single_sender<E, TP, any_time_executor_ref<E, TP>>;

template<class T, class E, class TP>
using not_any_time_executor =
  std::enable_if_t<
    !std::is_base_of<any_time_executor_base<E, TP>, std::decay_t<T>>::value,
    std::decay_t<T>>;
} // namespace detail

template <class E, class TP>
struct any_time_executor : detail::any_time_executor_base<E, TP> {
  using properties = property_set<is_time<>, is_executor<>, is_single<>>;
  constexpr any_time_executor() = default;
  using detail::any_time_executor_base<E, TP>::any_time_executor_base;
};

////////////////////////////////////////////////////////////////////////////////
// make_any_time_executor
template <
    class E = std::exception_ptr,
    class TP = std::chrono::system_clock::time_point>
auto make_any_time_executor() -> any_time_executor<E, TP> {
  return any_time_executor<E, TP>{};
}

PUSHMI_TEMPLATE(
    class E = std::exception_ptr,
    class Wrapped)
  (requires TimeSenderTo<
      detail::not_any_time_executor<Wrapped, E, time_point_t<Wrapped>>,
      any_receiver<E, any_time_executor_ref<E, time_point_t<Wrapped>>>>)
auto make_any_time_executor(Wrapped w) -> any_time_executor<E, time_point_t<Wrapped>> {
  return any_time_executor<E, time_point_t<Wrapped>>{std::move(w)};
}

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
any_time_executor() ->
    any_time_executor<
        std::exception_ptr,
        std::chrono::system_clock::time_point>;

PUSHMI_TEMPLATE(class Wrapped)
  (requires TimeSenderTo<
      detail::not_any_time_executor<
          Wrapped,
          std::exception_ptr,
          time_point_t<Wrapped>>,
      any_receiver<
          std::exception_ptr,
          any_time_executor_ref<
              std::exception_ptr,
              time_point_t<Wrapped>>>>)
any_time_executor(Wrapped) ->
    any_time_executor<
        std::exception_ptr,
        time_point_t<Wrapped>>;
#endif

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "executor.h"

namespace pushmi {

class inline_constrained_executor_t {
  public:
    using properties = property_set<is_constrained<>, is_executor<>, is_always_blocking<>, is_fifo_sequence<>, is_single<>>;

    std::ptrdiff_t top() {
      return 0;
    }
    auto executor() { return *this; }
    PUSHMI_TEMPLATE(class CV, class Out)
      (requires Regular<CV> && Receiver<Out>)
    void submit(CV, Out out) {
      ::pushmi::set_value(out, *this);
      ::pushmi::set_done(out);
    }
};

struct inlineConstrainedEXF {
  inline_constrained_executor_t operator()(){
    return {};
  }
};

inline inline_constrained_executor_t inline_constrained_executor() {
  return {};
}

class inline_time_executor_t {
  public:
    using properties = property_set<is_time<>, is_executor<>, is_always_blocking<>, is_fifo_sequence<>, is_single<>>;

    auto top() {
      return std::chrono::system_clock::now();
    }
    auto executor() { return *this; }
    PUSHMI_TEMPLATE(class TP, class Out)
      (requires Regular<TP> && Receiver<Out>)
    void submit(TP tp, Out out) {
      std::this_thread::sleep_until(tp);
      ::pushmi::set_value(out, *this);
      ::pushmi::set_done(out);
    }
};

struct inlineTimeEXF {
  inline_time_executor_t operator()(){
    return {};
  }
};

inline inline_time_executor_t inline_time_executor() {
  return {};
}

class inline_executor_t {
  public:
    using properties = property_set<is_sender<>, is_executor<>, is_always_blocking<>, is_fifo_sequence<>, is_single<>>;

    auto executor() { return *this; }
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void submit(Out out) {
      ::pushmi::set_value(out, *this);
      ::pushmi::set_done(out);
    }
};

struct inlineEXF {
  inline_executor_t operator()(){
    return {};
  }
};

inline inline_executor_t inline_executor() {
  return {};
}

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <algorithm>
//#include <chrono>
//#include <deque>
//#include <thread>
//#include "executor.h"

namespace pushmi {

struct recurse_t {};
constexpr const recurse_t recurse{};

struct _pipeable_sender_ {};

namespace detail {

PUSHMI_INLINE_VAR constexpr struct ownordelegate_t {} const ownordelegate {};
PUSHMI_INLINE_VAR constexpr struct ownornest_t {} const ownornest {};

class trampoline_id {
  std::thread::id threadid;
  uintptr_t trampolineid;

 public:
  template <class T>
  explicit trampoline_id(T* trampoline)
      : threadid(std::this_thread::get_id()), trampolineid(trampoline) {}
};

template <class E = std::exception_ptr>
class trampoline;

template <class E = std::exception_ptr>
class delegator : _pipeable_sender_ {
 public:
  using properties = property_set<is_sender<>, is_executor<>, is_maybe_blocking<>, is_fifo_sequence<>, is_single<>>;

  delegator executor() { return {}; }
  PUSHMI_TEMPLATE (class SingleReceiver)
    (requires ReceiveValue<remove_cvref_t<SingleReceiver>, any_executor_ref<E>>)
  void submit(SingleReceiver&& what) {
    trampoline<E>::submit(
        ownordelegate, std::forward<SingleReceiver>(what));
  }
};

template <class E = std::exception_ptr>
class nester : _pipeable_sender_ {
 public:
  using properties = property_set<is_sender<>, is_executor<>, is_maybe_blocking<>, is_fifo_sequence<>, is_single<>>;

  nester executor() { return {}; }
  PUSHMI_TEMPLATE (class SingleReceiver)
    (requires ReceiveValue<remove_cvref_t<SingleReceiver>, any_executor_ref<E>>)
  void submit(SingleReceiver&& what) {
    trampoline<E>::submit(ownornest, std::forward<SingleReceiver>(what));
  }
};

template <class E>
class trampoline {
 private:
  using error_type = std::decay_t<E>;
  using work_type =
     any_receiver<error_type, any_executor_ref<error_type>>;
  using queue_type = std::deque<work_type>;
  using pending_type = std::tuple<int, queue_type, bool>;

  inline static pending_type*& owner() {
    static thread_local pending_type* pending = nullptr;
    return pending;
  }

  inline static int& depth(pending_type& p) {
    return std::get<0>(p);
  }

  inline static queue_type& pending(pending_type& p) {
    return std::get<1>(p);
  }

  inline static bool& repeat(pending_type& p) {
    return std::get<2>(p);
  }

 public:
  inline static trampoline_id get_id() {
    return {owner()};
  }

  inline static bool is_owned() {
    return owner() != nullptr;
  }

  template <class Selector, class Derived>
  static void submit(Selector, Derived&, recurse_t) {
    if (!is_owned()) {
      abort();
    }
    repeat(*owner()) = true;
  }

  PUSHMI_TEMPLATE (class SingleReceiver)
    (requires not Same<SingleReceiver, recurse_t>)
  static void submit(ownordelegate_t, SingleReceiver awhat) {
    delegator<E> that;

    if (is_owned()) {
      // thread already owned

      // poor mans scope guard
      try {
        if (++depth(*owner()) > 100) {
          // defer work to owner
          pending(*owner()).push_back(work_type{std::move(awhat)});
        } else {
          // dynamic recursion - optimization to balance queueing and
          // stack usage and value interleaving on the same thread.
          ::pushmi::set_value(awhat, that);
          ::pushmi::set_done(awhat);
        }
      } catch(...) {
        --depth(*owner());
        throw;
      }
      --depth(*owner());
      return;
    }

    // take over the thread

    pending_type pending_store;
    owner() = &pending_store;
    depth(pending_store) = 0;
    repeat(pending_store) = false;
    // poor mans scope guard
    try {
      trampoline<E>::submit(ownornest, std::move(awhat));
    } catch(...) {

      // ignore exceptions while delivering the exception
      try {
        ::pushmi::set_error(awhat, std::current_exception());
        for (auto& what : pending(pending_store)) {
          ::pushmi::set_error(what, std::current_exception());
        }
      } catch (...) {
      }
      pending(pending_store).clear();

      if(!is_owned()) { std::abort(); }
      if(!pending(pending_store).empty()) { std::abort(); }
      owner() = nullptr;
      throw;
    }
    if(!is_owned()) { std::abort(); }
    if(!pending(pending_store).empty()) { std::abort(); }
    owner() = nullptr;
  }

  PUSHMI_TEMPLATE (class SingleReceiver)
    (requires not Same<SingleReceiver, recurse_t>)
  static void submit(ownornest_t, SingleReceiver awhat) {
    delegator<E> that;

    if (!is_owned()) {
      trampoline<E>::submit(ownordelegate, std::move(awhat));
      return;
    }

    auto& pending_store = *owner();

    // static recursion - tail call optimization
    if (pending(pending_store).empty()) {
      bool go = true;
      while (go) {
        repeat(pending_store) = false;
        ::pushmi::set_value(awhat, that);
        ::pushmi::set_done(awhat);
        go = repeat(pending_store);
      }
    } else {
      pending(pending_store)
          .push_back(work_type{std::move(awhat)});
    }

    if (pending(pending_store).empty()) {
      return;
    }

    while (!pending(pending_store).empty()) {
      auto what = std::move(pending(pending_store).front());
      pending(pending_store).pop_front();
      ::pushmi::set_value(what, any_executor_ref<error_type>{that});
      ::pushmi::set_done(what);
    }
  }
};

} // namespace detail

template <class E = std::exception_ptr>
detail::trampoline_id get_trampoline_id() {
  if(!detail::trampoline<E>::is_owned()) { std::abort(); }
  return detail::trampoline<E>::get_id();
}

template <class E = std::exception_ptr>
bool owned_by_trampoline() {
  return detail::trampoline<E>::is_owned();
}

template <class E = std::exception_ptr>
inline detail::delegator<E> trampoline() {
  return {};
}
template <class E = std::exception_ptr>
inline detail::nester<E> nested_trampoline() {
  return {};
}

// see boosters.h
struct trampolineEXF {
  auto operator()() { return trampoline(); }
};

namespace detail {

PUSHMI_TEMPLATE (class E)
  (requires SenderTo<delegator<E>, recurse_t>)
decltype(auto) repeat(delegator<E>& exec) {
  ::pushmi::submit(exec, recurse);
}
template <class AnyExec>
void repeat(AnyExec& exec) {
  std::abort();
}

} // namespace detail

inline auto repeat() {
  return [](auto& exec) { detail::repeat(exec); };
}

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "executor.h"
//#include "trampoline.h"

namespace pushmi {

// very poor perf example executor.
//

struct new_thread_executor {
  using properties = property_set<is_sender<>, is_executor<>, is_never_blocking<>, is_concurrent_sequence<>, is_single<>>;

  new_thread_executor executor() { return {}; }
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>)
  void submit(Out out) {
    std::thread t{[out = std::move(out)]() mutable {
      auto tr = ::pushmi::trampoline();
      ::pushmi::submit(tr, std::move(out));
    }};
    // pass ownership of thread to out
    t.detach();
  }
};

inline new_thread_executor new_thread() {
  return {};
}

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "single_sender.h"
//#include "executor.h"

//#include <queue>

namespace pushmi {

template<class E, class Executor>
class strand_executor;

template<class E, class Executor>
struct strand_queue_receiver;

template<class E>
class strand_item
{
public:

  strand_item(any_receiver<E, any_executor_ref<E>> out) :
    what(std::move(out)) {}

  any_receiver<E, any_executor_ref<E>> what;
};
template<class E, class TP>
bool operator<(const strand_item<E>& l, const strand_item<E>& r) {
  return l.when < r.when;
}
template<class E, class TP>
bool operator>(const strand_item<E>& l, const strand_item<E>& r) {
  return l.when > r.when;
}
template<class E, class TP>
bool operator==(const strand_item<E>& l, const strand_item<E>& r) {
  return l.when == r.when;
}
template<class E, class TP>
bool operator!=(const strand_item<E>& l, const strand_item<E>& r) {
  return !(l == r);
}
template<class E, class TP>
bool operator<=(const strand_item<E>& l, const strand_item<E>& r) {
  return !(l > r);
}
template<class E, class TP>
bool operator>=(const strand_item<E>& l, const strand_item<E>& r) {
  return !(l < r);
}

template<class E>
class strand_queue_base : public std::enable_shared_from_this<strand_queue_base<E>>{
public:
  std::mutex lock_;
  size_t remaining_ = 0;
  std::queue<strand_item<E>> items_;

  virtual ~strand_queue_base() {}

  strand_item<E>& front() {
    // :(
    return const_cast<strand_item<E>&>(this->items_.front());
  }

  virtual void dispatch()=0;
};

template<class E, class Executor>
class strand_queue : public strand_queue_base<E> {
public:
  ~strand_queue() {
  }
  strand_queue(Executor ex) :
    ex_(std::move(ex)) {}
  Executor ex_;

  void dispatch() override;

  auto shared_from_that() {
    return std::static_pointer_cast<
            strand_queue<E, Executor>>(
              this->shared_from_this());
  }

  template<class Exec>
  void value(Exec&&) {
    //
    // pull ready items from the queue in order.

    std::unique_lock<std::mutex> guard{this->lock_};

    // only allow one at a time
    if (this->remaining_ > 0) { return; }
    // skip when empty
    if (this->items_.empty()) { return; }

    // do not allow recursive queueing to block this executor
    this->remaining_ = this->items_.size();

    auto that = shared_from_that();
    auto subEx = strand_executor<E, Executor>{that};

    while (!this->items_.empty() && --this->remaining_ >= 0) {
      auto item{std::move(this->front())};
      this->items_.pop();
      guard.unlock();
      ::pushmi::set_value(item.what, any_executor_ref<E>{subEx});
      ::pushmi::set_done(item.what);
      guard.lock();
    }
  }
  template<class AE>
  void error(AE e) noexcept {
    std::unique_lock<std::mutex> guard{this->lock_};

    this->remaining_ = 0;

    while (!this->items_.empty()) {
      auto what{std::move(this->front().what)};
      this->items_.pop();
      guard.unlock();
      ::pushmi::set_error(what, detail::as_const(e));
      guard.lock();
    }
  }
  void done() {
    std::unique_lock<std::mutex> guard{this->lock_};

    // only allow one at a time
    if (this->remaining_ > 0) { return; }
    // skip when empty
    if (this->items_.empty()) { return; }

    auto that = shared_from_that();
    ::pushmi::submit(ex_, strand_queue_receiver<E, Executor>{that});
  }
};

template<class E, class Executor>
struct strand_queue_receiver : std::shared_ptr<strand_queue<E, Executor>> {
  ~strand_queue_receiver() {
  }
  explicit strand_queue_receiver(std::shared_ptr<strand_queue<E, Executor>> that) :
    std::shared_ptr<strand_queue<E, Executor>>(that)
    {}
  using properties = property_set<is_receiver<>>;
};

template<class E, class Executor>
void strand_queue<E, Executor>::dispatch() {
  ::pushmi::submit(ex_,
    strand_queue_receiver<E, Executor>{
      shared_from_that()});
}

//
// strand is used to build a fifo single_executor from a concurrent single_executor.
//

template<class E, class Executor>
class strand_executor {
  std::shared_ptr<strand_queue<E, Executor>> queue_;
public:
  using properties = property_set<is_sender<>, is_executor<>, property_set_index_t<properties_t<Executor>, is_never_blocking<>>, is_fifo_sequence<>, is_single<>>;

  strand_executor(
    std::shared_ptr<strand_queue<E, Executor>> queue) :
    queue_(std::move(queue)) {}

  auto executor() { return *this; }

  PUSHMI_TEMPLATE(class Out)
    (requires ReceiveValue<Out, any_executor_ref<E>> && ReceiveError<Out, E>)
  void submit(Out out) {
    // queue for later
    std::unique_lock<std::mutex> guard{queue_->lock_};
    queue_->items_.push(any_receiver<E, any_executor_ref<E>>{std::move(out)});
    if (queue_->remaining_ == 0) {
      // noone is minding the shop, send a worker
      ::pushmi::submit(queue_->ex_, strand_queue_receiver<E, Executor>{queue_});
    }
  }
};

//
// the strand executor factory produces a new fifo ordered queue each time that it is called.
//

template<class E, class ExecutorFactory>
class strand_executor_factory_fn {
  ExecutorFactory ef_;
public:
  explicit strand_executor_factory_fn(ExecutorFactory ef) : ef_(std::move(ef)) {}
  auto operator()() const {
    auto ex = ef_();
    auto queue = std::make_shared<strand_queue<E, decltype(ex)>>(
      std::move(ex));
    return strand_executor<E, decltype(ex)>{queue};
  }
};

template<class Exec>
class same_executor_factory_fn {
  Exec ex_;
public:
  explicit same_executor_factory_fn(Exec ex) : ex_(std::move(ex)) {}
  auto operator()() const {
    return ex_;
  }
};

PUSHMI_TEMPLATE(class E = std::exception_ptr, class ExecutorFactory)
  (requires Invocable<ExecutorFactory&> &&
    Executor<invoke_result_t<ExecutorFactory&>> &&
    ConcurrentSequence<invoke_result_t<ExecutorFactory&>>)
auto strands(ExecutorFactory ef) {
  return strand_executor_factory_fn<E, ExecutorFactory>{std::move(ef)};
}
PUSHMI_TEMPLATE(class E = std::exception_ptr, class Exec)
  (requires Executor<Exec> &&
    ConcurrentSequence<Exec>)
auto strands(Exec ex) {
  return strand_executor_factory_fn<E, same_executor_factory_fn<Exec>>{same_executor_factory_fn<Exec>{std::move(ex)}};
}

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "receiver.h"
//#include "executor.h"
//#include "inline.h"

namespace pushmi {

template <class E, class CV, class... VN>
class any_constrained_single_sender {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<int>)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static CV s_top(data&) { return CV{}; }
    static any_constrained_executor<E, CV> s_executor(data&) { return {}; }
    static void s_submit(data&, CV, any_receiver<E, VN...>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    CV (*top_)(data&) = vtable::s_top;
    any_constrained_executor<E, CV> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, CV, any_receiver<E, VN...>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_constrained_single_sender(Wrapped obj, std::false_type)
    : any_constrained_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static CV top(data& src) {
        return ::pushmi::top(*static_cast<Wrapped*>(src.pobj_));
      }
      static any_constrained_executor<E, CV> executor(data& src) {
        return any_constrained_executor<E, CV>{::pushmi::executor(*static_cast<Wrapped*>(src.pobj_))};
      }
      static void submit(data& src, CV at, any_receiver<E, VN...> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>(src.pobj_),
            std::move(at),
            std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::top, s::executor, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_constrained_single_sender(Wrapped obj, std::true_type) noexcept
    : any_constrained_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static CV top(data& src) {
        return ::pushmi::top(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static any_constrained_executor<E, CV> executor(data& src) {
        return any_constrained_executor<E, CV>{::pushmi::executor(*static_cast<Wrapped*>((void*)src.buffer_))};
      }
      static void submit(data& src, CV cv, any_receiver<E, VN...> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_),
            std::move(cv),
            std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::top, s::executor, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_constrained_single_sender>::value, U>;

 public:
  using properties = property_set<is_constrained<>, is_single<>>;

  any_constrained_single_sender() = default;
  any_constrained_single_sender(any_constrained_single_sender&& that) noexcept
      : any_constrained_single_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires ConstrainedSenderTo<wrapped_t<Wrapped>, any_receiver<E, VN...>>)
  explicit any_constrained_single_sender(Wrapped obj) noexcept(insitu<Wrapped>())
  : any_constrained_single_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {
  }
  ~any_constrained_single_sender() {
    vptr_->op_(data_, nullptr);
  }
  any_constrained_single_sender& operator=(any_constrained_single_sender&& that) noexcept {
    this->~any_constrained_single_sender();
    new ((void*)this) any_constrained_single_sender(std::move(that));
    return *this;
  }
  CV top() {
    return vptr_->top_(data_);
  }
  any_constrained_executor<E, CV> executor() {
    return vptr_->executor_(data_);
  }
  void submit(CV at, any_receiver<E, VN...> out) {
    vptr_->submit_(data_, std::move(at), std::move(out));
  }
};

// Class static definitions:
template <class E, class CV, class... VN>
constexpr typename any_constrained_single_sender<E, CV, VN...>::vtable const
    any_constrained_single_sender<E, CV, VN...>::noop_;

template<class SF, class ZF, class EXF>
  // (requires Invocable<ZF&> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
class constrained_single_sender<SF, ZF, EXF> {
  SF sf_;
  EXF exf_;
  ZF zf_;

 public:
  using properties = property_set<is_constrained<>, is_single<>>;

  constexpr constrained_single_sender() = default;
  constexpr explicit constrained_single_sender(SF sf)
      : sf_(std::move(sf)) {}
  constexpr constrained_single_sender(SF sf, EXF exf)
      : sf_(std::move(sf)), exf_(std::move(exf)) {}
  constexpr constrained_single_sender(SF sf, EXF exf, ZF zf)
      : sf_(std::move(sf)), zf_(std::move(zf)), exf_(std::move(exf)) {}

  auto top() {
    return zf_();
  }
  auto executor() { return exf_(); }
  PUSHMI_TEMPLATE(class CV, class Out)
    (requires Regular<CV> && Receiver<Out> &&
      Invocable<SF&, CV, Out>)
  void submit(CV cv, Out out) {
    sf_(std::move(cv), std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(ConstrainedSender<is_single<>>) Data, class DSF, class DZF, class DEXF>
#if __cpp_concepts
  requires Invocable<DZF&, Data&> && Invocable<DEXF&, Data&>
#endif
class constrained_single_sender<Data, DSF, DZF, DEXF> {
  Data data_;
  DSF sf_;
  DEXF exf_;
  DZF zf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_single<>>>;

  constexpr constrained_single_sender() = default;
  constexpr explicit constrained_single_sender(Data data)
      : data_(std::move(data)) {}
  constexpr constrained_single_sender(Data data, DSF sf, DEXF exf = DEXF{})
      : data_(std::move(data)), sf_(std::move(sf)), exf_(std::move(exf)) {}
  constexpr constrained_single_sender(Data data, DSF sf, DEXF exf, DZF zf)
      : data_(std::move(data)), sf_(std::move(sf)), zf_(std::move(zf)), exf_(std::move(exf)) {}

  auto top() {
    return zf_(data_);
  }
  auto executor() { return exf_(data_); }
  PUSHMI_TEMPLATE(class CV, class Out)
    (requires Regular<CV> && Receiver<Out> &&
      Invocable<DSF&, Data&, CV, Out>)
  void submit(CV cv, Out out) {
    sf_(data_, std::move(cv), std::move(out));
  }
};

template <>
class constrained_single_sender<>
    : public constrained_single_sender<ignoreSF, priorityZeroF, inlineConstrainedEXF> {
public:
  constrained_single_sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_constrained_single_sender
PUSHMI_INLINE_VAR constexpr struct make_constrained_single_sender_fn {
  inline auto operator()() const  {
    return constrained_single_sender<ignoreSF, priorityZeroF, inlineConstrainedEXF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return constrained_single_sender<SF, priorityZeroF, inlineConstrainedEXF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE (class SF, class EXF)
    (requires Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf) const {
    return constrained_single_sender<SF, priorityZeroF, EXF>{std::move(sf), std::move(exf)};
  }
  PUSHMI_TEMPLATE (class SF, class ZF, class EXF)
    (requires Invocable<ZF&> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf, ZF zf) const {
    return constrained_single_sender<SF, ZF, EXF>{std::move(sf), std::move(exf), std::move(zf)};
  }
  PUSHMI_TEMPLATE (class Data)
    (requires ConstrainedSender<Data, is_single<>>)
  auto operator()(Data d) const {
    return constrained_single_sender<Data, passDSF, passDZF, passDEXF>{std::move(d)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF)
    (requires ConstrainedSender<Data, is_single<>>)
  auto operator()(Data d, DSF sf) const {
    return constrained_single_sender<Data, DSF, passDZF, passDEXF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF, class DEXF)
    (requires ConstrainedSender<Data, is_single<>> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf) const  {
    return constrained_single_sender<Data, DSF, passDZF, DEXF>{std::move(d), std::move(sf),
      std::move(exf)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF, class DZF, class DEXF)
    (requires ConstrainedSender<Data, is_single<>> && Invocable<DZF&, Data&> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf, DZF zf) const  {
    return constrained_single_sender<Data, DSF, DZF, DEXF>{std::move(d), std::move(sf),
      std::move(exf), std::move(zf)};
  }
} const make_constrained_single_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
constrained_single_sender() -> constrained_single_sender<ignoreSF, priorityZeroF, inlineConstrainedEXF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
constrained_single_sender(SF) -> constrained_single_sender<SF, priorityZeroF, inlineConstrainedEXF>;

PUSHMI_TEMPLATE (class SF, class EXF)
  (requires Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
constrained_single_sender(SF, EXF) -> constrained_single_sender<SF, priorityZeroF, EXF>;

PUSHMI_TEMPLATE (class SF, class ZF, class EXF)
  (requires Invocable<ZF&> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
constrained_single_sender(SF, EXF, ZF) -> constrained_single_sender<SF, ZF, EXF>;

PUSHMI_TEMPLATE (class Data, class DSF)
  (requires ConstrainedSender<Data, is_single<>>)
constrained_single_sender(Data, DSF) -> constrained_single_sender<Data, DSF, passDZF, passDEXF>;

PUSHMI_TEMPLATE (class Data, class DSF, class DEXF)
  (requires ConstrainedSender<Data, is_single<>> && Invocable<DEXF&, Data&>)
constrained_single_sender(Data, DSF, DEXF) -> constrained_single_sender<Data, DSF, passDZF, DEXF>;

PUSHMI_TEMPLATE (class Data, class DSF, class DZF, class DEXF)
  (requires ConstrainedSender<Data, is_single<>> && Invocable<DZF&, Data&> && Invocable<DEXF&, Data&>)
constrained_single_sender(Data, DSF, DEXF, DZF) -> constrained_single_sender<Data, DSF, DZF, DEXF>;
#endif

template<>
struct construct_deduced<constrained_single_sender>
  : make_constrained_single_sender_fn {};

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "receiver.h"
//#include "executor.h"
//#include "inline.h"
//#include "constrained_single_sender.h"

namespace pushmi {


template <class E, class TP, class... VN>
class any_time_single_sender : public any_constrained_single_sender<E, TP, VN...> {
public:
  using properties = property_set<is_time<>, is_single<>>;
  constexpr any_time_single_sender() = default;
  template<class T>
  constexpr explicit any_time_single_sender(T t)
      : any_constrained_single_sender<E, TP, VN...>(std::move(t)) {}
  template<class T0, class T1, class... TN>
  constexpr any_time_single_sender(T0 t0, T1 t1, TN... tn)
      : any_constrained_single_sender<E, TP, VN...>(std::move(t0), std::move(t1), std::move(tn)...) {}

  any_time_executor<E, TP> executor() {
    return any_time_executor<E, TP>{any_constrained_single_sender<E, TP, VN...>::executor()};
  }

};

template<class SF, class NF, class EXF>
class time_single_sender<SF, NF, EXF> : public constrained_single_sender<SF, NF, EXF> {
public:
  using properties = property_set<is_time<>, is_single<>>;

  constexpr time_single_sender() = default;
  template<class T>
  constexpr explicit time_single_sender(T t)
      : constrained_single_sender<SF, NF, EXF>(std::move(t)) {}
  template<class T0, class T1, class... TN>
  constexpr time_single_sender(T0 t0, T1 t1, TN... tn)
      : constrained_single_sender<SF, NF, EXF>(std::move(t0), std::move(t1), std::move(tn)...) {}
};

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class time_single_sender : public constrained_single_sender<TN...> {
public:
  constexpr time_single_sender() = default;
  template<class T>
  constexpr explicit time_single_sender(T t)
      : constrained_single_sender<TN...>(std::move(t)) {}
  template<class C0, class C1, class... CN>
  constexpr time_single_sender(C0 c0, C1 c1, CN... cn)
      : constrained_single_sender<TN...>(std::move(c0), std::move(c1), std::move(cn)...) {}
};

template <>
class time_single_sender<>
    : public time_single_sender<ignoreSF, systemNowF, inlineTimeEXF> {
public:
  time_single_sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_time_single_sender
PUSHMI_INLINE_VAR constexpr struct make_time_single_sender_fn {
  inline auto operator()() const  {
    return time_single_sender<ignoreSF, systemNowF, inlineTimeEXF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return time_single_sender<SF, systemNowF, inlineTimeEXF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE (class SF, class EXF)
    (requires Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf) const {
    return time_single_sender<SF, systemNowF, EXF>{std::move(sf), std::move(exf)};
  }
  PUSHMI_TEMPLATE (class SF, class NF, class EXF)
    (requires Invocable<NF&> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf, NF nf) const {
    return time_single_sender<SF, NF, EXF>{std::move(sf), std::move(exf), std::move(nf)};
  }
  PUSHMI_TEMPLATE (class Data)
    (requires TimeSender<Data, is_single<>>)
  auto operator()(Data d) const {
    return time_single_sender<Data, passDSF, passDNF, passDEXF>{std::move(d)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF)
    (requires TimeSender<Data, is_single<>>)
  auto operator()(Data d, DSF sf) const {
    return time_single_sender<Data, DSF, passDNF, passDEXF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF, class DEXF)
    (requires TimeSender<Data, is_single<>> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf) const  {
    return time_single_sender<Data, DSF, passDNF, DEXF>{std::move(d), std::move(sf),
      std::move(exf)};
  }
  PUSHMI_TEMPLATE (class Data, class DSF, class DNF, class DEXF)
    (requires TimeSender<Data, is_single<>> && Invocable<DNF&, Data&> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf, DNF nf) const  {
    return time_single_sender<Data, DSF, DNF, DEXF>{std::move(d), std::move(sf),
      std::move(exf), std::move(nf)};
  }
} const make_time_single_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
time_single_sender() -> time_single_sender<ignoreSF, systemNowF, inlineTimeEXF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
time_single_sender(SF) -> time_single_sender<SF, systemNowF, inlineTimeEXF>;

PUSHMI_TEMPLATE (class SF, class EXF)
  (requires Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
time_single_sender(SF, EXF) -> time_single_sender<SF, systemNowF, EXF>;

PUSHMI_TEMPLATE (class SF, class NF, class EXF)
  (requires Invocable<NF&> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
time_single_sender(SF, EXF, NF) -> time_single_sender<SF, NF, EXF>;

PUSHMI_TEMPLATE (class Data, class DSF)
  (requires TimeSender<Data, is_single<>>)
time_single_sender(Data, DSF) -> time_single_sender<Data, DSF, passDNF, passDEXF>;

PUSHMI_TEMPLATE (class Data, class DSF, class DEXF)
  (requires TimeSender<Data, is_single<>> && Invocable<DEXF&, Data&>)
time_single_sender(Data, DSF, DEXF) -> time_single_sender<Data, DSF, passDNF, DEXF>;

PUSHMI_TEMPLATE (class Data, class DSF, class DNF, class DEXF)
  (requires TimeSender<Data, is_single<>> && Invocable<DNF&, Data&> && Invocable<DEXF&, Data&>)
time_single_sender(Data, DSF, DEXF, DNF) -> time_single_sender<Data, DSF, DNF, DEXF>;
#endif

template<>
struct construct_deduced<time_single_sender>
  : make_time_single_sender_fn {};

} //namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "time_single_sender.h"
//#include "executor.h"

//#include <queue>

//
// time_source is used to build a time_single_executor from a single_executor.
//

namespace pushmi {

template<class E, class TP>
class time_source_shared;

template<class E, class TP, class NF, class Executor>
class time_source_executor;

template<class E, class TP>
class time_heap_item
{
public:
  using time_point = std::decay_t<TP>;

  time_heap_item(time_point at, any_receiver<E, any_time_executor_ref<E, TP>> out) :
    when(std::move(at)), what(std::move(out)) {}

  time_point when;
  any_receiver<E, any_time_executor_ref<E, TP>> what;
};
template<class E, class TP>
bool operator<(const time_heap_item<E, TP>& l, const time_heap_item<E, TP>& r) {
  return l.when < r.when;
}
template<class E, class TP>
bool operator>(const time_heap_item<E, TP>& l, const time_heap_item<E, TP>& r) {
  return l.when > r.when;
}
template<class E, class TP>
bool operator==(const time_heap_item<E, TP>& l, const time_heap_item<E, TP>& r) {
  return l.when == r.when;
}
template<class E, class TP>
bool operator!=(const time_heap_item<E, TP>& l, const time_heap_item<E, TP>& r) {
  return !(l == r);
}
template<class E, class TP>
bool operator<=(const time_heap_item<E, TP>& l, const time_heap_item<E, TP>& r) {
  return !(l > r);
}
template<class E, class TP>
bool operator>=(const time_heap_item<E, TP>& l, const time_heap_item<E, TP>& r) {
  return !(l < r);
}

template<class E, class TP>
class time_source_queue_base : public std::enable_shared_from_this<time_source_queue_base<E, TP>>{
public:
  using time_point = std::decay_t<TP>;
  bool dispatching_ = false;
  bool pending_ = false;
  std::priority_queue<time_heap_item<E, TP>, std::vector<time_heap_item<E, TP>>, std::greater<>> heap_;

  virtual ~time_source_queue_base() {}

  time_heap_item<E, TP>& top() {
    // :(
    return const_cast<time_heap_item<E, TP>&>(this->heap_.top());
  }

  virtual void dispatch()=0;
};

template<class E, class TP, class NF, class Executor>
class time_source_queue : public time_source_queue_base<E, TP> {
public:
  using time_point = std::decay_t<TP>;
  ~time_source_queue() {
  }
  time_source_queue(std::weak_ptr<time_source_shared<E, time_point>> source, NF nf, Executor ex) :
    source_(std::move(source)), nf_(std::move(nf)), ex_(std::move(ex)) {}
  std::weak_ptr<time_source_shared<E, time_point>> source_;
  NF nf_;
  Executor ex_;

  void dispatch() override;

  auto shared_from_that() {
    return std::static_pointer_cast<
            time_source_queue<E, TP, NF, Executor>>(
              this->shared_from_this());
  }

  template<class Exec>
  void value(Exec&&) {
    auto s = source_.lock();

    if (s->t_.get_id() == std::this_thread::get_id()) {
      // Executor is not allowed to use the time_source thread
      std::abort();
    }

    //
    // pull ready items from the heap in order.

    // drain anything queued within the next 50ms before
    // going back to the pending queue.
    auto start = nf_() + std::chrono::milliseconds(50);

    std::unique_lock<std::mutex> guard{s->lock_};

    if (!this->dispatching_ || this->pending_) {
      std::abort();
    }

    if (this->heap_.empty()) { return; }
    auto that = shared_from_that();
    auto subEx = time_source_executor<E, TP, NF, Executor>{s, that};
    while (!this->heap_.empty() && this->heap_.top().when <= start) {
      auto item{std::move(this->top())};
      this->heap_.pop();
      guard.unlock();
      std::this_thread::sleep_until(item.when);
      ::pushmi::set_value(item.what, any_time_executor_ref<E, TP>{subEx});
      ::pushmi::set_done(item.what);
      guard.lock();
      // allows set_value to queue nested items
      --s->items_;
    }

    if (this->heap_.empty()) {
      // if this is empty, tell worker to check for the done condition.
      ++s->dirty_;
      s->wake_.notify_one();
    } else {
      if (!!s->error_) {
        while(!this->heap_.empty()) {
          try {
            auto what{std::move(this->top().what)};
            this->heap_.pop();
            --s->items_;
            guard.unlock();
            ::pushmi::set_error(what, *s->error_);
            guard.lock();
          } catch(...) {
            // we already have an error, ignore this one.
          }
        }
      }
    }
  }
  template<class AE>
  void error(AE e) noexcept {
    auto s = source_.lock();
    std::unique_lock<std::mutex> guard{s->lock_};

    if (!this->dispatching_ || this->pending_) {
      std::abort();
    }

    while (!this->heap_.empty()) {
      auto what{std::move(this->top().what)};
      this->heap_.pop();
      --s->items_;
      guard.unlock();
      ::pushmi::set_error(what, detail::as_const(e));
      guard.lock();
    }
    this->dispatching_ = false;
  }
  void done() {
    auto s = source_.lock();
    std::unique_lock<std::mutex> guard{s->lock_};

    if (!this->dispatching_ || this->pending_) {
      std::abort();
    }
    this->dispatching_ = false;

    // add back to pending_ to get the remaining items dispatched
    s->pending_.push_back(this->shared_from_this());
    this->pending_ = true;
    if (this->heap_.top().when <= s->earliest_) {
      // this is the earliest, tell worker to reset earliest_
      ++s->dirty_;
      s->wake_.notify_one();
    }
  }
};

template<class E, class TP, class NF, class Executor>
struct time_source_queue_receiver : std::shared_ptr<time_source_queue<E, TP, NF, Executor>> {
  ~time_source_queue_receiver() {
  }
  explicit time_source_queue_receiver(std::shared_ptr<time_source_queue<E, TP, NF, Executor>> that) :
    std::shared_ptr<time_source_queue<E, TP, NF, Executor>>(that),
    source_(that->source_.lock()) {
    }
  using properties = property_set<is_receiver<>>;
  std::shared_ptr<time_source_shared<E, TP>> source_;
};

template<class E, class TP, class NF, class Executor>
void time_source_queue<E, TP, NF, Executor>::dispatch() {
  ::pushmi::submit(ex_,
    time_source_queue_receiver<E, TP, NF, Executor>{
      shared_from_that()});
}

template<class E, class TP>
class time_queue_dispatch_pred_fn {
public:
  bool operator()(std::shared_ptr<time_source_queue_base<E, TP>>& q){
    return !q->heap_.empty();
  }
};

template<class E, class TP>
class time_item_process_pred_fn {
public:
  using time_point = std::decay_t<TP>;
  const time_point* start_;
  time_point* earliest_;
  bool operator()(const std::shared_ptr<time_source_queue_base<E, TP>>& q){
    // ready for dispatch if it has a ready item
    bool ready = !q->dispatching_ && !q->heap_.empty() && q->heap_.top().when <= *start_;
    q->dispatching_ = ready;
    q->pending_ = !ready && !q->heap_.empty();
    // ready queues are ignored, they will update earliest_ after they have processed the ready items
    *earliest_ = !ready && !q->heap_.empty() ? min(*earliest_, q->heap_.top().when) : *earliest_;
    return q->pending_;
  }
};

template<class E, class TP>
class time_source_shared_base : public std::enable_shared_from_this<time_source_shared_base<E, TP>> {
public:
  using time_point = std::decay_t<TP>;
  std::mutex lock_;
  std::condition_variable wake_;
  std::thread t_;
  std::chrono::system_clock::time_point earliest_;
  bool done_;
  bool joined_;
  int dirty_;
  int items_;
  detail::opt<E> error_;
  std::deque<std::shared_ptr<time_source_queue_base<E, TP>>> pending_;

  time_source_shared_base() :
    earliest_(std::chrono::system_clock::now() + std::chrono::hours(24)),
    done_(false),
    joined_(false),
    dirty_(0),
    items_(0) {}
};

template<class E, class TP>
class time_source_shared : public time_source_shared_base<E, TP> {
public:
  std::thread t_;
  // this is safe to reuse as long as there is only one thread in the time_source_shared
  std::vector<std::shared_ptr<time_source_queue_base<E, TP>>> ready_;

  ~time_source_shared() {
    // not allowed to be discarded without joining and completing all queued items
    if (t_.joinable() || this->items_ != 0) { std::abort(); }
  }
  time_source_shared() {
  }

  static void start(std::shared_ptr<time_source_shared<E, TP>> that) {
    that->t_ = std::thread{&time_source_shared<E, TP>::worker, that};
  }
  static void join(std::shared_ptr<time_source_shared<E, TP>> that) {
    std::unique_lock<std::mutex> guard{that->lock_};
    that->done_ = true;
    ++that->dirty_;
    that->wake_.notify_one();
    guard.unlock();
    that->t_.join();
  }

  static void worker(std::shared_ptr<time_source_shared<E, TP>> that) {
    try {
      std::unique_lock<std::mutex> guard{that->lock_};

      // once done_, keep going until empty
      while (!that->done_ || that->items_ > 0) {

        // wait for something to do
        that->wake_.wait_until(
          guard,
          that->earliest_,
          [&](){
            return that->dirty_ != 0 ||
              std::chrono::system_clock::now() >= that->earliest_;
          });
        that->dirty_ = 0;

        //
        // select ready and empty queues and reset earliest_

        auto start = std::chrono::system_clock::now();
        auto earliest = start + std::chrono::hours(24);
        auto process = time_item_process_pred_fn<E, TP>{&start, &earliest};

        auto process_begin = std::partition(that->pending_.begin(), that->pending_.end(), process);
        that->earliest_ = earliest;

        // copy out the queues that have ready items so that the lock
        // is not held during dispatch

        std::copy_if(process_begin, that->pending_.end(), std::back_inserter(that->ready_), time_queue_dispatch_pred_fn<E, TP>{});

        // remove processed queues from pending queue.
        that->pending_.erase(process_begin, that->pending_.end());

        // printf("d %lu, %lu, %d, %ld\n", that->pending_.size(), that->ready_.size(), that->items_, std::chrono::duration_cast<std::chrono::milliseconds>(earliest - start).count());

        // dispatch to queues with ready items
        guard.unlock();
        for (auto& q : that->ready_) {
          q->dispatch();
        }
        guard.lock();
        that->ready_.clear();
      }
      that->joined_ = true;
    } catch(...) {
      //
      // block any more items from being enqueued, all new items will be sent
      // this error on the same context that calls submit
      //
      // also dispatch errors to all items already in the queues from the
      // time thread
      std::unique_lock<std::mutex> guard{that->lock_};
      // creates a dependency that std::exception_ptr must be ConvertibleTo E
      // TODO: break this dependency rather than enforce it with concepts
      that->error_ = std::current_exception();
      for(auto& q : that->pending_) {
        while(!q->heap_.empty()) {
          try {
            auto what{std::move(q->top().what)};
            q->heap_.pop();
            --that->items_;
            guard.unlock();
            ::pushmi::set_error(what, *that->error_);
            guard.lock();
          } catch(...) {
            // we already have an error, ignore this one.
          }
        }
      }
    }
  }

  void insert(std::shared_ptr<time_source_queue_base<E, TP>> queue, time_heap_item<E, TP> item){
    std::unique_lock<std::mutex> guard{this->lock_};

    // deliver error_ and return
    if (!!this->error_) {::pushmi::set_error(item.what, *this->error_); return; }
    // once join() is called, new work queued to the executor is not safe unless it is nested in an existing item.
    if (!!this->joined_) { std::abort(); };

    queue->heap_.push(std::move(item));
    ++this->items_;

    if (!queue->dispatching_ && !queue->pending_) {
      // add queue to pending pending_ list if it is not already there
      this->pending_.push_back(queue);
      queue->pending_ = true;
    }

    if (queue->heap_.top().when < this->earliest_) {
      // this is the earliest, tell worker to reset earliest_
      ++this->dirty_;
      this->wake_.notify_one();
    }
  }

};

//
// the time executor will directly call the executor when the work is due now.
// the time executor will queue the work to the time ordered heap when the work is due in the future.
//

template<class E, class TP, class NF, class Executor>
class time_source_executor {
  using time_point = std::decay_t<TP>;
  std::shared_ptr<time_source_shared<E, time_point>> source_;
  std::shared_ptr<time_source_queue<E, time_point, NF, Executor>> queue_;
public:
  using properties = property_set<is_time<>, is_executor<>, is_maybe_blocking<>, is_fifo_sequence<>, is_single<>>;

  time_source_executor(
    std::shared_ptr<time_source_shared<E, time_point>> source,
    std::shared_ptr<time_source_queue<E, time_point, NF, Executor>> queue) :
    source_(std::move(source)), queue_(std::move(queue)) {}

  auto top() { return queue_->nf_(); }
  auto executor() { return *this; }

  PUSHMI_TEMPLATE(class TPA, class Out)
    (requires Regular<TPA> && ReceiveValue<Out, any_time_executor_ref<E, TP>> && ReceiveError<Out, E>)
  void submit(TPA tp, Out out) {
    // queue for later
    source_->insert(queue_, time_heap_item<E, TP>{tp, any_receiver<E, any_time_executor_ref<E, TP>>{std::move(out)}});
  }
};

//
// the time executor factory produces a new time ordered queue each time that it is called.
//

template<class E, class TP, class NF, class ExecutorFactory>
class time_source_executor_factory_fn {
  using time_point = std::decay_t<TP>;
  std::shared_ptr<time_source_shared<E, time_point>> source_;
  NF nf_;
  ExecutorFactory ef_;
public:
  time_source_executor_factory_fn(
    std::shared_ptr<time_source_shared<E, time_point>> source,
    NF nf,
    ExecutorFactory ef
  ) : source_(std::move(source)), nf_(std::move(nf)), ef_(std::move(ef)) {}
  auto operator()(){
    auto ex = ef_();
    auto queue = std::make_shared<time_source_queue<E, time_point, NF, decltype(ex)>>(
      source_,
      nf_,
      std::move(ex));
    return time_source_executor<E, time_point, NF, decltype(ex)>{source_, queue};
  }
};

//
// each time_source is an independent source of timed events
//
// a time_source is a time_single_executor factory, it is not an executor itself.
//
// each time_source has a single thread that is shared across all the
// time executors it produces. the thread is used to wait for the next time event.
// when a time event is ready the thread will use the executor passed into make()
// to callback on the receiver passed to the time executor submit()
//
// passing an executor to time_source.make() will create a time executor factory.
// the time executor factory is a function that will return a time executor when
// called with no arguments.
//
//
//

template<class E = std::exception_ptr, class TP = std::chrono::system_clock::time_point>
class time_source {
public:
  using time_point = std::decay_t<TP>;
private:
  std::shared_ptr<time_source_shared<E, time_point>> source_;
public:
  time_source() : source_(std::make_shared<time_source_shared<E, time_point>>()) {
    source_->start(source_);
  };

  PUSHMI_TEMPLATE(class NF, class ExecutorFactory)
    (requires Invocable<ExecutorFactory&> && Executor<invoke_result_t<ExecutorFactory&>> && NeverBlocking<invoke_result_t<ExecutorFactory&>>)
  auto make(NF nf, ExecutorFactory ef) {
    return time_source_executor_factory_fn<E, time_point, NF, ExecutorFactory>{source_, std::move(nf), std::move(ef)};
  }

  void join() {
    source_->join(source_);
  }
};
} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "receiver.h"
//#include "executor.h"
//#include "trampoline.h"

namespace pushmi {

template <class E, class... VN>
class any_single_sender {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::tuple<VN...>)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, any_receiver<E, VN...>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, any_receiver<E, VN...>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_single_sender(Wrapped obj, std::false_type) : any_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>(src.pobj_))};
      }
      static void submit(data& src, any_receiver<E, VN...> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_single_sender(Wrapped obj, std::true_type) noexcept
      : any_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>((void*)src.buffer_))};
      }
      static void submit(data& src, any_receiver<E, VN...> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_single_sender>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_single<>>;

  any_single_sender() = default;
  any_single_sender(any_single_sender&& that) noexcept
      : any_single_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }

  PUSHMI_TEMPLATE(class Wrapped)
    (requires SenderTo<wrapped_t<Wrapped>, any_receiver<E, VN...>>)
  explicit any_single_sender(Wrapped obj) noexcept(insitu<Wrapped>())
    : any_single_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~any_single_sender() {
    vptr_->op_(data_, nullptr);
  }
  any_single_sender& operator=(any_single_sender&& that) noexcept {
    this->~any_single_sender();
    new ((void*)this) any_single_sender(std::move(that));
    return *this;
  }
  any_executor<E> executor() {
    return vptr_->executor_(data_);
  }
  void submit(any_receiver<E, VN...> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class E, class... VN>
constexpr typename any_single_sender<E, VN...>::vtable const
  any_single_sender<E, VN...>::noop_;

template <class SF, class EXF>
class single_sender<SF, EXF> {
  SF sf_;
  EXF exf_;

 public:
  using properties = property_set<is_sender<>, is_single<>>;

  constexpr single_sender() = default;
  constexpr explicit single_sender(SF sf)
      : sf_(std::move(sf)) {}
  constexpr single_sender(SF sf, EXF exf)
      : sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(); }
  PUSHMI_TEMPLATE(class Out)
    (requires PUSHMI_EXP(lazy::Receiver<Out> PUSHMI_AND lazy::Invocable<SF&, Out>))
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender<is_single<>>) Data, class DSF, class DEXF>
class single_sender<Data, DSF, DEXF> {
  Data data_;
  DSF sf_;
  DEXF exf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_sender<>, is_single<>>>;

  constexpr single_sender() = default;
  constexpr explicit single_sender(Data data)
      : data_(std::move(data)) {}
  constexpr single_sender(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  constexpr single_sender(Data data, DSF sf, DEXF exf)
      : data_(std::move(data)), sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(data_); }
  PUSHMI_TEMPLATE(class Out)
    (requires PUSHMI_EXP(lazy::Receiver<Out> PUSHMI_AND
        lazy::Invocable<DSF&, Data&, Out>))
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

template <>
class single_sender<>
    : public single_sender<ignoreSF, trampolineEXF> {
public:
  single_sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_single_sender
PUSHMI_INLINE_VAR constexpr struct make_single_sender_fn {
  inline auto operator()() const {
    return single_sender<ignoreSF, trampolineEXF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return single_sender<SF, trampolineEXF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class SF, class EXF)
    (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf) const {
    return single_sender<SF, EXF>{std::move(sf), std::move(exf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires True<> && Sender<Data, is_single<>>)
  auto operator()(Data d) const {
    return single_sender<Data, passDSF, passDEXF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
    (requires Sender<Data, is_single<>>)
  auto operator()(Data d, DSF sf) const {
    return single_sender<Data, DSF, passDEXF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
    (requires Sender<Data, is_single<>> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf) const {
    return single_sender<Data, DSF, DEXF>{std::move(d), std::move(sf), std::move(exf)};
  }
} const make_single_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
single_sender() -> single_sender<ignoreSF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
single_sender(SF) -> single_sender<SF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF, class EXF)
  (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
single_sender(SF, EXF) -> single_sender<SF, EXF>;

PUSHMI_TEMPLATE(class Data)
  (requires True<> && Sender<Data, is_single<>>)
single_sender(Data) -> single_sender<Data, passDSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data, is_single<>>)
single_sender(Data, DSF) -> single_sender<Data, DSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
  (requires Sender<Data, is_single<>> && Invocable<DEXF&, Data&>)
single_sender(Data, DSF, DEXF) -> single_sender<Data, DSF, DEXF>;
#endif

template<>
struct construct_deduced<single_sender> : make_single_sender_fn {};

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "flow_receiver.h"
//#include "executor.h"
//#include "trampoline.h"

namespace pushmi {

template <class PE, class E, class... VN>
class any_flow_single_sender {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::tuple<VN...>)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, any_flow_receiver<PE, std::ptrdiff_t, E, VN...>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, any_flow_receiver<PE, std::ptrdiff_t, E, VN...>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_flow_single_sender(Wrapped obj, std::false_type) : any_flow_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>(src.pobj_))};
      }
      static void submit(data& src, any_flow_receiver<PE, std::ptrdiff_t, E, VN...> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_flow_single_sender(Wrapped obj, std::true_type) noexcept
    : any_flow_single_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>((void*)src.buffer_))};
      }
      static void submit(data& src, any_flow_receiver<PE, std::ptrdiff_t, E, VN...> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_),
            std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_flow_single_sender>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_flow<>, is_single<>>;

  any_flow_single_sender() = default;
  any_flow_single_sender(any_flow_single_sender&& that) noexcept
      : any_flow_single_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires FlowSender<wrapped_t<Wrapped>, is_single<>>)
  explicit any_flow_single_sender(Wrapped obj) noexcept(insitu<Wrapped>())
    : any_flow_single_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~any_flow_single_sender() {
    vptr_->op_(data_, nullptr);
  }
  any_flow_single_sender& operator=(any_flow_single_sender&& that) noexcept {
    this->~any_flow_single_sender();
    new ((void*)this) any_flow_single_sender(std::move(that));
    return *this;
  }
  any_executor<E> executor() {
    return vptr_->executor_(data_);
  }
  void submit(any_flow_receiver<PE, std::ptrdiff_t, E, VN...> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class PE, class E, class... VN>
constexpr typename any_flow_single_sender<PE, E, VN...>::vtable const
    any_flow_single_sender<PE, E, VN...>::noop_;

template <class SF, class EXF>
class flow_single_sender<SF, EXF> {
  SF sf_;
  EXF exf_;

 public:
  using properties = property_set<is_sender<>, is_flow<>, is_single<>>;

  constexpr flow_single_sender() = default;
  constexpr explicit flow_single_sender(SF sf)
      : sf_(std::move(sf)) {}
  constexpr flow_single_sender(SF sf, EXF exf)
      : sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(); }
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out> && Invocable<SF&, Out>)
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender<is_single<>, is_flow<>>) Data, class DSF, class DEXF>
class flow_single_sender<Data, DSF, DEXF> {
  Data data_;
  DSF sf_;
  DEXF exf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_sender<>, is_flow<>, is_single<>>>;

  constexpr flow_single_sender() = default;
  constexpr explicit flow_single_sender(Data data)
      : data_(std::move(data)) {}
  constexpr flow_single_sender(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  constexpr flow_single_sender(Data data, DSF sf, DEXF exf)
      : data_(std::move(data)), sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(data_); }
  PUSHMI_TEMPLATE(class Out)
    (requires PUSHMI_EXP(lazy::Receiver<Out> PUSHMI_AND
        lazy::Invocable<DSF&, Data&, Out>))
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

template <>
class flow_single_sender<>
    : public flow_single_sender<ignoreSF, trampolineEXF> {
public:
  flow_single_sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_flow_single_sender
PUSHMI_INLINE_VAR constexpr struct make_flow_single_sender_fn {
  inline auto operator()() const {
    return flow_single_sender<ignoreSF, trampolineEXF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return flow_single_sender<SF, trampolineEXF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class SF, class EXF)
    (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf) const {
    return flow_single_sender<SF, EXF>{std::move(sf), std::move(exf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires True<> && Sender<Data, is_single<>, is_flow<>>)
  auto operator()(Data d) const {
    return flow_single_sender<Data, passDSF, passDEXF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
    (requires Sender<Data, is_single<>, is_flow<>>)
  auto operator()(Data d, DSF sf) const {
    return flow_single_sender<Data, DSF, passDEXF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
    (requires Sender<Data, is_single<>, is_flow<>> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf) const {
    return flow_single_sender<Data, DSF, DEXF>{std::move(d), std::move(sf), std::move(exf)};
  }
} const make_flow_single_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
flow_single_sender() -> flow_single_sender<ignoreSF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
flow_single_sender(SF) -> flow_single_sender<SF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF, class EXF)
  (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
flow_single_sender(SF, EXF) -> flow_single_sender<SF, EXF>;

PUSHMI_TEMPLATE(class Data)
  (requires True<> && Sender<Data, is_single<>, is_flow<>>)
flow_single_sender(Data) -> flow_single_sender<Data, passDSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data, is_single<>, is_flow<>>)
flow_single_sender(Data, DSF) -> flow_single_sender<Data, DSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
  (requires Sender<Data, is_single<>, is_flow<>> && Invocable<DEXF&, Data&>)
flow_single_sender(Data, DSF, DEXF) -> flow_single_sender<Data, DSF, DEXF>;
#endif

template<>
struct construct_deduced<flow_single_sender>
  : make_flow_single_sender_fn {};


} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "receiver.h"
//#include "executor.h"
//#include "trampoline.h"

namespace pushmi {

template <class E, class... VN>
class any_many_sender {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::tuple<VN...>)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, any_receiver<E, VN...>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, any_receiver<E, VN...>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_many_sender(Wrapped obj, std::false_type) : any_many_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>(src.pobj_))};
      }
      static void submit(data& src, any_receiver<E, VN...> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_many_sender(Wrapped obj, std::true_type) noexcept
      : any_many_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>((void*)src.buffer_))};
      }
      static void submit(data& src, any_receiver<E, VN...> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_many_sender>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_many<>>;

  any_many_sender() = default;
  any_many_sender(any_many_sender&& that) noexcept
      : any_many_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }

  PUSHMI_TEMPLATE(class Wrapped)
    (requires SenderTo<wrapped_t<Wrapped>, any_receiver<E, VN...>, is_many<>>)
  explicit any_many_sender(Wrapped obj) noexcept(insitu<Wrapped>())
    : any_many_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~any_many_sender() {
    vptr_->op_(data_, nullptr);
  }
  any_many_sender& operator=(any_many_sender&& that) noexcept {
    this->~any_many_sender();
    new ((void*)this) any_many_sender(std::move(that));
    return *this;
  }
  any_executor<E> executor() {
    return vptr_->executor_(data_);
  }
  void submit(any_receiver<E, VN...> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class E, class... VN>
constexpr typename any_many_sender<E, VN...>::vtable const
  any_many_sender<E, VN...>::noop_;

template <class SF, class EXF>
class many_sender<SF, EXF> {
  SF sf_;
  EXF exf_;

 public:
  using properties = property_set<is_sender<>, is_many<>>;

  constexpr many_sender() = default;
  constexpr explicit many_sender(SF sf)
      : sf_(std::move(sf)) {}
  constexpr many_sender(SF sf, EXF exf)
      : sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(); }
  PUSHMI_TEMPLATE(class Out)
    (requires PUSHMI_EXP(lazy::Receiver<Out> PUSHMI_AND lazy::Invocable<SF&, Out>))
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender<is_many<>>) Data, class DSF, class DEXF>
class many_sender<Data, DSF, DEXF> {
  Data data_;
  DSF sf_;
  DEXF exf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_sender<>, is_many<>>>;

  constexpr many_sender() = default;
  constexpr explicit many_sender(Data data)
      : data_(std::move(data)) {}
  constexpr many_sender(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  constexpr many_sender(Data data, DSF sf, DEXF exf)
      : data_(std::move(data)), sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(data_); }
  PUSHMI_TEMPLATE(class Out)
    (requires PUSHMI_EXP(lazy::Receiver<Out> PUSHMI_AND
        lazy::Invocable<DSF&, Data&, Out>))
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

template <>
class many_sender<>
    : public many_sender<ignoreSF, trampolineEXF> {
public:
  many_sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_many_sender
PUSHMI_INLINE_VAR constexpr struct make_many_sender_fn {
  inline auto operator()() const {
    return many_sender<ignoreSF, trampolineEXF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return many_sender<SF, trampolineEXF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class SF, class EXF)
    (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf) const {
    return many_sender<SF, EXF>{std::move(sf), std::move(exf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires True<> && Sender<Data, is_many<>>)
  auto operator()(Data d) const {
    return many_sender<Data, passDSF, passDEXF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
    (requires Sender<Data, is_many<>>)
  auto operator()(Data d, DSF sf) const {
    return many_sender<Data, DSF, passDEXF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
    (requires Sender<Data, is_many<>> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf) const {
    return many_sender<Data, DSF, DEXF>{std::move(d), std::move(sf), std::move(exf)};
  }
} const make_many_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
many_sender() -> many_sender<ignoreSF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
many_sender(SF) -> many_sender<SF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF, class EXF)
  (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
many_sender(SF, EXF) -> many_sender<SF, EXF>;

PUSHMI_TEMPLATE(class Data)
  (requires True<> && Sender<Data, is_many<>>)
many_sender(Data) -> many_sender<Data, passDSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data, is_many<>>)
many_sender(Data, DSF) -> many_sender<Data, DSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
  (requires Sender<Data, is_many<>> && Invocable<DEXF&, Data&>)
many_sender(Data, DSF, DEXF) -> many_sender<Data, DSF, DEXF>;
#endif

template<>
struct construct_deduced<many_sender> : make_many_sender_fn {};


} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "flow_receiver.h"
//#include "executor.h"
//#include "trampoline.h"

namespace pushmi {

template <class PE, class PV, class E, class... VN>
class any_flow_many_sender {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::tuple<VN...>)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, any_flow_receiver<PE, PV, E, VN...>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, any_flow_receiver<PE, PV, E, VN...>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  any_flow_many_sender(Wrapped obj, std::false_type) : any_flow_many_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>(src.pobj_))};
      }
      static void submit(data& src, any_flow_receiver<PE, PV, E, VN...> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  any_flow_many_sender(Wrapped obj, std::true_type) noexcept
    : any_flow_many_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_) Wrapped(
              std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>((void*)src.buffer_))};
      }
      static void submit(data& src, any_flow_receiver<PE, PV, E, VN...> out) {
        ::pushmi::submit(
            *static_cast<Wrapped*>((void*)src.buffer_),
            std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, any_flow_many_sender>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_flow<>, is_many<>>;

  any_flow_many_sender() = default;
  any_flow_many_sender(any_flow_many_sender&& that) noexcept
      : any_flow_many_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires FlowSender<wrapped_t<Wrapped>, is_many<>>)
  explicit any_flow_many_sender(Wrapped obj) noexcept(insitu<Wrapped>())
    : any_flow_many_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~any_flow_many_sender() {
    vptr_->op_(data_, nullptr);
  }
  any_flow_many_sender& operator=(any_flow_many_sender&& that) noexcept {
    this->~any_flow_many_sender();
    new ((void*)this) any_flow_many_sender(std::move(that));
    return *this;
  }
  any_executor<E> executor() {
    return vptr_->executor_(data_);
  }
  void submit(any_flow_receiver<PE, PV, E, VN...> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class PE, class PV, class E, class... VN>
constexpr typename any_flow_many_sender<PE, PV, E, VN...>::vtable const
    any_flow_many_sender<PE, PV, E, VN...>::noop_;

template <class SF, class EXF>
class flow_many_sender<SF, EXF> {
  SF sf_;
  EXF exf_;

 public:
  using properties = property_set<is_sender<>, is_flow<>, is_many<>>;

  constexpr flow_many_sender() = default;
  constexpr explicit flow_many_sender(SF sf)
      : sf_(std::move(sf)) {}
  constexpr flow_many_sender(SF sf, EXF exf)
      : sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(); }
  PUSHMI_TEMPLATE(class Out)
    (requires FlowReceiver<Out> && Invocable<SF&, Out>)
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender<is_many<>, is_flow<>>) Data, class DSF, class DEXF>
class flow_many_sender<Data, DSF, DEXF> {
  Data data_;
  DSF sf_;
  DEXF exf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_sender<>, is_flow<>, is_many<>>>;

  constexpr flow_many_sender() = default;
  constexpr explicit flow_many_sender(Data data)
      : data_(std::move(data)) {}
  constexpr flow_many_sender(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  constexpr flow_many_sender(Data data, DSF sf, DEXF exf)
      : data_(std::move(data)), sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(data_); }
  PUSHMI_TEMPLATE(class Out)
    (requires PUSHMI_EXP(lazy::FlowReceiver<Out> PUSHMI_AND
        lazy::Invocable<DSF&, Data&, Out>))
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

template <>
class flow_many_sender<>
    : public flow_many_sender<ignoreSF, trampolineEXF> {
public:
  flow_many_sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_flow_many_sender
PUSHMI_INLINE_VAR constexpr struct make_flow_many_sender_fn {
  inline auto operator()() const {
    return flow_many_sender<ignoreSF, trampolineEXF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return flow_many_sender<SF, trampolineEXF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class SF, class EXF)
    (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf) const {
    return flow_many_sender<SF, EXF>{std::move(sf), std::move(exf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires True<> && Sender<Data, is_many<>, is_flow<>>)
  auto operator()(Data d) const {
    return flow_many_sender<Data, passDSF, passDEXF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
    (requires Sender<Data, is_many<>, is_flow<>>)
  auto operator()(Data d, DSF sf) const {
    return flow_many_sender<Data, DSF, passDEXF>{std::move(d), std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
    (requires Sender<Data, is_many<>, is_flow<>> && Invocable<DEXF&, Data&>)
  auto operator()(Data d, DSF sf, DEXF exf) const {
    return flow_many_sender<Data, DSF, DEXF>{std::move(d), std::move(sf), std::move(exf)};
  }
} const make_flow_many_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
flow_many_sender() -> flow_many_sender<ignoreSF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
flow_many_sender(SF) -> flow_many_sender<SF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF, class EXF)
  (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
flow_many_sender(SF, EXF) -> flow_many_sender<SF, EXF>;

PUSHMI_TEMPLATE(class Data)
  (requires True<> && Sender<Data, is_many<>, is_flow<>>)
flow_many_sender(Data) -> flow_many_sender<Data, passDSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data, is_many<>, is_flow<>>)
flow_many_sender(Data, DSF) -> flow_many_sender<Data, DSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
  (requires Sender<Data, is_many<>, is_flow<>> && Invocable<DEXF&, Data&>)
flow_many_sender(Data, DSF, DEXF) -> flow_many_sender<Data, DSF, DEXF>;
#endif

template<>
struct construct_deduced<flow_many_sender>
  : make_flow_many_sender_fn {};

// // TODO constrain me
// template <class V, class E = std::exception_ptr, Sender Wrapped>
// auto erase_cast(Wrapped w) {
//   return flow_many_sender<V, E>{std::move(w)};
// }

} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <tuple>
//#include "../piping.h"
//#include "../boosters.h"
//#include "../receiver.h"
//#include "../flow_receiver.h"
//#include "../single_sender.h"
//#include "../many_sender.h"
//#include "../time_single_sender.h"
//#include "../flow_single_sender.h"
//#include "../flow_many_sender.h"
//#include "../detail/if_constexpr.h"
//#include "../detail/functional.h"

namespace pushmi {

#if __cpp_lib_apply	>= 201603
using std::apply;
#else
namespace detail {
  PUSHMI_TEMPLATE (class F, class Tuple, std::size_t... Is)
    (requires requires (
      pushmi::invoke(std::declval<F>(), std::get<Is>(std::declval<Tuple>())...)
    ))
  constexpr decltype(auto) apply_impl(F&& f, Tuple&& t, std::index_sequence<Is...>) {
    return pushmi::invoke((F&&) f, std::get<Is>((Tuple&&) t)...);
  }
  template <class Tuple_, class Tuple = std::remove_reference_t<Tuple_>>
  using tupidxs = std::make_index_sequence<std::tuple_size<Tuple>::value>;
} // namespace detail

PUSHMI_TEMPLATE (class F, class Tuple)
  (requires requires (
    detail::apply_impl(std::declval<F>(), std::declval<Tuple>(), detail::tupidxs<Tuple>{})
  ))
constexpr decltype(auto) apply(F&& f, Tuple&& t) {
  return detail::apply_impl((F&&) f, (Tuple&&) t, detail::tupidxs<Tuple>{});
}
#endif

namespace detail {

template <class Cardinality, bool IsFlow = false>
struct make_receiver;
template <>
struct make_receiver<is_single<>> : construct_deduced<receiver> {};
template <>
struct make_receiver<is_many<>> : construct_deduced<receiver> {};
template <>
struct make_receiver<is_single<>, true> : construct_deduced<flow_receiver> {};
template <>
struct make_receiver<is_many<>, true> : construct_deduced<flow_receiver> {};

template <class Cardinality, bool IsFlow>
struct receiver_from_impl {
  using MakeReceiver = make_receiver<Cardinality, IsFlow>;
  PUSHMI_TEMPLATE (class... Ts)
   (requires Invocable<MakeReceiver, Ts...>)
  auto operator()(std::tuple<Ts...> args) const {
    return pushmi::apply(MakeReceiver(), std::move(args));
  }
  PUSHMI_TEMPLATE (class... Ts, class... Fns,
    class This = std::enable_if_t<sizeof...(Fns) != 0, receiver_from_impl>)
    (requires And<SemiMovable<Fns>...> &&
      Invocable<MakeReceiver, Ts...> &&
      Invocable<This, pushmi::invoke_result_t<MakeReceiver, Ts...>, Fns...>)
  auto operator()(std::tuple<Ts...> args, Fns...fns) const {
    return This()(This()(std::move(args)), std::move(fns)...);
  }
  PUSHMI_TEMPLATE(class Out, class...Fns)
    (requires Receiver<Out> && And<SemiMovable<Fns>...>)
  auto operator()(Out out, Fns... fns) const {
    return MakeReceiver()(std::move(out), std::move(fns)...);
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender) In>
using receiver_from_fn =
    receiver_from_impl<
        property_set_index_t<properties_t<In>, is_single<>>,
        property_query_v<properties_t<In>, is_flow<>>>;

template <class In, class FN>
struct submit_transform_out_1 {
  FN fn_;
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out> && Invocable<FN, Out> && SenderTo<In, pushmi::invoke_result_t<const FN&, Out>>)
  void operator()(In& in, Out out) const {
    ::pushmi::submit(in, fn_(std::move(out)));
  }
};
template <class In, class FN>
struct submit_transform_out_2 {
  FN fn_;
  PUSHMI_TEMPLATE(class CV, class Out)
    (requires Receiver<Out> && Invocable<FN, Out> && ConstrainedSenderTo<In, pushmi::invoke_result_t<const FN&, Out>>)
  void operator()(In& in, CV cv, Out out) const {
    ::pushmi::submit(in, cv, fn_(std::move(out)));
  }
};
template <class In, class SDSF>
struct submit_transform_out_3 {
  SDSF sdsf_;
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out> && Invocable<const SDSF&, In&, Out>)
  void operator()(In& in, Out out) const {
    sdsf_(in, std::move(out));
  }
};
template <class In, class TSDSF>
struct submit_transform_out_4 {
  TSDSF tsdsf_;
  PUSHMI_TEMPLATE(class CV, class Out)
    (requires Receiver<Out> && Invocable<const TSDSF&, In&, CV, Out>)
  void operator()(In& in, CV cv, Out out) const {
    tsdsf_(in, cv, std::move(out));
  }
};

PUSHMI_TEMPLATE(class In, class FN)
  (requires Sender<In> && SemiMovable<FN>
    PUSHMI_BROKEN_SUBSUMPTION(&& not ConstrainedSender<In>))
auto submit_transform_out(FN fn) {
  return on_submit(submit_transform_out_1<In, FN>{std::move(fn)});
}

PUSHMI_TEMPLATE(class In, class FN)
  (requires ConstrainedSender<In> && SemiMovable<FN>)
auto submit_transform_out(FN fn){
  return submit_transform_out_2<In, FN>{std::move(fn)};
}

PUSHMI_TEMPLATE(class In, class SDSF, class TSDSF)
  (requires Sender<In> && SemiMovable<SDSF> && SemiMovable<TSDSF>
    PUSHMI_BROKEN_SUBSUMPTION(&& not ConstrainedSender<In>))
auto submit_transform_out(SDSF sdsf, TSDSF) {
  return submit_transform_out_3<In, SDSF>{std::move(sdsf)};
}

PUSHMI_TEMPLATE(class In, class SDSF, class TSDSF)
  (requires ConstrainedSender<In> && SemiMovable<SDSF> && SemiMovable<TSDSF>)
auto submit_transform_out(SDSF, TSDSF tsdsf) {
  return submit_transform_out_4<In, TSDSF>{std::move(tsdsf)};
}

template <class Cardinality, bool IsConstrained = false, bool IsTime = false, bool IsFlow = false>
struct make_sender;
template <>
struct make_sender<is_single<>> : construct_deduced<single_sender> {};
template <>
struct make_sender<is_many<>> : construct_deduced<many_sender> {};
template <>
struct make_sender<is_single<>, false, false, true> : construct_deduced<flow_single_sender> {};
template <>
struct make_sender<is_many<>, false, false, true> : construct_deduced<flow_many_sender> {};
template <>
struct make_sender<is_single<>, true, true, false> : construct_deduced<time_single_sender> {};
template <>
struct make_sender<is_single<>, true, false, false> : construct_deduced<constrained_single_sender> {};

PUSHMI_INLINE_VAR constexpr struct sender_from_fn {
  PUSHMI_TEMPLATE(class In, class... FN)
    (requires Sender<In>)
  auto operator()(In in, FN&&... fn) const {
    using MakeSender =
        make_sender<
            property_set_index_t<properties_t<In>, is_single<>>,
            property_query_v<properties_t<In>, is_constrained<>>,
            property_query_v<properties_t<In>, is_time<>>,
            property_query_v<properties_t<In>, is_flow<>>>;
    return MakeSender{}(std::move(in), (FN&&) fn...);
  }
} const sender_from {};

PUSHMI_TEMPLATE(
    class In,
    class Out,
    bool SenderRequires,
    bool SingleSenderRequires,
    bool TimeSingleSenderRequires)
  (requires Sender<In> && Receiver<Out>)
constexpr bool sender_requires_from() {
  PUSHMI_IF_CONSTEXPR_RETURN( ((bool) TimeSenderTo<In, Out>) (
    return TimeSingleSenderRequires;
  ) else (
    PUSHMI_IF_CONSTEXPR_RETURN( ((bool) SenderTo<In, Out>) (
      return SingleSenderRequires;
    ) else (
      PUSHMI_IF_CONSTEXPR_RETURN( ((bool) SenderTo<In, Out>) (
        return SenderRequires;
      ) else (
      ))
    ))
  ))
}

struct set_value_fn {
private:
  template <class... VN>
  struct impl {
    std::tuple<VN...> vn_;
    PUSHMI_TEMPLATE(class Out)
      (requires ReceiveValue<Out, VN...>)
    void operator()(Out out) {
      ::pushmi::apply(::pushmi::set_value, std::tuple_cat(std::tuple<Out>{std::move(out)}, std::move(vn_)));
    }
  };
public:
  template <class... VN>
  auto operator()(VN&&... vn) const {
    return impl<std::decay_t<VN>...>{(VN&&) vn...};
  }
};

struct set_error_fn {
private:
  template <class E>
  struct impl {
    E e_;
    PUSHMI_TEMPLATE(class Out)
      (requires ReceiveError<Out, E>)
    void operator()(Out out) {
      ::pushmi::set_error(out, std::move(e_));
    }
  };
public:
  PUSHMI_TEMPLATE(class E)
    (requires SemiMovable<E>)
  auto operator()(E e) const {
    return impl<E>{std::move(e)};
  }
};

struct set_done_fn {
private:
  struct impl {
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void operator()(Out out) {
      ::pushmi::set_done(out);
    }
  };
public:
  auto operator()() const {
    return impl{};
  }
};

struct set_starting_fn {
private:
  template <class Up>
  struct impl {
    Up up_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void operator()(Out out) {
      ::pushmi::set_starting(out, std::move(up_));
    }
  };
public:
  PUSHMI_TEMPLATE(class Up)
    (requires Receiver<Up>)
  auto operator()(Up up) const {
    return impl<Up>{std::move(up)};
  }
};

struct executor_fn {
private:
  struct impl {
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In& in) const {
      return ::pushmi::executor(in);
    }
  };
public:
  auto operator()() const {
    return impl{};
  }
};

struct do_submit_fn {
private:
  template <class Out>
  struct impl {
    Out out_;
    PUSHMI_TEMPLATE (class In)
      (requires SenderTo<In, Out>)
    void operator()(In& in) {
      ::pushmi::submit(in, std::move(out_));
    }
  };
  template <class TP, class Out>
  struct time_impl {
    TP tp_;
    Out out_;
    PUSHMI_TEMPLATE (class In)
      (requires TimeSenderTo<In, Out>)
    void operator()(In& in) {
      ::pushmi::submit(in, std::move(tp_), std::move(out_));
    }
  };
public:
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>)
  auto operator()(Out out) const {
    return impl<Out>{std::move(out)};
  }
  PUSHMI_TEMPLATE(class TP, class Out)
    (requires Receiver<Out>)
  auto operator()(TP tp, Out out) const {
    return time_impl<TP, Out>{std::move(tp), std::move(out)};
  }
};

struct top_fn {
private:
  struct impl {
    PUSHMI_TEMPLATE (class In)
      (requires ConstrainedSender<In>)
    auto operator()(In& in) const {
      return ::pushmi::top(in);
    }
  };
public:
  auto operator()() const {
    return impl{};
  }
};

struct now_fn {
private:
  struct impl {
    PUSHMI_TEMPLATE (class In)
      (requires TimeSender<In>)
    auto operator()(In& in) const {
      return ::pushmi::now(in);
    }
  };
public:
  auto operator()() const {
    return impl{};
  }
};

} // namespace detail

namespace extension_operators {

PUSHMI_INLINE_VAR constexpr detail::set_done_fn set_done{};
PUSHMI_INLINE_VAR constexpr detail::set_error_fn set_error{};
PUSHMI_INLINE_VAR constexpr detail::set_value_fn set_value{};
PUSHMI_INLINE_VAR constexpr detail::set_starting_fn set_starting{};
PUSHMI_INLINE_VAR constexpr detail::executor_fn executor{};
PUSHMI_INLINE_VAR constexpr detail::do_submit_fn submit{};
PUSHMI_INLINE_VAR constexpr detail::now_fn now{};
PUSHMI_INLINE_VAR constexpr detail::top_fn top{};

} // namespace extension_operators

} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <functional>
//#include "../time_single_sender.h"
//#include "../boosters.h"
//#include "extension_operators.h"
//#include "../trampoline.h"
//#include "../detail/opt.h"
//#include "../detail/if_constexpr.h"

namespace pushmi {
namespace detail {
namespace submit_detail {
template <PUSHMI_TYPE_CONSTRAINT(Sender) In, class ...AN>
using receiver_type_t =
    pushmi::invoke_result_t<
        pushmi::detail::make_receiver<
          property_set_index_t<properties_t<In>, is_single<>>,
          property_query_v<properties_t<In>, is_flow<>>>,
        AN...>;

PUSHMI_CONCEPT_DEF(
  template (class In, class ... AN)
  (concept AutoSenderTo)(In, AN...),
    Sender<In> && SenderTo<In, receiver_type_t<In, AN...>>
);
PUSHMI_CONCEPT_DEF(
  template (class In, class ... AN)
  (concept AutoConstrainedSenderTo)(In, AN...),
    ConstrainedSenderTo<In, receiver_type_t<In, AN...>>
);
PUSHMI_CONCEPT_DEF(
  template (class In, class ... AN)
  (concept AutoTimeSenderTo)(In, AN...),
    TimeSenderTo<In, receiver_type_t<In, AN...>>
);
} // namespace submit_detail

struct submit_fn {
private:
  // TODO - only move, move-only types..
  // if out can be copied, then submit can be called multiple
  // times..
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
      (requires
        submit_detail::AutoSenderTo<In, AN...> &&
        Invocable<::pushmi::detail::receiver_from_fn<In>&, std::tuple<AN...>>
      )
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
      ::pushmi::submit(in, std::move(out));
      return in;
    }
  };
public:
  template <class... AN>
  auto operator()(AN&&... an) const {
    return submit_fn::fn<AN...>{{(AN&&) an...}};
  }
};

struct submit_at_fn {
private:
  template <class TP, class...AN>
  struct fn {
    TP at_;
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoTimeSenderTo<In, AN...>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
      ::pushmi::submit(in, std::move(at_), std::move(out));
      return in;
    }
  };
public:
  PUSHMI_TEMPLATE(class TP, class...AN)
    (requires Regular<TP>)
  auto operator()(TP at, AN... an) const {
    return submit_at_fn::fn<TP, AN...>{std::move(at), {(AN&&) an...}};
  }
};

struct submit_after_fn {
private:
  template <class D, class... AN>
  struct fn {
    D after_;
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoTimeSenderTo<In, AN...>)
    In operator()(In in) {
      // TODO - only move, move-only types..
      // if out can be copied, then submit can be called multiple
      // times..
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
      auto at = ::pushmi::now(in) + std::move(after_);
      ::pushmi::submit(in, std::move(at), std::move(out));
      return in;
    }
  };
public:
  PUSHMI_TEMPLATE(class D, class...AN)
    (requires Regular<D>)
  auto operator()(D after, AN... an) const {
    return submit_after_fn::fn<D, AN...>{std::move(after), {(AN&&) an...}};
  }
};

struct blocking_submit_fn {
private:
  struct lock_state {
    bool done{false};
    std::atomic<int> nested{0};
    std::mutex lock;
    std::condition_variable signaled;
  };
  template<class Out>
  struct nested_receiver_impl;
  PUSHMI_TEMPLATE (class Exec)
    (requires Sender<Exec> && Executor<Exec> )
  struct nested_executor_impl {
    nested_executor_impl(lock_state* state, Exec ex) :
      state_(state),
      ex_(std::move(ex)) {}
    lock_state* state_;
    Exec ex_;

    template<class U>
    using test_for_this = nested_executor_impl<U>;

    PUSHMI_TEMPLATE (class Ex)
      (requires Sender<Ex> && Executor<Ex> && detail::is_v<Ex, test_for_this>)
    static auto make(lock_state*, Ex ex) {
      return ex;
    }
    PUSHMI_TEMPLATE (class Ex)
      (requires Sender<Ex> && Executor<Ex> && not detail::is_v<Ex, test_for_this>)
    static auto make(lock_state* state, Ex ex) {
      return nested_executor_impl<Ex>{state, ex};
    }

    using properties = properties_t<Exec>;

    auto executor() {
      return make(state_, ::pushmi::executor(ex_));
    }

    PUSHMI_TEMPLATE (class... ZN)
      (requires Constrained<Exec>)
    auto top() { return ::pushmi::top(ex_); }

    PUSHMI_TEMPLATE (class CV, class Out)
      (requires Receiver<Out> && Constrained<Exec>)
    void submit(CV cv, Out out) {
      ++state_->nested;
      ::pushmi::submit(ex_, cv, nested_receiver_impl<Out>{state_, std::move(out)});
    }

    PUSHMI_TEMPLATE (class Out)
      (requires Receiver<Out> && not Constrained<Exec>)
    void submit(Out out) {
      ++state_->nested;
      ::pushmi::submit(ex_, nested_receiver_impl<Out>{state_, std::move(out)});
    }
  };
  template<class Out>
  struct nested_receiver_impl {
    nested_receiver_impl(lock_state* state, Out out) :
      state_(state),
      out_(std::move(out)) {}
    lock_state* state_;
    Out out_;

    using properties = properties_t<Out>;

    template<class V>
    void value(V&& v) {
      std::exception_ptr e;
      using executor_t = remove_cvref_t<V>;
      auto n = nested_executor_impl<executor_t>::make(state_, (V&&) v);
      ::pushmi::set_value(out_, any_executor_ref<>{n});
    }
    template<class E>
    void error(E&& e) noexcept {
      ::pushmi::set_error(out_, (E&&) e);
      if(--state_->nested == 0) {
        state_->signaled.notify_all();
      }
    }
    void done() {
      std::exception_ptr e;
      try{
        ::pushmi::set_done(out_);
      }
      catch(...) {e = std::current_exception();}
      if(--state_->nested == 0) {
        state_->signaled.notify_all();
      }
      if (e) {std::rethrow_exception(e);}
    }
  };
  struct nested_executor_impl_fn {
    PUSHMI_TEMPLATE (class Exec)
      (requires Executor<Exec>)
    auto operator()(lock_state* state, Exec ex) const {
      return nested_executor_impl<Exec>::make(state, std::move(ex));
    }
  };
  struct on_value_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE (class Out, class Value)
      (requires Executor<std::decay_t<Value>> &&
        ReceiveValue<Out,
          pushmi::invoke_result_t<nested_executor_impl_fn, lock_state*, std::decay_t<Value>>>)
    void operator()(Out out, Value&& v) const {
      ++state_->nested;
      ::pushmi::set_value(out, nested_executor_impl_fn{}(state_, (Value&&) v));
      if (--state_->nested == 0){
        std::unique_lock<std::mutex> guard{state_->lock};
        state_->signaled.notify_all();
      }
    }
    PUSHMI_TEMPLATE (class Out, class... VN)
      (requires True<> && ReceiveValue<Out, VN...> &&
        not (sizeof...(VN) == 1 && And<Executor<std::decay_t<VN>>...>) )
    void operator()(Out out, VN&&... vn) const {
      ::pushmi::set_value(out, (VN&&) vn...);
    }
  };
  struct on_error_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE(class Out, class E)
      (requires ReceiveError<Out, E>)
    void operator()(Out out, E e) const noexcept {
      ::pushmi::set_error(out, std::move(e));
      std::unique_lock<std::mutex> guard{state_->lock};
      state_->done = true;
      state_->signaled.notify_all();
    }
  };
  struct on_done_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void operator()(Out out) const {
      ::pushmi::set_done(out);
      std::unique_lock<std::mutex> guard{state_->lock};
      state_->done = true;
      state_->signaled.notify_all();
    }
  };

  template <class In>
  struct receiver_impl {
    PUSHMI_TEMPLATE(class... AN)
      (requires Sender<In>)
    auto operator()(lock_state* state, std::tuple<AN...> args) const {
      return ::pushmi::detail::receiver_from_fn<In>()(
        std::move(args),
        on_value_impl{state},
        on_error_impl{state},
        on_done_impl{state}
      );
    }
  };
  template <class In>
  struct submit_impl {
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out> && SenderTo<In, Out>)
    void operator()(In& in, Out out) const {
      ::pushmi::submit(in, std::move(out));
    }
  };
  // TODO - only move, move-only types..
  // if out can be copied, then submit can be called multiple
  // times..
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;

    PUSHMI_TEMPLATE(class In)
      (requires Sender<In> &&
        Invocable<submit_impl<In>&, In&, pushmi::invoke_result_t<receiver_impl<In>, lock_state*, std::tuple<AN...>&&>> &&
        not AlwaysBlocking<In>)
    In operator()(In in) {
      lock_state state{};

      auto make = receiver_impl<In>{};
      auto submit = submit_impl<In>{};
      submit(in, make(&state, std::move(args_)));

      std::unique_lock<std::mutex> guard{state.lock};
      state.signaled.wait(guard, [&]{
        return state.done && state.nested.load() == 0;
      });
      return in;
    }
  };
public:
  template <class... AN>
  auto operator()(AN... an) const {
    return blocking_submit_fn::fn<AN...>{{(AN&&) an...}};
  }
};

template <class T>
struct get_fn {
private:
  struct on_value_impl {
    pushmi::detail::opt<T>* result_;
    template<class... TN>
    void operator()(TN&&... tn) const { *result_ = T{(TN&&) tn...}; }
  };
  struct on_error_impl {
    pushmi::detail::opt<std::exception_ptr>* ep_;
    template <class E>
    void operator()(E e) const noexcept { *ep_ = std::make_exception_ptr(e); }
    void operator()(std::exception_ptr ep) const noexcept { *ep_ = ep; }
  };
public:
  // TODO constrain this better
  PUSHMI_TEMPLATE (class In)
    (requires Sender<In>)
  T operator()(In in) const {
    pushmi::detail::opt<T> result_;
    pushmi::detail::opt<std::exception_ptr> ep_;
    auto out = ::pushmi::make_receiver(
      on_value_impl{&result_},
      on_error_impl{&ep_}
    );
    using Out = decltype(out);
    static_assert(SenderTo<In, Out>,
        "'In' does not deliver value compatible with 'T' to 'Out'");
    std::conditional_t<AlwaysBlocking<In>, submit_fn, blocking_submit_fn>{}(std::move(out))(std::move(in));
    if (!!ep_) { std::rethrow_exception(*ep_); }
    return std::move(*result_);
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::submit_fn submit{};
PUSHMI_INLINE_VAR constexpr detail::submit_at_fn submit_at{};
PUSHMI_INLINE_VAR constexpr detail::submit_after_fn submit_after{};
PUSHMI_INLINE_VAR constexpr detail::blocking_submit_fn blocking_submit{};
template <class T>
PUSHMI_INLINE_VAR constexpr detail::get_fn<T> get{};
} // namespace operators

} // namespace pushmi
//#pragma once

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <vector>

//#include "time_single_sender.h"
//#include "trampoline.h"

namespace pushmi {

template<class... TN>
struct subject;

template<class PS, class... TN>
struct subject<PS, TN...> {

  using properties = property_set_insert_t<property_set<is_sender<>, is_single<>>, property_set<property_set_index_t<PS, is_single<>>>>;

  struct subject_shared {
    using receiver_t = any_receiver<std::exception_ptr, TN...>;
    bool done_ = false;
    pushmi::detail::opt<std::tuple<std::decay_t<TN>...>> t_;
    std::exception_ptr ep_;
    std::vector<receiver_t> receivers_;
    std::mutex lock_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void submit(Out out) {
      std::unique_lock<std::mutex> guard(lock_);
      if (ep_) {::pushmi::set_error(out, ep_); return;}
      if (!!t_) {auto args = *t_; ::pushmi::apply(::pushmi::set_value, std::tuple_cat(std::tuple<Out>{std::move(out)}, std::move(args))); return;}
      if (done_) {::pushmi::set_done(out); return;}
      receivers_.push_back(receiver_t{out});
    }
    PUSHMI_TEMPLATE(class... VN)
      (requires And<SemiMovable<VN>...>)
    void value(VN&&... vn) {
      std::unique_lock<std::mutex> guard(lock_);
      for (auto& out : receivers_) {::pushmi::apply(::pushmi::set_value, std::tuple<decltype(out), std::decay_t<TN>...>{out, detail::as_const(vn)...});}
      t_ = std::make_tuple((VN&&) vn...);
      receivers_.clear();
    }
    PUSHMI_TEMPLATE(class E)
      (requires SemiMovable<E>)
    void error(E e) noexcept {
      std::unique_lock<std::mutex> guard(lock_);
      ep_ = e;
      for (auto& out : receivers_) {::pushmi::set_error(out, std::move(e));}
      receivers_.clear();
    }
    void done() {
      std::unique_lock<std::mutex> guard(lock_);
      done_ = true;
      for (auto& out : receivers_) {::pushmi::set_done(out);}
      receivers_.clear();
    }
  };

  struct subject_receiver {

    using properties = property_set<is_receiver<>>;

    std::shared_ptr<subject_shared> s;

    PUSHMI_TEMPLATE(class... VN)
      (requires And<SemiMovable<VN>...>)
    void value(VN&&... vn) {
      s->value((VN&&) vn...);
    }
    PUSHMI_TEMPLATE(class E)
      (requires SemiMovable<E>)
    void error(E e) noexcept {
      s->error(std::move(e));
    }
    void done() {
      s->done();
    }
  };

  std::shared_ptr<subject_shared> s = std::make_shared<subject_shared>();

  auto executor() { return trampoline(); }
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>)
  void submit(Out out) {
    s->submit(std::move(out));
  }

  auto receiver() {
    return detail::receiver_from_fn<subject>{}(subject_receiver{s});
  }
};

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "extension_operators.h"
//#include "submit.h"

namespace pushmi {
namespace detail {

struct for_each_fn {
private:
  template<class... PN>
  struct subset { using properties = property_set<PN...>; };
  template<class In, class Out>
  struct Pull : Out {
    explicit Pull(Out out) : Out(std::move(out)) {}
    using properties = property_set_insert_t<properties_t<Out>, property_set<is_flow<>>>;
    std::function<void(ptrdiff_t)> pull;
    template<class V>
    void value(V&& v) {
      ::pushmi::set_value(static_cast<Out&>(*this), (V&&) v);
      pull(1);
    }
    PUSHMI_TEMPLATE(class Up)
      (requires Receiver<Up>)
    void starting(Up up){
      pull = [up = std::move(up)](std::ptrdiff_t requested) mutable {
        ::pushmi::set_value(up, requested);
      };
      pull(1);
    }
    PUSHMI_TEMPLATE(class Up)
      (requires ReceiveValue<Up>)
    void starting(Up up){}
  };
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In> && Flow<In> && Many<In>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<subset<is_sender<>, property_set_index_t<properties_t<In>, is_single<>>>>()(std::move(args_))};
      using Out = decltype(out);
      ::pushmi::submit(in, ::pushmi::detail::receiver_from_fn<In>()(Pull<In, Out>{std::move(out)}));
      return in;
    }
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In> && Constrained<In> && Flow<In> && Many<In>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<subset<is_sender<>, property_set_index_t<properties_t<In>, is_single<>>>>()(std::move(args_))};
      using Out = decltype(out);
      ::pushmi::submit(in, ::pushmi::top(in), ::pushmi::detail::receiver_from_fn<In>()(Pull<In, Out>{std::move(out)}));
      return in;
    }
  };
public:
  template <class... AN>
  auto operator()(AN&&... an) const {
    return for_each_fn::fn<AN...>{{(AN&&) an...}};
  }
};

} //namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::for_each_fn for_each{};
} //namespace operartors

} //namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../detail/functional.h"
//#include "submit.h"
//#include "extension_operators.h"

namespace pushmi {
namespace detail {
  struct single_empty_sender_base : single_sender<ignoreSF, inlineEXF> {
    using properties = property_set<is_sender<>, is_single<>, is_always_blocking<>, is_fifo_sequence<>>;
  };
  template <class... VN>
  struct single_empty_impl {
    PUSHMI_TEMPLATE(class Out)
      (requires ReceiveValue<Out, VN...>)
    void operator()(single_empty_sender_base&, Out out) {
      ::pushmi::set_done(out);
    }
  };
}

namespace operators {
template <class... VN>
auto empty() {
  return make_single_sender(detail::single_empty_sender_base{}, detail::single_empty_impl<VN...>{});
}

inline auto empty() {
  return make_single_sender(detail::single_empty_sender_base{}, detail::single_empty_impl<>{});
}

} // namespace operators
} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../many_sender.h"
//#include "../flow_many_sender.h"
//#include "../trampoline.h"
//#include "extension_operators.h"
//#include "submit.h"

namespace pushmi {

PUSHMI_CONCEPT_DEF(
  template (class R)
  concept Range,
    requires (R&& r) (
      implicitly_convertible_to<bool>(std::begin(r) == std::end(r))
    )
);

namespace operators {

PUSHMI_INLINE_VAR constexpr struct from_fn {
private:
  struct sender_base : many_sender<ignoreSF, inlineEXF> {
    using properties = property_set<is_sender<>, is_many<>, is_always_blocking<>, is_fifo_sequence<>>;
  };
  template <class I, class S>
  struct out_impl {
    I begin_;
    S end_;
    PUSHMI_TEMPLATE(class Out)
      (requires ReceiveValue<Out, typename std::iterator_traits<I>::value_type>)
    void operator()(sender_base&, Out out) const {
      auto c = begin_;
      for (; c != end_; ++c) {
        ::pushmi::set_value(out, *c);
      }
      ::pushmi::set_done(out);
    }
  };
public:
  PUSHMI_TEMPLATE(class I, class S)
    (requires
      DerivedFrom<
          typename std::iterator_traits<I>::iterator_category,
          std::forward_iterator_tag>)
  auto operator()(I begin, S end) const {
    return make_many_sender(sender_base{}, out_impl<I, S>{begin, end});
  }

  PUSHMI_TEMPLATE(class R)
    (requires Range<R>)
  auto operator()(R&& range) const {
    return (*this)(std::begin(range), std::end(range));
  }
} from {};

template <class I, class S, class Out, class Exec>
struct flow_from_producer {
  flow_from_producer(I begin, S end, Out out, Exec exec, bool s) :
    c(begin),
    end(end),
    out(std::move(out)),
    exec(std::move(exec)),
    stop(s) {}
  I c;
  S end;
  Out out;
  Exec exec;
  std::atomic<bool> stop;
};

template<class Producer>
struct flow_from_up {
  using properties = properties_t<receiver<>>;

  explicit flow_from_up(std::shared_ptr<Producer> p) : p(std::move(p)) {}
  std::shared_ptr<Producer> p;

  void value(ptrdiff_t requested) {
    if (requested < 1) {return;}
    // submit work to exec
    ::pushmi::submit(p->exec,
      make_receiver([p = p, requested](auto) {
        auto remaining = requested;
        // this loop is structured to work when there is re-entrancy
        // out.value in the loop may call up.value. to handle this the
        // state of p->c must be captured and the remaining and p->c
        // must be changed before out.value is called.
        while (remaining-- > 0 && !p->stop && p->c != p->end) {
          auto i = (p->c)++;
          ::pushmi::set_value(p->out, ::pushmi::detail::as_const(*i));
        }
        if (p->c == p->end) {
          ::pushmi::set_done(p->out);
        }
      }));
  }

  template<class E>
  void error(E e) noexcept {
    p->stop.store(true);
    ::pushmi::submit(p->exec,
      make_receiver([p = p](auto) {
        ::pushmi::set_done(p->out);
      }));
  }

  void done() {
    p->stop.store(true);
    ::pushmi::submit(p->exec,
      make_receiver([p = p](auto) {
        ::pushmi::set_done(p->out);
      }));
  }
};

PUSHMI_INLINE_VAR constexpr struct flow_from_fn {
private:
  template <class I, class S, class Exec>
  struct out_impl {
    I begin_;
    S end_;
    mutable Exec exec_;
    PUSHMI_TEMPLATE(class Out)
      (requires ReceiveValue<Out, typename std::iterator_traits<I>::value_type>)
    void operator()(Out out) const {
      using Producer = flow_from_producer<I, S, Out, Exec>;
      auto p = std::make_shared<Producer>(begin_, end_, std::move(out), exec_, false);

      ::pushmi::submit(exec_,
        make_receiver([p](auto exec) {
          // pass reference for cancellation.
          ::pushmi::set_starting(p->out, make_receiver(flow_from_up<Producer>{p}));
        }));
    }
  };
public:
  PUSHMI_TEMPLATE(class I, class S)
    (requires
      DerivedFrom<
          typename std::iterator_traits<I>::iterator_category,
          std::forward_iterator_tag>)
  auto operator()(I begin, S end) const {
    return (*this)(begin, end, trampoline());
  }

  PUSHMI_TEMPLATE(class R)
    (requires Range<R>)
  auto operator()(R&& range) const {
    return (*this)(std::begin(range), std::end(range), trampoline());
  }

  PUSHMI_TEMPLATE(class I, class S, class Exec)
    (requires
      DerivedFrom<
          typename std::iterator_traits<I>::iterator_category,
          std::forward_iterator_tag> &&
      Sender<Exec, is_single<>, is_executor<>>)
  auto operator()(I begin, S end, Exec exec) const {
    return make_flow_many_sender(out_impl<I, S, Exec>{begin, end, exec});
  }

  PUSHMI_TEMPLATE(class R, class Exec)
    (requires Range<R> && Sender<Exec, is_single<>, is_executor<>>)
  auto operator()(R&& range, Exec exec) const {
    return (*this)(std::begin(range), std::end(range), exec);
  }
} flow_from {};

} // namespace operators

} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../single_sender.h"
//#include "submit.h"
//#include "extension_operators.h"

namespace pushmi {

namespace operators {

PUSHMI_INLINE_VAR constexpr struct just_fn {
private:
  struct sender_base : single_sender<ignoreSF, inlineEXF> {
    using properties = property_set<is_sender<>, is_single<>, is_always_blocking<>, is_fifo_sequence<>>;
  };
  template <class... VN>
  struct impl {
    std::tuple<VN...> vn_;
    PUSHMI_TEMPLATE (class Out)
      (requires ReceiveValue<Out, VN...>)
    void operator()(sender_base&, Out out) {
      ::pushmi::apply(::pushmi::set_value, std::tuple_cat(std::tuple<Out&>{out}, std::move(vn_)));
      ::pushmi::set_done(std::move(out));
    }
  };
public:
  PUSHMI_TEMPLATE(class... VN)
    (requires And<SemiMovable<VN>...>)
  auto operator()(VN... vn) const {
    return make_single_sender(sender_base{}, impl<VN...>{std::tuple<VN...>{std::move(vn)...}});
  }
} just {};
} // namespace operators

} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "submit.h"
//#include "extension_operators.h"

namespace pushmi {
namespace detail {
  struct single_error_sender_base : single_sender<ignoreSF, inlineEXF> {
    using properties = property_set<is_sender<>, is_single<>, is_always_blocking<>, is_fifo_sequence<>>;
  };
  template <class E, class... VN>
  struct single_error_impl {
    E e_;
    PUSHMI_TEMPLATE(class Out)
      (requires ReceiveError<Out, E> && ReceiveValue<Out, VN...>)
    void operator()(single_error_sender_base&, Out out) {
      ::pushmi::set_error(out, std::move(e_));
    }
  };
}

namespace operators {

PUSHMI_TEMPLATE(class... VN, class E)
  (requires And<SemiMovable<VN>...> && SemiMovable<E>)
auto error(E e) {
  return make_single_sender(detail::single_error_sender_base{}, detail::single_error_impl<E, VN...>{std::move(e)});
}

} // namespace operators
} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../receiver.h"
//#include "../single_sender.h"
//#include "submit.h"
//#include "extension_operators.h"

namespace pushmi {

namespace operators {

PUSHMI_INLINE_VAR constexpr struct defer_fn {
private:
  template <class F>
  struct impl {
    F f_;
    PUSHMI_TEMPLATE(class Data, class Out)
      (requires Receiver<Out>)
    void operator()(Data&, Out out) {
      auto sender = f_();
      ::pushmi::submit(sender, std::move(out));
    }
  };
public:
  PUSHMI_TEMPLATE(class F)
    (requires Invocable<F&>)
  auto operator()(F f) const {
    struct sender_base : single_sender<> {
      using properties = properties_t<invoke_result_t<F&>>;
    };
    return make_single_sender(sender_base{}, impl<F>{std::move(f)});
  }
} defer {};

} // namespace operators

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../piping.h"
//#include "../executor.h"
//#include "extension_operators.h"

namespace pushmi {

namespace detail {

struct on_fn {
private:
  template <class In, class Out>
  struct on_value_impl {
    In in_;
    Out out_;
    void operator()(any) {
      ::pushmi::submit(in_, std::move(out_));
    }
  };
  template <class In, class ExecutorFactory>
  struct out_impl {
    ExecutorFactory ef_;
    PUSHMI_TEMPLATE(class Out)
      (requires SenderTo<In, Out>)
    void operator()(In& in, Out out) const {
      auto exec = ef_();
      ::pushmi::submit(exec,
        ::pushmi::make_receiver(on_value_impl<In, Out>{in, std::move(out)})
      );
    }
  };
  template <class In, class TP, class Out>
  struct time_on_value_impl {
    In in_;
    TP at_;
    Out out_;
    void operator()(any) {
      ::pushmi::submit(in_, at_, std::move(out_));
    }
  };
  template <class In, class ExecutorFactory>
  struct time_out_impl {
    ExecutorFactory ef_;
    PUSHMI_TEMPLATE(class TP, class Out)
      (requires TimeSenderTo<In, Out>)
    void operator()(In& in, TP at, Out out) const {
      auto exec = ef_();
      ::pushmi::submit(exec, at,
        ::pushmi::make_receiver(
          time_on_value_impl<In, TP, Out>{in, at, std::move(out)}
        )
      );
    }
  };
  template <class ExecutorFactory>
  struct in_impl {
    ExecutorFactory ef_;
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return ::pushmi::detail::sender_from(
        std::move(in),
        detail::submit_transform_out<In>(
          out_impl<In, ExecutorFactory>{ef_},
          time_out_impl<In, ExecutorFactory>{ef_}
        )
      );
    }
  };
public:
  PUSHMI_TEMPLATE(class ExecutorFactory)
    (requires Invocable<ExecutorFactory&> && Executor<invoke_result_t<ExecutorFactory&>>)
  auto operator()(ExecutorFactory ef) const {
    return in_impl<ExecutorFactory>{std::move(ef)};
  }
};

} // namespace detail

namespace operators {

PUSHMI_INLINE_VAR constexpr detail::on_fn on{};

} // namespace operators

} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <cassert>
//#include "extension_operators.h"

namespace pushmi {

namespace detail {

PUSHMI_TEMPLATE(class SideEffects, class Out)
  (requires Receiver<SideEffects> && Receiver<Out>)
struct tap_ {
  SideEffects sideEffects;
  Out out;

  // side effect has no effect on the properties.
  using properties = properties_t<Out>;

  PUSHMI_TEMPLATE(class... VN)
    (requires
      ReceiveValue<SideEffects, const std::remove_reference_t<VN>...> &&
      ReceiveValue<Out, std::remove_reference_t<VN>...>)
  void value(VN&&... vn) {
    ::pushmi::set_value(sideEffects, as_const(vn)...);
    ::pushmi::set_value(out, (VN&&) vn...);
  }
  PUSHMI_TEMPLATE(class E)
    (requires
      ReceiveError<SideEffects, const E> &&
      ReceiveError<Out, E>)
  void error(E e) noexcept {
    ::pushmi::set_error(sideEffects, as_const(e));
    ::pushmi::set_error(out, std::move(e));
  }
  void done() {
    ::pushmi::set_done(sideEffects);
    ::pushmi::set_done(out);
  }
  PUSHMI_TEMPLATE(class Up, class UUp = std::remove_reference_t<Up>)
    (requires
      FlowReceiver<SideEffects> &&
      FlowReceiver<Out>)
  void starting(Up&& up) {
    // up is not made const because sideEffects is allowed to call methods on up
    ::pushmi::set_starting(sideEffects, up);
    ::pushmi::set_starting(out, (Up&&) up);
  }
};

PUSHMI_INLINE_VAR constexpr struct make_tap_fn {
  PUSHMI_TEMPLATE(class SideEffects, class Out)
    (requires Receiver<SideEffects> && Receiver<Out> &&
      Receiver<tap_<SideEffects, Out>>)
  auto operator()(SideEffects se, Out out) const {
    return tap_<SideEffects, Out>{std::move(se), std::move(out)};
  }
} const make_tap {};

struct tap_fn {
private:
  PUSHMI_TEMPLATE (class In, class SideEffects)
    (requires Sender<In> && Receiver<SideEffects>)
  static auto impl(In in, SideEffects sideEffects) {
    return ::pushmi::detail::sender_from(
      std::move(in),
      ::pushmi::detail::submit_transform_out<In>(
        out_impl<In, SideEffects>{std::move(sideEffects)}
      )
    );
  }

  template <class... AN>
  struct in_impl {
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) {
      return tap_fn::impl(
        std::move(in),
        ::pushmi::detail::receiver_from_fn<In>()(std::move(args_)));
    }
  };
  PUSHMI_TEMPLATE (class In, class SideEffects)
    (requires Sender<In> && Receiver<SideEffects>)
  struct out_impl {
    SideEffects sideEffects_;
    PUSHMI_TEMPLATE (class Out)
      (requires Receiver<Out> && SenderTo<In, Out> &&
        SenderTo<In, decltype(::pushmi::detail::receiver_from_fn<In>()(
            detail::make_tap(std::declval<SideEffects>(), std::declval<Out>())))>)
    auto operator()(Out out) const {
      auto gang{::pushmi::detail::receiver_from_fn<In>()(
          detail::make_tap(sideEffects_, std::move(out)))};
      return gang;
    }
  };
public:
  template <class... AN>
  auto operator()(AN... an) const  {
    return in_impl<AN...>{{std::move(an)...}};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::tap_fn tap{};
} // namespace operators

} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../receiver.h"
//#include "../flow_receiver.h"
//#include "submit.h"
//#include "extension_operators.h"

namespace pushmi {

namespace detail {

template<class F, class Tag, bool IsFlow = false>
struct transform_on;

template<class F>
struct transform_on<F, is_single<>> {
  F f_;
  transform_on() = default;
  constexpr explicit transform_on(F f)
    : f_(std::move(f)) {}
  struct value_fn {
    F f_;
    value_fn() = default;
    constexpr explicit value_fn(F f)
      : f_(std::move(f)) {}
    template<class Out, class V0, class... VN>
    auto operator()(Out& out, V0&& v0, VN&&... vn) {
      using Result = ::pushmi::invoke_result_t<F, V0, VN...>;
      static_assert(::pushmi::SemiMovable<Result>,
        "none of the functions supplied to transform can convert this value");
      static_assert(::pushmi::ReceiveValue<Out, Result>,
        "Result of value transform cannot be delivered to Out");
      ::pushmi::set_value(out, f_((V0&&) v0, (VN&&) vn...));
    }
  };
  template<class Out>
  auto operator()(Out out) const {
    return ::pushmi::make_receiver(std::move(out), value_fn{f_});
  }
};

template<class F>
struct transform_on<F, is_single<>, true> {
  F f_;
  transform_on() = default;
  constexpr explicit transform_on(F f)
    : f_(std::move(f)) {}
  template<class Out>
  auto operator()(Out out) const {
    return make_flow_single(std::move(out), on_value(*this));
  }
  template<class Out, class V0, class... VN>
  auto operator()(Out& out, V0&& v0, VN&&... vn) {
    using Result = ::pushmi::invoke_result_t<F, V0, VN...>;
    static_assert(::pushmi::SemiMovable<Result>,
      "none of the functions supplied to transform can convert this value");
    static_assert(::pushmi::Flow<Out> && ::pushmi::ReceiveValue<Out, Result>,
      "Result of value transform cannot be delivered to Out");
      ::pushmi::set_value(out, f_((V0&&) v0, (VN&&) vn...));
  }
};

template<class F>
struct transform_on<F, is_many<>> {
  F f_;
  transform_on() = default;
  constexpr explicit transform_on(F f)
    : f_(std::move(f)) {}
  template<class Out>
  auto operator()(Out out) const {
    return ::pushmi::make_receiver(std::move(out), on_value(*this));
  }
  template<class Out, class V0, class... VN>
  auto operator()(Out& out, V0&& v0, VN&&... vn) {
    using Result = ::pushmi::invoke_result_t<F, V0, VN...>;
    static_assert(::pushmi::SemiMovable<Result>,
      "none of the functions supplied to transform can convert this value");
    static_assert(::pushmi::ReceiveValue<Out, Result>,
      "Result of value transform cannot be delivered to Out");
    ::pushmi::set_value(out, f_((V0&&) v0, (VN&&) vn...));
  }
};

template<class F>
struct transform_on<F, is_many<>, true> {
  F f_;
  transform_on() = default;
  constexpr explicit transform_on(F f)
    : f_(std::move(f)) {}
  template<class Out>
  auto operator()(Out out) const {
    return make_flow_receiver(std::move(out), on_value(*this));
  }
  template<class Out, class V0, class... VN>
  auto operator()(Out& out, V0&& v0, VN&&... vn) {
    using Result = ::pushmi::invoke_result_t<F, V0, VN...>;
    static_assert(::pushmi::SemiMovable<Result>,
      "none of the functions supplied to transform can convert this value");
    static_assert(::pushmi::Flow<Out> && ::pushmi::ReceiveValue<Out, Result>,
      "Result of value transform cannot be delivered to Out");
    ::pushmi::set_value(out, f_((V0&&) v0, (VN&&) vn...));
  }
};

struct transform_fn {
private:
  template <class F>
  struct impl {
    F f_;
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      using Cardinality = property_set_index_t<properties_t<In>, is_single<>>;
      return ::pushmi::detail::sender_from(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(
          // copy 'f_' to allow multiple calls to connect to multiple 'in'
          transform_on<F, Cardinality, property_query_v<properties_t<In>, is_flow<>>>{f_}
        )
      );
    }
  };
public:
  template <class... FN>
  auto operator()(FN... fn) const {
    auto f = ::pushmi::overload(std::move(fn)...);
    using F = decltype(f);
    return impl<F>{std::move(f)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::transform_fn transform{};
} // namespace operators

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../piping.h"
//#include "extension_operators.h"

namespace pushmi {

namespace detail {

struct filter_fn {
private:
  template <class In, class Predicate>
  struct on_value_impl {
    Predicate p_;
    PUSHMI_TEMPLATE(class Out, class... VN)
      (requires Receiver<Out>)
    void operator()(Out& out, VN&&... vn) const {
      if (p_(as_const(vn)...)) {
        ::pushmi::set_value(out, (VN&&) vn...);
      }
    }
  };
  template <class In, class Predicate>
  struct submit_impl {
    Predicate p_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    auto operator()(Out out) const {
      return ::pushmi::detail::receiver_from_fn<In>()(
        std::move(out),
        // copy 'p' to allow multiple calls to submit
        on_value_impl<In, Predicate>{p_}
      );
    }
  };
  template <class Predicate>
  struct adapt_impl {
    Predicate p_;
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return ::pushmi::detail::sender_from(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(submit_impl<In, Predicate>{p_})
      );
    }
  };
public:
  PUSHMI_TEMPLATE(class Predicate)
    (requires SemiMovable<Predicate>)
  auto operator()(Predicate p) const {
    return adapt_impl<Predicate>{std::move(p)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::filter_fn filter{};
} // namespace operators

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../piping.h"
//#include "extension_operators.h"

namespace pushmi {

namespace detail {

struct switch_on_error_fn {
private:
  template <class ErrorSelector>
  struct on_error_impl {
    ErrorSelector es_;
    PUSHMI_TEMPLATE (class Out, class E)
      (requires Receiver<Out> && Invocable<const ErrorSelector&, E> && SenderTo<pushmi::invoke_result_t<ErrorSelector&, E>, Out>)
    void operator()(Out& out, E&& e) const noexcept {
      static_assert(::pushmi::NothrowInvocable<const ErrorSelector&, E>,
        "switch_on_error - error selector function must be noexcept");
      auto next = es_((E&&) e);
      ::pushmi::submit(next, out);
    }
  };
  template <class In, class ErrorSelector>
  struct out_impl {
    ErrorSelector es_;
    PUSHMI_TEMPLATE (class Out)
      (requires Receiver<Out>)
    auto operator()(Out out) const {
      return ::pushmi::detail::receiver_from_fn<In>()(
        std::move(out),
        // copy 'es' to allow multiple calls to submit
        ::pushmi::on_error(on_error_impl<ErrorSelector>{es_})
      );
    }
  };
  template <class ErrorSelector>
  struct in_impl {
    ErrorSelector es_;
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return ::pushmi::detail::sender_from(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(
          out_impl<In, ErrorSelector>{es_}
        )
      );
    }
  };
public:
  PUSHMI_TEMPLATE(class ErrorSelector)
    (requires SemiMovable<ErrorSelector>)
  auto operator()(ErrorSelector es) const {
    return in_impl<ErrorSelector>{std::move(es)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::switch_on_error_fn switch_on_error{};
} // namespace operators

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../piping.h"
//#include "../executor.h"
//#include "extension_operators.h"

namespace pushmi {

namespace detail {

template<class Executor>
struct via_fn_base {
  Executor exec;
  bool done;
  explicit via_fn_base(Executor ex) : exec(std::move(ex)), done(false) {}
  via_fn_base& via_fn_base_ref() {return *this;}
};
template<class Executor, class Out>
struct via_fn_data : public Out, public via_fn_base<Executor> {

  via_fn_data(Out out, Executor exec) :
    Out(std::move(out)), via_fn_base<Executor>(std::move(exec)) {}

  using typename Out::properties;
  using Out::error;
  using Out::done;
};

template<class Out, class Executor>
auto make_via_fn_data(Out out, Executor ex) -> via_fn_data<Executor, Out> {
  return {std::move(out), std::move(ex)};
}

struct via_fn {
private:
  template <class Out>
  struct on_value_impl {
    template <class V>
    struct impl {
      V v_;
      Out out_;
      void operator()(any) {
        ::pushmi::set_value(out_, std::move(v_));
      }
    };
    template <class Data, class V>
    void operator()(Data& data, V&& v) const {
      if (data.via_fn_base_ref().done) {return;}
      ::pushmi::submit(
        data.via_fn_base_ref().exec,
        ::pushmi::make_receiver(
          impl<std::decay_t<V>>{(V&&) v, std::move(static_cast<Out&>(data))}
        )
      );
    }
  };
  template <class Out>
  struct on_error_impl {
    template <class E>
    struct impl {
      E e_;
      Out out_;
      void operator()(any) noexcept {
        ::pushmi::set_error(out_, std::move(e_));
      }
    };
    template <class Data, class E>
    void operator()(Data& data, E e) const noexcept {
      if (data.via_fn_base_ref().done) {return;}
      data.via_fn_base_ref().done = true;
      ::pushmi::submit(
        data.via_fn_base_ref().exec,
        ::pushmi::make_receiver(
          impl<E>{std::move(e), std::move(static_cast<Out&>(data))}
        )
      );
    }
  };
  template <class Out>
  struct on_done_impl {
    struct impl {
      Out out_;
      void operator()(any) {
        ::pushmi::set_done(out_);
      }
    };
    template <class Data>
    void operator()(Data& data) const {
      if (data.via_fn_base_ref().done) {return;}
      data.via_fn_base_ref().done = true;
      ::pushmi::submit(
        data.via_fn_base_ref().exec,
        ::pushmi::make_receiver(
          impl{std::move(static_cast<Out&>(data))}
        )
      );
    }
  };
  template <class In, class ExecutorFactory>
  struct executor_impl {
    ExecutorFactory ef_;
    template <class Data>
    auto operator()(Data& data) const {
      return ef_();
    }
  };
  template <class In, class ExecutorFactory>
  struct out_impl {
    ExecutorFactory ef_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    auto operator()(Out out) const {
      auto exec = ef_();
      return ::pushmi::detail::receiver_from_fn<In>()(
        make_via_fn_data(std::move(out), std::move(exec)),
        on_value_impl<Out>{},
        on_error_impl<Out>{},
        on_done_impl<Out>{}
      );
    }
  };
  template <class ExecutorFactory>
  struct in_impl {
    ExecutorFactory ef_;
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return ::pushmi::detail::sender_from(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(
          out_impl<In, ExecutorFactory>{ef_}
        ),
        ::pushmi::on_executor(executor_impl<In, ExecutorFactory>{ef_})
      );
    }
  };
public:
  PUSHMI_TEMPLATE(class ExecutorFactory)
    (requires Invocable<ExecutorFactory&> &&
      Executor<invoke_result_t<ExecutorFactory&>> &&
      FifoSequence<invoke_result_t<ExecutorFactory&>>)
  auto operator()(ExecutorFactory ef) const {
    return in_impl<ExecutorFactory>{std::move(ef)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::via_fn via{};
} // namespace operators

} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../receiver.h"
//#include "submit.h"
//#include "extension_operators.h"
//#include "via.h"

namespace pushmi {

template<typename In>
struct send_via {
    In in;
    PUSHMI_TEMPLATE(class... AN)
      (requires Invocable<decltype(::pushmi::operators::via), AN...> &&
      Invocable<invoke_result_t<decltype(::pushmi::operators::via), AN...>, In>)
    auto via(AN&&... an) {
        return in | ::pushmi::operators::via((AN&&) an...);
    }
};

namespace detail {

struct request_via_fn {
private:
  struct impl {
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return send_via<In>{in};
    }
  };
public:
  inline auto operator()() const {
    return impl{};
  }
};

} // namespace detail

namespace operators {

PUSHMI_INLINE_VAR constexpr detail::request_via_fn request_via{};

} // namespace operators

PUSHMI_TEMPLATE(class To, class In)
  (requires Same<To, is_sender<>> && Sender<In>)
auto via_cast(In in) {
  return in;
}

PUSHMI_TEMPLATE(class To, class In)
  (requires Same<To, is_sender<>>)
auto via_cast(send_via<In> ss) {
  return ss.in;
}

} // namespace pushmi
// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "../receiver.h"
//#include "submit.h"
//#include "extension_operators.h"

//#include "../subject.h"

namespace pushmi {

namespace detail {

template<class... TN>
struct share_fn {
private:
  struct impl {
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      subject<properties_t<In>, TN...> sub;
      ::pushmi::submit(in, sub.receiver());
      return sub;
    }
  };
public:
  auto operator()() const {
    return impl{};
  }
};

} // namespace detail

namespace operators {

template<class... TN>
PUSHMI_INLINE_VAR constexpr detail::share_fn<TN...> share{};

} // namespace operators

} // namespace pushmi

#endif // PUSHMI_SINGLE_HEADER
