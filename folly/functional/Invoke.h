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

#include <functional>
#include <type_traits>

#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/facilities/is_empty_variadic.hpp>
#include <boost/preprocessor/list/for_each.hpp>
#include <boost/preprocessor/logical/not.hpp>
#include <boost/preprocessor/tuple/to_list.hpp>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/Preprocessor.h>
#include <folly/Traits.h>
#include <folly/Utility.h>
#include <folly/lang/CustomizationPoint.h>

#define FOLLY_DETAIL_FORWARD_REF(a) static_cast<decltype(a)&&>(a)
#define FOLLY_DETAIL_FORWARD_BODY(e) \
  noexcept(noexcept(e))->decltype(e) { return e; }

/**
 *  include or backport:
 *  * std::invoke
 *  * std::invoke_result
 *  * std::invoke_result_t
 *  * std::is_invocable
 *  * std::is_invocable_v
 *  * std::is_invocable_r
 *  * std::is_invocable_r_v
 *  * std::is_nothrow_invocable
 *  * std::is_nothrow_invocable_v
 *  * std::is_nothrow_invocable_r
 *  * std::is_nothrow_invocable_r_v
 */

namespace folly {

//  invoke_fn
//  invoke
//
//  mimic: std::invoke, C++17
struct invoke_fn {
  template <typename F, typename... A>
  FOLLY_ERASE constexpr auto operator()(F&& f, A&&... a) const
      noexcept(noexcept(static_cast<F&&>(f)(static_cast<A&&>(a)...)))
          -> decltype(static_cast<F&&>(f)(static_cast<A&&>(a)...)) {
    return static_cast<F&&>(f)(static_cast<A&&>(a)...);
  }
  template <typename M, typename C, typename... A>
  FOLLY_ERASE constexpr auto operator()(M C::*f, A&&... a) const
      noexcept(noexcept(std::mem_fn(f)(static_cast<A&&>(a)...)))
          -> decltype(std::mem_fn(f)(static_cast<A&&>(a)...)) {
    return std::mem_fn(f)(static_cast<A&&>(a)...);
  }
};

FOLLY_INLINE_VARIABLE constexpr invoke_fn invoke;

} // namespace folly

namespace folly {

namespace invoke_detail {

template <typename F>
struct traits {
  template <typename... A>
  using result = decltype( //
      FOLLY_DECLVAL(F &&)(FOLLY_DECLVAL(A &&)...));
  template <typename... A>
  static constexpr bool nothrow = noexcept( //
      FOLLY_DECLVAL(F&&)(FOLLY_DECLVAL(A&&)...));
};
template <typename P>
struct traits_member_ptr {
  template <typename... A>
  using result = decltype( //
      std::mem_fn(FOLLY_DECLVAL(P))(FOLLY_DECLVAL(A &&)...));
  template <typename... A>
  static constexpr bool nothrow = noexcept( //
      std::mem_fn(FOLLY_DECLVAL(P))(FOLLY_DECLVAL(A&&)...));
};
template <typename M, typename C>
struct traits<M C::*> : traits_member_ptr<M C::*> {};
template <typename M, typename C>
struct traits<M C::*const> : traits_member_ptr<M C::*> {};
template <typename M, typename C>
struct traits<M C::*&> : traits_member_ptr<M C::*> {};
template <typename M, typename C>
struct traits<M C::*const&> : traits_member_ptr<M C::*> {};
template <typename M, typename C>
struct traits<M C::*&&> : traits_member_ptr<M C::*> {};
template <typename M, typename C>
struct traits<M C::*const&&> : traits_member_ptr<M C::*> {};

//  adapted from: http://en.cppreference.com/w/cpp/types/result_of, CC-BY-SA

template <typename F, typename... A>
using invoke_result_t = typename traits<F>::template result<A...>;

template <typename Void, typename F, typename... A>
struct invoke_result {};

template <typename F, typename... A>
struct invoke_result<void_t<invoke_result_t<F, A...>>, F, A...> {
  using type = invoke_result_t<F, A...>;
};

template <typename Void, typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool is_invocable_v = false;

template <typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool
    is_invocable_v<void_t<invoke_result_t<F, A...>>, F, A...> = true;

template <typename Void, typename R, typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool is_invocable_r_v = false;

template <typename R, typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool
    is_invocable_r_v<void_t<invoke_result_t<F, A...>>, R, F, A...> =
        std::is_convertible<invoke_result_t<F, A...>, R>::value;

template <typename Void, typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool is_nothrow_invocable_v = false;

template <typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool
    is_nothrow_invocable_v<void_t<invoke_result_t<F, A...>>, F, A...> =
        traits<F>::template nothrow<A...>;

template <typename Void, typename R, typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool is_nothrow_invocable_r_v = false;

template <typename R, typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool
    is_nothrow_invocable_r_v<void_t<invoke_result_t<F, A...>>, R, F, A...> =
        std::is_convertible<invoke_result_t<F, A...>, R>::value&&
            traits<F>::template nothrow<A...>;

} // namespace invoke_detail

//  mimic: std::invoke_result, C++17
template <typename F, typename... A>
using invoke_result = invoke_detail::invoke_result<void, F, A...>;

//  mimic: std::invoke_result_t, C++17
using invoke_detail::invoke_result_t;

//  mimic: std::is_invocable_v, C++17
template <typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool is_invocable_v =
    invoke_detail::is_invocable_v<void, F, A...>;

//  mimic: std::is_invocable, C++17
template <typename F, typename... A>
struct is_invocable : bool_constant<is_invocable_v<F, A...>> {};

//  mimic: std::is_invocable_r_v, C++17
template <typename R, typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool is_invocable_r_v =
    invoke_detail::is_invocable_r_v<void, R, F, A...>;

//  mimic: std::is_invocable_r, C++17
template <typename R, typename F, typename... A>
struct is_invocable_r : bool_constant<is_invocable_r_v<R, F, A...>> {};

//  mimic: std::is_nothrow_invocable_v, C++17
template <typename F, typename... A>
FOLLY_INLINE_VARIABLE constexpr bool is_nothrow_invocable_v =
    invoke_detail::is_nothrow_invocable_v<void, F, A...>;

//  mimic: std::is_nothrow_invocable, C++17
template <typename F, typename... A>
struct is_nothrow_invocable : bool_constant<is_nothrow_invocable_v<F, A...>> {};

//  mimic: std::is_nothrow_invocable_r_v, C++17
template <typename R, typename F, typename... Args>
FOLLY_INLINE_VARIABLE constexpr bool is_nothrow_invocable_r_v =
    invoke_detail::is_nothrow_invocable_r_v<void, R, F, Args...>;

//  mimic: std::is_nothrow_invocable_r, C++17
template <typename R, typename F, typename... A>
struct is_nothrow_invocable_r
    : bool_constant<is_nothrow_invocable_r_v<R, F, A...>> {};

} // namespace folly

namespace folly {

namespace detail {

struct invoke_private_overload;

template <bool, typename I>
struct invoke_traits_base_ {};
template <typename I>
struct invoke_traits_base_<false, I> {};
template <typename I>
struct invoke_traits_base_<true, I> {
  FOLLY_INLINE_VARIABLE static constexpr I invoke{};
};
template <typename I>
using invoke_traits_base =
    invoke_traits_base_<is_constexpr_default_constructible_v<I>, I>;

} // namespace detail

//  invoke_traits
//
//  A traits container struct with the following member types, type aliases, and
//  variables:
//
//  * invoke_result
//  * invoke_result_t
//  * is_invocable
//  * is_invocable_v
//  * is_invocable_r
//  * is_invocable_r_v
//  * is_nothrow_invocable
//  * is_nothrow_invocable_v
//  * is_nothrow_invocable_r
//  * is_nothrow_invocable_r_v
//
//  These members have behavior matching the behavior of C++17's corresponding
//  invocation traits types, type aliases, and variables, but using invoke_type
//  as the invocable argument passed to the usual nivocation traits.
//
//  The traits container struct also has a member type alias:
//
//  * invoke_type
//
//  And an invocable variable as a default-constructed instance of invoke_type,
//  if the latter is constexpr default-constructible:
//
//  * invoke
template <typename I>
struct invoke_traits : detail::invoke_traits_base<I> {
 public:
  using invoke_type = I;

  //  If invoke_type is constexpr default-constructible:
  //
  //    inline static constexpr invoke_type invoke{};

  template <typename... A>
  using invoke_result = invoke_detail::invoke_result<void, I, A...>;
  template <typename... A>
  using invoke_result_t = invoke_detail::invoke_result_t<I, A...>;
  template <typename... A>
  FOLLY_INLINE_VARIABLE static constexpr bool is_invocable_v =
      invoke_detail::is_invocable_v<void, I, A...>;
  template <typename... A>
  struct is_invocable //
      : bool_constant<invoke_detail::is_invocable_v<void, I, A...>> {};
  template <typename R, typename... A>
  FOLLY_INLINE_VARIABLE static constexpr bool is_invocable_r_v =
      invoke_detail::is_invocable_r_v<void, R, I, A...>;
  template <typename R, typename... A>
  struct is_invocable_r //
      : bool_constant<invoke_detail::is_invocable_r_v<void, R, I, A...>> {};
  template <typename... A>
  FOLLY_INLINE_VARIABLE static constexpr bool is_nothrow_invocable_v =
      invoke_detail::is_nothrow_invocable_v<void, I, A...>;
  template <typename... A>
  struct is_nothrow_invocable //
      : bool_constant<invoke_detail::is_nothrow_invocable_v<void, I, A...>> {};
  template <typename R, typename... A>
  FOLLY_INLINE_VARIABLE static constexpr bool is_nothrow_invocable_r_v =
      invoke_detail::is_nothrow_invocable_r_v<void, R, I, A...>;
  template <typename R, typename... A>
  struct is_nothrow_invocable_r //
      : bool_constant<
            invoke_detail::is_nothrow_invocable_r_v<void, R, I, A...>> {};
};

} // namespace folly

#define FOLLY_DETAIL_CREATE_FREE_INVOKE_TRAITS_USING_1(_, funcname, ns) \
  using ns::funcname;

#define FOLLY_DETAIL_CREATE_FREE_INVOKE_TRAITS_USING(_, funcname, ...) \
  BOOST_PP_EXPR_IIF(                                                   \
      BOOST_PP_NOT(BOOST_PP_IS_EMPTY(__VA_ARGS__)),                    \
      BOOST_PP_LIST_FOR_EACH(                                          \
          FOLLY_DETAIL_CREATE_FREE_INVOKE_TRAITS_USING_1,              \
          funcname,                                                    \
          BOOST_PP_TUPLE_TO_LIST((__VA_ARGS__))))

/***
 *  FOLLY_CREATE_FREE_INVOKER
 *
 *  Used to create an invoker type bound to a specific free-invocable name.
 *
 *  Example:
 *
 *    FOLLY_CREATE_FREE_INVOKER(foo_invoker, foo);
 *
 *  The type `foo_invoker` is generated in the current namespace and may be used
 *  as follows:
 *
 *    namespace Deep {
 *    struct CanFoo {};
 *    int foo(CanFoo const&, Bar&) { return 1; }
 *    int foo(CanFoo&&, Car&&) noexcept { return 2; }
 *    }
 *
 *    using traits = folly::invoke_traits<foo_invoker>;
 *
 *    traits::invoke(Deep::CanFoo{}, Car{}) // 2
 *
 *    traits::invoke_result<Deep::CanFoo, Bar&> // has member
 *    traits::invoke_result_t<Deep::CanFoo, Bar&> // int
 *    traits::invoke_result<Deep::CanFoo, Bar&&> // empty
 *    traits::invoke_result_t<Deep::CanFoo, Bar&&> // error
 *
 *    traits::is_invocable_v<CanFoo, Bar&> // true
 *    traits::is_invocable_v<CanFoo, Bar&&> // false
 *
 *    traits::is_invocable_r_v<int, CanFoo, Bar&> // true
 *    traits::is_invocable_r_v<char*, CanFoo, Bar&> // false
 *
 *    traits::is_nothrow_invocable_v<CanFoo, Bar&> // false
 *    traits::is_nothrow_invocable_v<CanFoo, Car&&> // true
 *
 *    traits::is_nothrow_invocable_v<int, CanFoo, Bar&> // false
 *    traits::is_nothrow_invocable_v<char*, CanFoo, Bar&> // false
 *    traits::is_nothrow_invocable_v<int, CanFoo, Car&&> // true
 *    traits::is_nothrow_invocable_v<char*, CanFoo, Car&&> // false
 *
 *  When a name has one or more primary definition in a fixed set of namespaces
 *  and alternate definitions in the namespaces of its arguments, the primary
 *  definitions may automatically be found as follows:
 *
 *    FOLLY_CREATE_FREE_INVOKER(swap_invoker, swap, std);
 *
 *  In this case, `swap_invoke_traits::invoke(int&, int&)` will use the primary
 *  definition found in `namespace std` relative to the current namespace, which
 *  may be equivalent to `namespace ::std`. In contrast:
 *
 *    namespace Deep {
 *    struct HasData {};
 *    void swap(HasData&, HasData&) { throw 7; }
 *    }
 *
 *    using traits = invoke_traits<swap_invoker>;
 *
 *    HasData a, b;
 *    traits::invoke(a, b); // throw 7
 */
#define FOLLY_CREATE_FREE_INVOKER(classname, funcname, ...)                \
  namespace classname##__folly_detail_invoke_ns {                          \
    FOLLY_MAYBE_UNUSED void funcname(                                      \
        ::folly::detail::invoke_private_overload&);                        \
    FOLLY_DETAIL_CREATE_FREE_INVOKE_TRAITS_USING(_, funcname, __VA_ARGS__) \
    struct __folly_detail_invoke_obj {                                     \
      template <typename... Args>                                          \
      FOLLY_MAYBE_UNUSED FOLLY_ERASE_HACK_GCC constexpr auto operator()(   \
          Args&&... args) const                                            \
          noexcept(noexcept(funcname(static_cast<Args&&>(args)...)))       \
              -> decltype(funcname(static_cast<Args&&>(args)...)) {        \
        return funcname(static_cast<Args&&>(args)...);                     \
      }                                                                    \
    };                                                                     \
  }                                                                        \
  struct classname                                                         \
      : classname##__folly_detail_invoke_ns::__folly_detail_invoke_obj {}

/***
 *  FOLLY_CREATE_FREE_INVOKER_SUITE
 *
 *  Used to create an invoker type and associated variable bound to a specific
 *  free-invocable name. The invoker variable is named like the free-invocable
 *  name and the invoker type is named with a suffix of _fn.
 *
 *  See FOLLY_CREATE_FREE_INVOKER.
 */
#define FOLLY_CREATE_FREE_INVOKER_SUITE(funcname, ...)             \
  FOLLY_CREATE_FREE_INVOKER(funcname##_fn, funcname, __VA_ARGS__); \
  FOLLY_MAYBE_UNUSED FOLLY_INLINE_VARIABLE constexpr funcname##_fn funcname {}

/***
 *  FOLLY_CREATE_QUAL_INVOKER
 *
 *  Used to create an invoker type bound to a specific free-invocable qualified
 *  name. It is permitted that the qualification be empty and that the name be
 *  unqualified in practice. This differs from FOLLY_CREATE_FREE_INVOKER in that
 *  it is required that the name be in scope and that it is not possible to
 *  provide a list of namespaces in which to look up the name..
 */
#define FOLLY_CREATE_QUAL_INVOKER(classname, funcpath)                 \
  struct classname {                                                   \
    template <typename... A>                                           \
    FOLLY_MAYBE_UNUSED FOLLY_ERASE_HACK_GCC constexpr auto operator()( \
        A&&... a) const                                                \
        FOLLY_DETAIL_FORWARD_BODY(funcpath(static_cast<A&&>(a)...))    \
  }

/***
 *  FOLLY_CREATE_QUAL_INVOKER_SUITE
 *
 *  Used to create an invoker type and associated variable bound to a specific
 *  free-invocable qualified name.
 *
 *  See FOLLY_CREATE_QUAL_INVOKER.
 */
#define FOLLY_CREATE_QUAL_INVOKER_SUITE(name, funcpath) \
  FOLLY_CREATE_QUAL_INVOKER(name##_fn, funcpath);       \
  FOLLY_MAYBE_UNUSED FOLLY_INLINE_VARIABLE constexpr name##_fn name {}

/***
 *  FOLLY_INVOKE_QUAL
 *
 *  An invoker expression resulting in an invocable which, when invoked, invokes
 *  the free-invocable qualified name with the given arguments.
 */
#define FOLLY_INVOKE_QUAL(funcpath)                    \
  [](auto&&... __folly_param_a)                        \
      FOLLY_CXX17_CONSTEXPR FOLLY_DETAIL_FORWARD_BODY( \
          funcpath(FOLLY_DETAIL_FORWARD_REF(__folly_param_a)...))

/***
 *  FOLLY_CREATE_MEMBER_INVOKER
 *
 *  Used to create an invoker type bound to a specific member-invocable name.
 *
 *  Example:
 *
 *    FOLLY_CREATE_MEMBER_INVOKER(foo_invoker, foo);
 *
 *  The type `foo_invoker` is generated in the current namespace and may be used
 *  as follows:
 *
 *    struct CanFoo {
 *      int foo(Bar&) { return 1; }
 *      int foo(Car&&) noexcept { return 2; }
 *    };
 *
 *    using traits = folly::invoke_traits<foo_invoker>;
 *
 *    traits::invoke(CanFoo{}, Car{}) // 2
 *
 *    traits::invoke_result<CanFoo, Bar&> // has member
 *    traits::invoke_result_t<CanFoo, Bar&> // int
 *    traits::invoke_result<CanFoo, Bar&&> // empty
 *    traits::invoke_result_t<CanFoo, Bar&&> // error
 *
 *    traits::is_invocable_v<CanFoo, Bar&> // true
 *    traits::is_invocable_v<CanFoo, Bar&&> // false
 *
 *    traits::is_invocable_r_v<int, CanFoo, Bar&> // true
 *    traits::is_invocable_r_v<char*, CanFoo, Bar&> // false
 *
 *    traits::is_nothrow_invocable_v<CanFoo, Bar&> // false
 *    traits::is_nothrow_invocable_v<CanFoo, Car&&> // true
 *
 *    traits::is_nothrow_invocable_v<int, CanFoo, Bar&> // false
 *    traits::is_nothrow_invocable_v<char*, CanFoo, Bar&> // false
 *    traits::is_nothrow_invocable_v<int, CanFoo, Car&&> // true
 *    traits::is_nothrow_invocable_v<char*, CanFoo, Car&&> // false
 */
#define FOLLY_CREATE_MEMBER_INVOKER(classname, membername)                 \
  struct classname {                                                       \
    template <typename O, typename... Args>                                \
    FOLLY_MAYBE_UNUSED FOLLY_ERASE_HACK_GCC constexpr auto operator()(     \
        O&& o, Args&&... args) const                                       \
        noexcept(noexcept(                                                 \
            static_cast<O&&>(o).membername(static_cast<Args&&>(args)...))) \
            -> decltype(static_cast<O&&>(o).membername(                    \
                static_cast<Args&&>(args)...)) {                           \
      return static_cast<O&&>(o).membername(static_cast<Args&&>(args)...); \
    }                                                                      \
  }

/***
 *  FOLLY_CREATE_MEMBER_INVOKER_SUITE
 *
 *  Used to create an invoker type and associated variable bound to a specific
 *  member-invocable name. The invoker variable is named like the member-
 *  invocable  name and the invoker type is named with a suffix of _fn.
 *
 *  See FOLLY_CREATE_MEMBER_INVOKER.
 */
#define FOLLY_CREATE_MEMBER_INVOKER_SUITE(membername)                \
  FOLLY_CREATE_MEMBER_INVOKER(membername##_fn, membername);          \
  FOLLY_MAYBE_UNUSED FOLLY_INLINE_VARIABLE constexpr membername##_fn \
      membername {}

/***
 *  FOLLY_INVOKE_MEMBER
 *
 *  An invoker expression resulting in an invocable which, when invoked, invokes
 *  the member on the object with the given arguments.
 *
 *  Example:
 *
 *    FOLLY_INVOKE_MEMBER(find)(map, key)
 *
 *  Equivalent to:
 *
 *    map.find(key)
 *
 *  But also equivalent to:
 *
 *    std::invoke(FOLLY_INVOKE_MEMBER(find), map, key)
 *
 *  As an implementation detail, the resulting callable is a lambda. This has
 *  two observable consequences.
 *  * Since C++17 only, lambda invocations may be marked constexpr.
 *  * Since C++20 only, lambda definitions may appear in an unevaluated context,
 *    namely, in an operand to decltype, noexcept, sizeof, or typeid.
 */
#define FOLLY_INVOKE_MEMBER(membername)                 \
  [](auto&& __folly_param_o, auto&&... __folly_param_a) \
      FOLLY_CXX17_CONSTEXPR FOLLY_DETAIL_FORWARD_BODY(  \
          FOLLY_DETAIL_FORWARD_REF(__folly_param_o)     \
              .membername(FOLLY_DETAIL_FORWARD_REF(__folly_param_a)...))

/***
 *  FOLLY_CREATE_STATIC_MEMBER_INVOKER
 *
 *  Used to create an invoker type template bound to a specific static-member-
 *  invocable name.
 *
 *  Example:
 *
 *    FOLLY_CREATE_STATIC_MEMBER_INVOKER(foo_invoker, foo);
 *
 *  The type template `foo_invoker` is generated in the current namespace and
 *  may be used as follows:
 *
 *    struct CanFoo {
 *      static int foo(Bar&) { return 1; }
 *      static int foo(Car&&) noexcept { return 2; }
 *    };
 *
 *    using traits = folly::invoke_traits<foo_invoker<CanFoo>>;
 *
 *    traits::invoke(Car{}) // 2
 *
 *    traits::invoke_result<Bar&> // has member
 *    traits::invoke_result_t<Bar&> // int
 *    traits::invoke_result<Bar&&> // empty
 *    traits::invoke_result_t<Bar&&> // error
 *
 *    traits::is_invocable_v<Bar&> // true
 *    traits::is_invocable_v<Bar&&> // false
 *
 *    traits::is_invocable_r_v<int, Bar&> // true
 *    traits::is_invocable_r_v<char*, Bar&> // false
 *
 *    traits::is_nothrow_invocable_v<Bar&> // false
 *    traits::is_nothrow_invocable_v<Car&&> // true
 *
 *    traits::is_nothrow_invocable_v<int, Bar&> // false
 *    traits::is_nothrow_invocable_v<char*, Bar&> // false
 *    traits::is_nothrow_invocable_v<int, Car&&> // true
 *    traits::is_nothrow_invocable_v<char*, Car&&> // false
 */
#define FOLLY_CREATE_STATIC_MEMBER_INVOKER(classname, membername)             \
  template <typename T>                                                       \
  struct classname {                                                          \
    template <typename... Args, typename U = T>                               \
    FOLLY_MAYBE_UNUSED FOLLY_ERASE constexpr auto operator()(Args&&... args)  \
        const noexcept(noexcept(U::membername(static_cast<Args&&>(args)...))) \
            -> decltype(U::membername(static_cast<Args&&>(args)...)) {        \
      return U::membername(static_cast<Args&&>(args)...);                     \
    }                                                                         \
  }

/***
 *  FOLLY_CREATE_STATIC_MEMBER_INVOKER_SUITE
 *
 *  Used to create an invoker type template and associated variable template
 *  bound to a specific static-member-invocable name. The invoker variable
 *  template is named like the static-member-invocable name and the invoker type
 *  template is named with a suffix of _fn.
 *
 *  See FOLLY_CREATE_STATIC_MEMBER_INVOKER.
 */
#define FOLLY_CREATE_STATIC_MEMBER_INVOKER_SUITE(membername)            \
  FOLLY_CREATE_STATIC_MEMBER_INVOKER(membername##_fn, membername);      \
  template <typename T>                                                 \
  FOLLY_MAYBE_UNUSED FOLLY_INLINE_VARIABLE constexpr membername##_fn<T> \
      membername {}

namespace folly {

namespace detail_tag_invoke_fn {

void tag_invoke();

struct tag_invoke_fn {
  template <typename Tag, typename... Args>
  constexpr auto operator()(Tag tag, Args&&... args) const noexcept(noexcept(
      tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...)))
      -> decltype(tag_invoke(
          static_cast<Tag&&>(tag), static_cast<Args&&>(args)...)) {
    return tag_invoke(static_cast<Tag&&>(tag), static_cast<Args&&>(args)...);
  }
};

// Manually implement the traits here rather than defining them in terms of
// the corresponding std::invoke_result/is_invocable/is_nothrow_invocable
// traits to improve compile-times. We don't need all of the generality of
// the std:: traits and the tag_invoke traits can be used heavily in CPO-based
// code so optimising them for compile times can make a big difference.

// Use the immediately-invoked function-pointer trick here to avoid
// instantiating the std::declval<T>() template.

template <typename Tag, typename... Args>
using tag_invoke_result_t = decltype(tag_invoke(
    static_cast<Tag && (*)() noexcept>(nullptr)(),
    static_cast<Args && (*)() noexcept>(nullptr)()...));

template <typename Tag, typename... Args>
auto try_tag_invoke(int) noexcept(
    noexcept(tag_invoke(FOLLY_DECLVAL(Tag&&), FOLLY_DECLVAL(Args&&)...)))
    -> decltype(
        static_cast<void>(
            tag_invoke(FOLLY_DECLVAL(Tag &&), FOLLY_DECLVAL(Args&&)...)),
        std::true_type{});

template <typename Tag, typename... Args>
std::false_type try_tag_invoke(...) noexcept(false);

template <template <typename...> class T, typename... Args>
struct defer {
  using type = T<Args...>;
};

struct empty {};

} // namespace detail_tag_invoke_fn

// The expression folly::tag_invoke(tag, args...) is equivalent to performing
// a call to the expression tag_invoke(tag, args...) using argument-dependent
// lookup (ADL).
//
// This is intended to be used by customization-point objects, which dispatch
// a call to the CPO to an ADL call to tag_invoke(cpo, args...), using the type
// of the first argument to disambiguate between customisations for different
// CPOs rather than using different ADL names for this.
//
// For example: Defining a new CPO in terms of tag_invoke.
//   struct FooCpo {
//     template<typename A, typename B>
//     auto operator()(A&& a, B&& b) const
//         noexcept(folly::is_nothrow_tag_invocable_v<FooCpo, A, B>)
//         -> folly::tag_invoke_result_t<FooCpo, A, B> {
//       return folly::tag_invoke(*this, (A&&)a, (B&&)b);
//     }
//   };
//   FOLLY_DEFINE_CPO(FooCpo, Foo)
//
// And then customising the Foo CPO for a particular type:
//   class SomeType {
//     ...
//     template<typename B>
//     friend int tag_invoke(folly::cpo_t<Foo>, const SomeType& a, B&& b) {
//       // implementation goes here
//     }
//   };
//
// For more details see the C++ standards proposal: https://wg21.link/P1895R0.
FOLLY_DEFINE_CPO(detail_tag_invoke_fn::tag_invoke_fn, tag_invoke)

// Query if the 'folly::tag_invoke()' CPO can be invoked with a tag and
// arguments of the the specified types.
//
// This checks whether an overload of the free-function tag_invoke() found
// by ADL can be invoked with the specified types.

template <typename Tag, typename... Args>
FOLLY_INLINE_VARIABLE constexpr bool is_tag_invocable_v =
    decltype(detail_tag_invoke_fn::try_tag_invoke<Tag, Args...>(0))::value;

template <typename Tag, typename... Args>
struct is_tag_invocable : bool_constant<is_tag_invocable_v<Tag, Args...>> {};

// Query whether the 'folly::tag_invoke()' CPO can be invoked with a tag
// and arguments of the specified type and that such an invocation is
// noexcept.

template <typename Tag, typename... Args>
FOLLY_INLINE_VARIABLE constexpr bool is_nothrow_tag_invocable_v =
    noexcept(detail_tag_invoke_fn::try_tag_invoke<Tag, Args...>(0));

template <typename Tag, typename... Args>
struct is_nothrow_tag_invocable
    : bool_constant<is_nothrow_tag_invocable_v<Tag, Args...>> {};

// Versions of the above that check in addition that the result is
// convertible to the given return type R.

template <typename R, typename Tag, typename... Args>
using is_tag_invocable_r =
    folly::is_invocable_r<R, decltype(folly::tag_invoke), Tag, Args...>;

template <typename R, typename Tag, typename... Args>
FOLLY_INLINE_VARIABLE constexpr bool is_tag_invocable_r_v =
    is_tag_invocable_r<R, decltype(folly::tag_invoke), Tag, Args...>::value;

template <typename R, typename Tag, typename... Args>
using is_nothrow_tag_invocable_r =
    folly::is_nothrow_invocable_r<R, decltype(folly::tag_invoke), Tag, Args...>;

template <typename R, typename Tag, typename... Args>
FOLLY_INLINE_VARIABLE constexpr bool is_nothrow_tag_invocable_r_v =
    is_nothrow_tag_invocable_r<R, Tag, Args...>::value;

using detail_tag_invoke_fn::tag_invoke_result_t;

template <typename Tag, typename... Args>
struct tag_invoke_result
    : conditional_t<
          is_tag_invocable_v<Tag, Args...>,
          detail_tag_invoke_fn::defer<tag_invoke_result_t, Tag, Args...>,
          detail_tag_invoke_fn::empty> {};

} // namespace folly
