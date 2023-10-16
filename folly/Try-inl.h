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

#include <folly/Utility.h>
#include <folly/functional/Invoke.h>

#include <stdexcept>
#include <tuple>
#include <utility>

namespace folly {

namespace detail {
template <class T>
TryBase<T>::TryBase(TryBase<T>&& t) noexcept(
    std::is_nothrow_move_constructible<T>::value)
    : contains_(t.contains_) {
  if (contains_ == Contains::VALUE) {
    ::new (static_cast<void*>(std::addressof(value_))) T(std::move(t.value_));
  } else if (contains_ == Contains::EXCEPTION) {
    new (&e_) exception_wrapper(std::move(t.e_));
  }
}

template <class T>
TryBase<T>& TryBase<T>::operator=(TryBase<T>&& t) noexcept(
    std::is_nothrow_move_constructible<T>::value) {
  if (this == &t) {
    return *this;
  }

  destroy();

  if (t.contains_ == Contains::VALUE) {
    ::new (static_cast<void*>(std::addressof(value_))) T(std::move(t.value_));
  } else if (t.contains_ == Contains::EXCEPTION) {
    new (&e_) exception_wrapper(std::move(t.e_));
  }

  contains_ = t.contains_;

  return *this;
}

template <class T>
TryBase<T>::TryBase(const TryBase<T>& t) noexcept(
    std::is_nothrow_copy_constructible<T>::value) {
  contains_ = t.contains_;
  if (contains_ == Contains::VALUE) {
    ::new (static_cast<void*>(std::addressof(value_))) T(t.value_);
  } else if (contains_ == Contains::EXCEPTION) {
    new (&e_) exception_wrapper(t.e_);
  }
}

template <class T>
TryBase<T>& TryBase<T>::operator=(const TryBase<T>& t) noexcept(
    std::is_nothrow_copy_constructible<T>::value) {
  if (this == &t) {
    return *this;
  }

  destroy();

  if (t.contains_ == Contains::VALUE) {
    ::new (static_cast<void*>(std::addressof(value_))) T(t.value_);
  } else if (t.contains_ == Contains::EXCEPTION) {
    new (&e_) exception_wrapper(t.e_);
  }

  contains_ = t.contains_;

  return *this;
}

template <class T>
void TryBase<T>::destroy() noexcept {
  auto oldContains = std::exchange(contains_, Contains::NOTHING);
  if (FOLLY_LIKELY(oldContains == Contains::VALUE)) {
    value_.~T();
  } else if (FOLLY_UNLIKELY(oldContains == Contains::EXCEPTION)) {
    e_.~exception_wrapper();
  }
}

template <class T>
template <class T2>
TryBase<T>::TryBase(typename std::enable_if<
                    std::is_same<Unit, T2>::value,
                    Try<void> const&>::type t) noexcept
    : contains_(Contains::NOTHING) {
  if (t.hasValue()) {
    contains_ = Contains::VALUE;
    ::new (static_cast<void*>(std::addressof(value_))) T();
  } else if (t.hasException()) {
    contains_ = Contains::EXCEPTION;
    new (&e_) exception_wrapper(t.exception());
  }
}

template <class T>
TryBase<T>::~TryBase() {
  if (FOLLY_LIKELY(contains_ == Contains::VALUE)) {
    value_.~T();
  } else if (FOLLY_UNLIKELY(contains_ == Contains::EXCEPTION)) {
    e_.~exception_wrapper();
  }
}

} // namespace detail

Try<void>::Try(const Try<Unit>& t) noexcept : hasValue_(!t.hasException()) {
  if (t.hasException()) {
    new (&this->e_) exception_wrapper(t.exception());
  }
}

template <typename T>
template <typename... Args>
T& Try<T>::emplace(Args&&... args) noexcept(
    std::is_nothrow_constructible<T, Args&&...>::value) {
  this->destroy();
  ::new (static_cast<void*>(std::addressof(this->value_)))
      T(static_cast<Args&&>(args)...);
  this->contains_ = Contains::VALUE;
  return this->value_;
}

template <typename T>
template <typename... Args>
exception_wrapper& Try<T>::emplaceException(Args&&... args) noexcept(
    std::is_nothrow_constructible<exception_wrapper, Args&&...>::value) {
  this->destroy();
  new (&this->e_) exception_wrapper(static_cast<Args&&>(args)...);
  this->contains_ = Contains::EXCEPTION;
  return this->e_;
}

template <class T>
T& Try<T>::value() & {
  throwUnlessValue();
  return this->value_;
}

template <class T>
T&& Try<T>::value() && {
  throwUnlessValue();
  return std::move(this->value_);
}

template <class T>
const T& Try<T>::value() const& {
  throwUnlessValue();
  return this->value_;
}

template <class T>
const T&& Try<T>::value() const&& {
  throwUnlessValue();
  return std::move(this->value_);
}

template <class T>
template <class U>
T Try<T>::value_or(U&& defaultValue) const& {
  return hasValue() ? **this : static_cast<T>(static_cast<U&&>(defaultValue));
}

template <class T>
template <class U>
T Try<T>::value_or(U&& defaultValue) && {
  return hasValue() ? std::move(**this)
                    : static_cast<T>(static_cast<U&&>(defaultValue));
}

template <class T>
void Try<T>::throwUnlessValue() const {
  switch (this->contains_) {
    case Contains::VALUE:
      return;
    case Contains::EXCEPTION:
      this->e_.throw_exception();
    case Contains::NOTHING:
    default:
      throw_exception<UsingUninitializedTry>();
  }
}

template <class T>
void Try<T>::throwIfFailed() const {
  throwUnlessValue();
}

Try<void>& Try<void>::operator=(const Try<void>& t) noexcept {
  if (t.hasException()) {
    if (hasException()) {
      this->e_ = t.e_;
    } else {
      new (&this->e_) exception_wrapper(t.e_);
      hasValue_ = false;
    }
  } else {
    if (hasException()) {
      this->e_.~exception_wrapper();
      hasValue_ = true;
    }
  }
  return *this;
}

template <typename... Args>
exception_wrapper& Try<void>::emplaceException(Args&&... args) noexcept(
    std::is_nothrow_constructible<exception_wrapper, Args&&...>::value) {
  if (hasException()) {
    this->e_.~exception_wrapper();
  }
  new (&this->e_) exception_wrapper(static_cast<Args&&>(args)...);
  hasValue_ = false;
  return this->e_;
}

void Try<void>::throwIfFailed() const {
  if (hasException()) {
    this->e_.throw_exception();
  }
}

void Try<void>::throwUnlessValue() const {
  throwIfFailed();
}

template <typename F>
typename std::enable_if<
    !std::is_same<invoke_result_t<F>, void>::value,
    Try<invoke_result_t<F>>>::type
makeTryWithNoUnwrap(F&& f) {
  using ResultType = invoke_result_t<F>;
  try {
    return Try<ResultType>(f());
  } catch (...) {
    return Try<ResultType>(exception_wrapper(std::current_exception()));
  }
}

template <typename F>
typename std::
    enable_if<std::is_same<invoke_result_t<F>, void>::value, Try<void>>::type
    makeTryWithNoUnwrap(F&& f) {
  try {
    f();
    return Try<void>();
  } catch (...) {
    return Try<void>(exception_wrapper(std::current_exception()));
  }
}

template <typename F>
typename std::
    enable_if<!isTry<invoke_result_t<F>>::value, Try<invoke_result_t<F>>>::type
    makeTryWith(F&& f) {
  return makeTryWithNoUnwrap(std::forward<F>(f));
}

template <typename F>
typename std::enable_if<isTry<invoke_result_t<F>>::value, invoke_result_t<F>>::
    type
    makeTryWith(F&& f) {
  using ResultType = invoke_result_t<F>;
  try {
    return f();
  } catch (...) {
    return ResultType(exception_wrapper(std::current_exception()));
  }
}

template <typename T, typename... Args>
T* tryEmplace(Try<T>& t, Args&&... args) noexcept {
  try {
    return std::addressof(t.emplace(static_cast<Args&&>(args)...));
  } catch (...) {
    t.emplaceException(std::current_exception());
    return nullptr;
  }
}

void tryEmplace(Try<void>& t) noexcept {
  t.emplace();
}

template <typename T, typename Func>
T* tryEmplaceWith(Try<T>& t, Func&& func) noexcept {
  static_assert(
      std::is_constructible<T, folly::invoke_result_t<Func>>::value,
      "Unable to initialise a value of type T with the result of 'func'");
  try {
    return std::addressof(t.emplace(static_cast<Func&&>(func)()));
  } catch (...) {
    t.emplaceException(std::current_exception());
    return nullptr;
  }
}

template <typename Func>
bool tryEmplaceWith(Try<void>& t, Func&& func) noexcept {
  static_assert(
      std::is_void<folly::invoke_result_t<Func>>::value,
      "Func returns non-void. Cannot be used to emplace Try<void>");
  try {
    static_cast<Func&&>(func)();
    t.emplace();
    return true;
  } catch (...) {
    t.emplaceException(std::current_exception());
    return false;
  }
}

namespace try_detail {

/**
 * Trait that removes the layer of Try abstractions from the passed in type
 */
template <typename Type>
struct RemoveTry;
template <template <typename...> class TupleType, typename... Types>
struct RemoveTry<TupleType<folly::Try<Types>...>> {
  using type = TupleType<Types...>;
};

template <std::size_t... Indices, typename Tuple>
auto unwrapTryTupleImpl(std::index_sequence<Indices...>, Tuple&& instance) {
  using std::get;
  using ReturnType = typename RemoveTry<typename std::decay<Tuple>::type>::type;
  return ReturnType{(get<Indices>(std::forward<Tuple>(instance)).value())...};
}
} // namespace try_detail

template <typename Tuple>
auto unwrapTryTuple(Tuple&& instance) {
  using TupleDecayed = typename std::decay<Tuple>::type;
  using Seq = std::make_index_sequence<std::tuple_size<TupleDecayed>::value>;
  return try_detail::unwrapTryTupleImpl(Seq{}, std::forward<Tuple>(instance));
}

template <typename T>
void tryAssign(Try<T>& t, Try<T>&& other) noexcept {
  try {
    t = std::move(other);
  } catch (...) {
    t.emplaceException(std::current_exception());
  }
}

// limited to the instances unconditionally forced by the futures library
extern template class Try<Unit>;

} // namespace folly
