// clang-format off
// clang format does not support the '<>' in the lambda syntax yet.. []<>()->{}
#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#if __cpp_lib_optional >= 201606
#include <optional>
#endif
#include <type_traits>

namespace pushmi {
namespace detail {
#if __cpp_lib_optional >= 201606
template <class T>
struct opt : private std::optional<T> {
   opt() = default;
   opt& operator=(T&& t) {
     this->std::optional<T>::operator=(std::move(t));
     return *this;
   }
   using std::optional<T>::operator*;
   using std::optional<T>::operator bool;
};
#else
template <class T>
struct opt {
private:
  bool empty_ = true;
  std::aligned_union_t<0, T> data_;
  T* ptr() {
    return static_cast<T*>((void*)&data_);
  }
  const T* ptr() const {
    return static_cast<const T*>((const void*)&data_);
  }
  void reset() {
    if (!empty_) {
      ptr()->~T();
      empty_ = true;
    }
  }
public:
  opt() = default;
  opt(T&& t) noexcept(std::is_nothrow_move_constructible<T>::value) {
    ::new(ptr()) T(std::move(t));
    empty_ = false;
  }
  opt(const T& t) {
    ::new(ptr()) T(t);
    empty_ = false;
  }
  opt(opt&& that) noexcept(std::is_nothrow_move_constructible<T>::value) {
    if (that) {
      ::new(ptr()) T(std::move(*that));
      empty_ = false;
      that.reset();
    }
  }
  opt(const opt& that) {
    if (that) {
      ::new(ptr()) T(*that);
      empty_ = false;
    }
  }
  ~opt() { reset(); }
  opt& operator=(opt&& that)
    noexcept(std::is_nothrow_move_constructible<T>::value &&
             std::is_nothrow_move_assignable<T>::value) {
    if (*this && that) {
      **this = std::move(*that);
      that.reset();
    } else if (*this) {
      reset();
    } else if (that) {
      ::new(ptr()) T(std::move(*that));
      empty_ = false;
    }
    return *this;
  }
  opt& operator=(const opt& that) {
    if (*this && that) {
      **this = *that;
    } else if (*this) {
      reset();
    } else if (that) {
      ::new(ptr()) T(*that);
      empty_ = false;
    }
    return *this;
  }
  opt& operator=(T&& t) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                 std::is_nothrow_move_assignable<T>::value) {
    if (*this)
      **this = std::move(t);
    else {
      ::new(ptr()) T(std::move(t));
      empty_ = false;
    }
    return *this;
  }
  opt& operator=(const T& t) {
    if (*this)
      **this = t;
    else {
      ::new(ptr()) T(t);
      empty_ = false;
    }
    return *this;
  }
  explicit operator bool() const noexcept {
    return !empty_;
  }
  T& operator*() noexcept {
    return *ptr();
  }
  const T& operator*() const noexcept {
    return *ptr();
  }
};
#endif

} // namespace detail
} // namespace pushmi
