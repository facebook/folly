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
#include <folly/Optional.h>
#include <stdexcept>
#include <vector>

#include "Try.h"
#include "Promise.h"
#include "Future.h"

namespace folly { namespace wangle { namespace detail {

/** The shared state object for Future and Promise. */
template<typename T>
class FutureObject {
 public:
  FutureObject() = default;

  // not copyable
  FutureObject(FutureObject const&) = delete;
  FutureObject& operator=(FutureObject const&) = delete;

  // not movable (see comment in the implementation of Future::then)
  FutureObject(FutureObject&&) noexcept = delete;
  FutureObject& operator=(FutureObject&&) = delete;

  Try<T>& getTry() {
    return *value_;
  }

  template <typename F>
  void setCallback_(F func) {
    if (continuation_) {
      throw std::logic_error("setCallback_ called twice");
    }

    continuation_ = std::move(func);

    if (shouldContinue_.test_and_set()) {
      continuation_(std::move(*value_));
      delete this;
    }
  }

  void fulfil(Try<T>&& t) {
    if (value_.hasValue()) {
      throw std::logic_error("fulfil called twice");
    }

    value_ = std::move(t);

    if (shouldContinue_.test_and_set()) {
      continuation_(std::move(*value_));
      delete this;
    }
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

 private:
  std::atomic_flag shouldContinue_ = ATOMIC_FLAG_INIT;
  folly::Optional<Try<T>> value_;
  std::function<void(Try<T>&&)> continuation_;
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
