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

#include <type_traits>

#include <folly/Traits.h>
#include <folly/Utility.h>
#include <folly/experimental/pushmi/detail/traits.h>
#include <folly/experimental/pushmi/detail/functional.h>

#define PUSHMI_NOEXCEPT_AUTO(...) \
  noexcept(noexcept(static_cast<decltype((__VA_ARGS__))>(__VA_ARGS__)))\
  /**/
#define PUSHMI_NOEXCEPT_RETURN(...) \
  PUSHMI_NOEXCEPT_AUTO(__VA_ARGS__) {\
    return (__VA_ARGS__);\
  }\
  /**/

namespace folly {
namespace pushmi {
#if __cpp_fold_expressions >= 201603
template <bool... Bs>
PUSHMI_INLINE_VAR constexpr bool and_v = (Bs && ...);

template <bool... Bs>
PUSHMI_INLINE_VAR constexpr bool or_v = (Bs || ...);

template <int... Is>
PUSHMI_INLINE_VAR constexpr int sum_v = (Is + ...);
#else
namespace detail {

template <bool...>
struct bools;

template <std::size_t N>
constexpr int sum_impl(int const (&rgi)[N], int i = 0, int state = 0) noexcept {
  return i == N ? state : sum_impl(rgi, i + 1, state + rgi[i]);
}
template <int... Is>
constexpr int sum_impl() noexcept {
  using RGI = int[sizeof...(Is)];
  return sum_impl(RGI{Is...});
}

} // namespace detail

template <bool... Bs>
PUSHMI_INLINE_VAR constexpr bool and_v =
    PUSHMI_PP_IS_SAME(detail::bools<Bs..., true>, detail::bools<true, Bs...>);

template <bool... Bs>
PUSHMI_INLINE_VAR constexpr bool or_v = !PUSHMI_PP_IS_SAME(
    detail::bools<Bs..., false>,
    detail::bools<false, Bs...>);

template <int... Is>
PUSHMI_INLINE_VAR constexpr int sum_v = detail::sum_impl<Is...>();
#endif

template <class...>
struct typelist;

template <class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template<class T>
PUSHMI_INLINE_VAR constexpr bool bool_v = T::value;

PUSHMI_CONCEPT_DEF(
  template(class... Args)
  (concept True)(Args...),
    true
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
  concept MoveConstructible,
    Constructible<T, T>
);

PUSHMI_CONCEPT_DEF(
  template (class A, class B)
  concept DerivedFrom,
    __is_base_of(B, A) &&
    std::is_convertible<const volatile A*, const volatile B*>::value
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

template <class T, class Self, class Derived = Self>
PUSHMI_PP_CONSTRAINED_USING(
    static_cast<bool>(
        not DerivedFrom<remove_cvref_t<T>, remove_cvref_t<Derived>> &&
        not Same<remove_cvref_t<T>, remove_cvref_t<Self>>),
    not_self_t =,
    T);

template <bool>
struct Enable_ {};
template <>
struct Enable_<true> {
  template <class T>
  using _type = T;
};

template <class...>
struct FrontOrVoid_ {
  using type = void;
};
template <class T, class... Us>
struct FrontOrVoid_<T, Us...> {
  using type = T;
};

// An alias for the type in the pack if the pack has exactly one type in it.
// Otherwise, this SFINAE's away. T cannot be an array type of an abstract type.
// Instantiation proceeds from left to right. The use of Enable_ here avoids
// needless instantiations of FrontOrVoid_.
template<class...Ts>
using identity_t = typename Enable_<sizeof...(Ts) == 1u>::
  template _type<FrontOrVoid_<Ts...>>::type;

template<class...Ts>
using identity_or_void_t = typename Enable_<sizeof...(Ts) <= 1u>::
  template _type<FrontOrVoid_<Ts...>>::type;

// inherit: a class that inherits from a bunch of bases
template <class... Ts>
struct inherit : Ts... {
  inherit() = default;
  constexpr inherit(Ts... ts) : Ts((Ts &&) ts)... {}
};
template <class T>
struct inherit<T> : T {
  inherit() = default;
  explicit constexpr inherit(T t) : T((T &&) t) {}
};
template <>
struct inherit<> {};

} // namespace detail

} // namespace pushmi
} // namespace folly
