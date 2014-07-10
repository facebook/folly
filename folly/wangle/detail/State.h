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

#include <folly/wangle/Try.h>
#include <folly/wangle/Promise.h>
#include <folly/wangle/Future.h>

namespace folly { namespace wangle { namespace detail {

/** The shared state object for Future and Promise. */
template<typename T>
class State {
 public:
  // This must be heap-constructed. There's probably a way to enforce that in
  // code but since this is just internal detail code and I don't know how
  // off-hand, I'm punting.
  State() = default;
  ~State() {
    assert(calledBack_);
    assert(detached_ == 2);
  }

  // not copyable
  State(State const&) = delete;
  State& operator=(State const&) = delete;

  // not movable (see comment in the implementation of Future::then)
  State(State&&) noexcept = delete;
  State& operator=(State&&) = delete;

  Try<T>& getTry() {
    return *value_;
  }

  template <typename F>
  void setCallback(F func) {
    {
      std::lock_guard<decltype(mutex_)> lock(mutex_);

      if (callback_) {
        throw std::logic_error("setCallback called twice");
      }

      callback_ = std::move(func);
    }

    maybeCallback();
  }

  void fulfil(Try<T>&& t) {
    {
      std::lock_guard<decltype(mutex_)> lock(mutex_);

      if (ready()) {
        throw std::logic_error("fulfil called twice");
      }

      value_ = std::move(t);
      assert(ready());
    }

    maybeCallback();
  }

  void setException(std::exception_ptr const& e) {
    fulfil(Try<T>(e));
  }

  template <class E> void setException(E const& e) {
    fulfil(Try<T>(std::make_exception_ptr<E>(e)));
  }

  bool ready() const {
    return value_.hasValue();
  }

  typename std::add_lvalue_reference<T>::type value() {
    if (ready()) {
      return value_->value();
    } else {
      throw FutureNotReady();
    }
  }

  // Called by a destructing Future
  void detachFuture() {
    if (!callback_) {
      setCallback([](Try<T>&&) {});
    }
    activate();
    detachOne();
  }

  // Called by a destructing Promise
  void detachPromise() {
    if (!ready()) {
      setException(BrokenPromise());
    }
    detachOne();
  }

  void deactivate() {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    active_ = false;
  }

  void activate() {
    {
      std::lock_guard<decltype(mutex_)> lock(mutex_);
      active_ = true;
    }
    maybeCallback();
  }

 private:
  void maybeCallback() {
    std::lock_guard<decltype(mutex_)> lock(mutex_);
    if (!calledBack_ &&
        value_ && callback_ && active_) {
      // TODO we should probably try/catch here
      callback_(std::move(*value_));
      calledBack_ = true;
    }
  }

  void detachOne() {
    bool shouldDelete;
    {
      std::lock_guard<decltype(mutex_)> lock(mutex_);
      detached_++;
      assert(detached_ == 1 || detached_ == 2);
      shouldDelete = (detached_ == 2);
    }

    if (shouldDelete) {
      // we should have already executed the callback with the value
      assert(calledBack_);
      delete this;
    }
  }

  folly::Optional<Try<T>> value_;
  std::function<void(Try<T>&&)> callback_;
  bool calledBack_ = false;
  unsigned char detached_ = 0;
  bool active_ = true;

  // this lock isn't meant to protect all accesses to members, only the ones
  // that need to be threadsafe: the act of setting value_ and callback_, and
  // seeing if they are set and whether we should then continue.
  std::recursive_mutex mutex_;
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

}}} // namespace
