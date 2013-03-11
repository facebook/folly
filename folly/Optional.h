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

#ifndef FOLLY_OPTIONAL_H_
#define FOLLY_OPTIONAL_H_

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
#include <utility>
#include <cassert>
#include <cstddef>
#include <type_traits>

#include <boost/operators.hpp>

namespace folly {

namespace detail { struct NoneHelper {}; }

typedef int detail::NoneHelper::*None;

const None none = nullptr;

/**
 * gcc-4.7 warns about use of uninitialized memory around the use of storage_
 * even though this is explicitly initialized at each point.
 */
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wuninitialized"
# pragma GCC diagnostic ignored "-Wpragmas"
# pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif // __GNUC__

template<class Value>
class Optional : boost::totally_ordered<Optional<Value>,
                 boost::totally_ordered<Optional<Value>, Value>> {
  typedef void (Optional::*bool_type)() const;
  void truthy() const {};
 public:
  static_assert(!std::is_reference<Value>::value,
                "Optional may not be used with reference types");

  Optional()
    : hasValue_(false) {
  }

  Optional(const Optional& src) {
    if (src.hasValue()) {
      construct(src.value());
    } else {
      hasValue_ = false;
    }
  }

  Optional(Optional&& src) {
    if (src.hasValue()) {
      construct(std::move(src.value()));
      src.clear();
    } else {
      hasValue_ = false;
    }
  }

  /* implicit */ Optional(const None& empty)
    : hasValue_(false) {
  }

  /* implicit */ Optional(Value&& newValue) {
    construct(std::move(newValue));
  }

  /* implicit */ Optional(const Value& newValue) {
    construct(newValue);
  }

  ~Optional() {
    clear();
  }

  void assign(const None&) {
    clear();
  }

  void assign(Optional&& src) {
    if (src.hasValue()) {
      assign(std::move(src.value()));
      src.clear();
    } else {
      clear();
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
      value_ = std::move(newValue);
    } else {
      construct(std::move(newValue));
    }
  }

  void assign(const Value& newValue) {
    if (hasValue()) {
      value_ = newValue;
    } else {
      construct(newValue);
    }
  }

  template<class Arg>
  Optional& operator=(Arg&& arg) {
    assign(std::forward<Arg>(arg));
    return *this;
  }

  Optional& operator=(Optional &&other) {
    assign(std::move(other));
    return *this;
  }

  Optional& operator=(const Optional &other) {
    assign(other);
    return *this;
  }

  bool operator<(const Optional& other) const {
    if (hasValue() != other.hasValue()) {
      return hasValue() < other.hasValue();
    }
    if (hasValue()) {
      return value() < other.value();
    }
    return false; // both empty
  }

  bool operator<(const Value& other) const {
    return !hasValue() || value() < other;
  }

  bool operator==(const Optional& other) const {
    if (hasValue()) {
      return other.hasValue() && value() == other.value();
    } else {
      return !other.hasValue();
    }
  }

  bool operator==(const Value& other) const {
    return hasValue() && value() == other;
  }

  template<class... Args>
  void emplace(Args&&... args) {
    clear();
    construct(std::forward<Args>(args)...);
  }

  void clear() {
    if (hasValue()) {
      hasValue_ = false;
      value_.~Value();
    }
  }

  const Value& value() const {
    assert(hasValue());
    return value_;
  }

  Value& value() {
    assert(hasValue());
    return value_;
  }

  bool hasValue() const { return hasValue_; }

  /* safe bool idiom */
  operator bool_type() const {
    return hasValue() ? &Optional::truthy : nullptr;
  }

  const Value& operator*() const { return value(); }
        Value& operator*()       { return value(); }

  const Value* operator->() const { return &value(); }
        Value* operator->()       { return &value(); }

 private:
  template<class... Args>
  void construct(Args&&... args) {
    const void* ptr = &value_;
    // for supporting const types
    new(const_cast<void*>(ptr)) Value(std::forward<Args>(args)...);
    hasValue_ = true;
  }

  // uninitialized
  union { Value value_; };
  bool hasValue_;
};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

template<class T>
const T* get_pointer(const Optional<T>& opt) {
  return opt ? &opt.value() : nullptr;
}

template<class T>
T* get_pointer(Optional<T>& opt) {
  return opt ? &opt.value() : nullptr;
}

template<class T>
void swap(Optional<T>& a, Optional<T>& b) {
  if (a.hasValue() && b.hasValue()) {
    // both full
    using std::swap;
    swap(a.value(), b.value());
  } else if (a.hasValue() || b.hasValue()) {
    std::swap(a, b); // fall back to default implementation if they're mixed.
  }
}

template<class T,
         class Opt = Optional<typename std::decay<T>::type>>
Opt make_optional(T&& v) {
  return Opt(std::forward<T>(v));
}

} // namespace folly

#endif//FOLLY_OPTIONAL_H_
