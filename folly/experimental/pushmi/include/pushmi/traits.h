#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <functional>
#include <type_traits>

#include "detail/concept_def.h"

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
  template(class T, template<class...> class C, class... Args)
  (concept Valid)(T, C, Args...),
    True< C<T, Args...> >
);

PUSHMI_CONCEPT_DEF(
  template (class T, template<class...> class Trait, class... Args)
  (concept Satisfies)(T, Trait, Args...),
    bool(Trait<T>::type::value)
);

PUSHMI_CONCEPT_DEF(
  template (class T, class U)
  concept Same,
    __is_same_as(T, U) && __is_same_as(U, T)
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
    __is_constructible(T, Args...)
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
  concept Derived,
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

#if __cpp_lib_invoke >= 201411
using std::invoke;
using std::invoke_result_t;
#else
PUSHMI_TEMPLATE (class F, class...As)
  (requires requires (
    std::declval<F>()(std::declval<As>()...)
  ))
decltype(auto) invoke(F&& f, As&&...as)
    noexcept(noexcept(((F&&) f)((As&&) as...))) {
  return ((F&&) f)((As&&) as...);
}
PUSHMI_TEMPLATE (class F, class...As)
  (requires requires (
    std::mem_fn(std::declval<F>())(std::declval<As>()...)
  ))
decltype(auto) invoke(F f, As&&...as)
    noexcept(noexcept(std::mem_fn(f)((As&&) as...))) {
  return std::mem_fn(f)((As&&) as...);
}
template <class F, class...As>
using invoke_result_t =
  decltype(pushmi::invoke(std::declval<F>(), std::declval<As>()...));
#endif

PUSHMI_CONCEPT_DEF(
  template (class F, class... Args)
  (concept Invocable)(F, Args...),
    requires(F&& f, Args&&... args) (
      pushmi::invoke((F &&) f, (Args &&) args...)
    )
);

PUSHMI_CONCEPT_DEF(
  template (class F, class... Args)
  (concept NothrowInvocable)(F, Args...),
    requires(F&& f, Args&&... args) (
      requires_<noexcept(pushmi::invoke((F &&) f, (Args &&) args...))>
    ) &&
    Invocable<F, Args...>
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
