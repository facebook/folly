#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "traits.h"

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

template <class T, class P>
using property_from_category_t =
  property_set_index_t<properties_t<T>, P>;

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
