/*
 * Copyright 2014 Facebook, Inc.
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

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <folly/Optional.h>
#include <folly/SmallLocks.h>

#include <folly/futures/Try.h>
#include <folly/futures/Promise.h>
#include <folly/futures/Future.h>
#include <folly/Executor.h>
#include <folly/futures/detail/FSM.h>

#include <folly/io/async/Request.h>

namespace folly { namespace detail {

// As of GCC 4.8.1, the std::function in libstdc++ optimizes only for pointers
// to functions, using a helper avoids a call to malloc.
template<typename T>
void empty_callback(Try<T>&&) { }

enum class State {
  Waiting,
  Interruptible,
  Interrupted,
  Done,
};

/** The shared state object for Future and Promise. */
template<typename T>
class Core : protected FSM<State> {
 public:
  // This must be heap-constructed. There's probably a way to enforce that in
  // code but since this is just internal detail code and I don't know how
  // off-hand, I'm punting.
  Core() : FSM<State>(State::Waiting) {}
  ~Core() {
    assert(calledBack_);
    assert(detached_ == 2);
  }

  // not copyable
  Core(Core const&) = delete;
  Core& operator=(Core const&) = delete;

  // not movable (see comment in the implementation of Future::then)
  Core(Core&&) noexcept = delete;
  Core& operator=(Core&&) = delete;

  Try<T>& getTry() {
    if (ready()) {
      return *result_;
    } else {
      throw FutureNotReady();
    }
  }

  template <typename F>
  void setCallback(F func) {
    auto setCallback_ = [&]{
      if (callback_) {
        throw std::logic_error("setCallback called twice");
      }

      context_ = RequestContext::saveContext();
      callback_ = std::move(func);
    };

    FSM_START
      case State::Waiting:
      case State::Interruptible:
      case State::Interrupted:
        FSM_UPDATE(state, setCallback_);
        break;

      case State::Done:
        FSM_UPDATE2(State::Done,
          setCallback_,
          [&]{ maybeCallback(); });
        break;
    FSM_END
  }

  void setResult(Try<T>&& t) {
    FSM_START
      case State::Waiting:
      case State::Interruptible:
      case State::Interrupted:
        FSM_UPDATE2(State::Done,
          [&]{ result_ = std::move(t); },
          [&]{ maybeCallback(); });
        break;

      case State::Done:
        throw std::logic_error("setResult called twice");
    FSM_END
  }

  bool ready() const {
    return getState() == State::Done;
  }

  // Called by a destructing Future
  void detachFuture() {
    if (!callback_) {
      setCallback(empty_callback<T>);
    }
    activate();
    detachOne();
  }

  // Called by a destructing Promise
  void detachPromise() {
    if (!ready()) {
      setResult(Try<T>(exception_wrapper(BrokenPromise())));
    }
    detachOne();
  }

  void deactivate() {
    active_ = false;
  }

  void activate() {
    active_ = true;
    if (ready()) {
      maybeCallback();
    }
  }

  bool isActive() { return active_; }

  void setExecutor(Executor* x) {
    executor_ = x;
  }

  void raise(exception_wrapper const& e) {
    FSM_START
      case State::Interruptible:
        FSM_UPDATE2(State::Interrupted,
          [&]{ interrupt_ = folly::make_unique<exception_wrapper>(e); },
          [&]{ interruptHandler_(*interrupt_); });
        break;

      case State::Waiting:
      case State::Interrupted:
        FSM_UPDATE(State::Interrupted,
          [&]{ interrupt_ = folly::make_unique<exception_wrapper>(e); });
        break;

      case State::Done:
        FSM_BREAK
    FSM_END
  }

  void setInterruptHandler(std::function<void(exception_wrapper const&)> fn) {
    FSM_START
      case State::Waiting:
      case State::Interruptible:
        FSM_UPDATE(State::Interruptible,
          [&]{ interruptHandler_ = std::move(fn); });
        break;

      case State::Interrupted:
        fn(*interrupt_);
        FSM_BREAK

      case State::Done:
        FSM_BREAK
    FSM_END
  }

 private:
  void maybeCallback() {
    assert(ready());
    if (isActive() && callback_) {
      if (!calledBack_.exchange(true)) {
        // TODO(5306911) we should probably try/catch
        Executor* x = executor_;

        RequestContext::setContext(context_);
        if (x) {
          MoveWrapper<std::function<void(Try<T>&&)>> cb(std::move(callback_));
          MoveWrapper<folly::Optional<Try<T>>> val(std::move(result_));
          x->add([cb, val]() mutable { (*cb)(std::move(**val)); });
        } else {
          callback_(std::move(*result_));
        }
      }
    }
  }

  void detachOne() {
    auto d = ++detached_;
    assert(d >= 1);
    assert(d <= 2);
    if (d == 2) {
      // we should have already executed the callback with the value
      assert(calledBack_);
      delete this;
    }
  }

  folly::Optional<Try<T>> result_;
  std::function<void(Try<T>&&)> callback_;
  std::shared_ptr<RequestContext> context_{nullptr};
  std::atomic<bool> calledBack_ {false};
  std::atomic<unsigned char> detached_ {0};
  std::atomic<bool> active_ {true};
  std::atomic<Executor*> executor_ {nullptr};
  std::unique_ptr<exception_wrapper> interrupt_;
  std::function<void(exception_wrapper const&)> interruptHandler_;
};

template <typename... Ts>
struct VariadicContext {
  VariadicContext() : total(0), count(0) {}
  Promise<std::tuple<Try<Ts>... > > p;
  std::tuple<Try<Ts>... > results;
  size_t total;
  std::atomic<size_t> count;
  typedef Future<std::tuple<Try<Ts>...>> type;
};

template <typename... Ts, typename THead, typename... Fs>
typename std::enable_if<sizeof...(Fs) == 0, void>::type
whenAllVariadicHelper(VariadicContext<Ts...> *ctx, THead&& head, Fs&&... tail) {
  head.setCallback_([ctx](Try<typename THead::value_type>&& t) {
    std::get<sizeof...(Ts) - sizeof...(Fs) - 1>(ctx->results) = std::move(t);
    if (++ctx->count == ctx->total) {
      ctx->p.setValue(std::move(ctx->results));
      delete ctx;
    }
  });
}

template <typename... Ts, typename THead, typename... Fs>
typename std::enable_if<sizeof...(Fs) != 0, void>::type
whenAllVariadicHelper(VariadicContext<Ts...> *ctx, THead&& head, Fs&&... tail) {
  head.setCallback_([ctx](Try<typename THead::value_type>&& t) {
    std::get<sizeof...(Ts) - sizeof...(Fs) - 1>(ctx->results) = std::move(t);
    if (++ctx->count == ctx->total) {
      ctx->p.setValue(std::move(ctx->results));
      delete ctx;
    }
  });
  // template tail-recursion
  whenAllVariadicHelper(ctx, std::forward<Fs>(tail)...);
}

template <typename T>
struct WhenAllContext {
  WhenAllContext() : count(0) {}
  Promise<std::vector<Try<T> > > p;
  std::vector<Try<T> > results;
  std::atomic<size_t> count;
};

template <typename T>
struct WhenAnyContext {
  explicit WhenAnyContext(size_t n) : done(false), ref_count(n) {};
  Promise<std::pair<size_t, Try<T>>> p;
  std::atomic<bool> done;
  std::atomic<size_t> ref_count;
  void decref() {
    if (--ref_count == 0) {
      delete this;
    }
  }
};

}} // folly::detail
