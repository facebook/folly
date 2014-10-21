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

#include <folly/wangle/Try.h>
#include <folly/wangle/Promise.h>
#include <folly/wangle/Future.h>
#include <folly/wangle/Executor.h>
#include <folly/wangle/detail/FSM.h>

namespace folly { namespace wangle { namespace detail {

// As of GCC 4.8.1, the std::function in libstdc++ optimizes only for pointers
// to functions, using a helper avoids a call to malloc.
template<typename T>
void empty_callback(Try<T>&&) { }

enum class State {
  Waiting,
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

      callback_ = std::move(func);
    };

    bool done = false;
    while (!done) {
      switch (getState()) {
      case State::Waiting:
        done = updateState(State::Waiting, State::Waiting, setCallback_);
        break;

      case State::Done:
        done = updateState(State::Done, State::Done,
                           setCallback_,
                           [&]{ maybeCallback(); });
        break;
      }
    }
  }

  void setResult(Try<T>&& t) {
    bool done = false;
    while (!done) {
      switch (getState()) {
      case State::Waiting:
        done = updateState(State::Waiting, State::Done,
          [&]{ result_ = std::move(t); },
          [&]{ maybeCallback(); });
        break;

      case State::Done:
        throw std::logic_error("setResult called twice");
      }
    }
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
      setResult(Try<T>(std::make_exception_ptr(BrokenPromise())));
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

 private:
  void maybeCallback() {
    assert(ready());
    if (!calledBack_ && isActive() && callback_) {
      // TODO(5306911) we should probably try/catch
      calledBack_ = true;
      Executor* x = executor_;
      if (x) {
        MoveWrapper<std::function<void(Try<T>&&)>> cb(std::move(callback_));
        MoveWrapper<folly::Optional<Try<T>>> val(std::move(result_));
        x->add([cb, val]() mutable { (*cb)(std::move(**val)); });
      } else {
        callback_(std::move(*result_));
      }
    }
  }

  void detachOne() {
    if (++detached_ == 2) {
      // we should have already executed the callback with the value
      assert(calledBack_);
      delete this;
    }
    assert(detached_ == 1 || detached_ == 2);
  }

  folly::Optional<Try<T>> result_;
  std::function<void(Try<T>&&)> callback_;
  std::atomic<bool> calledBack_ {false};
  std::atomic<unsigned char> detached_ {0};
  std::atomic<bool> active_ {true};
  std::atomic<Executor*> executor_ {nullptr};
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
  explicit WhenAllContext() : count(0), total(0) {}
  Promise<std::vector<Try<T> > > p;
  std::vector<Try<T> > results;
  std::atomic<size_t> count;
  size_t total;
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

template <typename T>
struct WhenAllLaterContext {
  explicit WhenAllLaterContext() : count(0), total(0) {}
  std::function<void(std::vector<Try<T>>&&)> fn;
  std::vector<Try<T> > results;
  std::atomic<size_t> count;
  size_t total;
};

}}} // namespace
