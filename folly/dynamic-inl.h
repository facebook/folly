/*
 * Copyright 2013 Facebook, Inc.
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

#ifndef FOLLY_DYNAMIC_INL_H_
#define FOLLY_DYNAMIC_INL_H_

#include <functional>
#include <boost/iterator/iterator_adaptor.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include "folly/Likely.h"
#include "folly/Conv.h"
#include "folly/Format.h"

//////////////////////////////////////////////////////////////////////

namespace std {

template<>
struct hash< ::folly::dynamic> {
  size_t operator()(::folly::dynamic const& d) const {
    return d.hash();
  }
};

}

//////////////////////////////////////////////////////////////////////

// This is a higher-order preprocessor macro to aid going from runtime
// types to the compile time type system.
#define FB_DYNAMIC_APPLY(type, apply) do {         \
  switch ((type)) {                             \
  case NULLT:   apply(void*);          break;   \
  case ARRAY:   apply(Array);          break;   \
  case BOOL:    apply(bool);           break;   \
  case DOUBLE:  apply(double);         break;   \
  case INT64:   apply(int64_t);        break;   \
  case OBJECT:  apply(ObjectImpl);     break;   \
  case STRING:  apply(fbstring);       break;   \
  default:      CHECK(0); abort();              \
  }                                             \
} while (0)

//////////////////////////////////////////////////////////////////////

namespace folly {

struct TypeError : std::runtime_error {
  explicit TypeError(const std::string& expected, dynamic::Type actual)
    : std::runtime_error(to<std::string>("TypeError: expected dynamic "
        "type `", expected, '\'', ", but had type `",
        dynamic::typeName(actual), '\''))
  {}
  explicit TypeError(const std::string& expected,
      dynamic::Type actual1, dynamic::Type actual2)
    : std::runtime_error(to<std::string>("TypeError: expected dynamic "
        "types `", expected, '\'', ", but had types `",
        dynamic::typeName(actual1), "' and `", dynamic::typeName(actual2),
        '\''))
  {}
};


//////////////////////////////////////////////////////////////////////

namespace detail {

  // This helper is used in destroy() to be able to run destructors on
  // types like "int64_t" without a compiler error.
  struct Destroy {
    template<class T> static void destroy(T* t) { t->~T(); }
  };

  /*
   * The enable_if junk here is necessary to avoid ambiguous
   * conversions relating to bool and double when you implicitly
   * convert an int or long to a dynamic.
   */
  template<class T, class Enable = void> struct ConversionHelper;
  template<class T>
  struct ConversionHelper<
    T,
    typename std::enable_if<
      std::is_integral<T>::value && !std::is_same<T,bool>::value
    >::type
  > {
    typedef int64_t type;
  };
  template<class T>
  struct ConversionHelper<
    T,
    typename std::enable_if<
      (!std::is_integral<T>::value || std::is_same<T,bool>::value) &&
      !std::is_same<T,std::nullptr_t>::value
    >::type
  > {
    typedef T type;
  };
  template<class T>
  struct ConversionHelper<
    T,
    typename std::enable_if<
      std::is_same<T,std::nullptr_t>::value
    >::type
  > {
    typedef void* type;
  };

  /*
   * Helper for implementing numeric conversions in operators on
   * numbers.  Just promotes to double when one of the arguments is
   * double, or throws if either is not a numeric type.
   */
  template<template<class> class Op>
  dynamic numericOp(dynamic const& a, dynamic const& b) {
    if (!a.isNumber() || !b.isNumber()) {
      throw TypeError("numeric", a.type(), b.type());
    }
    if (a.type() != b.type()) {
      auto& integ  = a.isInt() ? a : b;
      auto& nonint = a.isInt() ? b : a;
      return Op<double>()(to<double>(integ.asInt()), nonint.asDouble());
    }
    if (a.isDouble()) {
      return Op<double>()(a.asDouble(), b.asDouble());
    }
    return Op<int64_t>()(a.asInt(), b.asInt());
  }

}

//////////////////////////////////////////////////////////////////////

/*
 * We're doing this instead of a simple member typedef to avoid the
 * undefined behavior of parameterizing std::unordered_map<> with an
 * incomplete type.
 *
 * Note: Later we may add separate order tracking here (a multi-index
 * type of thing.)
 */
struct dynamic::ObjectImpl : std::unordered_map<dynamic, dynamic> {};

//////////////////////////////////////////////////////////////////////

// Helper object for creating objects conveniently.  See object and
// the dynamic::dynamic(ObjectMaker&&) ctor.
struct dynamic::ObjectMaker {
  friend struct dynamic;

  explicit ObjectMaker() : val_(dynamic::object) {}
  explicit ObjectMaker(dynamic const& key, dynamic val)
    : val_(dynamic::object)
  {
    val_.insert(key, std::move(val));
  }
  explicit ObjectMaker(dynamic&& key, dynamic val)
    : val_(dynamic::object)
  {
    val_.insert(std::move(key), std::move(val));
  }

  // Make sure no one tries to save one of these into an lvalue with
  // auto or anything like that.
  ObjectMaker(ObjectMaker&&) = default;
  ObjectMaker(ObjectMaker const&) = delete;
  ObjectMaker& operator=(ObjectMaker const&) = delete;
  ObjectMaker& operator=(ObjectMaker&&) = delete;

  // These return rvalue-references instead of lvalue-references to allow
  // constructs like this to moved instead of copied:
  //  dynamic a = dynamic::object("a", "b")("c", "d")
  ObjectMaker&& operator()(dynamic const& key, dynamic val) {
    val_.insert(key, std::move(val));
    return std::move(*this);
  }

  ObjectMaker&& operator()(dynamic&& key, dynamic val) {
    val_.insert(std::move(key), std::move(val));
    return std::move(*this);
  }

private:
  dynamic val_;
};

// This looks like a case for perfect forwarding, but our use of
// std::initializer_list for constructing dynamic arrays makes it less
// functional than doing this manually.
inline dynamic::ObjectMaker dynamic::object() { return ObjectMaker(); }
inline dynamic::ObjectMaker dynamic::object(dynamic&& a, dynamic&& b) {
  return ObjectMaker(std::move(a), std::move(b));
}
inline dynamic::ObjectMaker dynamic::object(dynamic const& a, dynamic&& b) {
  return ObjectMaker(a, std::move(b));
}
inline dynamic::ObjectMaker dynamic::object(dynamic&& a, dynamic const& b) {
  return ObjectMaker(std::move(a), b);
}
inline dynamic::ObjectMaker
dynamic::object(dynamic const& a, dynamic const& b) {
  return ObjectMaker(a, b);
}

//////////////////////////////////////////////////////////////////////

struct dynamic::const_item_iterator
  : boost::iterator_adaptor<dynamic::const_item_iterator,
                            dynamic::ObjectImpl::const_iterator> {
  /* implicit */ const_item_iterator(base_type b) : iterator_adaptor_(b) { }

 private:
  friend class boost::iterator_core_access;
};

struct dynamic::const_key_iterator
  : boost::iterator_adaptor<dynamic::const_key_iterator,
                            dynamic::ObjectImpl::const_iterator,
                            dynamic const> {
  /* implicit */ const_key_iterator(base_type b) : iterator_adaptor_(b) { }

 private:
  dynamic const& dereference() const {
    return base_reference()->first;
  }
  friend class boost::iterator_core_access;
};

struct dynamic::const_value_iterator
  : boost::iterator_adaptor<dynamic::const_value_iterator,
                            dynamic::ObjectImpl::const_iterator,
                            dynamic const> {
  /* implicit */ const_value_iterator(base_type b) : iterator_adaptor_(b) { }

 private:
  dynamic const& dereference() const {
    return base_reference()->second;
  }
  friend class boost::iterator_core_access;
};

//////////////////////////////////////////////////////////////////////

inline dynamic::dynamic(ObjectMaker (*)())
  : type_(OBJECT)
{
  new (getAddress<ObjectImpl>()) ObjectImpl();
}

inline dynamic::dynamic(char const* s)
  : type_(STRING)
{
  new (&u_.string) fbstring(s);
}

inline dynamic::dynamic(std::string const& s)
  : type_(STRING)
{
  new (&u_.string) fbstring(s);
}

inline dynamic::dynamic(std::initializer_list<dynamic> il)
  : type_(ARRAY)
{
  new (&u_.array) Array(il.begin(), il.end());
}

inline dynamic::dynamic(ObjectMaker&& maker)
  : type_(OBJECT)
{
  new (getAddress<ObjectImpl>())
    ObjectImpl(std::move(*maker.val_.getAddress<ObjectImpl>()));
}

inline dynamic::dynamic(dynamic const& o)
  : type_(NULLT)
{
  *this = o;
}

inline dynamic::dynamic(dynamic&& o)
  : type_(NULLT)
{
  *this = std::move(o);
}

inline dynamic::~dynamic() { destroy(); }

template<class T>
dynamic::dynamic(T t) {
  typedef typename detail::ConversionHelper<T>::type U;
  type_ = TypeInfo<U>::type;
  new (getAddress<U>()) U(std::move(t));
}

template<class Iterator>
dynamic::dynamic(Iterator first, Iterator last)
  : type_(ARRAY)
{
  new (&u_.array) Array(first, last);
}

//////////////////////////////////////////////////////////////////////

inline dynamic::const_iterator dynamic::begin() const {
  return get<Array>().begin();
}
inline dynamic::const_iterator dynamic::end() const {
  return get<Array>().end();
}

template <class It>
struct dynamic::IterableProxy {
  typedef It const_iterator;
  typedef typename It::value_type value_type;

  /* implicit */ IterableProxy(const dynamic::ObjectImpl* o) : o_(o) { }

  It begin() const {
    return o_->begin();
  }

  It end() const {
    return o_->end();
  }

 private:
  const dynamic::ObjectImpl* o_;
};

inline dynamic::IterableProxy<dynamic::const_key_iterator> dynamic::keys()
  const {
  return &(get<ObjectImpl>());
}

inline dynamic::IterableProxy<dynamic::const_value_iterator> dynamic::values()
  const {
  return &(get<ObjectImpl>());
}

inline dynamic::IterableProxy<dynamic::const_item_iterator> dynamic::items()
  const {
  return &(get<ObjectImpl>());
}

inline bool dynamic::isString() const { return get_nothrow<fbstring>(); }
inline bool dynamic::isObject() const { return get_nothrow<ObjectImpl>(); }
inline bool dynamic::isBool()   const { return get_nothrow<bool>(); }
inline bool dynamic::isArray()  const { return get_nothrow<Array>(); }
inline bool dynamic::isDouble() const { return get_nothrow<double>(); }
inline bool dynamic::isInt()    const { return get_nothrow<int64_t>(); }
inline bool dynamic::isNull()   const { return get_nothrow<void*>(); }
inline bool dynamic::isNumber() const { return isInt() || isDouble(); }

inline dynamic::Type dynamic::type() const {
  return type_;
}

inline fbstring dynamic::asString() const { return asImpl<fbstring>(); }
inline double   dynamic::asDouble() const { return asImpl<double>(); }
inline int64_t  dynamic::asInt()    const { return asImpl<int64_t>(); }
inline bool     dynamic::asBool()   const { return asImpl<bool>(); }

template<class T>
struct dynamic::CompareOp {
  static bool comp(T const& a, T const& b) { return a < b; }
};
template<>
struct dynamic::CompareOp<dynamic::ObjectImpl> {
  static bool comp(ObjectImpl const& a, ObjectImpl const& b) {
    // This code never executes; it is just here for the compiler.
    return false;
  }
};

inline bool dynamic::operator<(dynamic const& o) const {
  if (UNLIKELY(type_ == OBJECT || o.type_ == OBJECT)) {
    throw TypeError("object", type_);
  }
  if (type_ != o.type_) {
    return type_ < o.type_;
  }

#define FB_X(T) return CompareOp<T>::comp(*getAddress<T>(),   \
                                          *o.getAddress<T>())
  FB_DYNAMIC_APPLY(type_, FB_X);
#undef FB_X
}

inline bool dynamic::operator==(dynamic const& o) const {
  if (type() != o.type()) {
    if (isNumber() && o.isNumber()) {
      auto& integ = isInt() ? *this : o;
      auto& doubl = isInt() ? o     : *this;
      return integ.asInt() == doubl.asDouble();
    }
    return false;
  }

#define FB_X(T) return *getAddress<T>() == *o.getAddress<T>();
  FB_DYNAMIC_APPLY(type_, FB_X);
#undef FB_X
}

inline dynamic& dynamic::operator+=(dynamic const& o) {
  if (type() == STRING && o.type() == STRING) {
    *getAddress<fbstring>() += *o.getAddress<fbstring>();
    return *this;
  }
  *this = detail::numericOp<std::plus>(*this, o);
  return *this;
}

inline dynamic& dynamic::operator-=(dynamic const& o) {
  *this = detail::numericOp<std::minus>(*this, o);
  return *this;
}

inline dynamic& dynamic::operator*=(dynamic const& o) {
  *this = detail::numericOp<std::multiplies>(*this, o);
  return *this;
}

inline dynamic& dynamic::operator/=(dynamic const& o) {
  *this = detail::numericOp<std::divides>(*this, o);
  return *this;
}

#define FB_DYNAMIC_INTEGER_OP(op)                           \
  inline dynamic& dynamic::operator op(dynamic const& o) {  \
    if (!isInt() || !o.isInt()) {                           \
      throw TypeError("int64", type(), o.type());           \
    }                                                       \
    *getAddress<int64_t>() op o.asInt();                    \
    return *this;                                           \
  }

FB_DYNAMIC_INTEGER_OP(%=)
FB_DYNAMIC_INTEGER_OP(|=)
FB_DYNAMIC_INTEGER_OP(&=)
FB_DYNAMIC_INTEGER_OP(^=)

#undef FB_DYNAMIC_INTEGER_OP

inline dynamic& dynamic::operator++() {
  ++get<int64_t>();
  return *this;
}

inline dynamic& dynamic::operator--() {
  --get<int64_t>();
  return *this;
}

inline dynamic& dynamic::operator=(dynamic const& o) {
  if (&o != this) {
    destroy();
#define FB_X(T) new (getAddress<T>()) T(*o.getAddress<T>())
    FB_DYNAMIC_APPLY(o.type_, FB_X);
#undef FB_X
    type_ = o.type_;
  }
  return *this;
}

inline dynamic& dynamic::operator=(dynamic&& o) {
  if (&o != this) {
    destroy();
#define FB_X(T) new (getAddress<T>()) T(std::move(*o.getAddress<T>()))
    FB_DYNAMIC_APPLY(o.type_, FB_X);
#undef FB_X
    type_ = o.type_;
  }
  return *this;
}

inline dynamic& dynamic::operator[](dynamic const& k) {
  if (!isObject() && !isArray()) {
    throw TypeError("object/array", type());
  }
  if (isArray()) {
    return at(k);
  }
  auto& obj = get<ObjectImpl>();
  auto ret = obj.insert({k, nullptr});
  return ret.first->second;
}

inline dynamic const& dynamic::operator[](dynamic const& idx) const {
  return at(idx);
}

inline dynamic dynamic::getDefault(const dynamic& k, const dynamic& v) const {
  auto& obj = get<ObjectImpl>();
  auto it = obj.find(k);
  return it == obj.end() ? v : it->second;
}

inline dynamic&& dynamic::getDefault(const dynamic& k, dynamic&& v) const {
  auto& obj = get<ObjectImpl>();
  auto it = obj.find(k);
  if (it != obj.end()) {
    v = it->second;
  }

  return std::move(v);
}

template<class K, class V> inline dynamic& dynamic::setDefault(K&& k, V&& v) {
  auto& obj = get<ObjectImpl>();
  return obj.insert(std::make_pair(std::forward<K>(k),
                                   std::forward<V>(v))).first->second;
}

inline dynamic const& dynamic::at(dynamic const& idx) const {
  return const_cast<dynamic*>(this)->at(idx);
}

inline dynamic& dynamic::at(dynamic const& idx) {
  if (!isObject() && !isArray()) {
    throw TypeError("object/array", type());
  }

  if (auto* parray = get_nothrow<Array>()) {
    if (idx >= parray->size()) {
      throw std::out_of_range("out of range in dynamic array");
    }
    if (!idx.isInt()) {
      throw TypeError("int64", idx.type());
    }
    return (*parray)[idx.asInt()];
  }

  assert(get_nothrow<ObjectImpl>());
  auto it = find(idx);
  if (it == items().end()) {
    throw std::out_of_range(to<std::string>(
        "couldn't find key ", idx.asString(), " in dynamic object"));
  }
  return const_cast<dynamic&>(it->second);
}

inline bool dynamic::empty() const {
  if (isNull()) {
    return true;
  }
  return !size();
}

inline std::size_t dynamic::size() const {
  if (auto* ar = get_nothrow<Array>()) {
    return ar->size();
  }
  if (auto* obj = get_nothrow<ObjectImpl>()) {
    return obj->size();
  }
  if (auto* str = get_nothrow<fbstring>()) {
    return str->size();
  }
  throw TypeError("array/object", type());
}

inline std::size_t dynamic::count(dynamic const& key) const {
  return find(key) != items().end();
}

inline dynamic::const_item_iterator dynamic::find(dynamic const& key) const {
  return get<ObjectImpl>().find(key);
}

template<class K, class V> inline void dynamic::insert(K&& key, V&& val) {
  auto& obj = get<ObjectImpl>();
  auto rv = obj.insert(std::make_pair(std::forward<K>(key),
                                      std::forward<V>(val)));
  if (!rv.second) {
    // note, the second use of std:forward<V>(val) is only correct
    // if the first one did not result in a move. obj[key] = val
    // would be preferrable but doesn't compile because dynamic
    // is (intentionally) not default constructable
    rv.first->second = std::forward<V>(val);
  }
}

inline std::size_t dynamic::erase(dynamic const& key) {
  auto& obj = get<ObjectImpl>();
  return obj.erase(key);
}

inline dynamic::const_iterator dynamic::erase(const_iterator it) {
  auto& arr = get<Array>();
  // std::vector doesn't have an erase method that works on const iterators,
  // even though the standard says it should, so this hack converts to a
  // non-const iterator before calling erase.
  return get<Array>().erase(arr.begin() + (it - arr.begin()));
}

inline dynamic::const_iterator
dynamic::erase(const_iterator first, const_iterator last) {
  auto& arr = get<Array>();
  return get<Array>().erase(
    arr.begin() + (first - arr.begin()),
    arr.begin() + (last - arr.begin()));
}

inline dynamic::const_key_iterator dynamic::erase(const_key_iterator it) {
  return const_key_iterator(get<ObjectImpl>().erase(it.base()));
}

inline dynamic::const_key_iterator dynamic::erase(const_key_iterator first,
                                                  const_key_iterator last) {
  return const_key_iterator(get<ObjectImpl>().erase(first.base(),
                                                    last.base()));
}

inline dynamic::const_value_iterator dynamic::erase(const_value_iterator it) {
  return const_value_iterator(get<ObjectImpl>().erase(it.base()));
}

inline dynamic::const_value_iterator dynamic::erase(const_value_iterator first,
                                                    const_value_iterator last) {
  return const_value_iterator(get<ObjectImpl>().erase(first.base(),
                                                      last.base()));
}

inline dynamic::const_item_iterator dynamic::erase(const_item_iterator it) {
  return const_item_iterator(get<ObjectImpl>().erase(it.base()));
}

inline dynamic::const_item_iterator dynamic::erase(const_item_iterator first,
                                                   const_item_iterator last) {
  return const_item_iterator(get<ObjectImpl>().erase(first.base(),
                                                     last.base()));
}

inline void dynamic::resize(std::size_t sz, dynamic const& c) {
  auto& array = get<Array>();
  array.resize(sz, c);
}

inline void dynamic::push_back(dynamic const& v) {
  auto& array = get<Array>();
  array.push_back(v);
}

inline void dynamic::push_back(dynamic&& v) {
  auto& array = get<Array>();
  array.push_back(std::move(v));
}

inline void dynamic::pop_back() {
  auto& array = get<Array>();
  array.pop_back();
}

inline std::size_t dynamic::hash() const {
  switch (type()) {
  case OBJECT:
  case ARRAY:
  case NULLT:
    throw TypeError("not null/object/array", type());
  case INT64:
    return std::hash<int64_t>()(asInt());
  case DOUBLE:
    return std::hash<double>()(asDouble());
  case BOOL:
    return std::hash<bool>()(asBool());
  case STRING:
    return std::hash<fbstring>()(asString());
  default:
    CHECK(0); abort();
  }
}

//////////////////////////////////////////////////////////////////////

template<class T> struct dynamic::TypeInfo {
  static char const name[];
  static Type const type;
};

#define FB_DEC_TYPE(T)                                      \
  template<> char const dynamic::TypeInfo<T>::name[];       \
  template<> dynamic::Type const dynamic::TypeInfo<T>::type

FB_DEC_TYPE(void*);
FB_DEC_TYPE(bool);
FB_DEC_TYPE(fbstring);
FB_DEC_TYPE(dynamic::Array);
FB_DEC_TYPE(double);
FB_DEC_TYPE(int64_t);
FB_DEC_TYPE(dynamic::ObjectImpl);

#undef FB_DEC_TYPE

template<class T>
T dynamic::asImpl() const {
  switch (type()) {
  case INT64:    return to<T>(*get_nothrow<int64_t>());
  case DOUBLE:   return to<T>(*get_nothrow<double>());
  case BOOL:     return to<T>(*get_nothrow<bool>());
  case STRING:   return to<T>(*get_nothrow<fbstring>());
  default:
    throw TypeError("int/double/bool/string", type());
  }
}

// Return a T* to our type, or null if we're not that type.
template<class T>
T* dynamic::get_nothrow() {
  if (type_ != TypeInfo<T>::type) {
    return nullptr;
  }
  return getAddress<T>();
}

template<class T>
T const* dynamic::get_nothrow() const {
  return const_cast<dynamic*>(this)->get_nothrow<T>();
}

// Return T* for where we can put a T, without type checking.  (Memory
// might be uninitialized, even.)
template<class T>
T* dynamic::getAddress() {
  return GetAddrImpl<T>::get(u_);
}

template<class T>
T const* dynamic::getAddress() const {
  return const_cast<dynamic*>(this)->getAddress<T>();
}

template<class T> struct dynamic::GetAddrImpl {};
template<> struct dynamic::GetAddrImpl<void*> {
  static void** get(Data& d) { return &d.nul; }
};
template<> struct dynamic::GetAddrImpl<dynamic::Array> {
  static Array* get(Data& d) { return &d.array; }
};
template<> struct dynamic::GetAddrImpl<bool> {
  static bool* get(Data& d) { return &d.boolean; }
};
template<> struct dynamic::GetAddrImpl<int64_t> {
  static int64_t* get(Data& d) { return &d.integer; }
};
template<> struct dynamic::GetAddrImpl<double> {
  static double* get(Data& d) { return &d.doubl; }
};
template<> struct dynamic::GetAddrImpl<fbstring> {
  static fbstring* get(Data& d) { return &d.string; }
};
template<> struct dynamic::GetAddrImpl<dynamic::ObjectImpl> {
  static_assert(sizeof(ObjectImpl) <= sizeof(Data::objectBuffer),
    "In your implementation, std::unordered_map<> apparently takes different"
    " amount of space depending on its template parameters.  This is "
    "weird.  Make objectBuffer bigger if you want to compile dynamic.");

  static ObjectImpl* get(Data& d) {
    void* data = &d.objectBuffer;
    return static_cast<ObjectImpl*>(data);
  }
};

template<class T>
T& dynamic::get() {
  if (auto* p = get_nothrow<T>()) {
    return *p;
  }
  throw TypeError(TypeInfo<T>::name, type());
}

template<class T>
T const& dynamic::get() const {
  return const_cast<dynamic*>(this)->get<T>();
}

inline char const* dynamic::typeName(Type t) {
#define FB_X(T) return TypeInfo<T>::name
  FB_DYNAMIC_APPLY(t, FB_X);
#undef FB_X
}

inline void dynamic::destroy() {
  // This short-circuit speeds up some microbenchmarks.
  if (type_ == NULLT) return;

#define FB_X(T) detail::Destroy::destroy(getAddress<T>())
  FB_DYNAMIC_APPLY(type_, FB_X);
#undef FB_X
  type_ = NULLT;
  u_.nul = nullptr;
}

//////////////////////////////////////////////////////////////////////

/*
 * Helper for implementing operator<<.  Throws if the type shouldn't
 * support it.
 */
template<class T>
struct dynamic::PrintImpl {
  static void print(dynamic const&, std::ostream& out, T const& t) {
    out << t;
  }
};
template<>
struct dynamic::PrintImpl<dynamic::ObjectImpl> {
  static void print(dynamic const& d,
                    std::ostream& out,
                    dynamic::ObjectImpl const&) {
    d.print_as_pseudo_json(out);
  }
};
template<>
struct dynamic::PrintImpl<dynamic::Array> {
  static void print(dynamic const& d,
                    std::ostream& out,
                    dynamic::Array const&) {
    d.print_as_pseudo_json(out);
  }
};

inline void dynamic::print(std::ostream& out) const {
#define FB_X(T) PrintImpl<T>::print(*this, out, *getAddress<T>())
  FB_DYNAMIC_APPLY(type_, FB_X);
#undef FB_X
}

inline std::ostream& operator<<(std::ostream& out, dynamic const& d) {
  d.print(out);
  return out;
}

//////////////////////////////////////////////////////////////////////

// Secialization of FormatValue so dynamic objects can be formatted
template <>
class FormatValue<dynamic> {
 public:
  explicit FormatValue(const dynamic& val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    switch (val_.type()) {
    case dynamic::NULLT:
      FormatValue<std::nullptr_t>(nullptr).format(arg, cb);
      break;
    case dynamic::BOOL:
      FormatValue<bool>(val_.asBool()).format(arg, cb);
      break;
    case dynamic::INT64:
      FormatValue<int64_t>(val_.asInt()).format(arg, cb);
      break;
    case dynamic::STRING:
      FormatValue<fbstring>(val_.asString()).format(arg, cb);
      break;
    case dynamic::DOUBLE:
      FormatValue<double>(val_.asDouble()).format(arg, cb);
      break;
    case dynamic::ARRAY:
      FormatValue(val_.at(arg.splitIntKey())).format(arg, cb);
      break;
    case dynamic::OBJECT:
      FormatValue(val_.at(arg.splitKey().toFbstring())).format(arg, cb);
      break;
    }
  }

 private:
  const dynamic& val_;
};

}

#undef FB_DYNAMIC_APPLY

#endif
