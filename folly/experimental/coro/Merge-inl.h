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

#include <exception>
#include <memory>

#include <folly/CancellationToken.h>
#include <folly/Executor.h>
#include <folly/ScopeGuard.h>
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Materialize.h>
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
    CallbackRecord<Reference> record;
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
            state_->record = CallbackRecord<Reference>{
                callback_record_value, *std::move(item)};
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
      } catch (const std::exception& e) {
        ex = exception_wrapper{std::current_exception(), e};
      } catch (...) {
        ex = exception_wrapper{std::current_exception()};
      }

      if (ex) {
        state_->cancelSource.requestCancellation();

        auto lock = co_await co_viaIfAsync(
            state_->executor.get_alias(), state_->mutex.co_scoped_lock());
        if (!state_->record.hasError()) {
          state_->record =
              CallbackRecord<Reference>{callback_record_error, std::move(ex)};
          state_->recordPublished.post();
        }
      };
    };

    detail::Barrier barrier{1};

    auto& asyncFrame = co_await detail::co_current_async_stack_frame;

    exception_wrapper ex;
    try {
      while (auto item = co_await sources_.next()) {
        if (state->cancelSource.isCancellationRequested()) {
          break;
        }
        makeWorkerTask(state, *std::move(item)).start(&barrier, asyncFrame);
      }
    } catch (const std::exception& e) {
      ex = exception_wrapper{std::current_exception(), e};
    } catch (...) {
      ex = exception_wrapper{std::current_exception()};
    }

    if (ex) {
      state->cancelSource.requestCancellation();

      auto lock = co_await co_viaIfAsync(
          state->executor.get_alias(), state->mutex.co_scoped_lock());
      if (!state->record.hasError()) {
        state->record =
            CallbackRecord<Reference>{callback_record_error, std::move(ex)};
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
      state->record = CallbackRecord<Reference>{callback_record_none};
      state->recordPublished.post();
    }
  };

  auto state = std::make_shared<SharedState>(executor);

  SCOPE_EXIT { state->cancelSource.requestCancellation(); };

  // Start a task that consumes the stream of input streams.
  makeConsumerTask(state, std::move(sources))
      .scheduleOn(executor)
      .start([](auto&&) {}, state->cancelSource.getToken());

  // Consume values produced by the input streams.
  while (true) {
    if (!state->recordPublished.ready()) {
      folly::CancellationCallback cb{
          co_await co_current_cancellation_token,
          [&] { state->cancelSource.requestCancellation(); }};
      co_await state->recordPublished;
    }
    state->recordPublished.reset();

    SCOPE_EXIT { state->recordConsumed.post(); };

    if (state->record.hasValue()) {
      // next value
      co_yield std::move(state->record).value();
    } else if (state->record.hasError()) {
      std::move(state->record).error().throw_exception();
    } else {
      // none
      assert(state->record.hasNone());
      break;
    }
  }
}

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
