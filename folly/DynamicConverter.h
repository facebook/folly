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

// @author Nicholas Ormrod <njormrod@fb.com>
/**
 * DynamicConverter provides a simple, generic interface by which dynamic
 * objects can be converted to/from concrete C++ types.
 *
 * Can be used in conjunction with folly/json.h to read a JSON value (which
 * returns a folly::dynamic) and then turn that JSON value into a well-typed
 * representation.
 *
 * @file DynamicConverter.h
 * @refcode folly/docs/examples/folly/DynamicConverter.cpp
 */

#pragma once

#include <iterator>
#include <type_traits>

#include <boost/iterator/iterator_adaptor.hpp>

#include <folly/Likely.h>
#include <folly/Optional.h>
#include <folly/Traits.h>
#include <folly/Utility.h>
#include <folly/dynamic.h>
#include <folly/lang/Exception.h>

namespace folly {

/**
 * Return a well-typed representation of a dynamic.
 *
 * See docs/DynamicConverter.md for supported types and customization.
 *
 * @tparam T  A type representing the structure of the dynamic argument.
 * @refcode folly/docs/examples/folly/DynamicConverter.cpp
 */
template <typename T>
T convertTo(const dynamic&);

/**
 * Turn an arbitrary type into a dynamic.
 *
 * @see convertTo for customization
 */
template <typename T>
dynamic toDynamic(const T&);
} // namespace folly

namespace folly {

///////////////////////////////////////////////////////////////////////////////
// traits

namespace dynamicconverter_detail {

template <typename T>
using detect_member_type_value_type = typename T::value_type;
template <typename T>
using detect_member_type_iterator = typename T::iterator;
template <typename T>
using detect_member_type_mapped_type = typename T::mapped_type;
template <typename T>
using detect_member_type_key_type = typename T::key_type;
template <typename T>
using detect_like_pointer =
    decltype((static_cast<bool>(std::declval<const T&>()), *std::declval<const T&>()), void());
template <typename T>
using detect_like_optional =
    decltype(T(std::declval<typename T::value_type>()));

template <typename T>
struct iterator_class_is_container {
  typedef typename T::iterator some_iterator;
  enum {
    value = is_detected_v<detect_member_type_value_type, T> &&
        std::is_constructible<T, some_iterator, some_iterator>::value
  };
};

template <typename T>
using class_is_container = Conjunction<
    is_detected<detect_member_type_iterator, T>,
    iterator_class_is_container<T>>;

template <typename T>
using is_range = StrictConjunction<
    is_detected<detect_member_type_value_type, T>,
    is_detected<detect_member_type_iterator, T>>;

template <typename T>
using is_container = StrictConjunction<std::is_class<T>, class_is_container<T>>;

template <typename T>
using is_map = StrictConjunction<
    is_range<T>,
    is_detected<detect_member_type_mapped_type, T>>;

template <typename T>
using is_associative =
    StrictConjunction<is_range<T>, is_detected<detect_member_type_key_type, T>>;

template <typename T>
using is_like_pointer = Conjunction<
    // Exclude string literals.
    Negation<std::is_convertible<T, StringPiece>>,
    is_detected<detect_like_pointer, T>>;

template <typename T>
using is_optional = Conjunction<
    is_detected<detect_like_pointer, T>,
    is_detected<detect_like_optional, T>>;

} // namespace dynamicconverter_detail

///////////////////////////////////////////////////////////////////////////////
// custom iterators

/**
 * We have iterators that dereference to dynamics, but need iterators
 * that dereference to typename T.
 *
 * Implementation details:
 *   1. We cache the value of the dereference operator. This is necessary
 *      because boost::iterator_adaptor requires *it to return a
 *      reference.
 *   2. For const reasons, we cannot call operator= to refresh the
 *      cache: we must call the destructor then placement new.
 */

namespace dynamicconverter_detail {

template <typename T>
struct Dereferencer {
  static inline void derefToCache(
      Optional<T>* /* mem */, const dynamic::const_item_iterator& /* it */) {
    throw_exception<TypeError>("array", dynamic::Type::OBJECT);
  }

  static inline void derefToCache(
      Optional<T>* mem, const dynamic::const_iterator& it) {
    mem->emplace(convertTo<T>(*it));
  }
};

template <typename F, typename S>
struct Dereferencer<std::pair<F, S>> {
  static inline void derefToCache(
      Optional<std::pair<F, S>>* mem, const dynamic::const_item_iterator& it) {
    mem->emplace(convertTo<F>(it->first), convertTo<S>(it->second));
  }

  // Intentional duplication of the code in Dereferencer
  template <typename T>
  static inline void derefToCache(
      Optional<T>* mem, const dynamic::const_iterator& it) {
    mem->emplace(convertTo<T>(*it));
  }
};

template <typename T, typename It>
class Transformer
    : public boost::
          iterator_adaptor<Transformer<T, It>, It, typename T::value_type> {
  friend class boost::iterator_core_access;

  typedef typename T::value_type ttype;

  mutable Optional<ttype> cache_;

  void increment() {
    ++this->base_reference();
    cache_ = none;
  }

  ttype& dereference() const {
    if (!cache_) {
      Dereferencer<ttype>::derefToCache(&cache_, this->base_reference());
    }
    return cache_.value();
  }

 public:
  explicit Transformer(const It& it) : Transformer::iterator_adaptor_(it) {}

  ttype&& operator*() const { return std::move(dereference()); }
};

// conversion factory
template <typename T, typename It>
inline Transformer<T, It> conversionIterator(const It& it) {
  return Transformer<T, It>(it);
}

} // namespace dynamicconverter_detail

///////////////////////////////////////////////////////////////////////////////
// DynamicConverter specializations

/**
 * Each specialization of DynamicConverter has the function
 *     'static T convert(const dynamic&);'
 */

// default - intentionally unimplemented
template <typename T, typename Enable = void>
struct DynamicConverter;

// dynamic
template <>
struct DynamicConverter<dynamic> {
  static dynamic convert(const dynamic& d) { return d; }
};

// boolean
template <>
struct DynamicConverter<bool> {
  static bool convert(const dynamic& d) { return d.asBool(); }
};

// integrals
template <typename T>
struct DynamicConverter<
    T,
    typename std::enable_if<
        std::is_integral<T>::value && !std::is_same<T, bool>::value>::type> {
  static T convert(const dynamic& d) { return folly::to<T>(d.asInt()); }
};

// enums
template <typename T>
struct DynamicConverter<
    T,
    typename std::enable_if<std::is_enum<T>::value>::type> {
  static T convert(const dynamic& d) {
    using type = typename std::underlying_type<T>::type;
    return static_cast<T>(DynamicConverter<type>::convert(d));
  }
};

// floating point
template <typename T>
struct DynamicConverter<
    T,
    typename std::enable_if<std::is_floating_point<T>::value>::type> {
  static T convert(const dynamic& d) { return folly::to<T>(d.asDouble()); }
};

// fbstring
template <>
struct DynamicConverter<folly::fbstring> {
  static folly::fbstring convert(const dynamic& d) { return d.asString(); }
};

// std::string
template <>
struct DynamicConverter<std::string> {
  static std::string convert(const dynamic& d) { return d.asString(); }
};

// std::pair
template <typename F, typename S>
struct DynamicConverter<std::pair<F, S>> {
  static std::pair<F, S> convert(const dynamic& d) {
    if (d.isArray() && d.size() == 2) {
      return std::make_pair(convertTo<F>(d[0]), convertTo<S>(d[1]));
    } else if (d.isObject() && d.size() == 1) {
      auto it = d.items().begin();
      return std::make_pair(convertTo<F>(it->first), convertTo<S>(it->second));
    } else {
      throw_exception<TypeError>("array (size 2) or object (size 1)", d.type());
    }
  }
};

// optionals and other pointer-like types.
template <typename T>
struct DynamicConverter<
    T,
    typename std::enable_if<
        dynamicconverter_detail::is_optional<T>::value>::type> {
  static T convert(const dynamic& d) {
    if (d.isNull()) {
      return {};
    }
    return DynamicConverter<typename T::value_type>::convert(d);
  }
};

// non-associative containers
template <typename C>
struct DynamicConverter<
    C,
    typename std::enable_if<
        dynamicconverter_detail::is_container<C>::value &&
        !dynamicconverter_detail::is_associative<C>::value>::type> {
  static C convert(const dynamic& d) {
    if (d.isArray()) {
      return C(
          dynamicconverter_detail::conversionIterator<C>(d.begin()),
          dynamicconverter_detail::conversionIterator<C>(d.end()));
    } else if (d.isObject()) {
      return C(
          dynamicconverter_detail::conversionIterator<C>(d.items().begin()),
          dynamicconverter_detail::conversionIterator<C>(d.items().end()));
    } else {
      throw_exception<TypeError>("object or array", d.type());
    }
  }
};

// associative containers
template <typename C>
struct DynamicConverter<
    C,
    typename std::enable_if<
        dynamicconverter_detail::is_container<C>::value &&
        dynamicconverter_detail::is_associative<C>::value>::type> {
  static C convert(const dynamic& d) {
    C ret; // avoid direct initialization due to unordered_map's constructor
           // causing memory corruption if the iterator throws an exception
    if (d.isArray()) {
      ret.insert(
          dynamicconverter_detail::conversionIterator<C>(d.begin()),
          dynamicconverter_detail::conversionIterator<C>(d.end()));
    } else if (d.isObject()) {
      ret.insert(
          dynamicconverter_detail::conversionIterator<C>(d.items().begin()),
          dynamicconverter_detail::conversionIterator<C>(d.items().end()));
    } else {
      throw_exception<TypeError>("object or array", d.type());
    }
    return ret;
  }
};

///////////////////////////////////////////////////////////////////////////////
// DynamicConstructor specializations

/**
 * Each specialization of DynamicConstructor has the function
 *     'static dynamic construct(const C&);'
 */

// default
template <typename C, typename Enable = void>
struct DynamicConstructor {
  static dynamic construct(const C& x) { return dynamic(x); }
};

// identity
template <typename C>
struct DynamicConstructor<
    C,
    typename std::enable_if<std::is_same<C, dynamic>::value>::type> {
  static dynamic construct(const C& x) { return x; }
};

// enums
template <typename C>
struct DynamicConstructor<
    C,
    typename std::enable_if<std::is_enum<C>::value>::type> {
  static dynamic construct(const C& x) { return dynamic(to_underlying(x)); }
};

// maps
template <typename C>
struct DynamicConstructor<
    C,
    typename std::enable_if<
        !std::is_same<C, dynamic>::value &&
        dynamicconverter_detail::is_map<C>::value>::type> {
  static dynamic construct(const C& x) {
    dynamic d = dynamic::object;
    for (const auto& pair : x) {
      d.insert(toDynamic(pair.first), toDynamic(pair.second));
    }
    return d;
  }
};

// other ranges
template <typename C>
struct DynamicConstructor<
    C,
    typename std::enable_if<
        !std::is_same<C, dynamic>::value &&
        !dynamicconverter_detail::is_map<C>::value &&
        !std::is_constructible<StringPiece, const C&>::value &&
        dynamicconverter_detail::is_range<C>::value>::type> {
  static dynamic construct(const C& x) {
    dynamic d = dynamic::array;
    for (const auto& item : x) {
      d.push_back(toDynamic(item));
    }
    return d;
  }
};

// pair
template <typename A, typename B>
struct DynamicConstructor<std::pair<A, B>, void> {
  static dynamic construct(const std::pair<A, B>& x) {
    dynamic d = dynamic::array;
    d.push_back(toDynamic(x.first));
    d.push_back(toDynamic(x.second));
    return d;
  }
};

// optionals and other pointer-like types.
template <typename T>
struct DynamicConstructor<
    T,
    typename std::enable_if<
        dynamicconverter_detail::is_like_pointer<T>::value>::type> {
  static dynamic construct(const T& x) { return x ? toDynamic(*x) : dynamic(); }
};

// vector<bool>
template <>
struct DynamicConstructor<std::vector<bool>, void> {
  static dynamic construct(const std::vector<bool>& x) {
    dynamic d = dynamic::array;
    // Intentionally specifying the type as bool here.
    // std::vector<bool>'s iterators return a proxy which is a prvalue
    // and hence cannot bind to an lvalue reference such as auto&
    for (bool item : x) {
      d.push_back(toDynamic(item));
    }
    return d;
  }
};

///////////////////////////////////////////////////////////////////////////////
// implementation

template <typename T>
T convertTo(const dynamic& d) {
  return DynamicConverter<typename std::remove_cv<T>::type>::convert(d);
}

template <typename T>
dynamic toDynamic(const T& x) {
  return DynamicConstructor<typename std::remove_cv<T>::type>::construct(x);
}

} // namespace folly
