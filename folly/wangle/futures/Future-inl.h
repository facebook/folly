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

#include <chrono>
#include <thread>

#include <folly/Baton.h>
#include <folly/wangle/futures/detail/Core.h>
#include <folly/wangle/futures/Timekeeper.h>

namespace folly { namespace wangle {

class Timekeeper;

namespace detail {
  Timekeeper* getTimekeeperSingleton();
}

template <typename T>
struct isFuture {
  static const bool value = false;
};

template <typename T>
struct isFuture<Future<T> > {
  static const bool value = true;
};

template <class T>
Future<T>::Future(Future<T>&& other) noexcept : core_(nullptr) {
  *this = std::move(other);
}

template <class T>
Future<T>& Future<T>::operator=(Future<T>&& other) {
  std::swap(core_, other.core_);
  return *this;
}

template <class T>
Future<T>::~Future() {
  detach();
}

template <class T>
void Future<T>::detach() {
  if (core_) {
    core_->detachFuture();
    core_ = nullptr;
  }
}

template <class T>
void Future<T>::throwIfInvalid() const {
  if (!core_)
    throw NoState();
}

template <class T>
template <class F>
void Future<T>::setCallback_(F&& func) {
  throwIfInvalid();
  core_->setCallback(std::move(func));
}

// Variant: f.then([](Try<T>&& t){ return t.value(); });
template <class T>
template <class F>
typename std::enable_if<
  !isFuture<typename std::result_of<F(Try<T>&&)>::type>::value,
  Future<typename std::result_of<F(Try<T>&&)>::type> >::type
Future<T>::then(F&& func) {
  typedef typename std::result_of<F(Try<T>&&)>::type B;

  throwIfInvalid();

  // wrap these so we can move them into the lambda
  folly::MoveWrapper<Promise<B>> p;
  folly::MoveWrapper<F> funcm(std::forward<F>(func));

  // grab the Future now before we lose our handle on the Promise
  auto f = p->getFuture();

  /* This is a bit tricky.

     We can't just close over *this in case this Future gets moved. So we
     make a new dummy Future. We could figure out something more
     sophisticated that avoids making a new Future object when it can, as an
     optimization. But this is correct.

     core_ can't be moved, it is explicitly disallowed (as is copying). But
     if there's ever a reason to allow it, this is one place that makes that
     assumption and would need to be fixed. We use a standard shared pointer
     for core_ (by copying it in), which means in essence obj holds a shared
     pointer to itself.  But this shouldn't leak because Promise will not
     outlive the continuation, because Promise will setException() with a
     broken Promise if it is destructed before completed. We could use a
     weak pointer but it would have to be converted to a shared pointer when
     func is executed (because the Future returned by func may possibly
     persist beyond the callback, if it gets moved), and so it is an
     optimization to just make it shared from the get-go.

     We have to move in the Promise and func using the MoveWrapper
     hack. (func could be copied but it's a big drag on perf).

     Two subtle but important points about this design. detail::Core has no
     back pointers to Future or Promise, so if Future or Promise get moved
     (and they will be moved in performant code) we don't have to do
     anything fancy. And because we store the continuation in the
     detail::Core, not in the Future, we can execute the continuation even
     after the Future has gone out of scope. This is an intentional design
     decision. It is likely we will want to be able to cancel a continuation
     in some circumstances, but I think it should be explicit not implicit
     in the destruction of the Future used to create it.
     */
  setCallback_(
    [p, funcm](Try<T>&& t) mutable {
      p->fulfil([&]() {
          return (*funcm)(std::move(t));
        });
    });

  return std::move(f);
}

// Variant: f.then([](T&& t){ return t; });
template <class T>
template <class F>
typename std::enable_if<
  !std::is_same<T, void>::value &&
  !isFuture<typename std::result_of<
    F(typename detail::AliasIfVoid<T>::type&&)>::type>::value,
  Future<typename std::result_of<
    F(typename detail::AliasIfVoid<T>::type&&)>::type> >::type
Future<T>::then(F&& func) {
  typedef typename std::result_of<F(T&&)>::type B;

  throwIfInvalid();

  folly::MoveWrapper<Promise<B>> p;
  folly::MoveWrapper<F> funcm(std::forward<F>(func));
  auto f = p->getFuture();

  setCallback_(
    [p, funcm](Try<T>&& t) mutable {
      if (t.hasException()) {
        p->setException(t.getException());
      } else {
        p->fulfil([&]() {
          return (*funcm)(std::move(t.value()));
        });
      }
    });

  return std::move(f);
}

// Variant: f.then([](){ return; });
template <class T>
template <class F>
typename std::enable_if<
  std::is_same<T, void>::value &&
  !isFuture<typename std::result_of<F()>::type>::value,
  Future<typename std::result_of<F()>::type> >::type
Future<T>::then(F&& func) {
  typedef typename std::result_of<F()>::type B;

  throwIfInvalid();

  folly::MoveWrapper<Promise<B>> p;
  folly::MoveWrapper<F> funcm(std::forward<F>(func));
  auto f = p->getFuture();

  setCallback_(
    [p, funcm](Try<T>&& t) mutable {
      if (t.hasException()) {
        p->setException(t.getException());
      } else {
        p->fulfil([&]() {
          return (*funcm)();
        });
      }
    });

  return std::move(f);
}

// Variant: f.then([](Try<T>&& t){ return makeFuture<T>(t.value()); });
template <class T>
template <class F>
typename std::enable_if<
  isFuture<typename std::result_of<F(Try<T>&&)>::type>::value,
  Future<typename std::result_of<F(Try<T>&&)>::type::value_type> >::type
Future<T>::then(F&& func) {
  typedef typename std::result_of<F(Try<T>&&)>::type::value_type B;

  throwIfInvalid();

  // wrap these so we can move them into the lambda
  folly::MoveWrapper<Promise<B>> p;
  folly::MoveWrapper<F> funcm(std::forward<F>(func));

  // grab the Future now before we lose our handle on the Promise
  auto f = p->getFuture();

  setCallback_(
    [p, funcm](Try<T>&& t) mutable {
      try {
        auto f2 = (*funcm)(std::move(t));
        // that didn't throw, now we can steal p
        f2.setCallback_([p](Try<B>&& b) mutable {
            p->fulfilTry(std::move(b));
          });
      } catch (...) {
        p->setException(std::current_exception());
      }
    });

  return std::move(f);
}

// Variant: f.then([](T&& t){ return makeFuture<T>(t); });
template <class T>
template <class F>
typename std::enable_if<
  !std::is_same<T, void>::value &&
  isFuture<typename std::result_of<
    F(typename detail::AliasIfVoid<T>::type&&)>::type>::value,
  Future<typename std::result_of<
    F(typename detail::AliasIfVoid<T>::type&&)>::type::value_type> >::type
Future<T>::then(F&& func) {
  typedef typename std::result_of<F(T&&)>::type::value_type B;

  throwIfInvalid();

  folly::MoveWrapper<Promise<B>> p;
  folly::MoveWrapper<F> funcm(std::forward<F>(func));
  auto f = p->getFuture();

  setCallback_(
    [p, funcm](Try<T>&& t) mutable {
      if (t.hasException()) {
        p->setException(t.getException());
      } else {
        try {
          auto f2 = (*funcm)(std::move(t.value()));
          f2.setCallback_([p](Try<B>&& b) mutable {
              p->fulfilTry(std::move(b));
            });
        } catch (...) {
          p->setException(std::current_exception());
        }
      }
    });

  return std::move(f);
}

// Variant: f.then([](){ return makeFuture(); });
template <class T>
template <class F>
typename std::enable_if<
  std::is_same<T, void>::value &&
  isFuture<typename std::result_of<F()>::type>::value,
  Future<typename std::result_of<F()>::type::value_type> >::type
Future<T>::then(F&& func) {
  typedef typename std::result_of<F()>::type::value_type B;

  throwIfInvalid();

  folly::MoveWrapper<Promise<B>> p;
  folly::MoveWrapper<F> funcm(std::forward<F>(func));

  auto f = p->getFuture();

  setCallback_(
    [p, funcm](Try<T>&& t) mutable {
      if (t.hasException()) {
        p->setException(t.getException());
      } else {
        try {
          auto f2 = (*funcm)();
          f2.setCallback_([p](Try<B>&& b) mutable {
              p->fulfilTry(std::move(b));
            });
        } catch (...) {
          p->setException(std::current_exception());
        }
      }
    });

  return std::move(f);
}

template <class T>
Future<void> Future<T>::then() {
  return then([] (Try<T>&& t) {});
}

// onError where the callback returns T
template <class T>
template <class F>
typename std::enable_if<
  !detail::Extract<F>::ReturnsFuture::value,
  Future<T>>::type
Future<T>::onError(F&& func) {
  typedef typename detail::Extract<F>::FirstArg Exn;
  static_assert(
      std::is_same<typename detail::Extract<F>::RawReturn, T>::value,
      "Return type of onError callback must be T or Future<T>");

  Promise<T> p;
  auto f = p.getFuture();
  auto pm = folly::makeMoveWrapper(std::move(p));
  auto funcm = folly::makeMoveWrapper(std::move(func));
  setCallback_([pm, funcm](Try<T>&& t) mutable {
    try {
      t.throwIfFailed();
    } catch (Exn& e) {
      pm->fulfil([&]{
        return (*funcm)(e);
      });
      return;
    } catch (...) {
      // fall through
    }
    pm->fulfilTry(std::move(t));
  });

  return f;
}

// onError where the callback returns Future<T>
template <class T>
template <class F>
typename std::enable_if<
  detail::Extract<F>::ReturnsFuture::value,
  Future<T>>::type
Future<T>::onError(F&& func) {
  static_assert(
      std::is_same<typename detail::Extract<F>::Return, Future<T>>::value,
      "Return type of onError callback must be T or Future<T>");
  typedef typename detail::Extract<F>::FirstArg Exn;

  Promise<T> p;
  auto f = p.getFuture();
  auto pm = folly::makeMoveWrapper(std::move(p));
  auto funcm = folly::makeMoveWrapper(std::move(func));
  setCallback_([pm, funcm](Try<T>&& t) mutable {
    try {
      t.throwIfFailed();
    } catch (Exn& e) {
      try {
        auto f2 = (*funcm)(e);
        f2.setCallback_([pm](Try<T>&& t2) mutable {
          pm->fulfilTry(std::move(t2));
        });
      } catch (...) {
        pm->setException(std::current_exception());
      }
      return;
    } catch (...) {
      // fall through
    }
    pm->fulfilTry(std::move(t));
  });

  return f;
}

template <class T>
typename std::add_lvalue_reference<T>::type Future<T>::value() {
  throwIfInvalid();

  return core_->getTry().value();
}

template <class T>
typename std::add_lvalue_reference<const T>::type Future<T>::value() const {
  throwIfInvalid();

  return core_->getTry().value();
}

template <class T>
Try<T>& Future<T>::getTry() {
  throwIfInvalid();

  return core_->getTry();
}

template <class T>
template <typename Executor>
inline Future<T> Future<T>::via(Executor* executor) && {
  throwIfInvalid();

  this->deactivate();
  core_->setExecutor(executor);

  return std::move(*this);
}

template <class T>
template <typename Executor>
inline Future<T> Future<T>::via(Executor* executor) & {
  throwIfInvalid();

  MoveWrapper<Promise<T>> p;
  auto f = p->getFuture();
  then([p](Try<T>&& t) mutable { p->fulfilTry(std::move(t)); });
  return std::move(f).via(executor);
}

template <class T>
bool Future<T>::isReady() const {
  throwIfInvalid();
  return core_->ready();
}

template <class T>
void Future<T>::raise(std::exception_ptr exception) {
  core_->raise(exception);
}

// makeFuture

template <class T>
Future<typename std::decay<T>::type> makeFuture(T&& t) {
  Promise<typename std::decay<T>::type> p;
  auto f = p.getFuture();
  p.setValue(std::forward<T>(t));
  return std::move(f);
}

inline // for multiple translation units
Future<void> makeFuture() {
  Promise<void> p;
  auto f = p.getFuture();
  p.setValue();
  return std::move(f);
}

template <class F>
auto makeFutureTry(
    F&& func,
    typename std::enable_if<!std::is_reference<F>::value, bool>::type sdf)
    -> Future<decltype(func())> {
  Promise<decltype(func())> p;
  auto f = p.getFuture();
  p.fulfil(
    [&func]() {
      return (func)();
    });
  return std::move(f);
}

template <class F>
auto makeFutureTry(F const& func) -> Future<decltype(func())> {
  F copy = func;
  return makeFutureTry(std::move(copy));
}

template <class T>
Future<T> makeFuture(std::exception_ptr const& e) {
  Promise<T> p;
  auto f = p.getFuture();
  p.setException(e);
  return std::move(f);
}

template <class T, class E>
typename std::enable_if<std::is_base_of<std::exception, E>::value,
                        Future<T>>::type
makeFuture(E const& e) {
  Promise<T> p;
  auto f = p.getFuture();
  p.fulfil([&]() -> T { throw e; });
  return std::move(f);
}

template <class T>
Future<T> makeFuture(Try<T>&& t) {
  try {
    return makeFuture<T>(std::move(t.value()));
  } catch (...) {
    return makeFuture<T>(std::current_exception());
  }
}

template <>
inline Future<void> makeFuture(Try<void>&& t) {
  try {
    t.throwIfFailed();
    return makeFuture();
  } catch (...) {
    return makeFuture<void>(std::current_exception());
  }
}

// via
template <typename Executor>
Future<void> via(Executor* executor) {
  return makeFuture().via(executor);
}

// when (variadic)

template <typename... Fs>
typename detail::VariadicContext<
  typename std::decay<Fs>::type::value_type...>::type
whenAll(Fs&&... fs)
{
  auto ctx =
    new detail::VariadicContext<typename std::decay<Fs>::type::value_type...>();
  ctx->total = sizeof...(fs);
  auto f_saved = ctx->p.getFuture();
  detail::whenAllVariadicHelper(ctx,
    std::forward<typename std::decay<Fs>::type>(fs)...);
  return std::move(f_saved);
}

// when (iterator)

template <class InputIterator>
Future<
  std::vector<
  Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>
whenAll(InputIterator first, InputIterator last)
{
  typedef
    typename std::iterator_traits<InputIterator>::value_type::value_type T;

  auto n = std::distance(first, last);
  if (n == 0) {
    return makeFuture(std::vector<Try<T>>());
  }

  auto ctx = new detail::WhenAllContext<T>();

  ctx->results.resize(n);

  auto f_saved = ctx->p.getFuture();

  for (size_t i = 0; first != last; ++first, ++i) {
     assert(i < n);
     auto& f = *first;
     f.setCallback_([ctx, i, n](Try<T>&& t) {
         ctx->results[i] = std::move(t);
         if (++ctx->count == n) {
           ctx->p.setValue(std::move(ctx->results));
           delete ctx;
         }
       });
  }

  return std::move(f_saved);
}

template <class InputIterator>
Future<
  std::pair<size_t,
            Try<
              typename
              std::iterator_traits<InputIterator>::value_type::value_type> > >
whenAny(InputIterator first, InputIterator last) {
  typedef
    typename std::iterator_traits<InputIterator>::value_type::value_type T;

  auto ctx = new detail::WhenAnyContext<T>(std::distance(first, last));
  auto f_saved = ctx->p.getFuture();

  for (size_t i = 0; first != last; first++, i++) {
    auto& f = *first;
    f.setCallback_([i, ctx](Try<T>&& t) {
      if (!ctx->done.exchange(true)) {
        ctx->p.setValue(std::make_pair(i, std::move(t)));
      }
      ctx->decref();
    });
  }

  return std::move(f_saved);
}

template <class InputIterator>
Future<std::vector<std::pair<size_t, Try<typename
  std::iterator_traits<InputIterator>::value_type::value_type>>>>
whenN(InputIterator first, InputIterator last, size_t n) {
  typedef typename
    std::iterator_traits<InputIterator>::value_type::value_type T;
  typedef std::vector<std::pair<size_t, Try<T>>> V;

  struct ctx_t {
    V v;
    size_t completed;
    Promise<V> p;
  };
  auto ctx = std::make_shared<ctx_t>();
  ctx->completed = 0;

  // for each completed Future, increase count and add to vector, until we
  // have n completed futures at which point we fulfil our Promise with the
  // vector
  auto it = first;
  size_t i = 0;
  while (it != last) {
    it->then([ctx, n, i](Try<T>&& t) {
      auto& v = ctx->v;
      auto c = ++ctx->completed;
      if (c <= n) {
        assert(ctx->v.size() < n);
        v.push_back(std::make_pair(i, std::move(t)));
        if (c == n) {
          ctx->p.fulfilTry(Try<V>(std::move(v)));
        }
      }
    });

    it++;
    i++;
  }

  if (i < n) {
    ctx->p.setException(std::runtime_error("Not enough futures"));
  }

  return ctx->p.getFuture();
}

template <typename T>
Future<T>
waitWithSemaphore(Future<T>&& f) {
  Baton<> baton;
  auto done = f.then([&](Try<T> &&t) {
    baton.post();
    return std::move(t.value());
  });
  baton.wait();
  while (!done.isReady()) {
    // There's a race here between the return here and the actual finishing of
    // the future. f is completed, but the setup may not have finished on done
    // after the baton has posted.
    std::this_thread::yield();
  }
  return done;
}

template<>
inline Future<void> waitWithSemaphore<void>(Future<void>&& f) {
  Baton<> baton;
  auto done = f.then([&](Try<void> &&t) {
    baton.post();
    t.value();
  });
  baton.wait();
  while (!done.isReady()) {
    // There's a race here between the return here and the actual finishing of
    // the future. f is completed, but the setup may not have finished on done
    // after the baton has posted.
    std::this_thread::yield();
  }
  return done;
}

template <typename T, class Dur>
Future<T>
waitWithSemaphore(Future<T>&& f, Dur timeout) {
  auto baton = std::make_shared<Baton<>>();
  auto done = f.then([baton](Try<T> &&t) {
    baton->post();
    return std::move(t.value());
  });
  baton->timed_wait(std::chrono::system_clock::now() + timeout);
  return done;
}

template <class Dur>
Future<void>
waitWithSemaphore(Future<void>&& f, Dur timeout) {
  auto baton = std::make_shared<Baton<>>();
  auto done = f.then([baton](Try<void> &&t) {
    baton->post();
    t.value();
  });
  baton->timed_wait(std::chrono::system_clock::now() + timeout);
  return done;
}

namespace {
  template <class T>
  void getWaitHelper(Future<T>* f) {
    // If we already have a value do the cheap thing
    if (f->isReady()) {
      return;
    }

    folly::Baton<> baton;
    f->then([&](Try<T> const&) {
      baton.post();
    });
    baton.wait();
  }

  template <class T>
  Future<T> getWaitTimeoutHelper(Future<T>* f, Duration dur) {
    // TODO make and use variadic whenAny #5877971
    Promise<T> p;
    auto token = std::make_shared<std::atomic<bool>>();
    folly::Baton<> baton;

    folly::wangle::detail::getTimekeeperSingleton()->after(dur)
      .then([&,token](Try<void> const& t) {
        if (token->exchange(true) == false) {
          try {
            t.value();
            p.setException(TimedOut());
          } catch (std::exception const& e) {
            p.setException(std::current_exception());
          }
          baton.post();
        }
      });

    f->then([&, token](Try<T>&& t) {
      if (token->exchange(true) == false) {
        p.fulfilTry(std::move(t));
        baton.post();
      }
    });

    baton.wait();
    return p.getFuture();
  }
}

template <class T>
T Future<T>::get() {
  getWaitHelper(this);

  // Big assumption here: the then() call above, since it doesn't move out
  // the value, leaves us with a value to return here. This would be a big
  // no-no in user code, but I'm invoking internal developer privilege. This
  // is slightly more efficient (save a move()) especially if there's an
  // exception (save a throw).
  return std::move(value());
}

template <>
inline void Future<void>::get() {
  getWaitHelper(this);
  value();
}

template <class T>
T Future<T>::get(Duration dur) {
  return std::move(getWaitTimeoutHelper(this, dur).value());
}

template <>
inline void Future<void>::get(Duration dur) {
  getWaitTimeoutHelper(this, dur).value();
}

template <class T>
Future<T> Future<T>::within(Duration dur, Timekeeper* tk) {
  return within(dur, TimedOut(), tk);
}

template <class T>
template <class E>
Future<T> Future<T>::within(Duration dur, E e, Timekeeper* tk) {

  struct Context {
    Context(E ex) : exception(std::move(ex)), promise(), token(false) {}
    E exception;
    Promise<T> promise;
    std::atomic<bool> token;
  };
  auto ctx = std::make_shared<Context>(std::move(e));

  if (!tk) {
    tk = folly::wangle::detail::getTimekeeperSingleton();
  }

  tk->after(dur)
    .then([ctx](Try<void> const& t) {
      if (ctx->token.exchange(true) == false) {
        try {
          t.throwIfFailed();
          ctx->promise.setException(std::move(ctx->exception));
        } catch (std::exception const&) {
          ctx->promise.setException(std::current_exception());
        }
      }
    });

  this->then([ctx](Try<T>&& t) {
    if (ctx->token.exchange(true) == false) {
      ctx->promise.fulfilTry(std::move(t));
    }
  });

  return ctx->promise.getFuture();
}

template <class T>
Future<T> Future<T>::delayed(Duration dur, Timekeeper* tk) {
  return whenAll(*this, futures::sleep(dur, tk))
    .then([](Try<std::tuple<Try<T>, Try<void>>>&& tup) {
      Try<T>& t = std::get<0>(tup.value());
      return makeFuture<T>(std::move(t));
    });
}

}}

// I haven't included a Future<T&> specialization because I don't forsee us
// using it, however it is not difficult to add when needed. Refer to
// Future<void> for guidance. std::future and boost::future code would also be
// instructive.
