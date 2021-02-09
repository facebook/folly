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

#include <folly/ExceptionWrapper.h>
#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/Coroutine.h>

#include <variant>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

enum class CallbackRecordSelector { Invalid, Value, None, Error };

constexpr inline std::in_place_index_t<0> const callback_record_value{};
constexpr inline std::in_place_index_t<1> const callback_record_none{};
constexpr inline std::in_place_index_t<2> const callback_record_error{};

//
// CallbackRecord records the result of a single invocation of a callback.
//
// This is very related to Try and expected, but this also records None in
// addition to Value and Error results.
//
// When the callback supports multiple overloads of Value then T would be
// something like a variant<tuple<..>, ..>
//
// When the callback supports multiple overloads of Error then all the errors
// are coerced to folly::exception_wrapper
//
template <class T>
class CallbackRecord {
  static void clear(CallbackRecord* that) {
    auto selector =
        std::exchange(that->selector_, CallbackRecordSelector::Invalid);
    if (selector == CallbackRecordSelector::Value) {
      detail::deactivate(that->value_);
    } else if (selector == CallbackRecordSelector::Error) {
      detail::deactivate(that->error_);
    }
  }
  template <class OtherReference>
  static void convert_variant(
      CallbackRecord* that, const CallbackRecord<OtherReference>& other) {
    if (other.hasValue()) {
      detail::activate(that->value_, other.value_.get());
    } else if (other.hasError()) {
      detail::activate(that->error_, other.error_.get());
    }
    that->selector_ = other.selector_;
  }
  template <class OtherReference>
  static void convert_variant(
      CallbackRecord* that, CallbackRecord<OtherReference>&& other) {
    if (other.hasValue()) {
      detail::activate(that->value_, std::move(other.value_).get());
    } else if (other.hasError()) {
      detail::activate(that->error_, std::move(other.error_).get());
    }
    that->selector_ = other.selector_;
  }

 public:
  ~CallbackRecord() { clear(this); }

  CallbackRecord() noexcept : selector_(CallbackRecordSelector::Invalid) {}

  template <class V>
  CallbackRecord(const std::in_place_index_t<0>&, V&& v) noexcept(
      std::is_nothrow_constructible_v<T, V>)
      : CallbackRecord() {
    detail::activate(value_, std::forward<V>(v));
    selector_ = CallbackRecordSelector::Value;
  }
  explicit CallbackRecord(const std::in_place_index_t<1>&) noexcept
      : selector_(CallbackRecordSelector::None) {}
  CallbackRecord(
      const std::in_place_index_t<2>&, folly::exception_wrapper e) noexcept
      : CallbackRecord() {
    detail::activate(error_, std::move(e));
    selector_ = CallbackRecordSelector::Error;
  }

  CallbackRecord(CallbackRecord&& other) noexcept(
      std::is_nothrow_move_constructible_v<T>)
      : CallbackRecord() {
    convert_variant(this, std::move(other));
  }

  CallbackRecord& operator=(CallbackRecord&& other) noexcept(
      std::is_nothrow_move_constructible_v<T>) {
    if (&other != this) {
      clear(this);
      convert_variant(this, std::move(other));
    }
    return *this;
  }

  template <class U>
  CallbackRecord(CallbackRecord<U>&& other) noexcept(
      std::is_nothrow_constructible_v<T, U>)
      : CallbackRecord() {
    convert_variant(this, std::move(other));
  }

  bool hasNone() const noexcept {
    return selector_ == CallbackRecordSelector::None;
  }

  bool hasError() const noexcept {
    return selector_ == CallbackRecordSelector::Error;
  }

  decltype(auto) error() & {
    DCHECK(hasError());
    return error_.get();
  }

  decltype(auto) error() && {
    DCHECK(hasError());
    return std::move(error_).get();
  }

  decltype(auto) error() const& {
    DCHECK(hasError());
    return error_.get();
  }

  decltype(auto) error() const&& {
    DCHECK(hasError());
    return std::move(error_).get();
  }

  bool hasValue() const noexcept {
    return selector_ == CallbackRecordSelector::Value;
  }

  decltype(auto) value() & {
    DCHECK(hasValue());
    return value_.get();
  }

  decltype(auto) value() && {
    DCHECK(hasValue());
    return std::move(value_).get();
  }

  decltype(auto) value() const& {
    DCHECK(hasValue());
    return value_.get();
  }

  decltype(auto) value() const&& {
    DCHECK(hasValue());
    return std::move(value_).get();
  }

  explicit operator bool() const noexcept {
    return selector_ != CallbackRecordSelector::Invalid;
  }

 private:
  union {
    detail::ManualLifetime<T> value_;
    detail::ManualLifetime<folly::exception_wrapper> error_;
  };
  CallbackRecordSelector selector_;
};

// Materialize the results an input stream into a stream that
// contains all of the events of the input stream.
//
// The output is a stream of CallbackRecord.
//
// Example:
//   AsyncGenerator<int> stream();
//
//   Task<void> consumer() {
//     auto events = materialize(stream());
//     while (auto item = co_await events.next()) {
//       auto&& event = *item;
//       if (event.hasValue()) {
//          // Value
//          int value = std::move(event).value();
//          std::cout << "value " << value << "\n";
//       } else if (event.hasNone()) {
//         // None
//         std::cout << "end\n";
//       } else {
//         // Exception
//         folly::exception_wrapper error = std::move(event).error();
//         std::cout << "error " << error.what() << "\n";
//       }
//     }
//   }
template <typename Reference, typename Value>
AsyncGenerator<CallbackRecord<Reference>, CallbackRecord<Value>> materialize(
    AsyncGenerator<Reference, Value> source);

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Materialize-inl.h>
