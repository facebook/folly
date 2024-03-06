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

#include <exception>
#include <memory>

#include <folly/CancellationToken.h>
#include <folly/Executor.h>
#include <folly/ScopeGuard.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Mutex.h>
#include <folly/experimental/coro/Task.h>
#include <folly/experimental/coro/ViaIfAsync.h>
#include <folly/experimental/coro/WithCancellation.h>
#include <folly/experimental/coro/detail/Barrier.h>
#include <folly/experimental/coro/detail/BarrierTask.h>
#include <folly/experimental/coro/detail/CurrentAsyncFrame.h>
#include <folly/experimental/coro/detail/Helpers.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {
namespace detail {

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
} // namespace detail

template <typename Reference, typename Value>
AsyncGenerator<Reference, Value> merge(
    folly::Executor::KeepAlive<> executor,
    AsyncGenerator<AsyncGenerator<Reference, Value>> sources) {
  struct SharedState {
    explicit SharedState(folly::Executor::KeepAlive<> executor_)
        : executor(std::move(executor_)) {}

    const folly::Executor::KeepAlive<> executor;
    const folly::CancellationSource cancelSource;
    coro::Mutex mutex;
    coro::Baton recordPublished;
    coro::Baton recordConsumed;
    coro::Baton allTasksCompleted;
    detail::CallbackRecord<Reference> record;
  };

  auto makeConsumerTask =
      [](std::shared_ptr<SharedState> state,
         AsyncGenerator<AsyncGenerator<Reference, Value>> sources_)
      -> Task<void> {
    auto makeWorkerTask = [](std::shared_ptr<SharedState> state_,
                             AsyncGenerator<Reference, Value> generator)
        -> detail::DetachedBarrierTask {
      exception_wrapper ex;
      auto cancelToken = state_->cancelSource.getToken();
      try {
        while (auto item = co_await co_viaIfAsync(
                   state_->executor.get_alias(),
                   co_withCancellation(cancelToken, generator.next()))) {
          // We have a new value to emit in the merged stream.
          {
            auto lock = co_await co_viaIfAsync(
                state_->executor.get_alias(), state_->mutex.co_scoped_lock());

            if (cancelToken.isCancellationRequested()) {
              // Consumer has detached and doesn't want any more values.
              // Discard this value.
              break;
            }

            // Publish the value.
            state_->record = detail::CallbackRecord<Reference>{
                detail::callback_record_value, *std::move(item)};
            state_->recordPublished.post();

            // Wait until the consumer is finished with it.
            co_await co_viaIfAsync(
                state_->executor.get_alias(), state_->recordConsumed);
            state_->recordConsumed.reset();

            // Clear the result before releasing the lock.
            state_->record = {};
          }

          if (cancelToken.isCancellationRequested()) {
            break;
          }
        }
      } catch (...) {
        ex = exception_wrapper{std::current_exception()};
      }

      if (ex) {
        state_->cancelSource.requestCancellation();

        auto lock = co_await co_viaIfAsync(
            state_->executor.get_alias(), state_->mutex.co_scoped_lock());
        if (!state_->record.hasError()) {
          state_->record = detail::CallbackRecord<Reference>{
              detail::callback_record_error, std::move(ex)};
          state_->recordPublished.post();
        }
      };
    };

    detail::Barrier barrier{1};

    auto& asyncFrame = co_await detail::co_current_async_stack_frame;

    // Save the initial context and restore it after starting each task
    // as the task may have modified the context before suspending and we
    // want to make sure the next task is started with the same initial
    // context.
    const auto context = RequestContext::saveContext();

    exception_wrapper ex;
    try {
      while (auto item = co_await sources_.next()) {
        if (state->cancelSource.isCancellationRequested()) {
          break;
        }
        makeWorkerTask(state, *std::move(item)).start(&barrier, asyncFrame);
        RequestContext::setContext(context);
      }
    } catch (...) {
      ex = exception_wrapper{std::current_exception()};
    }

    if (ex) {
      state->cancelSource.requestCancellation();

      auto lock = co_await co_viaIfAsync(
          state->executor.get_alias(), state->mutex.co_scoped_lock());
      if (!state->record.hasError()) {
        state->record = detail::CallbackRecord<Reference>{
            detail::callback_record_error, std::move(ex)};
        state->recordPublished.post();
      }
    }

    // Wait for all worker tasks to finish consuming the entirety of their
    // input streams.
    co_await detail::UnsafeResumeInlineSemiAwaitable{barrier.arriveAndWait()};

    // Guaranteed there are no more concurrent producers trying to acquire
    // the mutex here.
    if (!state->record.hasError()) {
      // Stream not yet been terminated with an error.
      // Terminate the stream with the 'end()' signal.
      assert(!state->record.hasValue());
      state->record =
          detail::CallbackRecord<Reference>{detail::callback_record_none};
      state->recordPublished.post();
    }
  };

  auto state = std::make_shared<SharedState>(executor);

  SCOPE_EXIT {
    state->cancelSource.requestCancellation();
    // Make sure we resume the worker thread so that it has a chance to notice
    // that cancellation has been requested.
    state->recordConsumed.post();
  };

  // Start a task that consumes the stream of input streams.
  makeConsumerTask(state, std::move(sources))
      .scheduleOn(executor)
      .start(
          [state](auto&&) { state->allTasksCompleted.post(); },
          state->cancelSource.getToken());

  // Consume values produced by the input streams.
  while (true) {
    if (!state->recordPublished.ready()) {
      folly::CancellationCallback cb{
          co_await co_current_cancellation_token,
          [&] { state->cancelSource.requestCancellation(); }};
      co_await state->recordPublished;
    }
    state->recordPublished.reset();

    if (state->record.hasValue()) {
      // next value
      co_yield std::move(state->record).value();
      state->recordConsumed.post();
    } else {
      // We're closing the output stream. In the spirit of structured
      // concurrency, let's make sure to not leave any background tasks behind.
      co_await state->allTasksCompleted;

      if (state->record.hasError()) {
        std::move(state->record).error().throw_exception();
      } else {
        // none
        assert(state->record.hasNone());
        break;
      }
    }
  }
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
