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
#if defined(__clang__)
#define PUSHMI_PP_IGNORE_CXX2A_COMPAT_BEGIN \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wunknown-pragmas\"") \
    _Pragma("GCC diagnostic ignored \"-Wpragmas\"") \
    _Pragma("GCC diagnostic ignored \"-Wc++2a-compat\"") \
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
struct is_silent;
template<class...TN>
struct is_none;
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

template<class...TN>
struct is_time;
template<class...TN>
struct is_constrained;

// implementation types

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class none;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class single;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class many;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class many_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class constrained_single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class time_single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_single;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_single_sender;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_many;

template <PUSHMI_TYPE_CONSTRAINT(SemiMovable)... TN>
class flow_many_sender;


template<
  class V,
  class E = std::exception_ptr>
class any_single_sender;

template<
  class V,
  class E = std::exception_ptr,
  class C = std::ptrdiff_t>
struct any_constrained_single_sender;

template<
  class V,
  class E = std::exception_ptr,
  class TP = std::chrono::system_clock::time_point>
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
PUSHMI_TEMPLATE (class S, class V)
  (requires requires (std::declval<S&>().value(std::declval<V&&>())))
void set_value(S& s, V&& v) noexcept(noexcept(s.value((V&&) v))) {
  s.value((V&&) v);
}
PUSHMI_TEMPLATE (class S, class V)
  (requires requires (std::declval<S&>().next(std::declval<V&&>())))
void set_next(S& s, V&& v) noexcept(noexcept(s.next((V&&) v))) {
  s.next((V&&) v);
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
  (requires requires (std::declval<SD&>().submit(std::declval<Out>())))
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
        std::declval<TP(&)(TP)>()(std::declval<SD&>().top()),
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
PUSHMI_TEMPLATE (class S, class V)
  (requires requires (std::declval<S&>()->value(std::declval<V&&>())))
void set_value(S& s, V&& v) noexcept(noexcept(s->value((V&&) v))) {
  s->value((V&&) v);
}
PUSHMI_TEMPLATE (class S, class V)
  (requires requires (std::declval<S&>()->next(std::declval<V&&>())))
void set_next(S& s, V&& v) noexcept(noexcept(s->next((V&&) v))) {
  s->next((V&&) v);
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
        std::declval<TP(&)(TP)>()(std::declval<SD&>()->top()),
        std::declval<Out>())
  ))
void submit(SD& sd, TP tp, Out out)
  noexcept(noexcept(sd->submit(std::move(tp), std::move(out)))) {
  sd->submit(std::move(tp), std::move(out));
}

//
// add support for std::promise externally
//

template <class T>
void set_done(std::promise<T>& p) noexcept(
    noexcept(p.set_exception(std::make_exception_ptr(0)))) {
  p.set_exception(std::make_exception_ptr(
      std::logic_error("std::promise does not support done.")));
}
inline void set_done(std::promise<void>& p) noexcept(noexcept(p.set_value())) {
  p.set_value();
}
template <class T>
void set_error(std::promise<T>& s, std::exception_ptr e) noexcept {
  s.set_exception(std::move(e));
}
template <class T, class E>
void set_error(std::promise<T>& s, E e) noexcept {
  s.set_exception(std::make_exception_ptr(std::move(e)));
}
template <class T>
void set_value(std::promise<T>& s, T t) {
  s.set_value(std::move(t));
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
PUSHMI_TEMPLATE (class S, class V)
  (requires requires ( set_value(std::declval<S&>(), std::declval<V&&>()) ))
void set_value(std::reference_wrapper<S> s, V&& v) noexcept(
  noexcept(set_value(s.get(), (V&&) v))) {
  set_value(s.get(), (V&&) v);
}
PUSHMI_TEMPLATE (class S, class V)
  (requires requires ( set_next(std::declval<S&>(), std::declval<V&&>()) ))
void set_next(std::reference_wrapper<S> s, V&& v) noexcept(
  noexcept(set_next(s.get(), (V&&) v))) {
  set_next(s.get(), (V&&) v);
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
  PUSHMI_TEMPLATE (class S, class V)
    (requires requires (
      set_value(std::declval<S&>(), std::declval<V&&>()),
      set_error(std::declval<S&>(), std::current_exception())
    ))
  void operator()(S&& s, V&& v) const
      noexcept(noexcept(set_value(s, (V&&) v))) {
    try {
      set_value(s, (V&&) v);
    } catch (...) {
      set_error(s, std::current_exception());
    }
  }
};
struct set_next_fn {
  PUSHMI_TEMPLATE (class S, class V)
    (requires requires (
      set_next(std::declval<S&>(), std::declval<V&&>()),
      set_error(std::declval<S&>(), std::current_exception())
    ))
  void operator()(S&& s, V&& v) const
      noexcept(noexcept(set_next(s, (V&&) v))) {
    try {
      set_next(s, (V&&) v);
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
PUSHMI_INLINE_VAR constexpr __adl::set_next_fn set_next{};
PUSHMI_INLINE_VAR constexpr __adl::set_starting_fn set_starting{};
PUSHMI_INLINE_VAR constexpr __adl::get_executor_fn executor{};
PUSHMI_INLINE_VAR constexpr __adl::do_submit_fn submit{};
PUSHMI_INLINE_VAR constexpr __adl::get_top_fn now{};
PUSHMI_INLINE_VAR constexpr __adl::get_top_fn top{};

template <class T>
struct property_set_traits<std::promise<T>> {
  using properties = property_set<is_receiver<>, is_single<>>;
};
template <>
struct property_set_traits<std::promise<void>> {
  using properties = property_set<is_receiver<>, is_none<>>;
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

// flow affects both sender and receiver

struct flow_category {};

// sender and receiver are mutually exclusive

struct receiver_category {};

struct sender_category {};

// for senders that are executors

struct executor_category {};

// time and constrained are mutually exclusive refinements of sender (time is a special case of constrained and may be folded in later)


// Silent trait and tag
template<class... TN>
struct is_silent;
// Tag
template<>
struct is_silent<> { using property_category = cardinality_category; };
// Trait
template<class PS>
struct is_silent<PS> : property_query<PS, is_silent<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_silent_v = is_silent<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Silent,
    is_silent_v<PS>
);

// None trait and tag
template<class... TN>
struct is_none;
// Tag
template<>
struct is_none<> : is_silent<> {};
// Trait
template<class PS>
struct is_none<PS> : property_query<PS, is_none<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_none_v = is_none<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept None,
    Silent<PS> && is_none_v<PS>
);

// Single trait and tag
template<class... TN>
struct is_single;
// Tag
template<>
struct is_single<> : is_none<> {};
// Trait
template<class PS>
struct is_single<PS> : property_query<PS, is_single<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_single_v = is_single<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Single,
    None<PS> && is_single_v<PS>
);

// Many trait and tag
template<class... TN>
struct is_many;
// Tag
template<>
struct is_many<> : is_none<> {}; // many::value() does not terminate, so it is not a refinement of single
// Trait
template<class PS>
struct is_many<PS> : property_query<PS, is_many<>> {};
template<class PS>
PUSHMI_INLINE_VAR constexpr bool is_many_v = is_many<PS>::value;
PUSHMI_CONCEPT_DEF(
  template (class PS)
  concept Many,
    None<PS> && is_many_v<PS>
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

PUSHMI_CONCEPT_DEF(
  template (class S, class... PropertyN)
  (concept Receiver)(S, PropertyN...),
    requires (S& s) (
      ::pushmi::set_done(s)
    ) &&
    SemiMovable<S> &&
    property_query_v<S, PropertyN...> &&
    is_receiver_v<S> &&
    !is_sender_v<S>
);

PUSHMI_CONCEPT_DEF(
  template (class N, class E = std::exception_ptr)
  (concept NoneReceiver)(N, E),
    requires(N& n, E&& e) (
      ::pushmi::set_error(n, (E &&) e)
    ) &&
    Receiver<N> &&
    None<N> &&
    SemiMovable<E>
);

PUSHMI_CONCEPT_DEF(
  template (class S, class T, class E = std::exception_ptr)
  (concept SingleReceiver)(S, T, E),
    requires(S& s, T&& t) (
      ::pushmi::set_value(s, (T &&) t) // Semantics: called exactly once.
    ) &&
    NoneReceiver<S, E> &&
    SemiMovable<T> &&
    SemiMovable<E> &&
    Single<S>
);

PUSHMI_CONCEPT_DEF(
  template (class S, class T, class E = std::exception_ptr)
  (concept ManyReceiver)(S, T, E),
    requires(S& s, T&& t) (
      ::pushmi::set_next(s, (T &&) t) // Semantics: called 0-N times.
    ) &&
    NoneReceiver<S, E> &&
    SemiMovable<T> &&
    SemiMovable<E> &&
    Many<S>
);


// silent does not really make sense, but cannot test for
// None without the error type, use is_none<> to strengthen
// requirements
PUSHMI_CONCEPT_DEF(
  template (class D, class... PropertyN)
  (concept Sender)(D, PropertyN...),
    requires(D& d) (
      ::pushmi::executor(d),
      requires_<Executor<decltype(::pushmi::executor(d))>>
    ) &&
    SemiMovable<D> &&
    None<D> &&
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
  template (
    class N,
    class Up,
    class PE = std::exception_ptr,
    class E = PE)
  (concept FlowNoneReceiver)(N, Up, PE, E),
    requires(N& n, Up&& up) (
      ::pushmi::set_starting(n, (Up &&) up)
    ) &&
    FlowReceiver<N> &&
    Receiver<Up> &&
    SemiMovable<PE> &&
    SemiMovable<E> &&
    NoneReceiver<Up, PE> &&
    NoneReceiver<N, E>
);

PUSHMI_CONCEPT_DEF(
  template (
      class S,
      class Up,
      class T,
      class PE = std::exception_ptr,
      class E = PE)
  (concept FlowSingleReceiver)(S, Up, T, PE, E),
    SingleReceiver<S, T, E> &&
    FlowNoneReceiver<S, Up, PE, E>
);

PUSHMI_CONCEPT_DEF(
  template (
      class S,
      class Up,
      class T,
      class PT = std::ptrdiff_t,
      class PE = std::exception_ptr,
      class E = PE)
  (concept FlowManyReceiver)(S, Up, T, PT, PE, E),
    ManyReceiver<S, T, E> &&
    ManyReceiver<Up, PT, PE> &&
    FlowNoneReceiver<S, Up, PE, E>
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
    Constrained<D> &&
    None<D>
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
struct construct_deduced<none>;

template<>
struct construct_deduced<single>;

template<>
struct construct_deduced<many>;

template<>
struct construct_deduced<flow_single>;

template<>
struct construct_deduced<flow_many>;

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
  void operator()(detail::any) {}
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
  PUSHMI_TEMPLATE(class V, class Data)
    (requires requires (
      ::pushmi::set_value(std::declval<Data&>(), std::declval<V>())
    ) && Receiver<Data>)
  void operator()(Data& out, V&& v) const {
    ::pushmi::set_value(out, (V&&) v);
  }
};

struct passDEF {
  PUSHMI_TEMPLATE(class E, class Data)
    (requires NoneReceiver<Data, E>)
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

struct passDNXF {
  PUSHMI_TEMPLATE(class V, class Data)
    (requires requires (
      ::pushmi::set_next(std::declval<Data&>(), std::declval<V>())
    ) && Receiver<Data>)
  void operator()(Data& out, V&& v) const {
    ::pushmi::set_next(out, (V&&) v);
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
struct on_next_fn : overload_fn<Fns...> {
  constexpr on_next_fn() = default;
  using overload_fn<Fns...>::overload_fn;
};

template <class... Fns>
auto on_next(Fns... fns) -> on_next_fn<Fns...> {
  return on_next_fn<Fns...>{std::move(fns)...};
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

//#include "boosters.h"

namespace pushmi {

template <class E>
class none<E> {
  bool done_ = false;
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<int>)]; // can hold a std::promise in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static void s_done(data&) {}
    static void s_error(data&, E) noexcept { std::terminate(); };
    void (*op_)(data&, data*) = vtable::s_op;
    void (*done_)(data&) = vtable::s_done;
    void (*error_)(data&, E) noexcept = vtable::s_error;
  };
  static constexpr vtable const noop_ {};
  vtable  const* vptr_ = &noop_;
  template <class Wrapped>
  none(Wrapped obj, std::false_type) : none() {
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
    };
    static const vtable vtable_v{s::op, s::done, s::error};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtable_v;
  }
  template <class Wrapped>
  none(Wrapped obj, std::true_type) noexcept : none() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          new (dst->buffer_)
              Wrapped(std::move(*static_cast<Wrapped*>((void*)src.buffer_)));
        static_cast<Wrapped const*>((void*)src.buffer_)->~Wrapped();
      }
      static void done(data& src) {
        ::pushmi::set_done(*static_cast<Wrapped*>((void*)src.buffer_));
      }
      static void error(data& src, E e) noexcept {::pushmi::set_error(
          *static_cast<Wrapped*>((void*)src.buffer_),
          std::move(e));
      }
    };
    static const vtable vtbl{s::op, s::done, s::error};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, none>::value, U>;
public:
  using properties = property_set<is_receiver<>, is_none<>>;

  none() = default;
  none(none&& that) noexcept : none() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires NoneReceiver<wrapped_t<Wrapped>, E>)
  explicit none(Wrapped obj) noexcept(insitu<Wrapped>())
    : none{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~none() {
    vptr_->op_(data_, nullptr);
  }
  none& operator=(none&& that) noexcept {
    this->~none();
    new ((void*)this) none(std::move(that));
    return *this;
  }
  void error(E e) noexcept {
    if (done_) {return;}
    done_ = true;
    vptr_->error_(data_, std::move(e));
  }
  void done() {
    if (done_) {return;}
    done_ = true;
    vptr_->done_(data_);
  }
};

// Class static definitions:
template <class E>
constexpr typename none<E>::vtable const none<E>::noop_;

template <class EF, class DF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class none<EF, DF> {
  static_assert(
    !detail::is_v<EF, on_value_fn>,
    "the first parameter is the error implementation, but on_value{} was passed");
  static_assert(
    !detail::is_v<EF, single>,
    "the first parameter is the error implementation, but a single<> was passed");
  bool done_ = false;
  EF ef_;
  DF df_;

public:
  using properties = property_set<is_receiver<>, is_none<>>;

  none() = default;
  constexpr explicit none(EF ef)
      : none(std::move(ef), DF{}) {}
  constexpr explicit none(DF df)
      : none(EF{}, std::move(df)) {}
  constexpr none(EF ef, DF df)
      : done_(false), ef_(std::move(ef)), df_(std::move(df)) {}

  PUSHMI_TEMPLATE (class E)
    (requires Invocable<EF&, E>)
  void error(E e) noexcept {
    static_assert(
        noexcept(ef_(std::move(e))),
        "error function must be noexcept");
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

template <PUSHMI_TYPE_CONSTRAINT(Receiver<is_none<>>) Data, class DEF, class DDF>
#if __cpp_concepts
  requires Invocable<DDF&, Data&>
#endif
class none<Data, DEF, DDF> {
  bool done_ = false;
  Data data_;
  DEF ef_;
  DDF df_;
  static_assert(
    !detail::is_v<DEF, on_value_fn>,
    "the second parameter is the error implementation, but on_value{} was passed");
  static_assert(
    !detail::is_v<Data, single>,
    "none should not be used to wrap a single<>");
public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_receiver<>, is_none<>>>;

  constexpr explicit none(Data d) : none(std::move(d), DEF{}, DDF{}) {}
  constexpr none(Data d, DDF df)
      : done_(false), data_(std::move(d)), ef_(), df_(std::move(df)) {}
  constexpr none(Data d, DEF ef, DDF df = DDF{})
      : done_(false), data_(std::move(d)), ef_(std::move(ef)),
        df_(std::move(df)) {}
  PUSHMI_TEMPLATE (class E)
    (requires Invocable<DEF&, Data&, E>)
  void error(E e) noexcept {
    static_assert(
        noexcept(ef_(data_, std::move(e))), "error function must be noexcept");
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
class none<>
    : public none<abortEF, ignoreDF> {
};

////////////////////////////////////////////////////////////////////////////////
// make_flow_single
PUSHMI_INLINE_VAR constexpr struct make_none_fn {
  inline auto operator()() const {
    return none<>{};
  }
  PUSHMI_TEMPLATE(class EF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF> PUSHMI_AND not lazy::Invocable<EF&>)))
  auto operator()(EF ef) const {
    return none<EF, ignoreDF>{std::move(ef)};
  }
  PUSHMI_TEMPLATE(class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
  auto operator()(DF df) const {
    return none<abortEF, DF>{std::move(df)};
  }
  PUSHMI_TEMPLATE(class EF, class DF)
    (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
  auto operator()(EF ef, DF df) const {
    return none<EF, DF>{std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_none<>> PUSHMI_AND not lazy::Receiver<Data, is_single<>>))
  auto operator()(Data d) const {
    return none<Data, passDEF, passDDF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DEF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_none<>> PUSHMI_AND not lazy::Receiver<Data, is_single<>>
      PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
  auto operator()(Data d, DEF ef) const {
    return none<Data, DEF, passDDF>{std::move(d), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_none<>> PUSHMI_AND not lazy::Receiver<Data, is_single<>> PUSHMI_AND
      lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DDF df) const {
    return none<Data, passDEF, DDF>{std::move(d), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_none<>> PUSHMI_AND not lazy::Receiver<Data, is_single<>> PUSHMI_AND
      lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DEF ef, DDF df) const {
    return none<Data, DEF, DDF>{std::move(d), std::move(ef), std::move(df)};
  }
} const make_none {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
none() -> none<>;

PUSHMI_TEMPLATE(class EF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF> PUSHMI_AND not lazy::Invocable<EF&>)))
none(EF) -> none<EF, ignoreDF>;

PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
none(DF) -> none<abortEF, DF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
none(EF, DF) -> none<EF, DF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_none<>> PUSHMI_AND not lazy::Receiver<Data, is_single<>>))
none(Data) -> none<Data, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DEF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_none<>> PUSHMI_AND not lazy::Receiver<Data, is_single<>>
    PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
none(Data, DEF) -> none<Data, DEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_none<>> PUSHMI_AND not lazy::Receiver<Data, is_single<>> PUSHMI_AND
    lazy::Invocable<DDF&, Data&>))
none(Data, DDF) -> none<Data, passDEF, DDF>;

PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_none<>> PUSHMI_AND not lazy::Receiver<Data, is_single<>> PUSHMI_AND
    lazy::Invocable<DDF&, Data&>))
none(Data, DEF, DDF) -> none<Data, DEF, DDF>;
#endif

template <class E = std::exception_ptr>
using any_none = none<E>;

template<>
struct construct_deduced<none> : make_none_fn {};

// // this is ambiguous because NoneReceiver and SingleReceiver only constrain the done() method.
// // template <class E = std::exception_ptr, NoneReceiver<E> Wrapped>
// // auto erase_cast(Wrapped w) {
// //   return none<erase_cast_t, E>{std::move(w)};
// // }
// template <class E = std::exception_ptr, class... TN>
// auto erase_cast(none<TN...> w) {
//   return none<E>{std::move(w)};
// }
// template <class E = std::exception_ptr>
// auto erase_cast(std::promise<void> w) {
//   return none<E>{std::move(w)};
// }

PUSHMI_TEMPLATE (class Out)
  (requires SenderTo<Out, std::promise<void>, is_none<>>)
std::future<void> future_from(Out out) {
  std::promise<void> p;
  auto result = p.get_future();
  submit(out, std::move(p));
  return result;
}

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <future>
//#include "none.h"

namespace pushmi {

template <class V, class E>
class single<V, E> {
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
    static void s_rvalue(data&, V&&) {}
    static void s_lvalue(data&, V&) {}
    void (*op_)(data&, data*) = vtable::s_op;
    void (*done_)(data&) = vtable::s_done;
    void (*error_)(data&, E) noexcept = vtable::s_error;
    void (*rvalue_)(data&, V&&) = vtable::s_rvalue;
    void (*lvalue_)(data&, V&) = vtable::s_lvalue;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, single>::value, U>;
  template <class Wrapped>
  static void check() {
    static_assert(Invocable<decltype(::pushmi::set_value), Wrapped, V>,
      "Wrapped single must support values of type V");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, std::exception_ptr>,
      "Wrapped single must support std::exception_ptr and be noexcept");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, E>,
      "Wrapped single must support E and be noexcept");
  }
  template<class Wrapped>
  single(Wrapped obj, std::false_type) : single() {
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
      static void rvalue(data& src, V&& v) {
        ::pushmi::set_value(*static_cast<Wrapped*>(src.pobj_), (V&&) v);
      }
      static void lvalue(data& src, V& v) {
        ::pushmi::set_value(*static_cast<Wrapped*>(src.pobj_), v);
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::rvalue, s::lvalue};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template<class Wrapped>
  single(Wrapped obj, std::true_type) noexcept : single() {
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
      static void rvalue(data& src, V&& v) {
        ::pushmi::set_value(*static_cast<Wrapped*>((void*)src.buffer_), (V&&) v);
      }
      static void lvalue(data& src, V& v) {
        ::pushmi::set_value(*static_cast<Wrapped*>((void*)src.buffer_), v);
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::rvalue, s::lvalue};
    new ((void*)data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
public:
  using properties = property_set<is_receiver<>, is_single<>>;

  single() = default;
  single(single&& that) noexcept : single() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires SingleReceiver<wrapped_t<Wrapped>, V, E>)
  explicit single(Wrapped obj) noexcept(insitu<Wrapped>())
    : single{std::move(obj), bool_<insitu<Wrapped>()>{}} {
    check<Wrapped>();
  }
  ~single() {
    vptr_->op_(data_, nullptr);
  }
  single& operator=(single&& that) noexcept {
    this->~single();
    new ((void*)this) single(std::move(that));
    return *this;
  }
  PUSHMI_TEMPLATE (class T)
    (requires ConvertibleTo<T&&, V&&>)
  void value(T&& t) {
    if (!done_) {
      done_ = true;
      vptr_->rvalue_(data_, (T&&) t);
    }
  }
  PUSHMI_TEMPLATE (class T)
    (requires ConvertibleTo<T&, V&>)
  void value(T& t) {
    if (!done_) {
      done_ = true;
      vptr_->lvalue_(data_, t);
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
template <class V, class E>
constexpr typename single<V, E>::vtable const single<V, E>::noop_;

template <class VF, class EF, class DF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class single<VF, EF, DF> {
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
  using properties = property_set<is_receiver<>, is_single<>>;

  single() = default;
  constexpr explicit single(VF vf) : single(std::move(vf), EF{}, DF{}) {}
  constexpr explicit single(EF ef) : single(VF{}, std::move(ef), DF{}) {}
  constexpr explicit single(DF df) : single(VF{}, EF{}, std::move(df)) {}
  constexpr single(EF ef, DF df)
      : done_(false), vf_(), ef_(std::move(ef)), df_(std::move(df)) {}
  constexpr single(VF vf, EF ef, DF df = DF{})
      : done_(false), vf_(std::move(vf)), ef_(std::move(ef)), df_(std::move(df))
  {}

  PUSHMI_TEMPLATE (class V)
    (requires Invocable<VF&, V>)
  void value(V&& v) {
    if (done_) {return;}
    done_ = true;
    vf_((V&&) v);
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
class single<Data, DVF, DEF, DDF> {
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
  static_assert(NothrowInvocable<DEF, Data&, std::exception_ptr>,
      "error function must be noexcept and support std::exception_ptr");

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_receiver<>, is_single<>>>;

  constexpr explicit single(Data d)
      : single(std::move(d), DVF{}, DEF{}, DDF{}) {}
  constexpr single(Data d, DDF df)
      : done_(false), data_(std::move(d)), vf_(), ef_(), df_(df) {}
  constexpr single(Data d, DEF ef, DDF df = DDF{})
      : done_(false), data_(std::move(d)), vf_(), ef_(ef), df_(df) {}
  constexpr single(Data d, DVF vf, DEF ef = DEF{}, DDF df = DDF{})
      : done_(false), data_(std::move(d)), vf_(vf), ef_(ef), df_(df) {}

  PUSHMI_TEMPLATE(class V)
    (requires Invocable<DVF&, Data&, V>)
  void value(V&& v) {
    if (!done_) {
      done_ = true;
      vf_(data_, (V&&) v);
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
class single<>
    : public single<ignoreVF, abortEF, ignoreDF> {
public:
  single() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_single
PUSHMI_INLINE_VAR constexpr struct make_single_fn {
  inline auto operator()() const {
    return single<>{};
  }
  PUSHMI_TEMPLATE(class VF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF> PUSHMI_AND not lazy::Invocable<VF&>)))
  auto operator()(VF vf) const {
    return single<VF, abortEF, ignoreDF>{std::move(vf)};
  }
  template <class... EFN>
  auto operator()(on_error_fn<EFN...> ef) const {
    return single<ignoreVF, on_error_fn<EFN...>, ignoreDF>{std::move(ef)};
  }
  PUSHMI_TEMPLATE(class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
  auto operator()(DF df) const {
    return single<ignoreVF, abortEF, DF>{std::move(df)};
  }
  PUSHMI_TEMPLATE(class VF, class EF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF> PUSHMI_AND not lazy::Invocable<EF&>)))
  auto operator()(VF vf, EF ef) const {
    return single<VF, EF, ignoreDF>{std::move(vf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class EF, class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
  auto operator()(EF ef, DF df) const {
    return single<ignoreVF, EF, DF>{std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class VF, class EF, class DF)
    (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF>)))
  auto operator()(VF vf, EF ef, DF df) const {
    return single<VF, EF, DF>{std::move(vf), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_single<>>))
  auto operator()(Data d) const {
    return single<Data, passDVF, passDEF, passDDF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_single<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DVF&, Data&>)))
  auto operator()(Data d, DVF vf) const {
    return single<Data, DVF, passDEF, passDDF>{std::move(d), std::move(vf)};
  }
  PUSHMI_TEMPLATE(class Data, class... DEFN)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_single<>>))
  auto operator()(Data d, on_error_fn<DEFN...> ef) const {
    return single<Data, passDVF, on_error_fn<DEFN...>, passDDF>{std::move(d), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DDF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_single<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DDF df) const {
    return single<Data, passDVF, passDEF, DDF>{std::move(d), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF, class DEF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_single<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
  auto operator()(Data d, DVF vf, DEF ef) const {
    return single<Data, DVF, DEF, passDDF>{std::move(d), std::move(vf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_single<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DEF ef, DDF df) const {
    return single<Data, passDVF, DEF, DDF>{std::move(d), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_single<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DVF vf, DEF ef, DDF df) const {
    return single<Data, DVF, DEF, DDF>{std::move(d), std::move(vf), std::move(ef), std::move(df)};
  }
} const make_single {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
single() -> single<>;

PUSHMI_TEMPLATE(class VF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF> PUSHMI_AND not lazy::Invocable<VF&>)))
single(VF) -> single<VF, abortEF, ignoreDF>;

template <class... EFN>
single(on_error_fn<EFN...>) -> single<ignoreVF, on_error_fn<EFN...>, ignoreDF>;

PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
single(DF) -> single<ignoreVF, abortEF, DF>;

PUSHMI_TEMPLATE(class VF, class EF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF> PUSHMI_AND not lazy::Invocable<EF&>)))
single(VF, EF) -> single<VF, EF, ignoreDF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
single(EF, DF) -> single<ignoreVF, EF, DF>;

PUSHMI_TEMPLATE(class VF, class EF, class DF)
  (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF>)))
single(VF, EF, DF) -> single<VF, EF, DF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_single<>>))
single(Data d) -> single<Data, passDVF, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DVF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_single<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DVF&, Data&>)))
single(Data d, DVF vf) -> single<Data, DVF, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class... DEFN)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_single<>>))
single(Data d, on_error_fn<DEFN...>) ->
    single<Data, passDVF, on_error_fn<DEFN...>, passDDF>;

PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_single<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
single(Data d, DDF) -> single<Data, passDVF, passDEF, DDF>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_single<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
single(Data d, DVF vf, DEF ef) -> single<Data, DVF, DEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_single<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
single(Data d, DEF, DDF) -> single<Data, passDVF, DEF, DDF>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_single<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
single(Data d, DVF vf, DEF ef, DDF df) -> single<Data, DVF, DEF, DDF>;
#endif

template <class V, class E = std::exception_ptr>
using any_single = single<V, E>;

template<>
struct construct_deduced<single> : make_single_fn {};

// template <class V, class E = std::exception_ptr, class Wrapped>
//     requires SingleReceiver<Wrapped, V, E> && !detail::is_v<Wrapped, none>
// auto erase_cast(Wrapped w) {
//   return single<V, E>{std::move(w)};
// }

PUSHMI_TEMPLATE (class T, class Out)
  (requires SenderTo<Out, std::promise<T>, is_none<>>)
std::future<T> future_from(Out singleSender) {
  std::promise<T> p;
  auto result = p.get_future();
  submit(singleSender, std::move(p));
  return result;
}

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "single.h"

namespace pushmi {

template <class V, class PE, class E>
class flow_single<V, PE, E> {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<int>)]; // can hold a std::promise in-situ
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
    static void s_value(data&, V) {}
    static void s_starting(data&, any_none<PE>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    void (*done_)(data&) = vtable::s_done;
    void (*error_)(data&, E) noexcept = vtable::s_error;
    void (*value_)(data&, V) = vtable::s_value;
    void (*starting_)(data&, any_none<PE>) = vtable::s_starting;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  flow_single(Wrapped obj, std::false_type) : flow_single() {
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
      static void value(data& src, V v) {
        ::pushmi::set_value(*static_cast<Wrapped*>(src.pobj_), std::move(v));
      }
      static void starting(data& src, any_none<PE> up) {
        ::pushmi::set_starting(*static_cast<Wrapped*>(src.pobj_), std::move(up));
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::value, s::starting};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  flow_single(Wrapped obj, std::true_type) noexcept : flow_single() {
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
      static void value(data& src, V v) {
        ::pushmi::set_value(
            *static_cast<Wrapped*>((void*)src.buffer_), std::move(v));
      }
      static void starting(data& src, any_none<PE> up) {
        ::pushmi::set_starting(*static_cast<Wrapped*>((void*)src.buffer_), std::move(up));
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::value, s::starting};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, flow_single>::value, U>;
public:
  using properties = property_set<is_receiver<>, is_flow<>, is_single<>>;

  flow_single() = default;
  flow_single(flow_single&& that) noexcept : flow_single() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires FlowSingleReceiver<wrapped_t<Wrapped>, any_none<PE>, V, PE, E>)
  explicit flow_single(Wrapped obj) noexcept(insitu<Wrapped>())
    : flow_single{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~flow_single() {
    vptr_->op_(data_, nullptr);
  }
  flow_single& operator=(flow_single&& that) noexcept {
    this->~flow_single();
    new ((void*)this) flow_single(std::move(that));
    return *this;
  }
  void value(V v) {
    vptr_->value_(data_, std::move(v));
  }
  void error(E e) noexcept {
    vptr_->error_(data_, std::move(e));
  }
  void done() {
    vptr_->done_(data_);
  }

  void starting(any_none<PE> up) {
    vptr_->starting_(data_, std::move(up));
  }
};

// Class static definitions:
template <class V, class PE, class E>
constexpr typename flow_single<V, PE, E>::vtable const
  flow_single<V, PE, E>::noop_;

template <class VF, class EF, class DF, class StrtF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class flow_single<VF, EF, DF, StrtF> {
  VF vf_;
  EF ef_;
  DF df_;
  StrtF strtf_;

 public:
  using properties = property_set<is_receiver<>, is_flow<>, is_single<>>;

  static_assert(
      !detail::is_v<VF, on_error_fn>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<EF, on_value_fn>,
      "the second parameter is the error implementation, but on_value{} was passed");

  flow_single() = default;
  constexpr explicit flow_single(VF vf)
      : flow_single(std::move(vf), EF{}, DF{}) {}
  constexpr explicit flow_single(EF ef)
      : flow_single(VF{}, std::move(ef), DF{}) {}
  constexpr explicit flow_single(DF df)
      : flow_single(VF{}, EF{}, std::move(df)) {}
  constexpr flow_single(EF ef, DF df)
      : vf_(), ef_(std::move(ef)), df_(std::move(df)) {}
  constexpr flow_single(
      VF vf,
      EF ef,
      DF df = DF{},
      StrtF strtf = StrtF{})
      : vf_(std::move(vf)),
        ef_(std::move(ef)),
        df_(std::move(df)),
        strtf_(std::move(strtf)) {}
  PUSHMI_TEMPLATE (class V)
    (requires Invocable<VF&, V>)
  void value(V v) {
    vf_(v);
  }
  PUSHMI_TEMPLATE (class E)
    (requires Invocable<EF&, E>)
  void error(E e) noexcept {
    static_assert(NothrowInvocable<EF&, E>, "error function must be noexcept");
    ef_(std::move(e));
  }
  void done() {
    df_();
  }
  PUSHMI_TEMPLATE(class Up)
    (requires Receiver<Up, is_none<>> && Invocable<StrtF&, Up&&>)
  void starting(Up&& up) {
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
class flow_single<Data, DVF, DEF, DDF, DStrtF> {
  Data data_;
  DVF vf_;
  DEF ef_;
  DDF df_;
  DStrtF strtf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_receiver<>, is_flow<>, is_single<>>>;

  static_assert(
      !detail::is_v<DVF, on_error_fn>,
      "the first parameter is the value implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<DEF, on_value_fn>,
      "the second parameter is the error implementation, but on_value{} was passed");

  constexpr explicit flow_single(Data d)
      : flow_single(std::move(d), DVF{}, DEF{}, DDF{}) {}
  constexpr flow_single(Data d, DDF df)
      : data_(std::move(d)), vf_(), ef_(), df_(df) {}
  constexpr flow_single(Data d, DEF ef, DDF df = DDF{})
      : data_(std::move(d)), vf_(), ef_(ef), df_(df) {}
  constexpr flow_single(
      Data d,
      DVF vf,
      DEF ef = DEF{},
      DDF df = DDF{},
      DStrtF strtf = DStrtF{})
      : data_(std::move(d)),
        vf_(vf),
        ef_(ef),
        df_(df),
        strtf_(std::move(strtf)) {}
  PUSHMI_TEMPLATE (class V)
    (requires Invocable<DVF&, Data&, V>)
  void value(V v) {
    vf_(data_, v);
  }
  PUSHMI_TEMPLATE (class E)
    (requires Invocable<DEF&, Data&, E>)
  void error(E e) noexcept {
    static_assert(
        NothrowInvocable<DEF&, Data&, E>, "error function must be noexcept");
    ef_(data_, e);
  }
  void done() {
    df_(data_);
  }
  PUSHMI_TEMPLATE (class Up)
    (requires Invocable<DStrtF&, Data&, Up&&>)
  void starting(Up&& up) {
    strtf_(data_, (Up &&) up);
  }
};

template <>
class flow_single<>
    : public flow_single<ignoreVF, abortEF, ignoreDF, ignoreStrtF> {
};

// TODO winnow down the number of make_flow_single overloads and deduction
// guides here, as was done for make_single.

////////////////////////////////////////////////////////////////////////////////
// make_flow_single
PUSHMI_INLINE_VAR constexpr struct make_flow_single_fn {
  inline auto operator()() const {
    return flow_single<>{};
  }
  PUSHMI_TEMPLATE (class VF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF> PUSHMI_AND not lazy::Invocable<VF&>)))
  auto operator()(VF vf) const {
    return flow_single<VF, abortEF, ignoreDF, ignoreStrtF>{
      std::move(vf)};
  }
  template <class... EFN>
  auto operator()(on_error_fn<EFN...> ef) const {
    return flow_single<ignoreVF, on_error_fn<EFN...>, ignoreDF, ignoreStrtF>{
      std::move(ef)};
  }
  PUSHMI_TEMPLATE(class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
  auto operator()(DF df) const {
    return flow_single<ignoreVF, abortEF, DF, ignoreStrtF>{std::move(df)};
  }
  PUSHMI_TEMPLATE (class VF, class EF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF> PUSHMI_AND not lazy::Invocable<EF&>)))
  auto operator()(VF vf, EF ef) const {
    return flow_single<VF, EF, ignoreDF, ignoreStrtF>{std::move(vf),
      std::move(ef)};
  }
  PUSHMI_TEMPLATE(class EF, class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
  auto operator()(EF ef, DF df) const {
    return flow_single<ignoreVF, EF, DF, ignoreStrtF>{std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE (class VF, class EF, class DF)
    (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF>)))
  auto operator()(VF vf, EF ef, DF df) const {
    return flow_single<VF, EF, DF, ignoreStrtF>{std::move(vf),
      std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE (class VF, class EF, class DF, class StrtF)
    (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF>)))
  auto operator()(VF vf, EF ef, DF df, StrtF strtf) const {
    return flow_single<VF, EF, DF, StrtF>{std::move(vf), std::move(ef),
      std::move(df), std::move(strtf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data>))
  auto operator()(Data d) const {
    return flow_single<Data, passDVF, passDEF, passDDF, passDStrtF>{
        std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DVF&, Data&>)))
  auto operator()(Data d, DVF vf) const {
    return flow_single<Data, DVF, passDEF, passDDF, passDStrtF>{
      std::move(d), std::move(vf)};
  }
  PUSHMI_TEMPLATE(class Data, class... DEFN)
    (requires PUSHMI_EXP(lazy::Receiver<Data>))
  auto operator()(Data d, on_error_fn<DEFN...> ef) const {
    return flow_single<Data, passDVF, on_error_fn<DEFN...>, passDDF, passDStrtF>{
      std::move(d), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DDF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DDF df) const {
    return flow_single<Data, passDVF, passDEF, DDF, passDStrtF>{
      std::move(d), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF, class DEF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
  auto operator()(Data d, DVF vf, DEF ef) const {
    return flow_single<Data, DVF, DEF, passDDF, passDStrtF>{std::move(d), std::move(vf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DEF ef, DDF df) const {
    return flow_single<Data, passDVF, DEF, DDF, passDStrtF>{
      std::move(d), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DVF vf, DEF ef, DDF df) const {
    return flow_single<Data, DVF, DEF, DDF, passDStrtF>{std::move(d),
      std::move(vf), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF, class DStrtF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DVF vf, DEF ef, DDF df, DStrtF strtf) const {
    return flow_single<Data, DVF, DEF, DDF, DStrtF>{std::move(d),
      std::move(vf), std::move(ef), std::move(df), std::move(strtf)};
  }
} const make_flow_single {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
flow_single() -> flow_single<>;

PUSHMI_TEMPLATE(class VF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF> PUSHMI_AND not lazy::Invocable<VF&>)))
flow_single(VF) ->
  flow_single<VF, abortEF, ignoreDF, ignoreStrtF>;

template <class... EFN>
flow_single(on_error_fn<EFN...>) ->
  flow_single<ignoreVF, on_error_fn<EFN...>, ignoreDF, ignoreStrtF>;

PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
flow_single(DF) ->
  flow_single<ignoreVF, abortEF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class VF, class EF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF> PUSHMI_AND not lazy::Invocable<EF&>)))
flow_single(VF, EF) ->
  flow_single<VF, EF, ignoreDF, ignoreStrtF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
flow_single(EF, DF) ->
  flow_single<ignoreVF, EF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class VF, class EF, class DF)
  (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF>)))
flow_single(VF, EF, DF) ->
  flow_single<VF, EF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class VF, class EF, class DF, class StrtF)
  (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<VF>)))
flow_single(VF, EF, DF, StrtF) ->
  flow_single<VF, EF, DF, StrtF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data>))
flow_single(Data d) ->
  flow_single<Data, passDVF, passDEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DVF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DVF&, Data&>)))
flow_single(Data d, DVF vf) ->
  flow_single<Data, DVF, passDEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class... DEFN)
  (requires PUSHMI_EXP(lazy::Receiver<Data>))
flow_single(Data d, on_error_fn<DEFN...>) ->
  flow_single<Data, passDVF, on_error_fn<DEFN...>, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
flow_single(Data d, DDF) ->
    flow_single<Data, passDVF, passDEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
flow_single(Data d, DVF vf, DEF ef) ->
  flow_single<Data, DVF, DEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
flow_single(Data d, DEF, DDF) ->
  flow_single<Data, passDVF, DEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
flow_single(Data d, DVF vf, DEF ef, DDF df) ->
  flow_single<Data, DVF, DEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DVF, class DEF, class DDF, class DStrtF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&> ))
flow_single(Data d, DVF vf, DEF ef, DDF df, DStrtF strtf) ->
  flow_single<Data, DVF, DEF, DDF, DStrtF>;
#endif

template <class V, class PE = std::exception_ptr, class E = PE>
using any_flow_single = flow_single<V, PE, E>;

template<>
struct construct_deduced<flow_single> : make_flow_single_fn {};

// template <class V, class PE = std::exception_ptr, class E = PE, class Wrapped>
//     requires FlowSingleReceiver<Wrapped, V, PE, E> && !detail::is_v<Wrapped, none> &&
//     !detail::is_v<Wrapped, std::promise>
//     auto erase_cast(Wrapped w) {
//   return flow_single<V, PE, E>{std::move(w)};
// }

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <future>
//#include "none.h"

namespace pushmi {

template <class V, class E>
class many<V, E> {
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
    static void s_rnext(data&, V&&) {}
    static void s_lnext(data&, V&) {}
    void (*op_)(data&, data*) = vtable::s_op;
    void (*done_)(data&) = vtable::s_done;
    void (*error_)(data&, E) noexcept = vtable::s_error;
    void (*rnext_)(data&, V&&) = vtable::s_rnext;
    void (*lnext_)(data&, V&) = vtable::s_lnext;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, many>::value, U>;
  template <class Wrapped>
  static void check() {
    static_assert(Invocable<decltype(::pushmi::set_next), Wrapped, V>,
      "Wrapped many must support nexts of type V");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, std::exception_ptr>,
      "Wrapped many must support std::exception_ptr and be noexcept");
    static_assert(NothrowInvocable<decltype(::pushmi::set_error), Wrapped, E>,
      "Wrapped many must support E and be noexcept");
  }
  template<class Wrapped>
  many(Wrapped obj, std::false_type) : many() {
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
      static void rnext(data& src, V&& v) {
        ::pushmi::set_next(*static_cast<Wrapped*>(src.pobj_), (V&&) v);
      }
      static void lnext(data& src, V& v) {
        ::pushmi::set_next(*static_cast<Wrapped*>(src.pobj_), v);
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::rnext, s::lnext};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template<class Wrapped>
  many(Wrapped obj, std::true_type) noexcept : many() {
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
      static void rnext(data& src, V&& v) {
        ::pushmi::set_next(*static_cast<Wrapped*>((void*)src.buffer_), (V&&) v);
      }
      static void lnext(data& src, V& v) {
        ::pushmi::set_next(*static_cast<Wrapped*>((void*)src.buffer_), v);
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::rnext, s::lnext};
    new ((void*)data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
public:
  using properties = property_set<is_receiver<>, is_many<>>;

  many() = default;
  many(many&& that) noexcept : many() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires ManyReceiver<wrapped_t<Wrapped>, V, E>)
  explicit many(Wrapped obj) noexcept(insitu<Wrapped>())
    : many{std::move(obj), bool_<insitu<Wrapped>()>{}} {
    check<Wrapped>();
  }
  ~many() {
    vptr_->op_(data_, nullptr);
  }
  many& operator=(many&& that) noexcept {
    this->~many();
    new ((void*)this) many(std::move(that));
    return *this;
  }
  PUSHMI_TEMPLATE (class T)
    (requires ConvertibleTo<T&&, V&&>)
  void next(T&& t) {
    if (!done_) {
      vptr_->rnext_(data_, (T&&) t);
    }
  }
  PUSHMI_TEMPLATE (class T)
    (requires ConvertibleTo<T&, V&>)
  void next(T& t) {
    if (!done_) {
      vptr_->lnext_(data_, t);
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
template <class V, class E>
constexpr typename many<V, E>::vtable const many<V, E>::noop_;

template <class NF, class EF, class DF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class many<NF, EF, DF> {
  bool done_ = false;
  NF nf_;
  EF ef_;
  DF df_;

  static_assert(
      !detail::is_v<NF, on_error_fn>,
      "the first parameter is the next implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<EF, on_next_fn>,
      "the second parameter is the error implementation, but on_next{} was passed");
  static_assert(NothrowInvocable<EF&, std::exception_ptr>,
      "error function must be noexcept and support std::exception_ptr");
 public:
  using properties = property_set<is_receiver<>, is_many<>>;

  many() = default;
  constexpr explicit many(NF nf) : many(std::move(nf), EF{}, DF{}) {}
  constexpr explicit many(EF ef) : many(NF{}, std::move(ef), DF{}) {}
  constexpr explicit many(DF df) : many(NF{}, EF{}, std::move(df)) {}
  constexpr many(EF ef, DF df)
      : done_(false), nf_(), ef_(std::move(ef)), df_(std::move(df)) {}
  constexpr many(NF nf, EF ef, DF df = DF{})
      : done_(false), nf_(std::move(nf)), ef_(std::move(ef)), df_(std::move(df))
  {}

  PUSHMI_TEMPLATE (class V)
    (requires Invocable<NF&, V>)
  void next(V&& v) {
    if (done_) {return;}
    nf_((V&&) v);
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

template <PUSHMI_TYPE_CONSTRAINT(Receiver) Data, class DNF, class DEF, class DDF>
#if __cpp_concepts
  requires Invocable<DDF&, Data&>
#endif
class many<Data, DNF, DEF, DDF> {
  bool done_ = false;
  Data data_;
  DNF nf_;
  DEF ef_;
  DDF df_;

  static_assert(
      !detail::is_v<DNF, on_error_fn>,
      "the first parameter is the next implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<DEF, on_next_fn>,
      "the second parameter is the error implementation, but on_next{} was passed");
  static_assert(NothrowInvocable<DEF, Data&, std::exception_ptr>,
      "error function must be noexcept and support std::exception_ptr");

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_receiver<>, is_many<>>>;

  constexpr explicit many(Data d)
      : many(std::move(d), DNF{}, DEF{}, DDF{}) {}
  constexpr many(Data d, DDF df)
      : done_(false), data_(std::move(d)), nf_(), ef_(), df_(df) {}
  constexpr many(Data d, DEF ef, DDF df = DDF{})
      : done_(false), data_(std::move(d)), nf_(), ef_(ef), df_(df) {}
  constexpr many(Data d, DNF nf, DEF ef = DEF{}, DDF df = DDF{})
      : done_(false), data_(std::move(d)), nf_(nf), ef_(ef), df_(df) {}

  Data& data() {return data_;}
  PUSHMI_TEMPLATE(class V)
    (requires Invocable<DNF&, Data&, V>)
  void next(V&& v) {
    if (!done_) {
      nf_(data_, (V&&) v);
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
class many<>
    : public many<ignoreNF, abortEF, ignoreDF> {
public:
  many() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_many
PUSHMI_INLINE_VAR constexpr struct make_many_fn {
  inline auto operator()() const {
    return many<>{};
  }
  PUSHMI_TEMPLATE(class NF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<NF&>)))
  auto operator()(NF nf) const {
    return many<NF, abortEF, ignoreDF>{std::move(nf)};
  }
  template <class... EFN>
  auto operator()(on_error_fn<EFN...> ef) const {
    return many<ignoreNF, on_error_fn<EFN...>, ignoreDF>{std::move(ef)};
  }
  PUSHMI_TEMPLATE(class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
  auto operator()(DF df) const {
    return many<ignoreNF, abortEF, DF>{std::move(df)};
  }
  PUSHMI_TEMPLATE(class NF, class EF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<EF&>)))
  auto operator()(NF nf, EF ef) const {
    return many<NF, EF, ignoreDF>{std::move(nf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class EF, class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
  auto operator()(EF ef, DF df) const {
    return many<ignoreNF, EF, DF>{std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class NF, class EF, class DF)
    (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
  auto operator()(NF nf, EF ef, DF df) const {
    return many<NF, EF, DF>{std::move(nf), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>>))
  auto operator()(Data d) const {
    return many<Data, passDNXF, passDEF, passDDF>{std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DNF&, Data&>)))
  auto operator()(Data d, DNF nf) const {
    return many<Data, DNF, passDEF, passDDF>{std::move(d), std::move(nf)};
  }
  PUSHMI_TEMPLATE(class Data, class... DEFN)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>>))
  auto operator()(Data d, on_error_fn<DEFN...> ef) const {
    return many<Data, passDNXF, on_error_fn<DEFN...>, passDDF>{std::move(d), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DDF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DDF df) const {
    return many<Data, passDNXF, passDEF, DDF>{std::move(d), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF, class DEF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
  auto operator()(Data d, DNF nf, DEF ef) const {
    return many<Data, DNF, DEF, passDDF>{std::move(d), std::move(nf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DEF ef, DDF df) const {
    return many<Data, passDNXF, DEF, DDF>{std::move(d), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DNF nf, DEF ef, DDF df) const {
    return many<Data, DNF, DEF, DDF>{std::move(d), std::move(nf), std::move(ef), std::move(df)};
  }
} const make_many {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
many() -> many<>;

PUSHMI_TEMPLATE(class NF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<NF&>)))
many(NF) -> many<NF, abortEF, ignoreDF>;

template <class... EFN>
many(on_error_fn<EFN...>) -> many<ignoreNF, on_error_fn<EFN...>, ignoreDF>;

PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
many(DF) -> many<ignoreNF, abortEF, DF>;

PUSHMI_TEMPLATE(class NF, class EF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<EF&>)))
many(NF, EF) -> many<NF, EF, ignoreDF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
many(EF, DF) -> many<ignoreNF, EF, DF>;

PUSHMI_TEMPLATE(class NF, class EF, class DF)
  (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
many(NF, EF, DF) -> many<NF, EF, DF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>>))
many(Data d) -> many<Data, passDNXF, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DNF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DNF&, Data&>)))
many(Data d, DNF nf) -> many<Data, DNF, passDEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class... DEFN)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>>))
many(Data d, on_error_fn<DEFN...>) ->
    many<Data, passDNXF, on_error_fn<DEFN...>, passDDF>;

PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
many(Data d, DDF) -> many<Data, passDNXF, passDEF, DDF>;

PUSHMI_TEMPLATE(class Data, class DNF, class DEF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
many(Data d, DNF nf, DEF ef) -> many<Data, DNF, DEF, passDDF>;

PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
many(Data d, DEF, DDF) -> many<Data, passDNXF, DEF, DDF>;

PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data, is_many<>> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
many(Data d, DNF nf, DEF ef, DDF df) -> many<Data, DNF, DEF, DDF>;
#endif

template <class V, class E = std::exception_ptr>
using any_many = many<V, E>;

template<>
struct construct_deduced<many> : make_many_fn {};

// template <class V, class E = std::exception_ptr, class Wrapped>
//     requires ManyReceiver<Wrapped, V, E> && !detail::is_v<Wrapped, none>
// auto erase_cast(Wrapped w) {
//   return many<V, E>{std::move(w)};
// }

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "many.h"


namespace pushmi {
namespace detail {
struct erase_receiver_t {};
} // namespace detail

template <class V, class PV, class PE, class E>
class flow_many<detail::erase_receiver_t, V, PV, PE, E> {
  bool done_ = false;
  bool started_ = false;
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(V)]; // can hold V in-situ
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
    static void s_next(data&, V) {}
    static void s_starting(data&, any_many<PV, PE>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    void (*done_)(data&) = vtable::s_done;
    void (*error_)(data&, E) noexcept = vtable::s_error;
    void (*next_)(data&, V) = vtable::s_next;
    void (*starting_)(data&, any_many<PV, PE>) = vtable::s_starting;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  flow_many(Wrapped obj, std::false_type) : flow_many() {
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
      static void next(data& src, V v) {
        ::pushmi::set_next(*static_cast<Wrapped*>(src.pobj_), std::move(v));
      }
      static void starting(data& src, any_many<PV, PE> up) {
        ::pushmi::set_starting(*static_cast<Wrapped*>(src.pobj_), std::move(up));
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::next, s::starting};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  flow_many(Wrapped obj, std::true_type) noexcept : flow_many() {
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
      static void next(data& src, V v) {
        ::pushmi::set_next(
            *static_cast<Wrapped*>((void*)src.buffer_), std::move(v));
      }
      static void starting(data& src, any_many<PV, PE> up) {
        ::pushmi::set_starting(*static_cast<Wrapped*>((void*)src.buffer_), std::move(up));
      }
    };
    static const vtable vtbl{s::op, s::done, s::error, s::next, s::starting};
    new (data_.buffer_) Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class T, class U = std::decay_t<T>>
  using wrapped_t =
    std::enable_if_t<!std::is_same<U, flow_many>::value, U>;
public:
  using properties = property_set<is_receiver<>, is_flow<>, is_many<>>;

  flow_many() = default;
  flow_many(flow_many&& that) noexcept : flow_many() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires FlowManyReceiver<wrapped_t<Wrapped>, any_many<PV, PE>, V, PV, PE, E>)
  explicit flow_many(Wrapped obj) noexcept(insitu<Wrapped>())
    : flow_many{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~flow_many() {
    vptr_->op_(data_, nullptr);
  }
  flow_many& operator=(flow_many&& that) noexcept {
    this->~flow_many();
    new ((void*)this) flow_many(std::move(that));
    return *this;
  }
  void next(V v) {
    if (!started_) {std::abort();}
    if (done_){ return; }
    vptr_->next_(data_, std::move(v));
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

  void starting(any_many<PE> up) {
    if (started_) {std::abort();}
    started_ = true;
    vptr_->starting_(data_, std::move(up));
  }
};

// Class static definitions:
template <class V, class PV, class PE, class E>
constexpr typename flow_many<detail::erase_receiver_t, V, PV, PE, E>::vtable const
  flow_many<detail::erase_receiver_t, V, PV, PE, E>::noop_;

template <class NF, class EF, class DF, class StrtF>
#if __cpp_concepts
  requires Invocable<DF&>
#endif
class flow_many<NF, EF, DF, StrtF> {
  bool done_ = false;
  bool started_ = false;
  NF nf_;
  EF ef_;
  DF df_;
  StrtF strtf_;

 public:
  using properties = property_set<is_receiver<>, is_flow<>, is_many<>>;

  static_assert(
      !detail::is_v<NF, on_error_fn>,
      "the first parameter is the next implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<EF, on_next_fn>,
      "the second parameter is the error implementation, but on_next{} was passed");

  flow_many() = default;
  constexpr explicit flow_many(NF nf)
      : flow_many(std::move(nf), EF{}, DF{}) {}
  constexpr explicit flow_many(EF ef)
      : flow_many(NF{}, std::move(ef), DF{}) {}
  constexpr explicit flow_many(DF df)
      : flow_many(NF{}, EF{}, std::move(df)) {}
  constexpr flow_many(EF ef, DF df)
      : nf_(), ef_(std::move(ef)), df_(std::move(df)) {}
  constexpr flow_many(
      NF nf,
      EF ef,
      DF df = DF{},
      StrtF strtf = StrtF{})
      : nf_(std::move(nf)),
        ef_(std::move(ef)),
        df_(std::move(df)),
        strtf_(std::move(strtf)) {}
  PUSHMI_TEMPLATE (class V)
    (requires Invocable<NF&, V>)
  void next(V&& v) {
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
    class DNF,
    class DEF,
    class DDF,
    class DStrtF>
#if __cpp_concepts
  requires Invocable<DDF&, Data&>
#endif
class flow_many<Data, DNF, DEF, DDF, DStrtF> {
  bool done_ = false;
  bool started_ = false;
  Data data_;
  DNF nf_;
  DEF ef_;
  DDF df_;
  DStrtF strtf_;

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_receiver<>, is_flow<>, is_many<>>>;

  static_assert(
      !detail::is_v<DNF, on_error_fn>,
      "the first parameter is the next implementation, but on_error{} was passed");
  static_assert(
      !detail::is_v<DEF, on_next_fn>,
      "the second parameter is the error implementation, but on_next{} was passed");

  constexpr explicit flow_many(Data d)
      : flow_many(std::move(d), DNF{}, DEF{}, DDF{}) {}
  constexpr flow_many(Data d, DDF df)
      : data_(std::move(d)), nf_(), ef_(), df_(df) {}
  constexpr flow_many(Data d, DEF ef, DDF df = DDF{})
      : data_(std::move(d)), nf_(), ef_(ef), df_(df) {}
  constexpr flow_many(
      Data d,
      DNF nf,
      DEF ef = DEF{},
      DDF df = DDF{},
      DStrtF strtf = DStrtF{})
      : data_(std::move(d)),
        nf_(nf),
        ef_(ef),
        df_(df),
        strtf_(std::move(strtf)) {}
  PUSHMI_TEMPLATE (class V)
    (requires Invocable<DNF&, Data&, V>)
  void next(V&& v) {
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
class flow_many<>
    : public flow_many<ignoreNF, abortEF, ignoreDF, ignoreStrtF> {
};

// TODO winnow down the number of make_flow_many overloads and deduction
// guides here, as was done for make_many.

////////////////////////////////////////////////////////////////////////////////
// make_flow_many
PUSHMI_INLINE_VAR constexpr struct make_flow_many_fn {
  inline auto operator()() const {
    return flow_many<>{};
  }
  PUSHMI_TEMPLATE (class NF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<NF&>)))
  auto operator()(NF nf) const {
    return flow_many<NF, abortEF, ignoreDF, ignoreStrtF>{
      std::move(nf)};
  }
  template <class... EFN>
  auto operator()(on_error_fn<EFN...> ef) const {
    return flow_many<ignoreNF, on_error_fn<EFN...>, ignoreDF, ignoreStrtF>{
      std::move(ef)};
  }
  PUSHMI_TEMPLATE(class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
  auto operator()(DF df) const {
    return flow_many<ignoreNF, abortEF, DF, ignoreStrtF>{std::move(df)};
  }
  PUSHMI_TEMPLATE (class NF, class EF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<EF&>)))
  auto operator()(NF nf, EF ef) const {
    return flow_many<NF, EF, ignoreDF, ignoreStrtF>{std::move(nf),
      std::move(ef)};
  }
  PUSHMI_TEMPLATE(class EF, class DF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
  auto operator()(EF ef, DF df) const {
    return flow_many<ignoreNF, EF, DF, ignoreStrtF>{std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE (class NF, class EF, class DF)
    (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
  auto operator()(NF nf, EF ef, DF df) const {
    return flow_many<NF, EF, DF, ignoreStrtF>{std::move(nf),
      std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE (class NF, class EF, class DF, class StrtF)
    (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
  auto operator()(NF nf, EF ef, DF df, StrtF strtf) const {
    return flow_many<NF, EF, DF, StrtF>{std::move(nf), std::move(ef),
      std::move(df), std::move(strtf)};
  }
  PUSHMI_TEMPLATE(class Data)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data>))
  auto operator()(Data d) const {
    return flow_many<Data, passDNXF, passDEF, passDDF, passDStrtF>{
        std::move(d)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DNF&, Data&>)))
  auto operator()(Data d, DNF nf) const {
    return flow_many<Data, DNF, passDEF, passDDF, passDStrtF>{
      std::move(d), std::move(nf)};
  }
  PUSHMI_TEMPLATE(class Data, class... DEFN)
    (requires PUSHMI_EXP(lazy::Receiver<Data>))
  auto operator()(Data d, on_error_fn<DEFN...> ef) const {
    return flow_many<Data, passDNXF, on_error_fn<DEFN...>, passDDF, passDStrtF>{
      std::move(d), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DDF)
    (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DDF df) const {
    return flow_many<Data, passDNXF, passDEF, DDF, passDStrtF>{
      std::move(d), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF, class DEF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
  auto operator()(Data d, DNF nf, DEF ef) const {
    return flow_many<Data, DNF, DEF, passDDF, passDStrtF>{std::move(d), std::move(nf), std::move(ef)};
  }
  PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DEF ef, DDF df) const {
    return flow_many<Data, passDNXF, DEF, DDF, passDStrtF>{
      std::move(d), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DNF nf, DEF ef, DDF df) const {
    return flow_many<Data, DNF, DEF, DDF, passDStrtF>{std::move(d),
      std::move(nf), std::move(ef), std::move(df)};
  }
  PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF, class DStrtF)
    (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
  auto operator()(Data d, DNF nf, DEF ef, DDF df, DStrtF strtf) const {
    return flow_many<Data, DNF, DEF, DDF, DStrtF>{std::move(d),
      std::move(nf), std::move(ef), std::move(df), std::move(strtf)};
  }
} const make_flow_many {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
flow_many() -> flow_many<>;

PUSHMI_TEMPLATE(class NF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<NF&>)))
flow_many(NF) ->
  flow_many<NF, abortEF, ignoreDF, ignoreStrtF>;

template <class... EFN>
flow_many(on_error_fn<EFN...>) ->
  flow_many<ignoreNF, on_error_fn<EFN...>, ignoreDF, ignoreStrtF>;

PUSHMI_TEMPLATE(class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<DF>)))
flow_many(DF) ->
  flow_many<ignoreNF, abortEF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class NF, class EF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF> PUSHMI_AND not lazy::Invocable<EF&>)))
flow_many(NF, EF) ->
  flow_many<NF, EF, ignoreDF, ignoreStrtF>;

PUSHMI_TEMPLATE(class EF, class DF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<EF>)))
flow_many(EF, DF) ->
  flow_many<ignoreNF, EF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class NF, class EF, class DF)
  (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
flow_many(NF, EF, DF) ->
  flow_many<NF, EF, DF, ignoreStrtF>;

PUSHMI_TEMPLATE(class NF, class EF, class DF, class StrtF)
  (requires PUSHMI_EXP(lazy::Invocable<DF&> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Receiver<NF>)))
flow_many(NF, EF, DF, StrtF) ->
  flow_many<NF, EF, DF, StrtF>;

PUSHMI_TEMPLATE(class Data)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data>))
flow_many(Data d) ->
  flow_many<Data, passDNXF, passDEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DNF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DNF&, Data&>)))
flow_many(Data d, DNF nf) ->
  flow_many<Data, DNF, passDEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class... DEFN)
  (requires PUSHMI_EXP(lazy::Receiver<Data>))
flow_many(Data d, on_error_fn<DEFN...>) ->
  flow_many<Data, passDNXF, on_error_fn<DEFN...>, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DDF)
  (requires PUSHMI_EXP(lazy::True<> PUSHMI_AND lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
flow_many(Data d, DDF) ->
    flow_many<Data, passDNXF, passDEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DNF, class DEF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_BROKEN_SUBSUMPTION(PUSHMI_AND not lazy::Invocable<DEF&, Data&>)))
flow_many(Data d, DNF nf, DEF ef) ->
  flow_many<Data, DNF, DEF, passDDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
flow_many(Data d, DEF, DDF) ->
  flow_many<Data, passDNXF, DEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&>))
flow_many(Data d, DNF nf, DEF ef, DDF df) ->
  flow_many<Data, DNF, DEF, DDF, passDStrtF>;

PUSHMI_TEMPLATE(class Data, class DNF, class DEF, class DDF, class DStrtF)
  (requires PUSHMI_EXP(lazy::Receiver<Data> PUSHMI_AND lazy::Flow<Data> PUSHMI_AND lazy::Invocable<DDF&, Data&> ))
flow_many(Data d, DNF nf, DEF ef, DDF df, DStrtF strtf) ->
  flow_many<Data, DNF, DEF, DDF, DStrtF>;
#endif

template <class V, class PV = std::ptrdiff_t, class PE = std::exception_ptr, class E = PE>
using any_flow_many = flow_many<detail::erase_receiver_t, V, PV, PE, E>;

template<>
struct construct_deduced<flow_many> : make_flow_many_fn {};

// template <class V, class PE = std::exception_ptr, class E = PE, class Wrapped>
//     requires FlowManyReceiver<Wrapped, V, PE, E> && !detail::is_v<Wrapped, many> &&
//     !detail::is_v<Wrapped, std::promise>
//     auto erase_cast(Wrapped w) {
//   return flow_many<V, PE, E>{std::move(w)};
// }

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include <chrono>
//#include <functional>
//#include "single.h"

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
    // (requires SenderTo<wrapped_t<Wrapped>, single<This, E>>)
  any_executor_ref(Wrapped& w) {
    // This can't be a requirement because it asks if submit(w, single<T,E>)
    // is well-formed (where T is an alias for any_executor_ref). If w
    // has a submit that is constrained with SingleReceiver<single<T, E>, T'&, E'>, that
    // will ask whether value(single<T,E>, T'&) is well-formed. And *that* will
    // ask whether T'& is convertible to T. That brings us right back to this
    // constructor. Constraint recursion!
    static_assert(
      SenderTo<Wrapped, single<This, E>>,
      "Expecting to be passed a Sender that can send to a SingleReceiver"
      " that accpets a value of type This and an error of type E");
    struct s {
      static void submit(void* pobj, void* s) {
        return ::pushmi::submit(
          *static_cast<Wrapped*>(pobj),
          std::move(*static_cast<single<This, E>*>(s)));
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
    //   ConvertibleTo<SingleReceiver, any_single<This, E>>,
    //   "requires any_single<any_executor_ref<E, TP>, E>");
    any_single<This, E> s{(SingleReceiver&&) sa};
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
  any_single_sender<any_executor_ref<E>, E>;

template<class T, class E>
using not_any_executor =
  std::enable_if_t<
    !std::is_base_of<any_executor_base<E>, std::decay_t<T>>::value,
    std::decay_t<T>>;
} // namespace detail

template <class E>
struct any_executor : detail::any_executor_base<E> {
  constexpr any_executor() = default;
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
      single<any_executor_ref<E>, E>>)
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
      single<
          any_executor_ref<
              std::exception_ptr>,
          std::exception_ptr>>)
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
    // (requires ConstrainedSenderTo<wrapped_t<Wrapped>, single<This, E>>)
  any_constrained_executor_ref(Wrapped& w) {
    // This can't be a requirement because it asks if submit(w, top(w), single<T,E>)
    // is well-formed (where T is an alias for any_constrained_executor_ref). If w
    // has a submit that is constrained with SingleReceiver<single<T, E>, T'&, E'>, that
    // will ask whether value(single<T,E>, T'&) is well-formed. And *that* will
    // ask whether T'& is convertible to T. That brings us right back to this
    // constructor. Constraint recursion!
    static_assert(
      ConstrainedSenderTo<Wrapped, single<This, E>>,
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
          std::move(*static_cast<single<This, E>*>(s)));
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
    //   ConvertibleTo<SingleReceiver, any_single<This, E>>,
    //   "requires any_single<any_constrained_executor_ref<E, TP>, E>");
    any_single<This, E> s{(SingleReceiver&&) sa};
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
  any_constrained_single_sender<any_constrained_executor_ref<E, CV>, E, CV>;

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
      single<any_constrained_executor_ref<E, constraint_t<Wrapped>>, E>>)
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
      single<
          any_constrained_executor_ref<
              std::exception_ptr,
              constraint_t<Wrapped>>,
          std::exception_ptr>>)
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
    // (requires TimeSenderTo<wrapped_t<Wrapped>, single<This, E>>)
  any_time_executor_ref(Wrapped& w) {
    // This can't be a requirement because it asks if submit(w, now(w), single<T,E>)
    // is well-formed (where T is an alias for any_time_executor_ref). If w
    // has a submit that is constrained with SingleReceiver<single<T, E>, T'&, E'>, that
    // will ask whether value(single<T,E>, T'&) is well-formed. And *that* will
    // ask whether T'& is convertible to T. That brings us right back to this
    // constructor. Constraint recursion!
    static_assert(
      TimeSenderTo<Wrapped, single<This, E>>,
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
          std::move(*static_cast<single<This, E>*>(s)));
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
    //   ConvertibleTo<SingleReceiver, any_single<This, E>>,
    //   "requires any_single<any_time_executor_ref<E, TP>, E>");
    any_single<This, E> s{(SingleReceiver&&) sa};
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
  any_time_single_sender<any_time_executor_ref<E, TP>, E, TP>;

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
      single<any_time_executor_ref<E, time_point_t<Wrapped>>, E>>)
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
      single<
          any_time_executor_ref<
              std::exception_ptr,
              time_point_t<Wrapped>>,
          std::exception_ptr>>)
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
    using properties = property_set<is_constrained<>, is_executor<>, is_single<>>;

    std::ptrdiff_t top() {
      return 0;
    }
    auto executor() { return *this; }
    PUSHMI_TEMPLATE(class CV, class Out)
      (requires Regular<CV> && Receiver<Out, is_single<>>)
    void submit(CV, Out out) {
      ::pushmi::set_value(std::move(out), *this);
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
    using properties = property_set<is_time<>, is_executor<>, is_single<>>;

    auto top() {
      return std::chrono::system_clock::now();
    }
    auto executor() { return *this; }
    PUSHMI_TEMPLATE(class TP, class Out)
      (requires Regular<TP> && Receiver<Out, is_single<>>)
    void submit(TP tp, Out out) {
      std::this_thread::sleep_until(tp);
      ::pushmi::set_value(std::move(out), *this);
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
    using properties = property_set<is_sender<>, is_executor<>, is_single<>>;

    auto executor() { return *this; }
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out, is_single<>>)
    void submit(Out out) {
      ::pushmi::set_value(std::move(out), *this);
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
  using properties = property_set<is_sender<>, is_executor<>, is_single<>>;

  delegator executor() { return {}; }
  PUSHMI_TEMPLATE (class SingleReceiver)
    (requires Receiver<remove_cvref_t<SingleReceiver>, is_single<>>)
  void submit(SingleReceiver&& what) {
    trampoline<E>::submit(
        ownordelegate, std::forward<SingleReceiver>(what));
  }
};

template <class E = std::exception_ptr>
class nester : _pipeable_sender_ {
 public:
  using properties = property_set<is_sender<>, is_executor<>, is_single<>>;

  nester executor() { return {}; }
  template <class SingleReceiver>
  void submit(SingleReceiver&& what) {
    trampoline<E>::submit(ownornest, std::forward<SingleReceiver>(what));
  }
};

template <class E>
class trampoline {
 private:
  using error_type = std::decay_t<E>;
  using work_type =
     any_single<any_executor_ref<error_type>, error_type>;
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
      any_executor_ref<error_type> anythis{that};
      ::pushmi::set_value(what, anythis);
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
  using properties = property_set<is_sender<>, is_executor<>, is_single<>>;

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

//#include "single.h"
//#include "executor.h"
//#include "inline.h"

namespace pushmi {

template <class V, class E, class CV>
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
    static void s_submit(data&, CV, single<V, E>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    CV (*top_)(data&) = vtable::s_top;
    any_constrained_executor<E, CV> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, CV, single<V, E>) = vtable::s_submit;
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
      static void submit(data& src, CV at, single<V, E> out) {
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
      static void submit(data& src, CV cv, single<V, E> out) {
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
    (requires ConstrainedSenderTo<wrapped_t<Wrapped>, single<V, E>>)
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
  void submit(CV at, single<V, E> out) {
    vptr_->submit_(data_, std::move(at), std::move(out));
  }
};

// Class static definitions:
template <class V, class E, class CV>
constexpr typename any_constrained_single_sender<V, E, CV>::vtable const
    any_constrained_single_sender<V, E, CV>::noop_;

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
    (requires Regular<CV> && Receiver<Out, is_single<>> &&
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
    (requires Regular<CV> && Receiver<Out, is_single<>> &&
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

// template <
//     class V,
//     class E = std::exception_ptr,
//     class CV = std::chrono::system_clock::time_point,
//     ConstrainedSenderTo<single<V, E>, is_single<>> Wrapped>
// auto erase_cast(Wrapped w) {
//   return constrained_single_sender<V, E>{std::move(w)};
// }

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "single.h"
//#include "executor.h"
//#include "inline.h"
//#include "constrained_single_sender.h"

namespace pushmi {


template <class V, class E, class TP>
class any_time_single_sender : public any_constrained_single_sender<V, E, TP> {
public:
  using properties = property_set<is_time<>, is_single<>>;
  constexpr any_time_single_sender() = default;
  template<class T>
  constexpr explicit any_time_single_sender(T t)
      : any_constrained_single_sender<V, E, TP>(std::move(t)) {}
  template<class T0, class T1, class... TN>
  constexpr any_time_single_sender(T0 t0, T1 t1, TN... tn)
      : any_constrained_single_sender<V, E, TP>(std::move(t0), std::move(t1), std::move(tn)...) {}

  any_time_executor<E, TP> executor() {
    return any_time_executor<E, TP>{any_constrained_single_sender<V, E, TP>::executor()};
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

  time_heap_item(time_point at, any_single<any_time_executor_ref<E, TP>, E> out) :
    when(std::move(at)), what(std::move(out)) {}

  time_point when;
  any_single<any_time_executor_ref<E, TP>, E> what;
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
    auto subEx = time_source_executor<E, TP, NF, Executor>{s, shared_from_that()};
    while (!this->heap_.empty() && this->heap_.top().when <= start) {
      auto item{std::move(this->top())};
      this->heap_.pop();
      guard.unlock();
      std::this_thread::sleep_until(item.when);
      ::pushmi::set_value(item.what, subEx);
      guard.lock();
      // allows set_value to queue nested items
      --s->items_;
    }
    this->dispatching_ = false;

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
      } else {
        // add back to pending_ to get the remaining items dispatched
        s->pending_.push_back(this->shared_from_this());
        this->pending_ = true;
        if (this->heap_.top().when <= s->earliest_) {
          // this is the earliest, tell worker to reset earliest_
          ++s->dirty_;
          s->wake_.notify_one();
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
    auto done = false;
    std::unique_lock<std::mutex> guard{s->lock_};

    if (!this->dispatching_ || this->pending_) {
      std::abort();
    }

    while (!this->heap_.empty()) {
      auto what{std::move(this->top().what)};
      this->heap_.pop();
      --s->items_;
      guard.unlock();
      ::pushmi::set_done(what);
      guard.lock();
    }
    this->dispatching_ = false;
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
  using properties = property_set<is_receiver<>, is_single<>>;
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
  using properties = property_set<is_time<>, is_executor<>, is_single<>>;

  time_source_executor(
    std::shared_ptr<time_source_shared<E, time_point>> source,
    std::shared_ptr<time_source_queue<E, time_point, NF, Executor>> queue) :
    source_(std::move(source)), queue_(std::move(queue)) {}

  auto top() { return queue_->nf_(); }
  auto executor() { return *this; }

  PUSHMI_TEMPLATE(class TPA, class Out)
    (requires Regular<TPA> && Receiver<Out, is_single<>>)
  void submit(TPA tp, Out out) {
    // queue for later
    source_->insert(queue_, time_heap_item<E, TP>{tp, any_single<any_time_executor_ref<E, TP>, E>{std::move(out)}});
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
    (requires Invocable<ExecutorFactory&> && Executor<invoke_result_t<ExecutorFactory&>>)
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

//#include "none.h"
//#include "executor.h"
//#include "trampoline.h"

namespace pushmi {
namespace detail {
struct erase_sender_t {};
} // namespace detail

template <class E>
class sender<detail::erase_sender_t, E> {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(std::promise<void>)]; // can hold a void promise in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, any_none<E>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, any_none<E>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  sender(Wrapped obj, std::false_type) : sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>(src.pobj_))};
      }
      static void submit(data& src, any_none<E> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  sender(Wrapped obj, std::true_type) noexcept : sender() {
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
      static void submit(data& src, any_none<E> out) {
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
    std::enable_if_t<!std::is_same<U, sender>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_none<>>;

  sender() = default;
  sender(sender&& that) noexcept : sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires SenderTo<wrapped_t<Wrapped>, any_none<E>, is_none<>>)
  explicit sender(Wrapped obj) noexcept(insitu<Wrapped>())
    : sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~sender() {
    vptr_->op_(data_, nullptr);
  }
  sender& operator=(sender&& that) noexcept {
    this->~sender();
    new ((void*)this) sender(std::move(that));
    return *this;
  }
  any_executor<E> executor() {
    return vptr_->executor_(data_);
  }
  void submit(any_none<E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class E>
constexpr typename sender<detail::erase_sender_t, E>::vtable const
    sender<detail::erase_sender_t, E>::noop_;

template <class SF, class EXF>
class sender<SF, EXF> {
  SF sf_;
  EXF exf_;

 public:
  using properties = property_set<is_sender<>, is_none<>>;

  constexpr sender() = default;
  constexpr explicit sender(SF sf) : sf_(std::move(sf)) {}
  constexpr sender(SF sf, EXF exf) : sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(); }
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out, is_none<>> && Invocable<SF&, Out>)
  void submit(Out out) {
    sf_(std::move(out));
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender<is_none<>>) Data, class DSF, class DEXF>
class sender<Data, DSF, DEXF> {
  Data data_;
  DSF sf_;
  DEXF exf_;
    static_assert(Sender<Data, is_none<>>, "The Data template parameter "
    "must satisfy the Sender concept.");

 public:
  using properties = property_set_insert_t<properties_t<Data>, property_set<is_sender<>, is_none<>>>;

  constexpr sender() = default;
  constexpr explicit sender(Data data)
      : data_(std::move(data)) {}
  constexpr sender(Data data, DSF sf)
      : data_(std::move(data)), sf_(std::move(sf)) {}
  constexpr sender(Data data, DSF sf, DEXF exf)
      : data_(std::move(data)), sf_(std::move(sf)), exf_(std::move(exf)) {}

  auto executor() { return exf_(data_); }
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out, is_none<>> && Invocable<DSF&, Data&, Out>)
  void submit(Out out) {
    sf_(data_, std::move(out));
  }
};

template <>
class sender<>
    : public sender<ignoreSF, trampolineEXF> {
public:
  sender() = default;
};

////////////////////////////////////////////////////////////////////////////////
// make_sender
PUSHMI_INLINE_VAR constexpr struct make_sender_fn {
  inline auto operator()() const {
    return sender<ignoreSF, trampolineEXF>{};
  }
  PUSHMI_TEMPLATE(class SF)
    (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf) const {
    return sender<SF, trampolineEXF>{std::move(sf)};
  }
  PUSHMI_TEMPLATE(class SF, class EXF)
    (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
  auto operator()(SF sf, EXF exf) const {
    return sender<SF, EXF>{std::move(sf), std::move(exf)};
  }
  PUSHMI_TEMPLATE(class Wrapped)
    (requires Sender<Wrapped, is_none<>>)
  auto operator()(Wrapped w) const {
    return sender<detail::erase_sender_t, std::exception_ptr>{std::move(w)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF)
    (requires Sender<Data, is_none<>>)
  auto operator()(Data data, DSF sf) const {
    return sender<Data, DSF, passDEXF>{std::move(data), std::move(sf)};
  }
  PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
    (requires Sender<Data, is_none<>>)
  auto operator()(Data data, DSF sf, DEXF exf) const {
    return sender<Data, DSF, DEXF>{std::move(data), std::move(sf), std::move(exf)};
  }
} const make_sender {};

////////////////////////////////////////////////////////////////////////////////
// deduction guides
#if __cpp_deduction_guides >= 201703
sender() -> sender<ignoreSF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF)
  (requires True<> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
sender(SF) -> sender<SF, trampolineEXF>;

PUSHMI_TEMPLATE(class SF, class EXF)
  (requires True<> && Invocable<EXF&> PUSHMI_BROKEN_SUBSUMPTION(&& not Sender<SF>))
sender(SF, EXF) -> sender<SF, EXF>;

PUSHMI_TEMPLATE(class Wrapped)
  (requires Sender<Wrapped, is_none<>>)
sender(Wrapped) ->
    sender<detail::erase_sender_t, std::exception_ptr>;

PUSHMI_TEMPLATE(class Data, class DSF)
  (requires Sender<Data, is_none<>>)
sender(Data, DSF) -> sender<Data, DSF, passDEXF>;

PUSHMI_TEMPLATE(class Data, class DSF, class DEXF)
  (requires Sender<Data, is_none<>>)
sender(Data, DSF, DEXF) -> sender<Data, DSF, DEXF>;
#endif

template <class E = std::exception_ptr>
using any_sender = sender<detail::erase_sender_t, E>;

template<>
struct construct_deduced<sender> : make_sender_fn {};

// template <SenderTo<any_none<std::exception_ptr>, is_none<>> Wrapped>
// auto erase_cast(Wrapped w) {
//   return sender<detail::erase_sender_t, std::exception_ptr>{std::move(w)};
// }
//
// template <class E, SenderTo<any_none<E>, is_none<>> Wrapped>
//   requires Same<is_none<>, properties_t<Wrapped>>
// auto erase_cast(Wrapped w) {
//   return sender<detail::erase_sender_t, E>{std::move(w)};
// }

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "single.h"
//#include "executor.h"
//#include "trampoline.h"

namespace pushmi {

template <class V, class E>
class any_single_sender {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(V)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, single<V, E>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, single<V, E>) = vtable::s_submit;
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
      static void submit(data& src, single<V, E> out) {
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
      static void submit(data& src, single<V, E> out) {
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
    (requires SenderTo<wrapped_t<Wrapped>, single<V, E>, is_single<>>)
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
    vptr_->executor_(data_);
  }
  void submit(single<V, E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class V, class E>
constexpr typename any_single_sender<V, E>::vtable const
  any_single_sender<V, E>::noop_;

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
    (requires PUSHMI_EXP(lazy::Receiver<Out, is_single<>> PUSHMI_AND lazy::Invocable<SF&, Out>))
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
    (requires PUSHMI_EXP(lazy::Receiver<Out, is_single<>> PUSHMI_AND
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

// template <
//     class V,
//     class E = std::exception_ptr,
//     SenderTo<single<V, E>, is_single<>> Wrapped>
// auto erase_cast(Wrapped w) {
//   return single_sender<V, E>{std::move(w)};
// }

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "flow_single.h"
//#include "executor.h"
//#include "trampoline.h"

namespace pushmi {

template <class V, class PE = std::exception_ptr, class E = PE>
class any_flow_single_sender {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(V)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, flow_single<V, PE, E>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, flow_single<V, PE, E>) = vtable::s_submit;
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
      static void submit(data& src, flow_single<V, PE, E> out) {
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
      static void submit(data& src, flow_single<V, PE, E> out) {
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
  void submit(flow_single<V, PE, E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class V, class PE, class E>
constexpr typename any_flow_single_sender<V, PE, E>::vtable const
    any_flow_single_sender<V, PE, E>::noop_;

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
    (requires Receiver<Out, is_single<>, is_flow<>> && Invocable<SF&, Out>)
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
    (requires PUSHMI_EXP(lazy::Receiver<Out, is_single<>, is_flow<>> PUSHMI_AND
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

// // TODO constrain me
// template <class V, class E = std::exception_ptr, Sender Wrapped>
// auto erase_cast(Wrapped w) {
//   return flow_single_sender<V, E>{std::move(w)};
// }

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "many.h"
//#include "executor.h"
//#include "trampoline.h"

namespace pushmi {

template <class V, class E = std::exception_ptr>
class any_many_sender {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(V)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, many<V, E>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, many<V, E>) = vtable::s_submit;
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
      static void submit(data& src, many<V, E> out) {
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
      static void submit(data& src, many<V, E> out) {
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
    (requires SenderTo<wrapped_t<Wrapped>, many<V, E>, is_many<>>)
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
  void submit(many<V, E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class V, class E>
constexpr typename any_many_sender<V, E>::vtable const
  any_many_sender<V, E>::noop_;

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
    (requires PUSHMI_EXP(lazy::Receiver<Out, is_many<>> PUSHMI_AND lazy::Invocable<SF&, Out>))
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
    (requires PUSHMI_EXP(lazy::Receiver<Out, is_many<>> PUSHMI_AND
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

// template <
//     class V,
//     class E = std::exception_ptr,
//     SenderTo<many<V, E>, is_many<>> Wrapped>
// auto erase_cast(Wrapped w) {
//   return many_sender<V, E>{std::move(w)};
// }

} // namespace pushmi
//#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

//#include "flow_many.h"
//#include "executor.h"
//#include "trampoline.h"

namespace pushmi {

template <class V, class PV, class PE, class E>
class flow_many_sender<V, PV, PE, E> {
  union data {
    void* pobj_ = nullptr;
    char buffer_[sizeof(V)]; // can hold a V in-situ
  } data_{};
  template <class Wrapped>
  static constexpr bool insitu() {
    return sizeof(Wrapped) <= sizeof(data::buffer_) &&
        std::is_nothrow_move_constructible<Wrapped>::value;
  }
  struct vtable {
    static void s_op(data&, data*) {}
    static any_executor<E> s_executor(data&) { return {}; }
    static void s_submit(data&, any_flow_many<V, PV, PE, E>) {}
    void (*op_)(data&, data*) = vtable::s_op;
    any_executor<E> (*executor_)(data&) = vtable::s_executor;
    void (*submit_)(data&, any_flow_many<V, PV, PE, E>) = vtable::s_submit;
  };
  static constexpr vtable const noop_ {};
  vtable const* vptr_ = &noop_;
  template <class Wrapped>
  flow_many_sender(Wrapped obj, std::false_type) : flow_many_sender() {
    struct s {
      static void op(data& src, data* dst) {
        if (dst)
          dst->pobj_ = std::exchange(src.pobj_, nullptr);
        delete static_cast<Wrapped const*>(src.pobj_);
      }
      static any_executor<E> executor(data& src) {
        return any_executor<E>{::pushmi::executor(*static_cast<Wrapped*>(src.pobj_))};
      }
      static void submit(data& src, any_flow_many<V, PV, PE, E> out) {
        ::pushmi::submit(*static_cast<Wrapped*>(src.pobj_), std::move(out));
      }
    };
    static const vtable vtbl{s::op, s::executor, s::submit};
    data_.pobj_ = new Wrapped(std::move(obj));
    vptr_ = &vtbl;
  }
  template <class Wrapped>
  flow_many_sender(Wrapped obj, std::true_type) noexcept
    : flow_many_sender() {
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
      static void submit(data& src, any_flow_many<V, PV, PE, E> out) {
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
    std::enable_if_t<!std::is_same<U, flow_many_sender>::value, U>;
 public:
  using properties = property_set<is_sender<>, is_flow<>, is_many<>>;

  flow_many_sender() = default;
  flow_many_sender(flow_many_sender&& that) noexcept
      : flow_many_sender() {
    that.vptr_->op_(that.data_, &data_);
    std::swap(that.vptr_, vptr_);
  }
  PUSHMI_TEMPLATE (class Wrapped)
    (requires FlowSender<wrapped_t<Wrapped>, is_many<>>)
  explicit flow_many_sender(Wrapped obj) noexcept(insitu<Wrapped>())
    : flow_many_sender{std::move(obj), bool_<insitu<Wrapped>()>{}} {}
  ~flow_many_sender() {
    vptr_->op_(data_, nullptr);
  }
  flow_many_sender& operator=(flow_many_sender&& that) noexcept {
    this->~flow_many_sender();
    new ((void*)this) flow_many_sender(std::move(that));
    return *this;
  }
  any_executor<E> executor() {
    return vptr_->executor_(data_);
  }
  void submit(any_flow_many<V, PV, PE, E> out) {
    vptr_->submit_(data_, std::move(out));
  }
};

// Class static definitions:
template <class V, class PV, class PE, class E>
constexpr typename flow_many_sender<V, PV, PE, E>::vtable const
    flow_many_sender<V, PV, PE, E>::noop_;

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
    (requires Receiver<Out, is_many<>, is_flow<>> && Invocable<SF&, Out>)
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
    (requires PUSHMI_EXP(lazy::Receiver<Out, is_many<>, is_flow<>> PUSHMI_AND
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

template <class V, class PV = std::ptrdiff_t, class PE = std::exception_ptr, class E = PE>
using any_flow_many_sender = flow_many_sender<V, PV, PE, E>;

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
//#include "../single.h"
//#include "../sender.h"
//#include "../single_sender.h"
//#include "../many.h"
//#include "../many_sender.h"
//#include "../time_single_sender.h"
//#include "../flow_single.h"
//#include "../flow_single_sender.h"
//#include "../flow_many.h"
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
struct make_receiver<is_none<>> : construct_deduced<none> {};
template <>
struct make_receiver<is_single<>> : construct_deduced<single> {};
template <>
struct make_receiver<is_many<>> : construct_deduced<many> {};
template <>
struct make_receiver<is_single<>, true> : construct_deduced<flow_single> {};
template <>
struct make_receiver<is_many<>, true> : construct_deduced<flow_many> {};

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
      Invocable<MakeReceiver, std::tuple<Ts...>> &&
      Invocable<This, pushmi::invoke_result_t<MakeReceiver, std::tuple<Ts...>>, Fns...>)
  auto operator()(std::tuple<Ts...> args, Fns...fns) const {
    return This()(This()(std::move(args)), std::move(fns)...);
  }
  PUSHMI_TEMPLATE(class Out, class...Fns)
    (requires Receiver<Out, Cardinality> && And<SemiMovable<Fns>...>)
  auto operator()(Out out, Fns... fns) const {
    return MakeReceiver()(std::move(out), std::move(fns)...);
  }
};

template <PUSHMI_TYPE_CONSTRAINT(Sender) In>
using receiver_from_fn =
    receiver_from_impl<
        property_set_index_t<properties_t<In>, is_silent<>>,
        property_query_v<properties_t<In>, is_flow<>>>;

template <class In, class FN>
struct submit_transform_out_1 {
  FN fn_;
  PUSHMI_TEMPLATE(class Out)
    (requires Receiver<Out>)
  void operator()(In& in, Out out) const {
    ::pushmi::submit(in, fn_(std::move(out)));
  }
};
template <class In, class FN>
struct submit_transform_out_2 {
  FN fn_;
  PUSHMI_TEMPLATE(class TP, class Out)
    (requires Receiver<Out>)
  void operator()(In& in, TP tp, Out out) const {
    ::pushmi::submit(in, tp, fn_(std::move(out)));
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
  PUSHMI_TEMPLATE(class TP, class Out)
    (requires Receiver<Out> && Invocable<const TSDSF&, In&, TP, Out>)
  void operator()(In& in, TP tp, Out out) const {
    tsdsf_(in, tp, std::move(out));
  }
};

PUSHMI_TEMPLATE(class In, class FN)
  (requires Sender<In> && SemiMovable<FN>
    PUSHMI_BROKEN_SUBSUMPTION(&& not TimeSender<In>))
auto submit_transform_out(FN fn) {
  return on_submit(submit_transform_out_1<In, FN>{std::move(fn)});
}

PUSHMI_TEMPLATE(class In, class FN)
  (requires TimeSender<In> && SemiMovable<FN>)
auto submit_transform_out(FN fn){
  return on_submit(submit_transform_out_2<In, FN>{std::move(fn)});
}

PUSHMI_TEMPLATE(class In, class SDSF, class TSDSF)
  (requires Sender<In> && SemiMovable<SDSF> && SemiMovable<TSDSF>
    PUSHMI_BROKEN_SUBSUMPTION(&& not TimeSender<In>))
auto submit_transform_out(SDSF sdsf, TSDSF) {
  return on_submit(submit_transform_out_3<In, SDSF>{std::move(sdsf)});
}

PUSHMI_TEMPLATE(class In, class SDSF, class TSDSF)
  (requires TimeSender<In> && SemiMovable<SDSF> && SemiMovable<TSDSF>)
auto submit_transform_out(SDSF, TSDSF tsdsf) {
  return on_submit(submit_transform_out_4<In, TSDSF>{std::move(tsdsf)});
}

template <class Cardinality, bool IsConstrained = false, bool IsTime = false, bool IsFlow = false>
struct make_sender;
template <>
struct make_sender<is_none<>> : construct_deduced<sender> {};
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
            property_set_index_t<properties_t<In>, is_silent<>>,
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
  PUSHMI_IF_CONSTEXPR_RETURN( ((bool) TimeSenderTo<In, Out, is_single<>>) (
    return TimeSingleSenderRequires;
  ) else (
    PUSHMI_IF_CONSTEXPR_RETURN( ((bool) SenderTo<In, Out, is_single<>>) (
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
  template <class V>
  struct impl {
    V v_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out, is_single<>>)
    void operator()(Out out) {
      ::pushmi::set_value(out, std::move(v_));
    }
  };
public:
  template <class V>
  auto operator()(V&& v) const {
    return impl<std::decay_t<V>>{(V&&) v};
  }
};

struct set_error_fn {
private:
  template <class E>
  struct impl {
    E e_;
    PUSHMI_TEMPLATE(class Out)
      (requires NoneReceiver<Out, E>)
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

struct set_next_fn {
private:
  template <class V>
  struct impl {
    V v_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out, is_many<>>)
    void operator()(Out out) {
      ::pushmi::set_next(out, std::move(v_));
    }
  };
public:
  template <class V>
  auto operator()(V&& v) const {
    return impl<std::decay_t<V>>{(V&&) v};
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
PUSHMI_INLINE_VAR constexpr detail::set_next_fn set_next{};
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
          property_set_index_t<properties_t<In>, is_silent<>>,
          property_query_v<properties_t<In>, is_flow<>>>,
        AN...>;

PUSHMI_CONCEPT_DEF(
  template (class In, class ... AN)
  (concept AutoSenderTo)(In, AN...),
    SenderTo<In, receiver_type_t<In, AN...>>
);
PUSHMI_CONCEPT_DEF(
  template (class In, class ... AN)
  (concept AutoConstrainedSenderTo)(In, AN...),
    ConstrainedSenderTo<In, receiver_type_t<In, AN...>> && not Time<In>
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
      (requires submit_detail::AutoSenderTo<In, AN...>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
      ::pushmi::submit(in, std::move(out));
      return in;
    }
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoConstrainedSenderTo<In, AN...>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
      ::pushmi::submit(in, ::pushmi::top(in), std::move(out));
      return in;
    }
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoTimeSenderTo<In, AN...>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<In>()(std::move(args_))};
      ::pushmi::submit(in, ::pushmi::now(in), std::move(out));
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
    (requires Sender<Exec> && Executor<Exec>)
  struct nested_executor_impl {
    nested_executor_impl(lock_state* state, Exec ex) :
      state_(state),
      ex_(std::move(ex)) {}
    lock_state* state_;
    Exec ex_;

    using properties = properties_t<Exec>;

    auto executor() { return ::pushmi::executor(ex_); }

    PUSHMI_TEMPLATE (class CV, class Out)
      (requires Receiver<Out> && Constrained<Exec>)
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
      try{
        using executor_t = remove_cvref_t<V>;
        auto n = nested_executor_impl<executor_t>{state_, (V&&) v};
        ::pushmi::set_value(out_, any_executor_ref<>{n});
      }
      catch(...) {e = std::current_exception();}
      if(--state_->nested == 0) {
        state_->signaled.notify_all();
      }
      if (e) {std::rethrow_exception(e);}
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
      return nested_executor_impl<Exec>{state, std::move(ex)};
    }
  };
  struct on_value_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE (class Out, class Value)
      (requires Receiver<Out, is_single<>>)
    void operator()(Out out, Value&& v) const {
      using V = remove_cvref_t<Value>;
      ++state_->nested;
      PUSHMI_IF_CONSTEXPR( ((bool)Executor<V>) (
        id(::pushmi::set_value)(out, id(nested_executor_impl_fn{})(state_, id((Value&&) v)));
      ) else (
        id(::pushmi::set_value)(out, id((Value&&) v));
      ))
      std::unique_lock<std::mutex> guard{state_->lock};
      state_->done = true;
      if (--state_->nested == 0){
        state_->signaled.notify_all();
      }
    }
  };
  struct on_next_impl {
    PUSHMI_TEMPLATE (class Out, class Value)
      (requires Receiver<Out, is_many<>>)
    void operator()(Out out, Value&& v) const {
      using V = remove_cvref_t<Value>;
      ::pushmi::set_next(out, (Value&&) v);
    }
  };
  struct on_error_impl {
    lock_state* state_;
    PUSHMI_TEMPLATE(class Out, class E)
      (requires NoneReceiver<Out, E>)
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
  template <bool IsConstrainedSender, bool IsTimeSender, class In>
  struct submit_impl {
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void operator()(In& in, Out out) const {
      PUSHMI_IF_CONSTEXPR( (IsTimeSender) (
        id(::pushmi::submit)(in, id(::pushmi::now)(in), std::move(out));
      ) else (
        PUSHMI_IF_CONSTEXPR( (IsConstrainedSender) (
          id(::pushmi::submit)(in, id(::pushmi::top)(in), std::move(out));
        ) else (
          id(::pushmi::submit)(in, std::move(out));
        ))
      ))
    }
  };
  // TODO - only move, move-only types..
  // if out can be copied, then submit can be called multiple
  // times..
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;

    template <bool IsConstrainedSender, bool IsTimeSender, class In>
    In impl_(In in) {
      lock_state state{};

      auto submit = submit_impl<IsConstrainedSender, IsTimeSender, In>{};
      PUSHMI_IF_CONSTEXPR( ((bool)Many<In>) (
        auto out{::pushmi::detail::receiver_from_fn<In>()(
          std::move(args_),
          on_next(on_next_impl{}),
          on_error(on_error_impl{&state}),
          on_done(on_done_impl{&state})
        )};
        submit(in, std::move(out));
      ) else (
        auto out{::pushmi::detail::receiver_from_fn<In>()(
          std::move(args_),
          on_value(on_value_impl{&state}),
          on_error(on_error_impl{&state}),
          on_done(on_done_impl{&state})
        )};
        submit(in, std::move(out));
      ))

      std::unique_lock<std::mutex> guard{state.lock};
      state.signaled.wait(guard, [&]{
        return state.done && state.nested.load() == 0;
      });
      return in;
    }

    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoSenderTo<In, AN...>)
    In operator()(In in) {
      return this->impl_<false, false>(std::move(in));
    }
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoConstrainedSenderTo<In, AN...>)
    In operator()(In in) {
      return this->impl_<true, false>(std::move(in));
    }
    PUSHMI_TEMPLATE(class In)
      (requires submit_detail::AutoTimeSenderTo<In, AN...>)
    In operator()(In in) {
      return this->impl_<true, true>(std::move(in));
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
    void operator()(T t) const { *result_ = std::move(t); }
  };
  struct on_error_impl {
    std::exception_ptr* ep_;
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
    std::exception_ptr ep_;
    auto out = make_single(
      on_value(on_value_impl{&result_}),
      on_error(on_error_impl{&ep_})
    );
    using Out = decltype(out);
    static_assert(SenderTo<In, Out, is_single<>> ||
        TimeSenderTo<In, Out, is_single<>>,
        "'In' does not deliver value compatible with 'T' to 'Out'");
    blocking_submit_fn{}(std::move(out))(in);
    if (!!ep_) { std::rethrow_exception(ep_); }
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

template<class T, class PS>
struct subject<T, PS> {

  using properties = property_set_insert_t<property_set<is_sender<>, is_single<>>, property_set<property_set_index_t<PS, is_silent<>>>>;

  struct subject_shared {
    bool done_ = false;
    pushmi::detail::opt<T> t_;
    std::exception_ptr ep_;
    std::vector<any_single<T>> receivers_;
    std::mutex lock_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void submit(Out out) {
      std::unique_lock<std::mutex> guard(lock_);
      if (ep_) {::pushmi::set_error(out, ep_); return;}
      if (!!t_) {::pushmi::set_value(out, (const T&)t_); return;}
      if (done_) {::pushmi::set_done(out); return;}
      receivers_.push_back(any_single<T>{out});
    }
    PUSHMI_TEMPLATE(class V)
      (requires SemiMovable<V>)
    void value(V&& v) {
      std::unique_lock<std::mutex> guard(lock_);
      t_ = detail::as_const(v);
      for (auto& out : receivers_) {::pushmi::set_value(out, (V&&) v);}
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

  // need a template overload of none/sender and the rest that stores a 'ptr' with its own lifetime management
  struct subject_receiver {

    using properties = property_set_insert_t<property_set<is_receiver<>, is_single<>>, property_set<property_set_index_t<PS, is_silent<>>>>;

    std::shared_ptr<subject_shared> s;

    PUSHMI_TEMPLATE(class V)
      (requires SemiMovable<V>)
    void value(V&& v) {
      s->value((V&&) v);
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
    void next(V&& v) {
      ::pushmi::set_next(static_cast<Out&>(*this), (V&&) v);
      pull(1);
    }
    PUSHMI_TEMPLATE(class Up)
      (requires ManyReceiver<Up, std::ptrdiff_t>)
    void starting(Up up){
      pull = [up = std::move(up)](std::ptrdiff_t requested) mutable {
        ::pushmi::set_next(up, requested);
      };
      pull(1);
    }
    PUSHMI_TEMPLATE(class Up)
      (requires None<Up> && not Many<Up>)
    void starting(Up up){}
  };
  template <class... AN>
  struct fn {
    std::tuple<AN...> args_;
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In> && Flow<In> && Many<In>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<subset<is_sender<>, property_set_index_t<properties_t<In>, is_silent<>>>>()(std::move(args_))};
      using Out = decltype(out);
      ::pushmi::submit(in, ::pushmi::detail::receiver_from_fn<In>()(Pull<In, Out>{std::move(out)}));
      return in;
    }
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In> && Time<In> && Flow<In> && Many<In>)
    In operator()(In in) {
      auto out{::pushmi::detail::receiver_from_fn<subset<is_sender<>, property_set_index_t<properties_t<In>, is_silent<>>>>()(std::move(args_))};
      using Out = decltype(out);
      ::pushmi::submit(in, ::pushmi::now(in), ::pushmi::detail::receiver_from_fn<In>()(Pull<In, Out>{std::move(out)}));
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

//#include "../sender.h"
//#include "../single_sender.h"
//#include "../detail/functional.h"

namespace pushmi {
namespace detail {
  template <class V>
  struct single_empty_impl {
    PUSHMI_TEMPLATE(class Out)
      (requires SingleReceiver<Out, V>)
    void operator()(Out out) {
      ::pushmi::set_done(out);
    }
  };
  struct empty_impl {
    PUSHMI_TEMPLATE(class Out)
      (requires NoneReceiver<Out>)
    void operator()(Out out) {
      ::pushmi::set_done(out);
    }
  };
}

namespace operators {
template <class V>
auto empty() {
  return make_single_sender(detail::single_empty_impl<V>{});
}

inline auto empty() {
  return make_sender(detail::empty_impl{});
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
  template <class I, class S>
  struct out_impl {
    I begin_;
    S end_;
    PUSHMI_TEMPLATE(class Out)
      (requires ManyReceiver<Out, typename std::iterator_traits<I>::value_type>)
    void operator()(Out out) const {
      auto c = begin_;
      for (; c != end_; ++c) {
        ::pushmi::set_next(out, *c);
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
    return make_many_sender(out_impl<I, S>{begin, end});
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
  using properties = properties_t<many<>>;

  explicit flow_from_up(std::shared_ptr<Producer> p) : p(std::move(p)) {}
  std::shared_ptr<Producer> p;

  void next(ptrdiff_t requested) {
    if (requested < 1) {return;}
    // submit work to exec
    ::pushmi::submit(p->exec,
      make_single([p = p, requested](auto) {
        auto remaining = requested;
        // this loop is structured to work when there is re-entrancy
        // out.next in the loop may call up.next. to handle this the
        // state of p->c must be captured and the remaining and p->c
        // must be changed before out.next is called.
        while (remaining-- > 0 && !p->stop && p->c != p->end) {
          auto i = (p->c)++;
          ::pushmi::set_next(p->out, ::pushmi::detail::as_const(*i));
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
      make_single([p = p](auto) {
        ::pushmi::set_done(p->out);
      }));
  }

  void done() {
    p->stop.store(true);
    ::pushmi::submit(p->exec,
      make_single([p = p](auto) {
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
      (requires ManyReceiver<Out, typename std::iterator_traits<I>::value_type>)
    void operator()(Out out) const {
      using Producer = flow_from_producer<I, S, Out, Exec>;
      auto p = std::make_shared<Producer>(begin_, end_, std::move(out), exec_, false);

      ::pushmi::submit(exec_,
        make_single([p](auto exec) {
          // pass reference for cancellation.
          ::pushmi::set_starting(p->out, make_many(flow_from_up<Producer>{p}));
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
  template <class V>
  struct impl {
    V v_;
    PUSHMI_TEMPLATE (class Out)
      (requires SingleReceiver<Out, V>)
    void operator()(Out out) {
      ::pushmi::set_value(out, std::move(v_));
    }
  };
public:
  PUSHMI_TEMPLATE(class V)
    (requires SemiMovable<V>)
  auto operator()(V v) const {
    return make_single_sender(impl<V>{std::move(v)});
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

//#include "../sender.h"
//#include "submit.h"
//#include "extension_operators.h"

namespace pushmi {
namespace detail {
  template <class E>
  struct error_impl {
    E e_;
    PUSHMI_TEMPLATE(class Out)
      (requires NoneReceiver<Out, E>)
    void operator()(Out out) {
      ::pushmi::set_error(out, std::move(e_));
    }
  };
  template <class V, class E>
  struct single_error_impl {
    E e_;
    PUSHMI_TEMPLATE(class Out)
      (requires SingleReceiver<Out, V, E>)
    void operator()(Out out) {
      ::pushmi::set_error(out, std::move(e_));
    }
  };
}

namespace operators {

PUSHMI_TEMPLATE(class E)
  (requires SemiMovable<E>)
auto error(E e) {
  return make_sender(detail::error_impl<E>{std::move(e)});
}

PUSHMI_TEMPLATE(class V, class E)
  (requires SemiMovable<V> && SemiMovable<E>)
auto error(E e) {
  return make_single_sender(detail::single_error_impl<V, E>{std::move(e)});
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

//#include "../single.h"
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
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void operator()(Out out) {
      auto sender = f_();
      PUSHMI_IF_CONSTEXPR( ((bool)TimeSender<decltype(sender)>) (
        ::pushmi::submit(sender, ::pushmi::now(id(sender)), std::move(out));
      ) else (
        ::pushmi::submit(sender, std::move(out));
      ));
    }
  };
public:
  PUSHMI_TEMPLATE(class F)
    (requires Invocable<F&>)
  auto operator()(F f) const {
    return make_single_sender(impl<F>{std::move(f)});
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
        ::pushmi::make_single(on_value_impl<In, Out>{in, std::move(out)})
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
        ::pushmi::make_single(
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

#if 0
namespace detail {

template <class ExecutorFactory>
class fsdon {
  using executor_factory_type = std::decay_t<ExecutorFactory>;

  executor_factory_type factory_;

  template <class In>
  class start_on {
    using in_type = std::decay_t<In>;

    executor_factory_type factory_;
    in_type in_;

    template <class Out, class Executor>
    class out_on {
      using out_type = std::decay_t<Out>;
      using exec_type = std::decay_t<Executor>;

      template <class Producer>
      struct producer_proxy {
        RefWrapper<Producer> up_;
        std::shared_ptr<std::atomic_bool> stopped_;
        exec_type exec_;

        producer_proxy(
            RefWrapper<Producer> p,
            std::shared_ptr<std::atomic_bool> stopped,
            exec_type exec)
            : up_(std::move(p)),
              stopped_(std::move(stopped)),
              exec_(std::move(exec)) {}

        template <class V>
        void value(V v) {
          auto up = wrap_ref(up_.get());
          exec_ |
              execute([up = std::move(up),
                       v = std::move(v),
                       stopped = std::move(stopped_)](auto) mutable {
                if (*stopped) {
                  return;
                }
                up.get().value(std::move(v));
              });
        }

        template <class E>
        void error(E e) {
          auto up = wrap_ref(up_.get());
          exec_ |
              execute([up = std::move(up),
                       e = std::move(e),
                       stopped = std::move(stopped_)](auto) mutable {
                if (*stopped) {
                  return;
                }
                up.get().error(std::move(e));
              });
        }
      };

      bool done_;
      std::shared_ptr<std::atomic_bool> stopped_;
      out_type out_;
      exec_type exec_;
      AnyNone<> upProxy_;

     public:
      out_on(out_type out, exec_type exec)
          : done_(false),
            stopped_(std::make_shared<std::atomic_bool>(false)),
            out_(std::move(out)),
            exec_(std::move(exec)),
            upProxy_() {}

      template <class T>
      void value(T t) {
        if (done_) {
          return;
        }
        done_ = true;
        out_.value(std::move(t));
      }

      template <class E>
      void error(E e) {
        if (done_) {
          return;
        }
        done_ = true;
        out_.error(std::move(e));
      }

      template <class Producer>
      void starting(RefWrapper<Producer> up) {
        upProxy_ =
            producer_proxy<Producer>{std::move(up), stopped_, std::move(exec_)};
        out_.starting(wrap_ref(upProxy_));
      }
    };

   public:
    start_on(executor_factory_type&& ef, in_type&& in)
        : factory_(std::move(ef)), in_(std::move(in)) {}

    template <class Out>
    auto then(Out out) {
      auto exec = factory_();
      auto myout = out_on<Out, decltype(exec)>{std::move(out), exec};
      exec | execute([in = in_, myout = std::move(myout)](auto) mutable {
        in.then(std::move(myout));
      });
    }
  };

 public:
  explicit fsdon(executor_factory_type&& ef) : factory_(std::move(ef)) {}

  template <class In>
  auto operator()(In in) {
    return start_on<In>{std::move(factory_), std::move(in)};
  }
};

} // namespace detail

namespace fsd {
template <class ExecutorFactory>
auto on(ExecutorFactory factory) {
  return detail::fsdon<ExecutorFactory>{std::move(factory)};
}
} // namespace fsd
#endif

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
//#include "../sender.h"
//#include "../single_sender.h"
//#include "../time_single_sender.h"

namespace pushmi {

namespace detail {

PUSHMI_TEMPLATE(class SideEffects, class Out)
  (requires Receiver<SideEffects> && Receiver<Out>)
struct tap_ {
  SideEffects sideEffects;
  Out out;

  // side effect has no effect on the properties.
  using properties = properties_t<Out>;

  PUSHMI_TEMPLATE(class V, class UV = std::remove_reference_t<V>)
    (requires
      SingleReceiver<SideEffects, const UV> &&
      SingleReceiver<Out, UV>)
  void value(V&& v) {
    ::pushmi::set_value(sideEffects, as_const(v));
    ::pushmi::set_value(out, (V&&) v);
  }
  PUSHMI_TEMPLATE(class V, class UV = std::remove_reference_t<V>)
    (requires
      ManyReceiver<SideEffects, const UV> &&
      ManyReceiver<Out, UV>)
  void next(V&& v) {
    ::pushmi::set_next(sideEffects, as_const(v));
    ::pushmi::set_next(out, (V&&) v);
  }
  PUSHMI_TEMPLATE(class E)
    (requires
      NoneReceiver<SideEffects, const E> &&
      NoneReceiver<Out, E>)
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
      Receiver<tap_<SideEffects, Out>, property_set_index_t<properties_t<Out>, is_silent<>>>)
  auto operator()(SideEffects se, Out out) const {
    return tap_<SideEffects, Out>{std::move(se), std::move(out)};
  }
} const make_tap {};

#if __NVCC__
#define PUSHMI_STATIC_ASSERT(...)
#elif __cpp_if_constexpr >= 201606
#define PUSHMI_STATIC_ASSERT static_assert
#else
#define PUSHMI_STATIC_ASSERT detail::do_assert
inline void do_assert(bool condition, char const*) {
  assert(condition);
}
#endif

struct tap_fn {
private:
  template <class In, class SideEffects>
  static auto impl(In in, SideEffects sideEffects) {
    // PUSHMI_STATIC_ASSERT(
    //   ::pushmi::detail::sender_requires_from<In, SideEffects,
    //     SenderTo<In, SideEffects, is_none<>>,
    //     SenderTo<In, SideEffects, is_single<>>,
    //     TimeSenderTo<In, SideEffects, is_single<>> >(),
    //     "'In' is not deliverable to 'SideEffects'");

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
  template <class In, class SideEffects>
  struct out_impl {
    SideEffects sideEffects_;
    PUSHMI_TEMPLATE (class Out)
      (requires Receiver<Out>)
    auto operator()(Out out) const {
      // PUSHMI_STATIC_ASSERT(
      //   ::pushmi::detail::sender_requires_from<In, SideEffects,
      //     SenderTo<In, Out, is_none<>>,
      //     SenderTo<In, Out, is_single<>>,
      //     TimeSenderTo<In, Out, is_single<>> >(),
      //     "'In' is not deliverable to 'Out'");
      auto gang{::pushmi::detail::receiver_from_fn<In>()(
          detail::make_tap(sideEffects_, std::move(out)))};
      using Gang = decltype(gang);
      // PUSHMI_STATIC_ASSERT(
      //   ::pushmi::detail::sender_requires_from<In, SideEffects,
      //     SenderTo<In, Gang>,
      //     SenderTo<In, Gang, is_single<>>,
      //     TimeSenderTo<In, Gang, is_single<>> >(),
      //     "'In' is not deliverable to 'Out' & 'SideEffects'");
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

//#include "../single.h"
//#include "../many.h"
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
  template<class Out>
  auto operator()(Out out) const {
    return make_single(std::move(out), on_value(*this));
  }
  template<class Out, class V>
  auto operator()(Out& out, V&& v) {
    using Result = decltype(f_((V&&) v));
    static_assert(::pushmi::SemiMovable<Result>,
      "none of the functions supplied to transform can convert this value");
    static_assert(::pushmi::SingleReceiver<Out, Result>,
      "Result of value transform cannot be delivered to Out");
    ::pushmi::set_value(out, f_((V&&) v));
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
  template<class Out, class V>
  auto operator()(Out& out, V&& v) {
    using Result = decltype(f_((V&&) v));
    static_assert(::pushmi::SemiMovable<Result>,
      "none of the functions supplied to transform can convert this value");
    static_assert(::pushmi::Flow<Out> && ::pushmi::Single<Out>,
      "Result of value transform cannot be delivered to Out");
    ::pushmi::set_value(out, f_((V&&) v));
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
    return make_many(std::move(out), on_next(*this));
  }
  template<class Out, class V>
  auto operator()(Out& out, V&& v) {
    using Result = decltype(f_((V&&) v));
    static_assert(::pushmi::SemiMovable<Result>,
      "none of the functions supplied to transform can convert this value");
    static_assert(::pushmi::ManyReceiver<Out, Result>,
      "Result of value transform cannot be delivered to Out");
    ::pushmi::set_next(out, f_((V&&) v));
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
    return make_flow_many(std::move(out), on_next(*this));
  }
  template<class Out, class V>
  auto operator()(Out& out, V&& v) {
    using Result = decltype(f_((V&&) v));
    static_assert(::pushmi::SemiMovable<Result>,
      "none of the functions supplied to transform can convert this value");
    static_assert(::pushmi::Flow<Out> && ::pushmi::Many<Out>,
      "Result of value transform cannot be delivered to Out");
    ::pushmi::set_next(out, f_((V&&) v));
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
      using Cardinality = property_set_index_t<properties_t<In>, is_silent<>>;
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
  template <class Predicate>
  struct on_value_impl {
    Predicate p_;
    template <class Out, class V>
    void operator()(Out& out, V&& v) const {
      if (p_(as_const(v))) {
        ::pushmi::set_value(out, (V&&) v);
      } else {
        ::pushmi::set_done(out);
      }
    }
  };
  template <class In, class Predicate>
  struct out_impl {
    Predicate p_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    auto operator()(Out out) const {
      return ::pushmi::detail::receiver_from_fn<In>()(
        std::move(out),
        // copy 'p' to allow multiple calls to submit
        ::pushmi::on_value(on_value_impl<Predicate>{p_})
      );
    }
  };
  template <class Predicate>
  struct in_impl {
    Predicate p_;
    PUSHMI_TEMPLATE(class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return ::pushmi::detail::sender_from(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(out_impl<In, Predicate>{p_})
      );
    }
  };
public:
  PUSHMI_TEMPLATE(class Predicate)
    (requires SemiMovable<Predicate>)
  auto operator()(Predicate p) const {
    return in_impl<Predicate>{std::move(p)};
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
    template <class Out, class E>
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

template<class Executor, class Out>
struct via_fn_data : public Out {
  Executor exec;

  via_fn_data(Out out, Executor exec) :
    Out(std::move(out)), exec(std::move(exec)) {}
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
      ::pushmi::submit(
        data.exec,
        ::pushmi::make_single(
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
      void operator()(any) {
        ::pushmi::set_error(out_, std::move(e_));
      }
    };
    template <class Data, class E>
    void operator()(Data& data, E e) const noexcept {
      ::pushmi::submit(
        data.exec,
        ::pushmi::make_single(
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
      ::pushmi::submit(
        data.exec,
        ::pushmi::make_single(
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
        ::pushmi::on_value(on_value_impl<Out>{}),
        ::pushmi::on_error(on_error_impl<Out>{}),
        ::pushmi::on_done(on_done_impl<Out>{})
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
    (requires Invocable<ExecutorFactory&> && Executor<invoke_result_t<ExecutorFactory&>>)
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

//#include "../single.h"
//#include "submit.h"
//#include "extension_operators.h"
//#include "via.h"

namespace pushmi {

template<typename In>
struct send_via {
    In in;
    template<class... AN>
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

//#include "../single.h"
//#include "submit.h"
//#include "extension_operators.h"

//#include "../subject.h"

namespace pushmi {

namespace detail {

template<class T>
struct share_fn {
private:
  struct impl {
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      subject<T, properties_t<In>> sub;
      PUSHMI_IF_CONSTEXPR( ((bool)TimeSender<In>) (
        ::pushmi::submit(in, ::pushmi::now(id(in)), sub.receiver());
      ) else (
        ::pushmi::submit(id(in), sub.receiver());
      ));
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

template<class T>
PUSHMI_INLINE_VAR constexpr detail::share_fn<T> share{};

} // namespace operators

} // namespace pushmi

#endif // PUSHMI_SINGLE_HEADER
