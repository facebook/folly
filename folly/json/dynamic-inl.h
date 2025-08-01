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

#include <folly/CPortability.h>
#include <folly/Conv.h>
#include <folly/Format.h>
#include <folly/Likely.h>
#include <folly/Portability.h>
#include <folly/detail/Iterators.h>
#include <folly/lang/Exception.h>

namespace folly {
namespace detail {

struct DynamicHasher {
  using is_transparent = void;
  using folly_is_avalanching = std::true_type;

  size_t operator()(dynamic const& d) const { return d.hash(); }

  template <typename T>
  std::enable_if_t<std::is_convertible<T, StringPiece>::value, size_t>
  operator()(T const& val) const {
    // keep consistent with dynamic::hash() for strings
    return Hash()(static_cast<StringPiece>(val));
  }
};

struct DynamicKeyEqual {
  using is_transparent = void;

  bool operator()(const dynamic& lhs, const dynamic& rhs) const {
    return std::equal_to<dynamic>()(lhs, rhs);
  }

  // Dynamic objects contains a map<dynamic, dynamic>. At least one of the
  // operands should be a dynamic. Hence, an operator() where both operands are
  // convertible to StringPiece is unnecessary.
  template <typename A, typename B>
  std::enable_if_t<
      std::is_convertible<A, StringPiece>::value &&
          std::is_convertible<B, StringPiece>::value,
      bool>
  operator()(A const& lhs, B const& rhs) const = delete;

  template <typename A>
  std::enable_if_t<std::is_convertible<A, StringPiece>::value, bool> operator()(
      A const& lhs, dynamic const& rhs) const {
    return FOLLY_LIKELY(rhs.type() == dynamic::Type::STRING) &&
        std::equal_to<StringPiece>()(lhs, rhs.stringPiece());
  }

  template <typename B>
  std::enable_if_t<std::is_convertible<B, StringPiece>::value, bool> operator()(
      dynamic const& lhs, B const& rhs) const {
    return FOLLY_LIKELY(lhs.type() == dynamic::Type::STRING) &&
        std::equal_to<StringPiece>()(lhs.stringPiece(), rhs);
  }
};
} // namespace detail
} // namespace folly

//////////////////////////////////////////////////////////////////////

/* clang-format off */
// This is a higher-order preprocessor macro to aid going from runtime
// types to the compile time type system.

#define FB_DYNAMIC_APPLY(type, apply)                                             \
  do {                                                                            \
    FOLLY_PUSH_WARNING FOLLY_CLANG_DISABLE_WARNING("-Wcovered-switch-default")    \
    switch ((type)) {                                                             \
      case NULLT:                                                                 \
        apply(std::nullptr_t);                                                    \
        break;                                                                    \
      case ARRAY:                                                                 \
        apply(Array);                                                             \
        break;                                                                    \
      case BOOL:                                                                  \
        apply(bool);                                                              \
        break;                                                                    \
      case DOUBLE:                                                                \
        apply(double);                                                            \
        break;                                                                    \
      case INT64:                                                                 \
        apply(int64_t);                                                           \
        break;                                                                    \
      case OBJECT:                                                                \
        apply(ObjectImpl);                                                        \
        break;                                                                    \
      case STRING:                                                                \
        apply(std::string);                                                       \
        break;                                                                    \
      default:                                                                    \
        abort();                                                                  \
    }                                                                             \
    FOLLY_POP_WARNING                                                             \
  } while (0)
/* clang-format on */
//////////////////////////////////////////////////////////////////////

namespace folly {

struct FOLLY_EXPORT TypeError : std::runtime_error {
  explicit TypeError(const std::string& expected, dynamic::Type actual);
  explicit TypeError(
      const std::string& expected,
      dynamic::Type actual1,
      dynamic::Type actual2);
};

//////////////////////////////////////////////////////////////////////

namespace detail {

/*
 * Helper for implementing numeric conversions in operators on
 * numbers.  Just promotes to double when one of the arguments is
 * double, or throws if either is not a numeric type.
 */
template <template <class> class Op>
dynamic numericOp(dynamic const& a, dynamic const& b) {
  if (!a.isNumber() || !b.isNumber()) {
    throw_exception<TypeError>("numeric", a.type(), b.type());
  }
  if (a.isDouble() || b.isDouble()) {
    return Op<double>()(a.asDouble(), b.asDouble());
  }
  return Op<int64_t>()(a.asInt(), b.asInt());
}

} // namespace detail

//////////////////////////////////////////////////////////////////////

/*
 * We're doing this instead of a simple member typedef to avoid the
 * undefined behavior of parameterizing F14NodeMap<> with an
 * incomplete type.
 *
 * Note: Later we may add separate order tracking here (a multi-index
 * type of thing.)
 */
struct dynamic::ObjectImpl
    : F14NodeMap<
          dynamic,
          dynamic,
          detail::DynamicHasher,
          detail::DynamicKeyEqual> {};

//////////////////////////////////////////////////////////////////////

// Helper object for creating objects conveniently.  See object and
// the dynamic::dynamic(ObjectMaker&&) ctor.
struct dynamic::ObjectMaker {
  friend struct dynamic;

  explicit ObjectMaker() : val_(dynamic::object) {}
  explicit ObjectMaker(dynamic key, dynamic val) : val_(dynamic::object) {
    val_.insert(std::move(key), std::move(val));
  }

  // Make sure no one tries to save one of these into an lvalue with
  // auto or anything like that.
  ObjectMaker(ObjectMaker&&) = default;
  ObjectMaker(ObjectMaker const&) = delete;
  ObjectMaker& operator=(ObjectMaker const&) = delete;
  ObjectMaker& operator=(ObjectMaker&&) = delete;

  // This returns an rvalue-reference instead of an lvalue-reference
  // to allow constructs like this to moved instead of copied:
  //  dynamic a = dynamic::object("a", "b")("c", "d")
  ObjectMaker&& operator()(dynamic key, dynamic val) {
    val_.insert(std::move(key), std::move(val));
    return std::move(*this);
  }

 private:
  dynamic val_;
};

inline void dynamic::array(EmptyArrayTag) {}

template <class... Args>
inline dynamic dynamic::array(Args&&... args) {
  return dynamic(Array{std::forward<Args>(args)...});
}

inline dynamic::ObjectMaker dynamic::object() {
  return ObjectMaker();
}
inline dynamic::ObjectMaker dynamic::object(dynamic a, dynamic b) {
  return ObjectMaker(std::move(a), std::move(b));
}

//////////////////////////////////////////////////////////////////////

struct dynamic::item_iterator
    : detail::IteratorAdaptor<
          dynamic::item_iterator,
          dynamic::ObjectImpl::iterator,
          std::pair<dynamic const, dynamic>,
          std::forward_iterator_tag> {
  using Super = detail::IteratorAdaptor<
      dynamic::item_iterator,
      dynamic::ObjectImpl::iterator,
      std::pair<dynamic const, dynamic>,
      std::forward_iterator_tag>;
  item_iterator() = default;
  /* implicit */ item_iterator(dynamic::ObjectImpl::iterator b) : Super(b) {}

  using object_type = dynamic::ObjectImpl;
};

struct dynamic::value_iterator
    : detail::IteratorAdaptor<
          dynamic::value_iterator,
          dynamic::ObjectImpl::iterator,
          dynamic,
          std::forward_iterator_tag> {
  using Super = detail::IteratorAdaptor<
      dynamic::value_iterator,
      dynamic::ObjectImpl::iterator,
      dynamic,
      std::forward_iterator_tag>;
  value_iterator() = default;
  /* implicit */ value_iterator(dynamic::ObjectImpl::iterator b) : Super(b) {}

  using object_type = dynamic::ObjectImpl;

  dynamic& dereference() const { return base()->second; }
};

struct dynamic::const_item_iterator
    : detail::IteratorAdaptor<
          dynamic::const_item_iterator,
          dynamic::ObjectImpl::const_iterator,
          std::pair<dynamic const, dynamic> const,
          std::forward_iterator_tag> {
  using Super = detail::IteratorAdaptor<
      dynamic::const_item_iterator,
      dynamic::ObjectImpl::const_iterator,
      std::pair<dynamic const, dynamic> const,
      std::forward_iterator_tag>;
  const_item_iterator() = default;
  /* implicit */ const_item_iterator(dynamic::ObjectImpl::const_iterator b)
      : Super(b) {}
  /* implicit */ const_item_iterator(item_iterator i) : Super(i.base()) {}

  using object_type = dynamic::ObjectImpl const;
};

struct dynamic::const_key_iterator
    : detail::IteratorAdaptor<
          dynamic::const_key_iterator,
          dynamic::ObjectImpl::const_iterator,
          dynamic const,
          std::forward_iterator_tag> {
  using Super = detail::IteratorAdaptor<
      dynamic::const_key_iterator,
      dynamic::ObjectImpl::const_iterator,
      dynamic const,
      std::forward_iterator_tag>;
  const_key_iterator() = default;
  /* implicit */ const_key_iterator(dynamic::ObjectImpl::const_iterator b)
      : Super(b) {}

  using object_type = dynamic::ObjectImpl const;

  dynamic const& dereference() const { return base()->first; }
};

struct dynamic::const_value_iterator
    : detail::IteratorAdaptor<
          dynamic::const_value_iterator,
          dynamic::ObjectImpl::const_iterator,
          dynamic const,
          std::forward_iterator_tag> {
  using Super = detail::IteratorAdaptor<
      dynamic::const_value_iterator,
      dynamic::ObjectImpl::const_iterator,
      dynamic const,
      std::forward_iterator_tag>;
  const_value_iterator() = default;
  /* implicit */ const_value_iterator(dynamic::ObjectImpl::const_iterator b)
      : Super(b) {}
  /* implicit */ const_value_iterator(value_iterator i) : Super(i.base()) {}
  /* implicit */ const_value_iterator(dynamic::ObjectImpl::iterator i)
      : Super(i) {}

  using object_type = dynamic::ObjectImpl const;

  dynamic const& dereference() const { return base()->second; }
};

//////////////////////////////////////////////////////////////////////

inline dynamic::dynamic() : dynamic(nullptr) {}

inline dynamic::dynamic(std::nullptr_t) : type_(NULLT) {}

inline dynamic::dynamic(void (*)(EmptyArrayTag)) : type_(ARRAY) {
  new (&u_.array) Array();
}

inline dynamic::dynamic(ObjectMaker (*)()) : type_(OBJECT) {
  new (getAddress<ObjectImpl>()) ObjectImpl();
}

inline dynamic::dynamic(char const* s) : type_(STRING) {
  new (&u_.string) std::string(s);
}

inline dynamic::dynamic(std::string s) : type_(STRING) {
  new (&u_.string) std::string(std::move(s));
}

template <typename Stringish, typename>
inline dynamic::dynamic(Stringish&& s) : type_(STRING) {
  new (&u_.string) std::string(s.data(), s.size());
}

inline dynamic::dynamic(ObjectMaker&& maker) : type_(OBJECT) {
  new (getAddress<ObjectImpl>())
      ObjectImpl(std::move(*maker.val_.getAddress<ObjectImpl>()));
}

inline dynamic::~dynamic() noexcept {
  destroy();
}

// Integral types except bool convert to int64_t, float types to double.
template <class T>
struct dynamic::NumericTypeHelper<
    T,
    typename std::enable_if<std::is_integral<T>::value>::type> {
  static_assert(
      !kIsObjC || sizeof(T) > sizeof(char),
      "char-sized types are ambiguous in objc; cast to bool or wider type");
  using type = int64_t;
};
template <>
struct dynamic::NumericTypeHelper<bool> {
  using type = bool;
};
template <>
struct dynamic::NumericTypeHelper<float> {
  using type = double;
};
template <>
struct dynamic::NumericTypeHelper<double> {
  using type = double;
};

inline dynamic::dynamic(std::vector<bool>::reference b)
    : dynamic(static_cast<bool>(b)) {}
inline dynamic::dynamic(VectorBoolConstRefCtorType b)
    : dynamic(static_cast<bool>(b)) {}

template <
    class T,
    class NumericType /* = typename NumericTypeHelper<T>::type */>
dynamic::dynamic(T t) {
  type_ = TypeInfo<NumericType>::type;
  new (getAddress<NumericType>()) NumericType(NumericType(t));
}

template <class Iterator>
dynamic::dynamic(array_range_construct_t, Iterator first, Iterator last)
    : type_(ARRAY) {
  new (&u_.array) Array(first, last);
}

template <
    class T,
    class NumericType /* = typename NumericTypeHelper<T>::type */>
dynamic& dynamic::operator=(T t) {
  const auto newType = TypeInfo<NumericType>::type;
  if (type_ == newType) {
    *getAddress<NumericType>() = t;
  } else {
    destroy();
    new (getAddress<NumericType>()) NumericType(t);
    type_ = newType;
  }
  return *this;
}
//////////////////////////////////////////////////////////////////////

inline dynamic::const_iterator dynamic::begin() const {
  return get<Array>().begin();
}
inline dynamic::const_iterator dynamic::end() const {
  return get<Array>().end();
}

inline dynamic::iterator dynamic::begin() {
  return get<Array>().begin();
}
inline dynamic::iterator dynamic::end() {
  return get<Array>().end();
}

template <class It>
struct dynamic::IterableProxy {
  typedef It iterator;
  typedef typename It::value_type value_type;
  typedef typename It::object_type object_type;

  /* implicit */ IterableProxy(object_type* o) : o_(o) {}

  It begin() const { return o_->begin(); }

  It end() const { return o_->end(); }

 private:
  object_type* o_;
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

inline dynamic::IterableProxy<dynamic::value_iterator> dynamic::values() {
  return &(get<ObjectImpl>());
}

inline dynamic::IterableProxy<dynamic::item_iterator> dynamic::items() {
  return &(get<ObjectImpl>());
}

inline bool dynamic::isString() const {
  return get_nothrow<std::string>() != nullptr;
}
inline bool dynamic::isObject() const {
  return get_nothrow<ObjectImpl>() != nullptr;
}
inline bool dynamic::isBool() const {
  return get_nothrow<bool>() != nullptr;
}
inline bool dynamic::isArray() const {
  return get_nothrow<Array>() != nullptr;
}
inline bool dynamic::isDouble() const {
  return get_nothrow<double>() != nullptr;
}
inline bool dynamic::isInt() const {
  return get_nothrow<int64_t>() != nullptr;
}
inline bool dynamic::isNull() const {
  return get_nothrow<std::nullptr_t>() != nullptr;
}
inline bool dynamic::isNumber() const {
  return isInt() || isDouble();
}

inline dynamic::Type dynamic::type() const {
  return type_;
}

inline std::string dynamic::asString() const {
  return asImpl<std::string>();
}
inline double dynamic::asDouble() const {
  return asImpl<double>();
}
inline int64_t dynamic::asInt() const {
  return asImpl<int64_t>();
}
inline bool dynamic::asBool() const {
  return asImpl<bool>();
}

inline const std::string& dynamic::getString() const& {
  return get<std::string>();
}
inline double dynamic::getDouble() const& {
  return get<double>();
}
inline int64_t dynamic::getInt() const& {
  return get<int64_t>();
}
inline bool dynamic::getBool() const& {
  return get<bool>();
}

inline std::string& dynamic::getString() & {
  return get<std::string>();
}
inline double& dynamic::getDouble() & {
  return get<double>();
}
inline int64_t& dynamic::getInt() & {
  return get<int64_t>();
}
inline bool& dynamic::getBool() & {
  return get<bool>();
}

inline std::string&& dynamic::getString() && {
  return std::move(get<std::string>());
}
inline double dynamic::getDouble() && {
  return get<double>();
}
inline int64_t dynamic::getInt() && {
  return get<int64_t>();
}
inline bool dynamic::getBool() && {
  return get<bool>();
}

inline const char* dynamic::c_str() const& {
  return get<std::string>().c_str();
}
inline StringPiece dynamic::stringPiece() const {
  return get<std::string>();
}

template <class T>
struct dynamic::CompareOp {
  static bool comp(T const& a, T const& b) { return a < b; }
};
template <>
struct dynamic::CompareOp<dynamic::ObjectImpl> {
  static bool comp(ObjectImpl const&, ObjectImpl const&) {
    // This code never executes; it is just here for the compiler.
    return false;
  }
};
template <>
struct dynamic::CompareOp<std::nullptr_t> {
  static bool comp(std::nullptr_t const&, std::nullptr_t const&) {
    return false;
  }
};

inline dynamic& dynamic::operator+=(dynamic const& o) {
  if (type() == STRING && o.type() == STRING) {
    *getAddress<std::string>() += *o.getAddress<std::string>();
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

#define FB_DYNAMIC_INTEGER_OP(op)                            \
  inline dynamic& dynamic::operator op(dynamic const& o) {   \
    if (!isInt() || !o.isInt()) {                            \
      throw_exception<TypeError>("int64", type(), o.type()); \
    }                                                        \
    *getAddress<int64_t>() op o.asInt();                     \
    return *this;                                            \
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

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic const&> dynamic::operator[](
    K&& idx) const& {
  return at(std::forward<K>(idx));
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic&> dynamic::operator[](
    K&& idx) & {
  if (!isObject() && !isArray()) {
    throw_exception<TypeError>("object/array", type());
  }
  if (isArray()) {
    return at(std::forward<K>(idx));
  }
  auto& obj = get<ObjectImpl>();
  auto ret = obj.emplace(std::forward<K>(idx), nullptr);
  return ret.first->second;
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic&&> dynamic::operator[](
    K&& idx) && {
  return std::move((*this)[std::forward<K>(idx)]);
}

inline dynamic const& dynamic::operator[](StringPiece k) const& {
  return at(k);
}

inline dynamic&& dynamic::operator[](StringPiece k) && {
  return std::move((*this)[k]);
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic> dynamic::getDefault(
    K&& k, const dynamic& v) const& {
  auto& obj = get<ObjectImpl>();
  auto it = obj.find(std::forward<K>(k));
  return it == obj.end() ? v : it->second;
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic> dynamic::getDefault(
    K&& k, dynamic&& v) const& {
  auto& obj = get<ObjectImpl>();
  auto it = obj.find(std::forward<K>(k));
  // Avoid clang bug with ternary
  if (it == obj.end()) {
    return std::move(v);
  } else {
    return it->second;
  }
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic> dynamic::getDefault(
    K&& k, const dynamic& v) && {
  auto& obj = get<ObjectImpl>();
  auto it = obj.find(std::forward<K>(k));
  // Avoid clang bug with ternary
  if (it == obj.end()) {
    return v;
  } else {
    return std::move(it->second);
  }
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic> dynamic::getDefault(
    K&& k, dynamic&& v) && {
  auto& obj = get<ObjectImpl>();
  auto it = obj.find(std::forward<K>(k));
  return std::move(it == obj.end() ? v : it->second);
}

template <typename K, typename V>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic&> dynamic::setDefault(
    K&& k, V&& v) {
  auto& obj = get<ObjectImpl>();
  return obj.emplace(std::forward<K>(k), std::forward<V>(v)).first->second;
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic&> dynamic::setDefault(
    K&& k, dynamic&& v) {
  auto& obj = get<ObjectImpl>();
  return obj.emplace(std::forward<K>(k), std::move(v)).first->second;
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic&> dynamic::setDefault(
    K&& k, const dynamic& v) {
  auto& obj = get<ObjectImpl>();
  return obj.emplace(std::forward<K>(k), v).first->second;
}

template <typename V>
dynamic& dynamic::setDefault(StringPiece k, V&& v) {
  auto& obj = get<ObjectImpl>();
  return obj.emplace(k, std::forward<V>(v)).first->second;
}

inline dynamic& dynamic::setDefault(StringPiece k, dynamic&& v) {
  auto& obj = get<ObjectImpl>();
  return obj.emplace(k, std::move(v)).first->second;
}

inline dynamic& dynamic::setDefault(StringPiece k, const dynamic& v) {
  auto& obj = get<ObjectImpl>();
  return obj.emplace(k, v).first->second;
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic const*> dynamic::get_ptr(
    K&& k) const& {
  return get_ptrImpl(std::forward<K>(k));
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic*> dynamic::get_ptr(
    K&& idx) & {
  return const_cast<dynamic*>(const_cast<dynamic const*>(this)->get_ptr(idx));
}

inline dynamic* dynamic::get_ptr(StringPiece idx) & {
  return const_cast<dynamic*>(const_cast<dynamic const*>(this)->get_ptr(idx));
}

// clang-format off
inline
dynamic::resolved_json_pointer<dynamic>
dynamic::try_get_ptr(json_pointer const& jsonPtr) & {
  auto ret = const_cast<dynamic const*>(this)->try_get_ptr(jsonPtr);
  if (ret.hasValue()) {
    return json_pointer_resolved_value<dynamic>{
        const_cast<dynamic*>(ret.value().parent),
        const_cast<dynamic*>(ret.value().value),
        ret.value().parent_key, ret.value().parent_index};
  } else {
    return makeUnexpected(
        json_pointer_resolution_error<dynamic>{
            ret.error().error_code,
            ret.error().index,
            const_cast<dynamic*>(ret.error().context)}
        );
  }
}
// clang-format on

inline dynamic* dynamic::get_ptr(json_pointer const& jsonPtr) & {
  return const_cast<dynamic*>(
      const_cast<dynamic const*>(this)->get_ptr(jsonPtr));
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic const&> dynamic::at(
    K&& k) const& {
  return atImpl(std::forward<K>(k));
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic&> dynamic::at(K&& idx) & {
  return const_cast<dynamic&>(const_cast<dynamic const*>(this)->at(idx));
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic&&> dynamic::at(K&& idx) && {
  return std::move(at(idx));
}

inline dynamic& dynamic::at(StringPiece idx) & {
  return const_cast<dynamic&>(const_cast<dynamic const*>(this)->at(idx));
}

inline dynamic&& dynamic::at(StringPiece idx) && {
  return std::move(at(idx));
}

inline bool dynamic::empty() const {
  if (isNull()) {
    return true;
  }
  return !size();
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic::const_item_iterator>
dynamic::find(K&& key) const {
  return get<ObjectImpl>().find(std::forward<K>(key));
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, dynamic::item_iterator>
dynamic::find(K&& key) {
  return get<ObjectImpl>().find(std::forward<K>(key));
}

inline dynamic::const_item_iterator dynamic::find(StringPiece key) const {
  return get<ObjectImpl>().find(key);
}

inline dynamic::item_iterator dynamic::find(StringPiece key) {
  return get<ObjectImpl>().find(key);
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, std::size_t> dynamic::count(
    K&& key) const {
  return find(std::forward<K>(key)) != items().end() ? 1u : 0u;
}

inline std::size_t dynamic::count(StringPiece key) const {
  return find(key) != items().end() ? 1u : 0u;
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, bool> dynamic::contains(
    K&& key) const {
  return count(key);
}

inline bool dynamic::contains(StringPiece key) const {
  return count(key);
}

template <class K, class V>
inline dynamic::IfNotIterator<K, void> dynamic::insert(K&& key, V&& val) {
  auto& obj = get<ObjectImpl>();
  obj[std::forward<K>(key)] = std::forward<V>(val);
}

template <class... Args>
inline std::pair<dynamic::item_iterator, bool> dynamic::emplace(
    Args&&... args) {
  auto& obj = get<ObjectImpl>();
  return obj.emplace(std::forward<Args>(args)...);
}

template <class K, class... Args>
inline std::pair<dynamic::item_iterator, bool> dynamic::try_emplace(
    K&& key, Args&&... args) {
  auto& obj = get<ObjectImpl>();
  return obj.try_emplace(std::forward<K>(key), std::forward<Args>(args)...);
}

template <class T>
inline dynamic::iterator dynamic::insert(const_iterator pos, T&& value) {
  auto& arr = get<Array>();
  return arr.insert(pos, std::forward<T>(value));
}

template <class InputIt>
inline dynamic::iterator dynamic::insert(
    const_iterator pos, InputIt first, InputIt last) {
  auto& arr = get<Array>();
  return arr.insert(pos, first, last);
}

inline void dynamic::update(const dynamic& mergeObj) {
  if (!isObject() || !mergeObj.isObject()) {
    throw_exception<TypeError>("object", type(), mergeObj.type());
  }

  for (const auto& pair : mergeObj.items()) {
    (*this)[pair.first] = pair.second;
  }
}

inline void dynamic::update_missing(const dynamic& mergeObj1) {
  if (!isObject() || !mergeObj1.isObject()) {
    throw_exception<TypeError>("object", type(), mergeObj1.type());
  }

  // Only add if not already there
  for (const auto& pair : mergeObj1.items()) {
    if ((*this).find(pair.first) == (*this).items().end()) {
      (*this)[pair.first] = pair.second;
    }
  }
}

inline void dynamic::merge_patch(const dynamic& patch) {
  auto& self = *this;
  if (!patch.isObject()) {
    self = patch;
    return;
  }
  // if we are not an object, erase all contents, reset to object
  if (!isObject()) {
    self = object;
  }
  for (const auto& pair : patch.items()) {
    if (pair.second.isNull()) {
      // if name could be found in current object, remove it
      auto it = self.find(pair.first);
      if (it != self.items().end()) {
        self.erase(it);
      }
    } else {
      self[pair.first].merge_patch(pair.second);
    }
  }
}

inline dynamic dynamic::merge(
    const dynamic& mergeObj1, const dynamic& mergeObj2) {
  // No checks on type needed here because they are done in update_missing
  // Note that we do update_missing here instead of update() because
  // it will prevent the extra writes that would occur with update()
  auto ret = mergeObj2;
  ret.update_missing(mergeObj1);
  return ret;
}

template <typename K>
dynamic::IfIsNonStringDynamicConvertible<K, std::size_t> dynamic::erase(
    K&& key) {
  auto& obj = get<ObjectImpl>();
  return obj.erase(std::forward<K>(key));
}
inline std::size_t dynamic::erase(StringPiece key) {
  auto& obj = get<ObjectImpl>();
  return obj.erase(key);
}

inline dynamic::iterator dynamic::erase(const_iterator it) {
  auto& arr = get<Array>();
  // std::vector doesn't have an erase method that works on const iterators,
  // even though the standard says it should, so this hack converts to a
  // non-const iterator before calling erase.
  return get<Array>().erase(arr.begin() + (it - arr.begin()));
}

inline dynamic::const_key_iterator dynamic::erase(const_key_iterator it) {
  return const_key_iterator(get<ObjectImpl>().erase(it.base()));
}

inline dynamic::const_key_iterator dynamic::erase(
    const_key_iterator first, const_key_iterator last) {
  return const_key_iterator(get<ObjectImpl>().erase(first.base(), last.base()));
}

inline dynamic::value_iterator dynamic::erase(const_value_iterator it) {
  return value_iterator(get<ObjectImpl>().erase(it.base()));
}

inline dynamic::value_iterator dynamic::erase(
    const_value_iterator first, const_value_iterator last) {
  return value_iterator(get<ObjectImpl>().erase(first.base(), last.base()));
}

inline dynamic::item_iterator dynamic::erase(const_item_iterator it) {
  return item_iterator(get<ObjectImpl>().erase(it.base()));
}

inline dynamic::item_iterator dynamic::erase(
    const_item_iterator first, const_item_iterator last) {
  return item_iterator(get<ObjectImpl>().erase(first.base(), last.base()));
}

inline void dynamic::resize(std::size_t sz, dynamic const& c) {
  auto& arr = get<Array>();
  arr.resize(sz, c);
}

inline void dynamic::push_back(dynamic const& v) {
  auto& arr = get<Array>();
  arr.push_back(v);
}

inline void dynamic::push_back(dynamic&& v) {
  auto& arr = get<Array>();
  arr.push_back(std::move(v));
}

inline void dynamic::pop_back() {
  auto& arr = get<Array>();
  arr.pop_back();
}

inline const dynamic& dynamic::back() const {
  auto& arr = get<Array>();
  return arr.back();
}

//////////////////////////////////////////////////////////////////////

inline dynamic::dynamic(Array&& r) : type_(ARRAY) {
  new (&u_.array) Array(std::move(r));
}

#define FOLLY_DYNAMIC_DEC_TYPEINFO(T, val)     \
  template <>                                  \
  struct dynamic::TypeInfo<T> {                \
    static const char* const name;             \
    static constexpr dynamic::Type type = val; \
  };                                           \
  //

FOLLY_DYNAMIC_DEC_TYPEINFO(std::nullptr_t, dynamic::NULLT)
FOLLY_DYNAMIC_DEC_TYPEINFO(bool, dynamic::BOOL)
FOLLY_DYNAMIC_DEC_TYPEINFO(std::string, dynamic::STRING)
FOLLY_DYNAMIC_DEC_TYPEINFO(dynamic::Array, dynamic::ARRAY)
FOLLY_DYNAMIC_DEC_TYPEINFO(double, dynamic::DOUBLE)
FOLLY_DYNAMIC_DEC_TYPEINFO(int64_t, dynamic::INT64)
FOLLY_DYNAMIC_DEC_TYPEINFO(dynamic::ObjectImpl, dynamic::OBJECT)

#undef FOLLY_DYNAMIC_DEC_TYPEINFO

template <class T>
T dynamic::asImpl() const {
  FOLLY_PUSH_WARNING
  FOLLY_CLANG_DISABLE_WARNING("-Wcovered-switch-default")
  switch (type()) {
    case INT64:
      return to<T>(*get_nothrow<int64_t>());
    case DOUBLE:
      return to<T>(*get_nothrow<double>());
    case BOOL:
      return to<T>(*get_nothrow<bool>());
    case STRING:
      return to<T>(*get_nothrow<std::string>());
    case NULLT:
    case ARRAY:
    case OBJECT:
    default:
      throw_exception<TypeError>("int/double/bool/string", type());
  }
  FOLLY_POP_WARNING
}

// Return a T* to our type, or null if we're not that type.
template <class T>
T* dynamic::get_nothrow() & noexcept {
  if (type_ != TypeInfo<T>::type) {
    return nullptr;
  }
  return getAddress<T>();
}

template <class T>
T const* dynamic::get_nothrow() const& noexcept {
  return const_cast<dynamic*>(this)->get_nothrow<T>();
}

// Return T* for where we can put a T, without type checking.  (Memory
// might be uninitialized, even.)
template <class T>
T* dynamic::getAddress() noexcept {
  return GetAddrImpl<T>::get(u_);
}

template <class T>
T const* dynamic::getAddress() const noexcept {
  return const_cast<dynamic*>(this)->getAddress<T>();
}

template <class T>
struct dynamic::GetAddrImpl {};
template <>
struct dynamic::GetAddrImpl<std::nullptr_t> {
  static std::nullptr_t* get(Data& d) noexcept { return &d.nul; }
};
template <>
struct dynamic::GetAddrImpl<dynamic::Array> {
  static Array* get(Data& d) noexcept { return &d.array; }
};
template <>
struct dynamic::GetAddrImpl<bool> {
  static bool* get(Data& d) noexcept { return &d.boolean; }
};
template <>
struct dynamic::GetAddrImpl<int64_t> {
  static int64_t* get(Data& d) noexcept { return &d.integer; }
};
template <>
struct dynamic::GetAddrImpl<double> {
  static double* get(Data& d) noexcept { return &d.doubl; }
};
template <>
struct dynamic::GetAddrImpl<std::string> {
  static std::string* get(Data& d) noexcept { return &d.string; }
};
template <>
struct dynamic::GetAddrImpl<dynamic::ObjectImpl> {
  static_assert(
      sizeof(ObjectImpl) <= sizeof(Data::objectBuffer),
      "In your implementation, F14NodeMap<> apparently takes different"
      " amount of space depending on its template parameters.  This is "
      "weird.  Make objectBuffer bigger if you want to compile dynamic.");

  static ObjectImpl* get(Data& d) noexcept {
    void* data = &d.objectBuffer;
    return static_cast<ObjectImpl*>(data);
  }
};

template <class T>
T& dynamic::get() {
  if (auto* p = get_nothrow<T>()) {
    return *p;
  }
  throw_exception<TypeError>(TypeInfo<T>::name, type());
}

template <class T>
T const& dynamic::get() const {
  return const_cast<dynamic*>(this)->get<T>();
}

//////////////////////////////////////////////////////////////////////

/*
 * Helper for implementing operator<<.  Throws if the type shouldn't
 * support it.
 */
template <class T>
struct dynamic::PrintImpl {
  static void print(dynamic const&, std::ostream& out, T const& t) { out << t; }
};
// Otherwise, null, being (void*)0, would print as 0.
template <>
struct dynamic::PrintImpl<std::nullptr_t> {
  static void print(
      dynamic const& /* d */, std::ostream& out, std::nullptr_t const&) {
    out << "null";
  }
};
template <>
struct dynamic::PrintImpl<dynamic::ObjectImpl> {
  static void print(
      dynamic const& d, std::ostream& out, dynamic::ObjectImpl const&) {
    d.print_as_pseudo_json(out);
  }
};
template <>
struct dynamic::PrintImpl<dynamic::Array> {
  static void print(
      dynamic const& d, std::ostream& out, dynamic::Array const&) {
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

inline const_dynamic_view::const_dynamic_view(dynamic const& d) noexcept
    : d_(&d) {}

inline const_dynamic_view::const_dynamic_view(dynamic const* d) noexcept
    : d_(d) {}

inline const_dynamic_view::operator bool() const noexcept {
  return !empty();
}

inline bool const_dynamic_view::empty() const noexcept {
  return d_ == nullptr;
}

inline void const_dynamic_view::reset() noexcept {
  d_ = nullptr;
}

template <typename Key, typename... Keys>
inline const_dynamic_view const_dynamic_view::descend(
    Key const& key, Keys const&... keys) const noexcept {
  return descend_(key, keys...);
}

template <typename Key1, typename Key2, typename... Keys>
inline dynamic const* const_dynamic_view::descend_(
    Key1 const& key1, Key2 const& key2, Keys const&... keys) const noexcept {
  if (!d_) {
    return nullptr;
  }
  return const_dynamic_view{descend_unchecked_(key1)}.descend_(key2, keys...);
}

template <typename Key>
inline dynamic const* const_dynamic_view::descend_(
    Key const& key) const noexcept {
  if (!d_) {
    return nullptr;
  }
  return descend_unchecked_(key);
}

template <typename Key>
inline dynamic::IfIsNonStringDynamicConvertible<Key, dynamic const*>
const_dynamic_view::descend_unchecked_(Key const& key) const noexcept {
  if (auto* parray = d_->get_nothrow<dynamic::Array>()) {
    if constexpr (!std::is_integral<Key>::value) {
      return nullptr;
    }
    if (key < 0 || folly::to_unsigned(key) >= parray->size()) {
      return nullptr;
    }
    return &(*parray)[size_t(key)];
  } else if (auto* pobject = d_->get_nothrow<dynamic::ObjectImpl>()) {
    auto it = pobject->find(key);
    if (it == pobject->end()) {
      return nullptr;
    }
    return &it->second;
  }
  return nullptr;
}

inline dynamic const* const_dynamic_view::descend_unchecked_(
    folly::StringPiece key) const noexcept {
  if (auto* pobject = d_->get_nothrow<dynamic::ObjectImpl>()) {
    auto it = pobject->find(key);
    if (it == pobject->end()) {
      return nullptr;
    }
    return &it->second;
  }
  return nullptr;
}

inline dynamic const_dynamic_view::value_or(dynamic&& val) const {
  if (d_) {
    return *d_;
  }
  return std::move(val);
}

template <typename T, typename... Args>
inline T const_dynamic_view::get_copy(Args&&... args) const {
  if (auto* v = (d_ ? d_->get_nothrow<T>() : nullptr)) {
    return *v;
  }
  return T(std::forward<Args>(args)...);
}

inline std::string const_dynamic_view::string_or(char const* val) const {
  return get_copy<std::string>(val);
}

inline std::string const_dynamic_view::string_or(std::string val) const {
  return get_copy<std::string>(std::move(val));
}

// Specialized version for StringPiece, FixedString, and other types which are
// not convertible to std::string, but can construct one from .data and .size
// to std::string. Will not trigger a copy unless data and size require it.
template <typename Stringish, typename>
inline std::string const_dynamic_view::string_or(Stringish&& val) const {
  return get_copy(val.data(), val.size());
}

inline double const_dynamic_view::double_or(double val) const noexcept {
  return get_copy<double>(val);
}

inline int64_t const_dynamic_view::int_or(int64_t val) const noexcept {
  return get_copy<int64_t>(val);
}

inline bool const_dynamic_view::bool_or(bool val) const noexcept {
  return get_copy<bool>(val);
}

inline dynamic_view::dynamic_view(dynamic& d) noexcept
    : const_dynamic_view(d) {}

template <typename Key, typename... Keys>
inline dynamic_view dynamic_view::descend(
    Key const& key, Keys const&... keys) const noexcept {
  if (auto* child = const_dynamic_view::descend_(key, keys...)) {
    return *const_cast<dynamic*>(child);
  }
  return {};
}

inline dynamic dynamic_view::move_value_or(dynamic&& val) noexcept {
  if (d_) {
    return std::move(*const_cast<dynamic*>(d_));
  }
  return std::move(val);
}

template <typename T, typename... Args>
inline T dynamic_view::get_move(Args&&... args) {
  if (auto* v = (d_ ? const_cast<dynamic*>(d_)->get_nothrow<T>() : nullptr)) {
    return std::move(*v);
  }
  return T(std::forward<Args>(args)...);
}

inline std::string dynamic_view::move_string_or(char const* val) {
  return get_move<std::string>(val);
}

inline std::string dynamic_view::move_string_or(std::string val) noexcept {
  return get_move<std::string>(std::move(val));
}

template <typename Stringish, typename>
inline std::string dynamic_view::move_string_or(Stringish&& val) {
  return get_move<std::string>(val.begin(), val.end());
}

//////////////////////////////////////////////////////////////////////

// Specialization of FormatValue so dynamic objects can be formatted
template <>
class FormatValue<dynamic> {
 public:
  explicit FormatValue(const dynamic& val) : val_(val) {}

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
        FormatValue<std::string>(val_.asString()).format(arg, cb);
        break;
      case dynamic::DOUBLE:
        FormatValue<double>(val_.asDouble()).format(arg, cb);
        break;
      case dynamic::ARRAY:
        FormatValue(val_.at(arg.splitIntKey())).format(arg, cb);
        break;
      case dynamic::OBJECT:
        FormatValue(val_.at(arg.splitKey().toString())).format(arg, cb);
        break;
      default:
        folly::assume_unreachable();
    }
  }

 private:
  const dynamic& val_;
};

template <class V>
class FormatValue<detail::DefaultValueWrapper<dynamic, V>> {
 public:
  explicit FormatValue(const detail::DefaultValueWrapper<dynamic, V>& val)
      : val_(val) {}

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    auto& c = val_.container;
    switch (c.type()) {
      case dynamic::NULLT:
      case dynamic::BOOL:
      case dynamic::INT64:
      case dynamic::STRING:
      case dynamic::DOUBLE:
        FormatValue<dynamic>(c).format(arg, cb);
        break;
      case dynamic::ARRAY: {
        int key = arg.splitIntKey();
        if (key >= 0 && size_t(key) < c.size()) {
          FormatValue<dynamic>(c.at(key)).format(arg, cb);
        } else {
          FormatValue<V>(val_.defaultValue).format(arg, cb);
        }
        break;
      }
      case dynamic::OBJECT: {
        auto pos = c.find(arg.splitKey());
        if (pos != c.items().end()) {
          FormatValue<dynamic>(pos->second).format(arg, cb);
        } else {
          FormatValue<V>(val_.defaultValue).format(arg, cb);
        }
        break;
      }
    }
  }

 private:
  const detail::DefaultValueWrapper<dynamic, V>& val_;
};

} // namespace folly

#undef FB_DYNAMIC_APPLY
