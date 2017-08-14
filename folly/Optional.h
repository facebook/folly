/*
 * Copyright 2017 Facebook, Inc.
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

#pragma once

/*
 * Optional - For conditional initialization of values, like boost::optional,
 * but with support for move semantics and emplacement.  Reference type support
 * has not been included due to limited use cases and potential confusion with
 * semantics of assignment: Assigning to an optional reference could quite
 * reasonably copy its value or redirect the reference.
 *
 * Optional can be useful when a variable might or might not be needed:
 *
 *  Optional<Logger> maybeLogger = ...;
 *  if (maybeLogger) {
 *    maybeLogger->log("hello");
 *  }
 *
 * Optional enables a 'null' value for types which do not otherwise have
 * nullability, especially useful for parameter passing:
 *
 * void testIterator(const unique_ptr<Iterator>& it,
 *                   initializer_list<int> idsExpected,
 *                   Optional<initializer_list<int>> ranksExpected = none) {
 *   for (int i = 0; it->next(); ++i) {
 *     EXPECT_EQ(it->doc().id(), idsExpected[i]);
 *     if (ranksExpected) {
 *       EXPECT_EQ(it->doc().rank(), (*ranksExpected)[i]);
 *     }
 *   }
 * }
 *
 * Optional models OptionalPointee, so calling 'get_pointer(opt)' will return a
 * pointer to nullptr if the 'opt' is empty, and a pointer to the value if it is
 * not:
 *
 *  Optional<int> maybeInt = ...;
 *  if (int* v = get_pointer(maybeInt)) {
 *    cout << *v << endl;
 *  }
 */

#include <cstddef>
#include <functional>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <folly/Launder.h>
#include <folly/Portability.h>
#include <folly/Utility.h>

namespace folly {

namespace detail {
struct NoneHelper {};

// Allow each translation unit to control its own -fexceptions setting.
// If exceptions are disabled, std::terminate() will be called instead of
// throwing OptionalEmptyException when the condition fails.
[[noreturn]] void throw_optional_empty_exception();
}

typedef int detail::NoneHelper::*None;

const None none = nullptr;

class OptionalEmptyException : public std::runtime_error {
 public:
  OptionalEmptyException()
      : std::runtime_error("Empty Optional cannot be unwrapped") {}
};

template <class Value>
class Optional {
 public:
  typedef Value value_type;

  static_assert(
      !std::is_reference<Value>::value,
      "Optional may not be used with reference types");
  static_assert(
      !std::is_abstract<Value>::value,
      "Optional may not be used with abstract types");

  Optional() noexcept {}

  Optional(const Optional& src) noexcept(
      std::is_nothrow_copy_constructible<Value>::value) {
    if (src.hasValue()) {
      storage_.construct(src.value());
    }
  }

  Optional(Optional&& src) noexcept(
      std::is_nothrow_move_constructible<Value>::value) {
    if (src.hasValue()) {
      storage_.construct(std::move(src.value()));
      src.clear();
    }
  }

  /* implicit */ Optional(const None&) noexcept {}

  /* implicit */ Optional(Value&& newValue) noexcept(
      std::is_nothrow_move_constructible<Value>::value) {
    storage_.construct(std::move(newValue));
  }

  /* implicit */ Optional(const Value& newValue) noexcept(
      std::is_nothrow_copy_constructible<Value>::value) {
    storage_.construct(newValue);
  }

  template <typename... Args>
  explicit Optional(in_place_t, Args&&... args) noexcept(
      std::is_nothrow_constructible<Value, Args...>::value) {
    storage_.construct(std::forward<Args>(args)...);
  }

  void assign(const None&) {
    clear();
  }

  void assign(Optional&& src) {
    if (this != &src) {
      if (src.hasValue()) {
        assign(std::move(src.value()));
        src.clear();
      } else {
        clear();
      }
    }
  }

  void assign(const Optional& src) {
    if (src.hasValue()) {
      assign(src.value());
    } else {
      clear();
    }
  }

  void assign(Value&& newValue) {
    if (hasValue()) {
      *storage_.value_pointer() = std::move(newValue);
    } else {
      storage_.construct(std::move(newValue));
    }
  }

  void assign(const Value& newValue) {
    if (hasValue()) {
      *storage_.value_pointer() = newValue;
    } else {
      storage_.construct(newValue);
    }
  }

  template <class Arg>
  Optional& operator=(Arg&& arg) {
    assign(std::forward<Arg>(arg));
    return *this;
  }

  Optional& operator=(Optional&& other) noexcept(
      std::is_nothrow_move_assignable<Value>::value) {
    assign(std::move(other));
    return *this;
  }

  Optional& operator=(const Optional& other) noexcept(
      std::is_nothrow_copy_assignable<Value>::value) {
    assign(other);
    return *this;
  }

  template <class... Args>
  void emplace(Args&&... args) {
    clear();
    storage_.construct(std::forward<Args>(args)...);
  }

  void clear() {
    storage_.clear();
  }

  const Value& value() const & {
    require_value();
    return *storage_.value_pointer();
  }

  Value& value() & {
    require_value();
    return *storage_.value_pointer();
  }

  Value&& value() && {
    require_value();
    return std::move(*storage_.value_pointer());
  }

  const Value&& value() const && {
    require_value();
    return std::move(*storage_.value_pointer());
  }

  const Value* get_pointer() const & {
    return storage_.value_pointer();
  }
  Value* get_pointer() & {
    return storage_.value_pointer();
  }
  Value* get_pointer() && = delete;

  bool hasValue() const noexcept {
    return storage_.hasValue();
  }

  explicit operator bool() const noexcept {
    return hasValue();
  }

  const Value& operator*() const & {
    return value();
  }
  Value& operator*() & {
    return value();
  }
  const Value&& operator*() const && {
    return std::move(value());
  }
  Value&& operator*() && {
    return std::move(value());
  }

  const Value* operator->() const {
    return &value();
  }
  Value* operator->() {
    return &value();
  }

  // Return a copy of the value if set, or a given default if not.
  template <class U>
  Value value_or(U&& dflt) const & {
    if (storage_.hasValue()) {
      return *storage_.value_pointer();
    }

    return std::forward<U>(dflt);
  }

  template <class U>
  Value value_or(U&& dflt) && {
    if (storage_.hasValue()) {
      return std::move(*storage_.value_pointer());
    }

    return std::forward<U>(dflt);
  }

 private:
  void require_value() const {
    if (!storage_.hasValue()) {
      detail::throw_optional_empty_exception();
    }
  }

  struct StorageTriviallyDestructible {
   protected:
    bool hasValue_;
    typename std::aligned_storage<sizeof(Value), alignof(Value)>::type
        value_[1];

   public:
    StorageTriviallyDestructible() : hasValue_{false} {}
    void clear() {
      hasValue_ = false;
    }
  };

  struct StorageNonTriviallyDestructible {
   protected:
    bool hasValue_;
    typename std::aligned_storage<sizeof(Value), alignof(Value)>::type
        value_[1];

   public:
    StorageNonTriviallyDestructible() : hasValue_{false} {}
    ~StorageNonTriviallyDestructible() {
      clear();
    }

    void clear() {
      if (hasValue_) {
        hasValue_ = false;
        launder(reinterpret_cast<Value*>(value_))->~Value();
      }
    }
  };

  struct Storage : std::conditional<
                       std::is_trivially_destructible<Value>::value,
                       StorageTriviallyDestructible,
                       StorageNonTriviallyDestructible>::type {
    bool hasValue() const noexcept {
      return this->hasValue_;
    }

    Value* value_pointer() {
      if (this->hasValue_) {
        return launder(reinterpret_cast<Value*>(this->value_));
      }
      return nullptr;
    }

    Value const* value_pointer() const {
      if (this->hasValue_) {
        return launder(reinterpret_cast<Value const*>(this->value_));
      }
      return nullptr;
    }

    template <class... Args>
    void construct(Args&&... args) {
      new (raw_pointer()) Value(std::forward<Args>(args)...);
      this->hasValue_ = true;
    }

   private:
    void* raw_pointer() {
      return static_cast<void*>(this->value_);
    }
  };

  Storage storage_;
};

template <class T>
const T* get_pointer(const Optional<T>& opt) {
  return opt.get_pointer();
}

template <class T>
T* get_pointer(Optional<T>& opt) {
  return opt.get_pointer();
}

template <class T>
void swap(Optional<T>& a, Optional<T>& b) {
  if (a.hasValue() && b.hasValue()) {
    // both full
    using std::swap;
    swap(a.value(), b.value());
  } else if (a.hasValue() || b.hasValue()) {
    std::swap(a, b); // fall back to default implementation if they're mixed.
  }
}

template <class T, class Opt = Optional<typename std::decay<T>::type>>
Opt make_optional(T&& v) {
  return Opt(std::forward<T>(v));
}

///////////////////////////////////////////////////////////////////////////////
// Comparisons.

template <class U, class V>
bool operator==(const Optional<U>& a, const V& b) {
  return a.hasValue() && a.value() == b;
}

template <class U, class V>
bool operator!=(const Optional<U>& a, const V& b) {
  return !(a == b);
}

template <class U, class V>
bool operator==(const U& a, const Optional<V>& b) {
  return b.hasValue() && b.value() == a;
}

template <class U, class V>
bool operator!=(const U& a, const Optional<V>& b) {
  return !(a == b);
}

template <class U, class V>
bool operator==(const Optional<U>& a, const Optional<V>& b) {
  if (a.hasValue() != b.hasValue()) {
    return false;
  }
  if (a.hasValue()) {
    return a.value() == b.value();
  }
  return true;
}

template <class U, class V>
bool operator!=(const Optional<U>& a, const Optional<V>& b) {
  return !(a == b);
}

template <class U, class V>
bool operator<(const Optional<U>& a, const Optional<V>& b) {
  if (a.hasValue() != b.hasValue()) {
    return a.hasValue() < b.hasValue();
  }
  if (a.hasValue()) {
    return a.value() < b.value();
  }
  return false;
}

template <class U, class V>
bool operator>(const Optional<U>& a, const Optional<V>& b) {
  return b < a;
}

template <class U, class V>
bool operator<=(const Optional<U>& a, const Optional<V>& b) {
  return !(b < a);
}

template <class U, class V>
bool operator>=(const Optional<U>& a, const Optional<V>& b) {
  return !(a < b);
}

// Suppress comparability of Optional<T> with T, despite implicit conversion.
template <class V>
bool operator<(const Optional<V>&, const V& other) = delete;
template <class V>
bool operator<=(const Optional<V>&, const V& other) = delete;
template <class V>
bool operator>=(const Optional<V>&, const V& other) = delete;
template <class V>
bool operator>(const Optional<V>&, const V& other) = delete;
template <class V>
bool operator<(const V& other, const Optional<V>&) = delete;
template <class V>
bool operator<=(const V& other, const Optional<V>&) = delete;
template <class V>
bool operator>=(const V& other, const Optional<V>&) = delete;
template <class V>
bool operator>(const V& other, const Optional<V>&) = delete;

// Comparisons with none
template <class V>
bool operator==(const Optional<V>& a, None) noexcept {
  return !a.hasValue();
}
template <class V>
bool operator==(None, const Optional<V>& a) noexcept {
  return !a.hasValue();
}
template <class V>
bool operator<(const Optional<V>&, None) noexcept {
  return false;
}
template <class V>
bool operator<(None, const Optional<V>& a) noexcept {
  return a.hasValue();
}
template <class V>
bool operator>(const Optional<V>& a, None) noexcept {
  return a.hasValue();
}
template <class V>
bool operator>(None, const Optional<V>&) noexcept {
  return false;
}
template <class V>
bool operator<=(None, const Optional<V>&) noexcept {
  return true;
}
template <class V>
bool operator<=(const Optional<V>& a, None) noexcept {
  return !a.hasValue();
}
template <class V>
bool operator>=(const Optional<V>&, None) noexcept {
  return true;
}
template <class V>
bool operator>=(None, const Optional<V>& a) noexcept {
  return !a.hasValue();
}

///////////////////////////////////////////////////////////////////////////////

} // namespace folly

// Allow usage of Optional<T> in std::unordered_map and std::unordered_set
FOLLY_NAMESPACE_STD_BEGIN
template <class T>
struct hash<folly::Optional<T>> {
  size_t operator()(folly::Optional<T> const& obj) const {
    if (!obj.hasValue()) {
      return 0;
    }
    return hash<typename remove_const<T>::type>()(*obj);
  }
};
FOLLY_NAMESPACE_STD_END
