/*
 * Copyright 2012 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// @author Nicholas Ormrod <njormrod@fb.com>

#ifndef DYNAMIC_CONVERTER_H
#define DYNAMIC_CONVERTER_H

#include "folly/dynamic.h"
namespace folly {
  template <typename T> T convertTo(const dynamic&);
}

/**
 * convertTo returns a well-typed representation of the input dynamic.
 *
 * Example:
 *
 *   dynamic d = { { 1, 2, 3 }, { 4, 5 } }; // a vector of vector of int
 *   auto vvi = convertTo<fbvector<fbvector<int>>>(d);
 *
 * See docs/DynamicConverter.md for supported types and customization
 */


#include <type_traits>
#include <iterator>
#include <boost/iterator/iterator_adaptor.hpp>
#include <boost/mpl/has_xxx.hpp>
#include "folly/Likely.h"

namespace folly {

///////////////////////////////////////////////////////////////////////////////
// traits

namespace dynamicconverter_detail {

BOOST_MPL_HAS_XXX_TRAIT_DEF(value_type);
BOOST_MPL_HAS_XXX_TRAIT_DEF(iterator);

template <typename T> struct class_is_container {
  typedef std::reverse_iterator<T*> some_iterator;
  enum { value = has_value_type<T>::value &&
                 has_iterator<T>::value &&
              std::is_constructible<T, some_iterator, some_iterator>::value };
};

template <typename T> struct is_container
  : std::conditional<
      std::is_class<T>::value,
      class_is_container<T>,
      std::false_type
    >::type {};

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

template<typename T>
struct Dereferencer {
  static inline void
  derefToCache(T* mem, const dynamic::const_item_iterator& it) {
    throw TypeError("array", dynamic::Type::OBJECT);
  }

  static inline void derefToCache(T* mem, const dynamic::const_iterator& it) {
    new (mem) T(convertTo<T>(*it));
  }
};

template<typename F, typename S>
struct Dereferencer<std::pair<F, S>> {
  static inline void
  derefToCache(std::pair<F, S>* mem, const dynamic::const_item_iterator& it) {
    new (mem) std::pair<F, S>(
        convertTo<F>(it->first), convertTo<S>(it->second)
    );
  }

  // Intentional duplication of the code in Dereferencer
  template <typename T>
  static inline void derefToCache(T* mem, const dynamic::const_iterator& it) {
    new (mem) T(convertTo<T>(*it));
  }
};

template <typename T, typename It>
class Transformer : public boost::iterator_adaptor<
                             Transformer<T, It>,
                             It,
                             typename T::value_type
                           > {
  friend class boost::iterator_core_access;

  typedef typename T::value_type ttype;

  mutable ttype cache_;
  mutable bool valid_;

  void increment() {
    ++this->base_reference();
    valid_ = false;
  }

  ttype& dereference() const {
    if (LIKELY(!valid_)) {
      cache_.~ttype();
      Dereferencer<ttype>::derefToCache(&cache_, this->base_reference());
      valid_ = true;
    }
    return cache_;
  }

public:
  explicit Transformer(const It& it)
    : Transformer::iterator_adaptor_(it), valid_(false) {}
};

// conversion factory
template <typename T, typename It>
static inline std::move_iterator<Transformer<T, It>>
conversionIterator(const It& it) {
  return std::make_move_iterator(Transformer<T, It>(it));
}

} // namespace dynamicconverter_detail

///////////////////////////////////////////////////////////////////////////////
// DynamicConverter specializations

template <typename T, typename Enable = void> struct DynamicConverter;

/**
 * Each specialization of DynamicConverter has the function
 *     'static T convert(const dynamic& d);'
 */

// boolean
template <>
struct DynamicConverter<bool> {
  static bool convert(const dynamic& d) {
    return d.asBool();
  }
};

// integrals
template <typename T>
struct DynamicConverter<T,
    typename std::enable_if<std::is_integral<T>::value &&
                            !std::is_same<T, bool>::value>::type> {
  static T convert(const dynamic& d) {
    return static_cast<T>(d.asInt());
  }
};

// floating point
template <typename T>
struct DynamicConverter<T,
    typename std::enable_if<std::is_floating_point<T>::value>::type> {
  static T convert(const dynamic& d) {
    return static_cast<T>(d.asDouble());
  }
};

// fbstring
template <>
struct DynamicConverter<folly::fbstring> {
  static folly::fbstring convert(const dynamic& d) {
    return d.asString();
  }
};

// std::string
template <>
struct DynamicConverter<std::string> {
  static std::string convert(const dynamic& d) {
    return d.asString().toStdString();
  }
};

// std::pair
template <typename F, typename S>
struct DynamicConverter<std::pair<F,S>> {
  static std::pair<F, S> convert(const dynamic& d) {
    if (d.isArray() && d.size() == 2) {
      return std::make_pair(convertTo<F>(d[0]), convertTo<S>(d[1]));
    } else if (d.isObject() && d.size() == 1) {
      auto it = d.items().begin();
      return std::make_pair(convertTo<F>(it->first), convertTo<S>(it->second));
    } else {
      throw TypeError("array (size 2) or object (size 1)", d.type());
    }
  }
};

// containers
template <typename C>
struct DynamicConverter<C,
    typename std::enable_if<
      dynamicconverter_detail::is_container<C>::value>::type> {
  static C convert(const dynamic& d) {
    if (d.isArray()) {
      return C(dynamicconverter_detail::conversionIterator<C>(d.begin()),
               dynamicconverter_detail::conversionIterator<C>(d.end()));
    } else if (d.isObject()) {
      return C(dynamicconverter_detail::conversionIterator<C>
                 (d.items().begin()),
               dynamicconverter_detail::conversionIterator<C>
                 (d.items().end()));
    } else {
      throw TypeError("object or array", d.type());
    }
  }
};

///////////////////////////////////////////////////////////////////////////////
// convertTo implementation

template <typename T>
T convertTo(const dynamic& d) {
  return DynamicConverter<typename std::remove_cv<T>::type>::convert(d);
}

} // namespace folly

#endif // DYNAMIC_CONVERTER_H

