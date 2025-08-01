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

#include <folly/json/dynamic.h>

#include <numeric>

#include <glog/logging.h>

#include <folly/Format.h>
#include <folly/container/Enumerate.h>
#include <folly/hash/Hash.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Exception.h>

namespace folly {

//////////////////////////////////////////////////////////////////////

#define FOLLY_DYNAMIC_DEF_TYPEINFO(T, str)            \
  const char* const dynamic::TypeInfo<T>::name = str; \
  //

FOLLY_DYNAMIC_DEF_TYPEINFO(std::nullptr_t, "null")
FOLLY_DYNAMIC_DEF_TYPEINFO(bool, "boolean")
FOLLY_DYNAMIC_DEF_TYPEINFO(std::string, "string")
FOLLY_DYNAMIC_DEF_TYPEINFO(dynamic::Array, "array")
FOLLY_DYNAMIC_DEF_TYPEINFO(double, "double")
FOLLY_DYNAMIC_DEF_TYPEINFO(int64_t, "int64")
FOLLY_DYNAMIC_DEF_TYPEINFO(dynamic::ObjectImpl, "object")

#undef FOLLY_DYNAMIC_DEF_TYPEINFO

const char* dynamic::typeName() const {
  return typeName(type_);
}

TypeError::TypeError(const std::string& expected, dynamic::Type actual)
    : std::runtime_error(sformat(
          "TypeError: expected dynamic type '{}', but had type '{}'",
          expected,
          dynamic::typeName(actual))) {}

TypeError::TypeError(
    const std::string& expected, dynamic::Type actual1, dynamic::Type actual2)
    : std::runtime_error(sformat(
          "TypeError: expected dynamic types '{}', but had types '{}' and '{}'",
          expected,
          dynamic::typeName(actual1),
          dynamic::typeName(actual2))) {}

// This is a higher-order preprocessor macro to aid going from runtime
// types to the compile time type system.
#define FB_DYNAMIC_APPLY(type, apply) \
  do {                                \
    switch ((type)) {                 \
      case dynamic::NULLT:            \
        apply(std::nullptr_t);        \
        break;                        \
      case dynamic::ARRAY:            \
        apply(dynamic::Array);        \
        break;                        \
      case dynamic::BOOL:             \
        apply(bool);                  \
        break;                        \
      case dynamic::DOUBLE:           \
        apply(double);                \
        break;                        \
      case dynamic::INT64:            \
        apply(int64_t);               \
        break;                        \
      case dynamic::OBJECT:           \
        apply(dynamic::ObjectImpl);   \
        break;                        \
      case dynamic::STRING:           \
        apply(std::string);           \
        break;                        \
      default:                        \
        CHECK(0);                     \
        abort();                      \
    }                                 \
  } while (0)

bool operator<(dynamic const& a, dynamic const& b) {
  constexpr auto obj = dynamic::OBJECT;
  if (FOLLY_UNLIKELY(a.type_ == obj || b.type_ == obj)) {
    auto type = a.type_ == obj ? b.type_ : b.type_ == obj ? a.type_ : obj;
    throw_exception<TypeError>("object", type);
  }
  if (a.type_ != b.type_) {
    if (a.isNumber() && b.isNumber()) {
      // The only isNumber() types are double and int64 - so guaranteed one will
      // be double and one will be int.
      return a.isInt() ? a.asInt() < b.asDouble() : a.asDouble() < b.asInt();
    }

    return a.type_ < b.type_;
  }

#define FB_X(T) \
  return dynamic::CompareOp<T>::comp(*a.getAddress<T>(), *b.getAddress<T>())
  FB_DYNAMIC_APPLY(a.type_, FB_X);
#undef FB_X
}

bool operator==(dynamic const& a, dynamic const& b) {
  if (a.type() != b.type()) {
    if (a.isNumber() && b.isNumber()) {
      auto& integ = a.isInt() ? a : b;
      auto& doubl = a.isInt() ? b : a;
      return integ.asInt() == doubl.asDouble();
    }
    return false;
  }

#define FB_X(T) return *a.getAddress<T>() == *b.getAddress<T>();
  FB_DYNAMIC_APPLY(a.type_, FB_X);
#undef FB_X
}

dynamic::dynamic(dynamic const& o) : type_(o.type_) {
#define FB_X(T) new (getAddress<T>()) T(*o.getAddress<T>())
  FB_DYNAMIC_APPLY(o.type_, FB_X);
#undef FB_X
}

dynamic::dynamic(dynamic&& o) noexcept : type_(o.type_) {
#define FB_X(T) new (getAddress<T>()) T(std::move(*o.getAddress<T>()))
  FB_DYNAMIC_APPLY(o.type_, FB_X);
#undef FB_X
}

dynamic& dynamic::operator=(dynamic const& o) {
  if (&o != this) {
    if (type_ == o.type_) {
#define FB_X(T) *getAddress<T>() = *o.getAddress<T>()
      FB_DYNAMIC_APPLY(type_, FB_X);
#undef FB_X
    } else {
      destroy();
#define FB_X(T) new (getAddress<T>()) T(*o.getAddress<T>())
      FB_DYNAMIC_APPLY(o.type_, FB_X);
#undef FB_X
      type_ = o.type_;
    }
  }
  return *this;
}

dynamic& dynamic::operator=(dynamic&& o) noexcept {
  if (&o != this) {
    if (type_ == o.type_) {
#define FB_X(T) *getAddress<T>() = std::move(*o.getAddress<T>())
      FB_DYNAMIC_APPLY(type_, FB_X);
#undef FB_X
    } else {
      destroy();
#define FB_X(T) new (getAddress<T>()) T(std::move(*o.getAddress<T>()))
      FB_DYNAMIC_APPLY(o.type_, FB_X);
#undef FB_X
      type_ = o.type_;
    }
  }
  return *this;
}

dynamic& dynamic::operator=(std::nullptr_t) {
  if (type_ == NULLT) {
    // Do nothing -- nul has only one possible value.
  } else {
    destroy();
    u_.nul = nullptr;
    type_ = NULLT;
  }
  return *this;
}

dynamic const& dynamic::atImpl(dynamic const& idx) const& {
  if (auto* parray = get_nothrow<Array>()) {
    if (!idx.isInt()) {
      throw_exception<TypeError>("int64", idx.type());
    }
    if (idx < 0 || idx >= parray->size()) {
      throw_exception<std::out_of_range>("out of range in dynamic array");
    }
    return (*parray)[size_t(idx.asInt())];
  } else if (auto* pobject = get_nothrow<ObjectImpl>()) {
    auto it = pobject->find(idx);
    if (it == pobject->end()) {
      throw_exception<std::out_of_range>(
          sformat("couldn't find key {} in dynamic object", idx.asString()));
    }
    return it->second;
  } else {
    throw_exception<TypeError>("object/array", type());
  }
}

dynamic const& dynamic::at(StringPiece idx) const& {
  auto* pobject = get_nothrow<ObjectImpl>();
  if (!pobject) {
    throw_exception<TypeError>("object", type());
  }
  auto it = pobject->find(idx);
  if (it == pobject->end()) {
    throw_exception<std::out_of_range>(
        sformat("couldn't find key {} in dynamic object", idx));
  }
  return it->second;
}

dynamic& dynamic::operator[](StringPiece k) & {
  auto& obj = get<ObjectImpl>();
  auto ret = obj.emplace(k, nullptr);
  return ret.first->second;
}

dynamic dynamic::getDefault(StringPiece k, const dynamic& v) const& {
  auto& obj = get<ObjectImpl>();
  auto it = obj.find(k);
  return it == obj.end() ? v : it->second;
}

dynamic dynamic::getDefault(StringPiece k, dynamic&& v) const& {
  auto& obj = get<ObjectImpl>();
  auto it = obj.find(k);
  // Avoid clang bug with ternary
  if (it == obj.end()) {
    return std::move(v);
  } else {
    return it->second;
  }
}

dynamic dynamic::getDefault(StringPiece k, const dynamic& v) && {
  auto& obj = get<ObjectImpl>();
  auto it = obj.find(k);
  // Avoid clang bug with ternary
  if (it == obj.end()) {
    return v;
  } else {
    return std::move(it->second);
  }
}

dynamic dynamic::getDefault(StringPiece k, dynamic&& v) && {
  auto& obj = get<ObjectImpl>();
  auto it = obj.find(k);
  return std::move(it == obj.end() ? v : it->second);
}

const dynamic* dynamic::get_ptrImpl(dynamic const& idx) const& {
  if (auto* parray = get_nothrow<Array>()) {
    if (!idx.isInt()) {
      throw_exception<TypeError>("int64", idx.type());
    }
    if (idx < 0 || idx >= parray->size()) {
      return nullptr;
    }
    return &(*parray)[size_t(idx.asInt())];
  } else if (auto* pobject = get_nothrow<ObjectImpl>()) {
    auto it = pobject->find(idx);
    if (it == pobject->end()) {
      return nullptr;
    }
    return &it->second;
  } else {
    throw_exception<TypeError>("object/array", type());
  }
}

const dynamic* dynamic::get_ptr(StringPiece idx) const& {
  auto* pobject = get_nothrow<ObjectImpl>();
  if (!pobject) {
    throw_exception<TypeError>("object", type());
  }
  auto it = pobject->find(idx);
  if (it == pobject->end()) {
    return nullptr;
  }
  return &it->second;
}

std::size_t dynamic::size() const {
  if (auto* ar = get_nothrow<Array>()) {
    return ar->size();
  }
  if (auto* obj = get_nothrow<ObjectImpl>()) {
    return obj->size();
  }
  if (auto* str = get_nothrow<std::string>()) {
    return str->size();
  }
  throw_exception<TypeError>("array/object/string", type());
}

dynamic::iterator dynamic::erase(const_iterator first, const_iterator last) {
  auto& arr = get<Array>();
  return get<Array>().erase(
      arr.begin() + (first - arr.begin()), arr.begin() + (last - arr.begin()));
}

namespace {

//  UBSAN traps on casts from floating-point to integral types when the
//  floating-point value at runtime is outside of the representable range of the
//  interal type. This is normally helpful for catching bugs. But the goal here
//  is to test at runtime whether the floating-point value could roundtrip via
//  the integral type back to the floating-point type unchanged. For this, UBSAN
//  must be suppressed. It is possibleto emulate such tests, but emulation is
//  slower.
template <typename D, typename S>
FOLLY_DISABLE_SANITIZERS D static_cast_nosan(S s) {
  return static_cast<D>(s);
}
template <typename D, typename S>
FOLLY_ERASE D static_cast_unchecked(S s) {
  return kIsSanitize ? static_cast_nosan<D>(s) : static_cast<D>(s);
}

} // namespace

std::size_t dynamic::hash() const {
  switch (type()) {
    case NULLT:
      return 0xBAAAAAAD;
    case OBJECT: {
      // Accumulate using addition instead of using hash_range (as in the ARRAY
      // case), as we need a commutative hash operation since unordered_map's
      // iteration order is unspecified.
      auto h = std::hash<std::pair<dynamic const, dynamic>>{};
      return std::accumulate(
          items().begin(),
          items().end(),
          size_t{0x0B1EC7},
          [&](auto acc, auto const& item) { return acc + h(item); });
    }
    case ARRAY:
      return static_cast<std::size_t>(folly::hash::hash_range(begin(), end()));
    case INT64:
      return Hash()(getInt());
    case DOUBLE: {
      double valueAsDouble = getDouble();
      int64_t valueAsDoubleAsInt =
          static_cast_unchecked<int64_t>(valueAsDouble);
      // Given that we do implicit conversion in operator==, have identical
      // values hash the same to keep behavior consistent, but leave others use
      // double hashing to avoid restricting the hash range unnecessarily.
      if (double(valueAsDoubleAsInt) == valueAsDouble) {
        return Hash()(valueAsDoubleAsInt);
      }
      return Hash()(valueAsDouble);
    }
    case BOOL:
      return Hash()(getBool());
    case STRING:
      // keep consistent with detail::DynamicHasher
      return Hash()(getString());
  }
  FOLLY_ASSUME_UNREACHABLE();
}

char const* dynamic::typeName(Type t) {
#define FB_X(T) return TypeInfo<T>::name
  FB_DYNAMIC_APPLY(t, FB_X);
#undef FB_X
}

// NOTE: like ~dynamic, destroy() leaves type_ and u_ in an invalid state.
void dynamic::destroy() noexcept {
  // This short-circuit speeds up some microbenchmarks.
  if (type_ == NULLT) {
    return;
  }

#define FB_X(T) std::destroy_at(getAddress<T>())
  FB_DYNAMIC_APPLY(type_, FB_X);
#undef FB_X
}

dynamic dynamic::merge_diff(const dynamic& source, const dynamic& target) {
  if (!source.isObject() || !target.isObject()) {
    return target;
  }

  dynamic diff = object;

  // added/modified keys
  for (const auto& pair : target.items()) {
    auto it = source.find(pair.first);
    if (it == source.items().end()) {
      diff[pair.first] = pair.second;
    } else {
      const auto& ssource = it->second;
      const auto& starget = pair.second;
      if (ssource.isObject() && starget.isObject()) {
        auto sdiff = merge_diff(ssource, starget);
        if (!sdiff.empty()) {
          diff[pair.first] = std::move(sdiff);
        }
      } else if (ssource != starget) {
        diff[pair.first] = merge_diff(ssource, starget);
      }
    }
  }

  // removed keys
  for (const auto& pair : source.items()) {
    auto it = target.find(pair.first);
    if (it == target.items().end()) {
      diff[pair.first] = nullptr;
    }
  }

  return diff;
}

// clang-format off
dynamic::resolved_json_pointer<dynamic const>
// clang-format on
dynamic::try_get_ptr(json_pointer const& jsonPtr) const& {
  using err_code = json_pointer_resolution_error_code;
  using error = json_pointer_resolution_error<dynamic const>;

  auto const& tokens = jsonPtr.tokens();
  if (tokens.empty()) {
    return json_pointer_resolved_value<dynamic const>{
        nullptr, this, {nullptr, nullptr}, 0};
  }

  dynamic const* curr = this;
  dynamic const* prev = nullptr;

  size_t curr_idx{0};
  StringPiece curr_key{};

  for (auto it : enumerate(tokens)) {
    // hit bottom but pointer not exhausted yet
    if (!curr) {
      return makeUnexpected(
          error{err_code::json_pointer_out_of_bounds, it.index, prev});
    }
    prev = curr;
    // handle lookup in array
    if (auto const* parray = curr->get_nothrow<dynamic::Array>()) {
      if (it->size() > 1 && it->at(0) == '0') {
        return makeUnexpected(
            error{err_code::index_has_leading_zero, it.index, prev});
      }
      // if last element of pointer is '-', this is an append operation
      if (it->size() == 1 && it->at(0) == '-') {
        // was '-' the last token in pointer?
        if (it.index == tokens.size() - 1) {
          return makeUnexpected(
              error{err_code::append_requested, it.index, prev});
        }
        // Cannot resolve past '-' in an array
        curr = nullptr;
        continue;
      }
      auto const idx = tryTo<size_t>(*it);
      if (!idx.hasValue()) {
        return makeUnexpected(
            error{err_code::index_not_numeric, it.index, prev});
      }
      if (idx.value() < parray->size()) {
        curr = &(*parray)[idx.value()];
        curr_idx = idx.value();
      } else {
        return makeUnexpected(
            error{err_code::index_out_of_bounds, it.index, prev});
      }
      continue;
    }
    // handle lookup in object
    if (auto const* pobject = curr->get_nothrow<dynamic::ObjectImpl>()) {
      auto const sub_it = pobject->find(*it);
      if (sub_it == pobject->end()) {
        return makeUnexpected(error{err_code::key_not_found, it.index, prev});
      }
      curr = &sub_it->second;
      curr_key = *it;
      continue;
    }
    return makeUnexpected(
        error{err_code::element_not_object_or_array, it.index, prev});
  }
  return json_pointer_resolved_value<dynamic const>{
      prev, curr, curr_key, curr_idx};
}

const dynamic* dynamic::get_ptr(json_pointer const& jsonPtr) const& {
  using err_code = json_pointer_resolution_error_code;

  auto ret = try_get_ptr(jsonPtr);
  if (ret.hasValue()) {
    return ret.value().value;
  }

  auto const ctx = ret.error().context;
  auto const objType = ctx ? ctx->type() : Type::NULLT;

  switch (ret.error().error_code) {
    case err_code::key_not_found:
      return nullptr;
    case err_code::index_out_of_bounds:
      return nullptr;
    case err_code::append_requested:
      return nullptr;
    case err_code::index_not_numeric:
      throw std::invalid_argument("array index is not numeric");
    case err_code::index_has_leading_zero:
      throw std::invalid_argument(
          "leading zero not allowed when indexing arrays");
    case err_code::element_not_object_or_array:
      throw_exception<TypeError>("object/array", objType);
    case err_code::json_pointer_out_of_bounds:
      return nullptr;
    case err_code::other:
    default:
      return nullptr;
  }
}

void dynamic::reserve(std::size_t capacity) {
  if (auto* ar = get_nothrow<Array>()) {
    ar->reserve(capacity);
  } else if (auto* obj = get_nothrow<ObjectImpl>()) {
    obj->reserve(capacity);
  } else if (auto* str = get_nothrow<std::string>()) {
    str->reserve(capacity);
  } else {
    throw_exception<TypeError>("array/object/string", type());
  }
}

//////////////////////////////////////////////////////////////////////

} // namespace folly
