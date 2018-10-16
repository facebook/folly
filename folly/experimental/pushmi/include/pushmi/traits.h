#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <type_traits>

namespace pushmi {

template <class T, template <class> class C>
concept bool Valid = requires { typename C<T>; };

template <class T, template<class...> class Trait, class... Args>
concept bool Satisfies = bool(Trait<T>::type::value);

template <class T>
concept bool Object = requires(T* p) {
  *p;
  { p } -> const volatile void*;
};

template <class T, class... Args>
concept bool Constructible = __is_constructible(T, Args...);

template <class T>
concept bool MoveConstructible = Constructible<T, T>;

template <class From, class To>
concept bool ConvertibleTo =
    std::is_convertible_v<From, To>&& requires(From (&f)()) {
  static_cast<To>(f());
};

template <class T, class U>
concept bool Same = __is_same_as(T, U) && __is_same_as(U, T);

template <class A, class B>
concept bool Derived = __is_base_of(B, A);

template <class A>
concept bool Decayed = Same<A, std::decay_t<A>>;

template <class T, class U>
concept bool Assignable = Same<T, T&>&& requires(T t, U&& u) {
  { t = (U &&) u } -> Same<T>&&;
};

template <class T>
concept bool EqualityComparable = requires(std::remove_cvref_t<T> const & t) {
  { t == t } -> bool;
  { t != t } -> bool;
};

template <class T>
concept bool SemiMovable =
    Object<T>&& Constructible<T, T>&& ConvertibleTo<T, T>;

template <class T>
concept bool Movable = SemiMovable<T>&& Assignable<T&, T>;

template <class T>
concept bool Copyable = Movable<T>&&
  Assignable<T&, const T&> &&
  ConvertibleTo<const T&, T>;

template <class T>
concept bool Semiregular = Copyable<T>&& Constructible<T>;

template <class T>
concept bool Regular = Semiregular<T>&& EqualityComparable<T>;

template <class F, class... Args>
concept bool Invocable = requires(F&& f, Args&&... args) {
  std::invoke((F &&) f, (Args &&) args...);
};

template <class F, class... Args>
concept bool NothrowInvocable =
    Invocable<F, Args...> && requires(F&& f, Args&&... args) {
  { std::invoke((F &&) f, (Args &&) args...) } noexcept;
};

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

} // namespace detail

} // namespace pushmi

#if 1

#define PUSHMI_VOID_LAMBDA_REQUIRES(RequiresExp...) \
  ->::pushmi::detail::requires_<(RequiresExp)>

#define PUSHMI_T_LAMBDA_REQUIRES(T, RequiresExp...) \
  ->::pushmi::detail::requires_<(RequiresExp), T>
#elif 0

// unsupported syntax..

#define PUSHMI_VOID_LAMBDA_REQUIRES(RequiresExp...) ->void requires(RequiresExp)

#define PUSHMI_T_LAMBDA_REQUIRES(T, RequiresExp...) ->T requires(RequiresExp)

#else

#define PUSHMI_VOID_LAMBDA_REQUIRES(RequiresExp...) ->void

#define PUSHMI_T_LAMBDA_REQUIRES(T, RequiresExp...) ->T

#endif
