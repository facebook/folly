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

#include <folly/experimental/pushmi/traits.h>
#include <folly/Traits.h>

#include <folly/experimental/pushmi/forwards.h>

namespace folly {
namespace pushmi {

// property_set implements a map of category-type to property-type.
// for each category only one property in that category is allowed in the set.
//

// customization point for a property with a category

template <class T, class Void=void>
struct member_property_category {};

template <class T>
struct member_property_category<
    T,
    void_t<typename T::property_category>> {
  using property_category = typename T::property_category;
};

template <class T>
using __property_category_t = typename member_property_category<T>::property_category;

// allow specializations to use enable_if to constrain
template <class T, class Void>
struct property_traits : std::conditional_t<
  std::is_same<T, std::decay_t<T>>::value,
  member_property_category<T>,
  property_traits<std::decay_t<T>, Void>>
{};
template <class T>
struct property_traits<
    T,
    void_t<__property_category_t<T>>> {
  using property_category = __property_category_t<T>;
};

template <class T>
using property_category_t = __property_category_t<property_traits<T>>;

PUSHMI_CONCEPT_DEF(
    template(class T)
concept Property,
    True<__property_category_t<property_traits<T>>>
);

// in cases where Set contains T, allow T to find itself only once
PUSHMI_CONCEPT_DEF(
    template(class T, class... Set)(
concept FoundExactlyOnce)(T, Set...),
    sum_v<(PUSHMI_PP_IS_SAME(T, Set) ? 1 : 0)...> == 1
);

PUSHMI_CONCEPT_DEF(
    template(class... PropertyN)(
concept UniqueCategory)(PropertyN...),
    And<FoundExactlyOnce<
        property_category_t<PropertyN>,
        property_category_t<PropertyN>...>...>&& And<Property<PropertyN>...>
);

namespace detail {
template <PUSHMI_TYPE_CONSTRAINT(Property) P, class = property_category_t<P>>
struct property_set_element {};
} // namespace detail

template <class... PropertyN>
struct property_set : detail::property_set_element<PropertyN>... {
  static_assert(
      and_v<Property<PropertyN>...>,
      "property_set only supports types that match the Property concept");
  static_assert(
      UniqueCategory<PropertyN...>,
      "property_set has multiple properties from the same category");
  using properties = property_set;
};

PUSHMI_CONCEPT_DEF(
    template(class T)
concept PropertySet,
    detail::is_v<T, property_set>
);

// customization point for a type with properties

template <class T, class = void>
struct member_properties {
    using properties = property_set<>;
};

template <class T>
struct member_properties<T, void_t<typename T::properties>> {
  using properties = typename T::properties;
};

template <class T>
using __properties_t = typename member_properties<T>::properties;

// allow specializations to use enable_if to constrain
template <class T, class Void>
struct property_set_traits : std::conditional_t<
  std::is_same<T, std::decay_t<T>>::value,
  member_properties<T>,
  property_set_traits<std::decay_t<T>>>
{};

template <class T, class Target, class Void>
struct property_set_traits_disable : std::false_type {};

template <class T, class Target>
PUSHMI_INLINE_VAR constexpr bool property_set_traits_disable_v =
  property_set_traits_disable<T, Target>::value;

template <class T>
using properties_t =
  std::enable_if_t<
    PropertySet<__properties_t<property_set_traits<T>>>,
    __properties_t<property_set_traits<T>>>;

PUSHMI_CONCEPT_DEF(
  template(class T)
  concept Properties,
    PropertySet<__properties_t<property_set_traits<T>>>
);

// find property in the specified set that matches the category of the property
// specified.
namespace detail {
template <class PCategory, class POut>
POut __property_set_index_fn(
    property_set_element<POut, PCategory>*);

template <class PIn, class POut, class... Ps>
property_set<std::conditional_t<PUSHMI_PP_IS_SAME(Ps, POut), PIn, Ps>...>
__property_set_insert_fn(
    property_set<Ps...>,
    property_set_element<POut, property_category_t<PIn>>);

template <class PIn, class... Ps>
property_set<Ps..., PIn> __property_set_insert_fn(property_set<Ps...>, ...);

template <class PS, class P>
using property_set_insert_one_t =
    decltype(detail::__property_set_insert_fn<P>(PS{}, PS{}));

template <class PS0, class>
struct property_set_insert {
    // fix MSVC issue
    ~property_set_insert();
  using type = PS0;
};

template <class PS0, class P, class... P1>
struct property_set_insert<PS0, property_set<P, P1...>>
    : property_set_insert<
          property_set_insert_one_t<PS0, P>,
          property_set<P1...>> {
    // fix MSVC issue
    ~property_set_insert();
};

} // namespace detail

template <class PS, class P>
using property_set_index_t = std::enable_if_t<
    PropertySet<PS> && Property<P>,
    decltype(detail::__property_set_index_fn<property_category_t<P>>(std::declval<PS*>()))>;

template <class PS0, class PS1>
using property_set_insert_t = typename std::enable_if_t<
    PropertySet<PS0> && PropertySet<PS1>,
    detail::property_set_insert<PS0, PS1>>::type;

// query for properties on types with properties.

namespace detail {
template <class PIn, class POut>
std::is_base_of<PIn, POut> property_query_fn(
    property_set_element<POut, property_category_t<PIn>>*);
template <class PIn>
std::false_type property_query_fn(void*);

template <class PS, class... ExpectedN>
struct property_query_impl : bool_<and_v<bool_v<decltype(property_query_fn<ExpectedN>(
                                 (properties_t<PS>*)nullptr))>...>> {};
} // namespace detail

template <class PS, class... ExpectedN>
struct property_query : std::conditional_t<
                            Properties<PS> && And<Property<ExpectedN>...>,
                            detail::property_query_impl<PS, ExpectedN...>,
                            std::false_type> {};

template <class PS, class... ExpectedN>
PUSHMI_INLINE_VAR constexpr bool property_query_v =
    property_query<PS, ExpectedN...>::value;

// query for categories on types with properties.

namespace detail {
template <class CIn, class POut>
std::true_type category_query_fn(property_set_element<POut, CIn>*);
template <class C>
std::false_type category_query_fn(void*);

template <class PS, class... ExpectedN>
struct category_query_impl : bool_<and_v<bool_v<decltype(category_query_fn<ExpectedN>(
                                 (properties_t<PS>*)nullptr))>...>> {};
} // namespace detail

template <class PS, class... ExpectedN>
struct category_query : std::conditional_t<
                            Properties<PS> && !Or<Property<ExpectedN>...>,
                            detail::category_query_impl<PS, ExpectedN...>,
                            std::false_type> {};

template <class PS, class... ExpectedN>
PUSHMI_INLINE_VAR constexpr bool category_query_v =
    category_query<PS, ExpectedN...>::value;

} // namespace pushmi
} // namespace folly
