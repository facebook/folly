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

#include <cassert>
#include <exception>
#include <type_traits>
#include <utility>

#include <folly/Portability.h>

#include <folly/experimental/pushmi/awaitable/concepts.h>
#include <folly/experimental/pushmi/sender/detail/concepts.h>
#include <folly/experimental/pushmi/sender/primitives.h>
#include <folly/experimental/pushmi/sender/properties.h>
#include <folly/experimental/pushmi/traits.h>
#include <folly/experimental/pushmi/receiver/tags.h>

#if FOLLY_HAS_COROUTINES
#include <experimental/coroutine>
#include <folly/experimental/coro/detail/ManualLifetime.h>
#endif

namespace folly {
namespace pushmi {

struct operation_cancelled : std::exception {
  virtual const char* what() const noexcept {
    return "operation cancelled";
  }
};

#if FOLLY_HAS_COROUTINES

PUSHMI_TEMPLATE(class From)( //
  requires SingleTypedSender<From> //
) //
struct sender_awaiter {
private:
  using value_type = sender_values_t<From, detail::identity_or_void_t>;
  using coro_handle = std::experimental::coroutine_handle<>;

  std::add_pointer_t<From> sender_{};
  enum class state { empty, value, exception };

  coro_handle continuation_{};
  state state_ = state::empty;
  union {
    coro::detail::ManualLifetime<value_type> value_{};
    coro::detail::ManualLifetime<std::exception_ptr> exception_;
  };

  using is_always_blocking = property_query<From, is_always_blocking<>>;

  struct internal_receiver {
    sender_awaiter *this_;

    using receiver_category = receiver_tag;

    PUSHMI_TEMPLATE(class U)(               //
      requires ConvertibleTo<U, value_type> //
    )                                       //
    void value(U&& value)
      noexcept(std::is_nothrow_constructible<value_type, U>::value) {
      this_->value_.construct(static_cast<U&&>(value));
      this_->state_ = state::value;
    }

    PUSHMI_TEMPLATE(class V = value_type)(  //
      requires std::is_void<V>::value       //
    )                                       //
    void value() noexcept {
      this_->value_.construct();
      this_->state_ = state::value;
    }

    void done() noexcept {
      if (!is_always_blocking::value)
        this_->continuation_.resume();
    }

    template<typename Error>
    void error(Error error) noexcept {
      assert(this_->state_ != state::value);
      PUSHMI_IF_CONSTEXPR( (std::is_same<Error, std::exception_ptr>::value) (
        this_->exception_.construct(std::move(error));
      ) else (
        this_->exception_.construct(std::make_exception_ptr(std::move(error)));
      ))
      this_->state_ = state::exception;
      if (!is_always_blocking::value)
        this_->continuation_.resume();
    }
  };

public:
  sender_awaiter() {}
  sender_awaiter(From&& sender) noexcept
  : sender_(std::addressof(sender))
  {}
  sender_awaiter(sender_awaiter &&that)
    noexcept(std::is_nothrow_move_constructible<value_type>::value ||
      std::is_void<value_type>::value)
  : sender_(std::exchange(that.sender_, nullptr))
  , continuation_{std::exchange(that.continuation_, {})}
  , state_(that.state_) {
    if (that.state_ == state::value) {
      PUSHMI_IF_CONSTEXPR( (!std::is_void<value_type>::value) (
        id(value_).construct(std::move(that.value_).get());
      ) else (
      ))
      that.value_.destruct();
      that.state_ = state::empty;
    } else if (that.state_ == state::exception) {
      exception_.construct(std::move(that.exception_).get());
      that.exception_.destruct();
      that.state_ = state::empty;
    }
  }

  ~sender_awaiter() {
    if (state_ == state::value) {
      value_.destruct();
    } else if (state_ == state::exception) {
      exception_.destruct();
    }
  }

  static constexpr bool await_ready() noexcept {
    return false;
  }

  // Add detection and handling of blocking completion here, and
  // return 'false' from await_suspend() in that case rather than
  // potentially recursively resuming the awaiting coroutine which
  // could eventually lead to a stack-overflow.
  using await_suspend_result_t =
    std::conditional_t<is_always_blocking::value, bool, void>;

  await_suspend_result_t await_suspend(coro_handle continuation) noexcept {
    continuation_ = continuation;
    pushmi::submit(static_cast<From&&>(*sender_), internal_receiver{this});
    return await_suspend_result_t(); // return false or void
  }

  decltype(auto) await_resume() {
    if (state_ == state::exception) {
      std::rethrow_exception(std::move(exception_).get());
    } else if (state_ == state::empty) {
      throw operation_cancelled{};
    } else {
      return std::move(value_).get();
    }
  }
};

#if __cpp_deduction_guides >= 201703
template<typename From>
sender_awaiter(From&&) -> sender_awaiter<From>;
#endif

namespace awaitable_senders {
  // Any TypedSender that inherits from `sender` or `sender_traits` is
  // automatically awaitable with the following operator co_await through the
  // magic of associated namespaces. To make any other TypedSender awaitable,
  // from within the body of the awaiting coroutine, do:
  // `using namespace ::folly::pushmi::awaitable_senders;`
  PUSHMI_TEMPLATE(class From)( //
    requires not Awaiter<From> && SingleTypedSender<From> //
  ) //
  sender_awaiter<From> operator co_await(From&& from) {
    return static_cast<From&&>(from);
  }
} // namespace awaitable_senders

#endif // FOLLY_HAS_COROUTINES

} // namespace pushmi
} // namespace folly
