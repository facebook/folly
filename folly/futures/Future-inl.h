/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/experimental/fibers/Baton.h>
#include <folly/Optional.h>
#include <folly/futures/detail/Core.h>
#include <folly/futures/Timekeeper.h>

namespace folly {

class Timekeeper;

namespace detail {
  Timekeeper* getTimekeeperSingleton();
}

template <class T>
Future<T>::Future(Future<T>&& other) noexcept : core_(other.core_) {
  other.core_ = nullptr;
}

template <class T>
Future<T>& Future<T>::operator=(Future<T>&& other) noexcept {
  std::swap(core_, other.core_);
  return *this;
}

template <class T>
template <class T2>
Future<T>::Future(T2&& val) : core_(nullptr) {
  Promise<T> p;
  p.setValue(std::forward<T2>(val));
  *this = p.getFuture();
}

template <>
template <class F,
          typename std::enable_if<std::is_void<F>::value, int>::type>
Future<void>::Future() : core_(nullptr) {
  Promise<void> p;
  p.setValue();
  *this = p.getFuture();
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

// unwrap

template <class T>
template <class F>
typename std::enable_if<isFuture<F>::value,
                        Future<typename isFuture<T>::Inner>>::type
Future<T>::unwrap() {
  return then([](Future<typename isFuture<T>::Inner> internal_future) {
      return internal_future;
  });
}

// then

// Variant: returns a value
// e.g. f.then([](Try<T>&& t){ return t.value(); });
template <class T>
template <typename F, typename R, bool isTry, typename... Args>
typename std::enable_if<!R::ReturnsFuture::value, typename R::Return>::type
Future<T>::thenImplementation(F func, detail::argResult<isTry, F, Args...>) {
  static_assert(sizeof...(Args) <= 1, "Then must take zero/one argument");
  typedef typename R::ReturnsFuture::Inner B;

  throwIfInvalid();

  // wrap these so we can move them into the lambda
  folly::MoveWrapper<Promise<B>> p;
  folly::MoveWrapper<F> funcm(std::forward<F>(func));

  // grab the Future now before we lose our handle on the Promise
  auto f = p->getFuture();
  if (getExecutor()) {
    f.setExecutor(getExecutor());
  }

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
      if (!isTry && t.hasException()) {
        p->setException(std::move(t.exception()));
      } else {
        p->setWith([&]() {
          return (*funcm)(t.template get<isTry, Args>()...);
        });
      }
    });

  return f;
}

// Variant: returns a Future
// e.g. f.then([](T&& t){ return makeFuture<T>(t); });
template <class T>
template <typename F, typename R, bool isTry, typename... Args>
typename std::enable_if<R::ReturnsFuture::value, typename R::Return>::type
Future<T>::thenImplementation(F func, detail::argResult<isTry, F, Args...>) {
  static_assert(sizeof...(Args) <= 1, "Then must take zero/one argument");
  typedef typename R::ReturnsFuture::Inner B;

  throwIfInvalid();

  // wrap these so we can move them into the lambda
  folly::MoveWrapper<Promise<B>> p;
  folly::MoveWrapper<F> funcm(std::forward<F>(func));

  // grab the Future now before we lose our handle on the Promise
  auto f = p->getFuture();
  if (getExecutor()) {
    f.setExecutor(getExecutor());
  }

  setCallback_(
    [p, funcm](Try<T>&& t) mutable {
      if (!isTry && t.hasException()) {
        p->setException(std::move(t.exception()));
      } else {
        try {
          auto f2 = (*funcm)(t.template get<isTry, Args>()...);
          // that didn't throw, now we can steal p
          f2.setCallback_([p](Try<B>&& b) mutable {
            p->setTry(std::move(b));
          });
        } catch (const std::exception& e) {
          p->setException(exception_wrapper(std::current_exception(), e));
        } catch (...) {
          p->setException(exception_wrapper(std::current_exception()));
        }
      }
    });

  return f;
}

template <typename T>
template <typename R, typename Caller, typename... Args>
  Future<typename isFuture<R>::Inner>
Future<T>::then(R(Caller::*func)(Args...), Caller *instance) {
  typedef typename std::remove_cv<
    typename std::remove_reference<
      typename detail::ArgType<Args...>::FirstArg>::type>::type FirstArg;
  return then([instance, func](Try<T>&& t){
    return (instance->*func)(t.template get<isTry<FirstArg>::value, Args>()...);
  });
}

// TODO(6838553)
#ifndef __clang__
template <class T>
template <class... Args>
auto Future<T>::then(Executor* x, Args&&... args)
  -> decltype(this->then(std::forward<Args>(args)...))
{
  auto oldX = getExecutor();
  setExecutor(x);
  return this->then(std::forward<Args>(args)...).via(oldX);
}
#endif

template <class T>
Future<void> Future<T>::then() {
  return then([] (Try<T>&& t) {});
}

// onError where the callback returns T
template <class T>
template <class F>
typename std::enable_if<
  !detail::callableWith<F, exception_wrapper>::value &&
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
    if (!t.template withException<Exn>([&] (Exn& e) {
          pm->setWith([&]{
            return (*funcm)(e);
          });
        })) {
      pm->setTry(std::move(t));
    }
  });

  return f;
}

// onError where the callback returns Future<T>
template <class T>
template <class F>
typename std::enable_if<
  !detail::callableWith<F, exception_wrapper>::value &&
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
    if (!t.template withException<Exn>([&] (Exn& e) {
          try {
            auto f2 = (*funcm)(e);
            f2.setCallback_([pm](Try<T>&& t2) mutable {
              pm->setTry(std::move(t2));
            });
          } catch (const std::exception& e2) {
            pm->setException(exception_wrapper(std::current_exception(), e2));
          } catch (...) {
            pm->setException(exception_wrapper(std::current_exception()));
          }
        })) {
      pm->setTry(std::move(t));
    }
  });

  return f;
}

template <class T>
template <class F>
Future<T> Future<T>::ensure(F func) {
  MoveWrapper<F> funcw(std::move(func));
  return this->then([funcw](Try<T>&& t) {
    (*funcw)();
    return makeFuture(std::move(t));
  });
}

template <class T>
template <class F>
Future<T> Future<T>::onTimeout(Duration dur, F&& func, Timekeeper* tk) {
  auto funcw = folly::makeMoveWrapper(std::forward<F>(func));
  return within(dur, tk)
    .onError([funcw](TimedOut const&) { return (*funcw)(); });
}

template <class T>
template <class F>
typename std::enable_if<
  detail::callableWith<F, exception_wrapper>::value &&
  detail::Extract<F>::ReturnsFuture::value,
  Future<T>>::type
Future<T>::onError(F&& func) {
  static_assert(
      std::is_same<typename detail::Extract<F>::Return, Future<T>>::value,
      "Return type of onError callback must be T or Future<T>");

  Promise<T> p;
  auto f = p.getFuture();
  auto pm = folly::makeMoveWrapper(std::move(p));
  auto funcm = folly::makeMoveWrapper(std::move(func));
  setCallback_([pm, funcm](Try<T> t) mutable {
    if (t.hasException()) {
      try {
        auto f2 = (*funcm)(std::move(t.exception()));
        f2.setCallback_([pm](Try<T> t2) mutable {
          pm->setTry(std::move(t2));
        });
      } catch (const std::exception& e2) {
        pm->setException(exception_wrapper(std::current_exception(), e2));
      } catch (...) {
        pm->setException(exception_wrapper(std::current_exception()));
      }
    } else {
      pm->setTry(std::move(t));
    }
  });

  return f;
}

// onError(exception_wrapper) that returns T
template <class T>
template <class F>
typename std::enable_if<
  detail::callableWith<F, exception_wrapper>::value &&
  !detail::Extract<F>::ReturnsFuture::value,
  Future<T>>::type
Future<T>::onError(F&& func) {
  static_assert(
      std::is_same<typename detail::Extract<F>::Return, Future<T>>::value,
      "Return type of onError callback must be T or Future<T>");

  Promise<T> p;
  auto f = p.getFuture();
  auto pm = folly::makeMoveWrapper(std::move(p));
  auto funcm = folly::makeMoveWrapper(std::move(func));
  setCallback_([pm, funcm](Try<T> t) mutable {
    if (t.hasException()) {
      pm->setWith([&]{
        return (*funcm)(std::move(t.exception()));
      });
    } else {
      pm->setTry(std::move(t));
    }
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
Optional<Try<T>> Future<T>::poll() {
  Optional<Try<T>> o;
  if (core_->ready()) {
    o = std::move(core_->getTry());
  }
  return o;
}

template <class T>
template <typename Executor>
inline Future<T> Future<T>::via(Executor* executor) && {
  throwIfInvalid();

  setExecutor(executor);

  return std::move(*this);
}

template <class T>
template <typename Executor>
inline Future<T> Future<T>::via(Executor* executor) & {
  throwIfInvalid();

  MoveWrapper<Promise<T>> p;
  auto f = p->getFuture();
  then([p](Try<T>&& t) mutable { p->setTry(std::move(t)); });
  return std::move(f).via(executor);
}

template <class T>
bool Future<T>::isReady() const {
  throwIfInvalid();
  return core_->ready();
}

template <class T>
void Future<T>::raise(exception_wrapper exception) {
  core_->raise(std::move(exception));
}

// makeFuture

template <class T>
Future<typename std::decay<T>::type> makeFuture(T&& t) {
  Promise<typename std::decay<T>::type> p;
  p.setValue(std::forward<T>(t));
  return p.getFuture();
}

inline // for multiple translation units
Future<void> makeFuture() {
  Promise<void> p;
  p.setValue();
  return p.getFuture();
}

template <class F>
auto makeFutureWith(
    F&& func,
    typename std::enable_if<!std::is_reference<F>::value, bool>::type sdf)
    -> Future<decltype(func())> {
  Promise<decltype(func())> p;
  p.setWith(
    [&func]() {
      return (func)();
    });
  return p.getFuture();
}

template <class F>
auto makeFutureWith(F const& func) -> Future<decltype(func())> {
  F copy = func;
  return makeFutureWith(std::move(copy));
}

template <class T>
Future<T> makeFuture(std::exception_ptr const& e) {
  Promise<T> p;
  p.setException(e);
  return p.getFuture();
}

template <class T>
Future<T> makeFuture(exception_wrapper ew) {
  Promise<T> p;
  p.setException(std::move(ew));
  return p.getFuture();
}

template <class T, class E>
typename std::enable_if<std::is_base_of<std::exception, E>::value,
                        Future<T>>::type
makeFuture(E const& e) {
  Promise<T> p;
  p.setException(make_exception_wrapper<E>(e));
  return p.getFuture();
}

template <class T>
Future<T> makeFuture(Try<T>&& t) {
  Promise<typename std::decay<T>::type> p;
  p.setTry(std::move(t));
  return p.getFuture();
}

template <>
inline Future<void> makeFuture(Try<void>&& t) {
  if (t.hasException()) {
    return makeFuture<void>(std::move(t.exception()));
  } else {
    return makeFuture();
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
collectAll(Fs&&... fs) {
  auto ctx =
    new detail::VariadicContext<typename std::decay<Fs>::type::value_type...>();
  ctx->total = sizeof...(fs);
  auto f_saved = ctx->p.getFuture();
  detail::collectAllVariadicHelper(ctx,
    std::forward<typename std::decay<Fs>::type>(fs)...);
  return f_saved;
}

// when (iterator)

template <class InputIterator>
Future<
  std::vector<
  Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>
collectAll(InputIterator first, InputIterator last) {
  typedef
    typename std::iterator_traits<InputIterator>::value_type::value_type T;

  if (first >= last) {
    return makeFuture(std::vector<Try<T>>());
  }
  size_t n = std::distance(first, last);

  auto ctx = new detail::WhenAllContext<T>();

  ctx->results.resize(n);

  auto f_saved = ctx->p.getFuture();

  for (size_t i = 0; first != last; ++first, ++i) {
     assert(i < n);
     auto& f = *first;
     f.setCallback_([ctx, i, n](Try<T> t) {
       ctx->results[i] = std::move(t);
       if (++ctx->count == n) {
         ctx->p.setValue(std::move(ctx->results));
         delete ctx;
       }
     });
  }

  return f_saved;
}

namespace detail {

template <class, class, typename = void> struct CollectContextHelper;

template <class T, class VecT>
struct CollectContextHelper<T, VecT,
    typename std::enable_if<std::is_same<T, VecT>::value>::type> {
  static inline std::vector<T>&& getResults(std::vector<VecT>& results) {
    return std::move(results);
  }
};

template <class T, class VecT>
struct CollectContextHelper<T, VecT,
    typename std::enable_if<!std::is_same<T, VecT>::value>::type> {
  static inline std::vector<T> getResults(std::vector<VecT>& results) {
    std::vector<T> finalResults;
    finalResults.reserve(results.size());
    for (auto& opt : results) {
      finalResults.push_back(std::move(opt.value()));
    }
    return finalResults;
  }
};

template <typename T>
struct CollectContext {

  typedef typename std::conditional<
    std::is_default_constructible<T>::value,
    T,
    Optional<T>
   >::type VecT;

  explicit CollectContext(int n) : count(0), threw(false) {
    results.resize(n);
  }

  Promise<std::vector<T>> p;
  std::vector<VecT> results;
  std::atomic<size_t> count;
  std::atomic_bool threw;

  typedef std::vector<T> result_type;

  static inline Future<std::vector<T>> makeEmptyFuture() {
    return makeFuture(std::vector<T>());
  }

  inline void setValue() {
    p.setValue(CollectContextHelper<T, VecT>::getResults(results));
  }

  inline void addResult(int i, Try<T>& t) {
    results[i] = std::move(t.value());
  }
};

template <>
struct CollectContext<void> {

  explicit CollectContext(int n) : count(0), threw(false) {}

  Promise<void> p;
  std::atomic<size_t> count;
  std::atomic_bool threw;

  typedef void result_type;

  static inline Future<void> makeEmptyFuture() {
    return makeFuture();
  }

  inline void setValue() {
    p.setValue();
  }

  inline void addResult(int i, Try<void>& t) {
    // do nothing
  }
};

} // detail

template <class InputIterator>
Future<typename detail::CollectContext<
  typename std::iterator_traits<InputIterator>::value_type::value_type
>::result_type>
collect(InputIterator first, InputIterator last) {
  typedef
    typename std::iterator_traits<InputIterator>::value_type::value_type T;

  if (first >= last) {
    return detail::CollectContext<T>::makeEmptyFuture();
  }

  size_t n = std::distance(first, last);
  auto ctx = new detail::CollectContext<T>(n);
  auto f_saved = ctx->p.getFuture();

  for (size_t i = 0; first != last; ++first, ++i) {
     assert(i < n);
     auto& f = *first;
     f.setCallback_([ctx, i, n](Try<T> t) {
       auto c = ++ctx->count;

       if (t.hasException()) {
         if (!ctx->threw.exchange(true)) {
           ctx->p.setException(std::move(t.exception()));
         }
       } else if (!ctx->threw) {
         ctx->addResult(i, t);
         if (c == n) {
           ctx->setValue();
         }
       }

       if (c == n) {
         delete ctx;
       }
     });
  }

  return f_saved;
}

template <class InputIterator>
Future<
  std::pair<size_t,
            Try<
              typename
              std::iterator_traits<InputIterator>::value_type::value_type> > >
collectAny(InputIterator first, InputIterator last) {
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

  return f_saved;
}

template <class InputIterator>
Future<std::vector<std::pair<size_t, Try<typename
  std::iterator_traits<InputIterator>::value_type::value_type>>>>
collectN(InputIterator first, InputIterator last, size_t n) {
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
  // have n completed futures at which point we fulfill our Promise with the
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
          ctx->p.setTry(Try<V>(std::move(v)));
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

template <class It, class T, class F, class ItT, class Arg>
typename std::enable_if<!isFutureResult<F, T, Arg>::value, Future<T>>::type
reduce(It first, It last, T initial, F func) {
  if (first == last) {
    return makeFuture(std::move(initial));
  }

  typedef isTry<Arg> IsTry;

  return collectAll(first, last)
    .then([initial, func](std::vector<Try<ItT>>& vals) mutable {
      for (auto& val : vals) {
        initial = func(std::move(initial),
                       // Either return a ItT&& or a Try<ItT>&& depending
                       // on the type of the argument of func.
                       val.template get<IsTry::value, Arg&&>());
      }
      return initial;
    });
}

template <class It, class T, class F, class ItT, class Arg>
typename std::enable_if<isFutureResult<F, T, Arg>::value, Future<T>>::type
reduce(It first, It last, T initial, F func) {
  if (first == last) {
    return makeFuture(std::move(initial));
  }

  typedef isTry<Arg> IsTry;

  auto f = first->then([initial, func](Try<ItT>& head) mutable {
    return func(std::move(initial),
                head.template get<IsTry::value, Arg&&>());
  });

  for (++first; first != last; ++first) {
    f = collectAll(f, *first).then([func](std::tuple<Try<T>, Try<ItT>>& t) {
      return func(std::move(std::get<0>(t).value()),
                  // Either return a ItT&& or a Try<ItT>&& depending
                  // on the type of the argument of func.
                  std::get<1>(t).template get<IsTry::value, Arg&&>());
    });
  }

  return f;
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
    tk = folly::detail::getTimekeeperSingleton();
  }

  tk->after(dur)
    .then([ctx](Try<void> const& t) {
      if (ctx->token.exchange(true) == false) {
        if (t.hasException()) {
          ctx->promise.setException(std::move(t.exception()));
        } else {
          ctx->promise.setException(std::move(ctx->exception));
        }
      }
    });

  this->then([ctx](Try<T>&& t) {
    if (ctx->token.exchange(true) == false) {
      ctx->promise.setTry(std::move(t));
    }
  });

  return ctx->promise.getFuture();
}

template <class T>
Future<T> Future<T>::delayed(Duration dur, Timekeeper* tk) {
  return collectAll(*this, futures::sleep(dur, tk))
    .then([](std::tuple<Try<T>, Try<void>> tup) {
      Try<T>& t = std::get<0>(tup);
      return makeFuture<T>(std::move(t));
    });
}

namespace detail {

template <class T>
void waitImpl(Future<T>& f) {
  // short-circuit if there's nothing to do
  if (f.isReady()) return;

  folly::fibers::Baton baton;
  f = f.then([&](Try<T> t) {
    baton.post();
    return makeFuture(std::move(t));
  });
  baton.wait();

  // There's a race here between the return here and the actual finishing of
  // the future. f is completed, but the setup may not have finished on done
  // after the baton has posted.
  while (!f.isReady()) {
    std::this_thread::yield();
  }
}

template <class T>
void waitImpl(Future<T>& f, Duration dur) {
  // short-circuit if there's nothing to do
  if (f.isReady()) return;

  auto baton = std::make_shared<folly::fibers::Baton>();
  f = f.then([baton](Try<T> t) {
    baton->post();
    return makeFuture(std::move(t));
  });

  // Let's preserve the invariant that if we did not timeout (timed_wait returns
  // true), then the returned Future is complete when it is returned to the
  // caller. We need to wait out the race for that Future to complete.
  if (baton->timed_wait(dur)) {
    while (!f.isReady()) {
      std::this_thread::yield();
    }
  }
}

template <class T>
void waitViaImpl(Future<T>& f, DrivableExecutor* e) {
  while (!f.isReady()) {
    e->drive();
  }
}

} // detail

template <class T>
Future<T>& Future<T>::wait() & {
  detail::waitImpl(*this);
  return *this;
}

template <class T>
Future<T>&& Future<T>::wait() && {
  detail::waitImpl(*this);
  return std::move(*this);
}

template <class T>
Future<T>& Future<T>::wait(Duration dur) & {
  detail::waitImpl(*this, dur);
  return *this;
}

template <class T>
Future<T>&& Future<T>::wait(Duration dur) && {
  detail::waitImpl(*this, dur);
  return std::move(*this);
}

template <class T>
Future<T>& Future<T>::waitVia(DrivableExecutor* e) & {
  detail::waitViaImpl(*this, e);
  return *this;
}

template <class T>
Future<T>&& Future<T>::waitVia(DrivableExecutor* e) && {
  detail::waitViaImpl(*this, e);
  return std::move(*this);
}

template <class T>
T Future<T>::get() {
  return std::move(wait().value());
}

template <>
inline void Future<void>::get() {
  wait().value();
}

template <class T>
T Future<T>::get(Duration dur) {
  wait(dur);
  if (isReady()) {
    return std::move(value());
  } else {
    throw TimedOut();
  }
}

template <>
inline void Future<void>::get(Duration dur) {
  wait(dur);
  if (isReady()) {
    return;
  } else {
    throw TimedOut();
  }
}

template <class T>
T Future<T>::getVia(DrivableExecutor* e) {
  return std::move(waitVia(e).value());
}

template <>
inline void Future<void>::getVia(DrivableExecutor* e) {
  waitVia(e).value();
}

template <class T>
Future<bool> Future<T>::willEqual(Future<T>& f) {
  return collectAll(*this, f).then([](const std::tuple<Try<T>, Try<T>>& t) {
    if (std::get<0>(t).hasValue() && std::get<1>(t).hasValue()) {
      return std::get<0>(t).value() == std::get<1>(t).value();
    } else {
      return false;
      }
  });
}

template <class T>
template <class F>
Future<T> Future<T>::filter(F predicate) {
  auto p = folly::makeMoveWrapper(std::move(predicate));
  return this->then([p](T val) {
    T const& valConstRef = val;
    if (!(*p)(valConstRef)) {
      throw PredicateDoesNotObtain();
    }
    return val;
  });
}

namespace futures {
  namespace {
    template <class Z>
    Future<Z> chainHelper(Future<Z> f) {
      return f;
    }

    template <class Z, class F, class Fn, class... Callbacks>
    Future<Z> chainHelper(F f, Fn fn, Callbacks... fns) {
      return chainHelper<Z>(f.then(fn), fns...);
    }
  }

  template <class A, class Z, class... Callbacks>
  std::function<Future<Z>(Try<A>)>
  chain(Callbacks... fns) {
    MoveWrapper<Promise<A>> pw;
    MoveWrapper<Future<Z>> fw(chainHelper<Z>(pw->getFuture(), fns...));
    return [=](Try<A> t) mutable {
      pw->setTry(std::move(t));
      return std::move(*fw);
    };
  }

  template <class It, class F, class ItT, class Result>
  std::vector<Future<Result>> map(It first, It last, F func) {
    std::vector<Future<Result>> results;
    for (auto it = first; it != last; it++) {
      results.push_back(it->then(func));
    }
    return results;
  }
}

} // namespace folly

// I haven't included a Future<T&> specialization because I don't forsee us
// using it, however it is not difficult to add when needed. Refer to
// Future<void> for guidance. std::future and boost::future code would also be
// instructive.
