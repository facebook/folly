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

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

#include <folly/experimental/fibers/Baton.h>
#include <folly/Optional.h>
#include <folly/Random.h>
#include <folly/Traits.h>
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
template <class T2, typename>
Future<T>::Future(T2&& val)
  : core_(new detail::Core<T>(Try<T>(std::forward<T2>(val)))) {}

template <class T>
template <typename, typename>
Future<T>::Future()
  : core_(new detail::Core<T>(Try<T>(T()))) {}

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
  p->core_->setInterruptHandlerNoLock(core_->getInterruptHandler());
  folly::MoveWrapper<F> funcm(std::forward<F>(func));

  // grab the Future now before we lose our handle on the Promise
  auto f = p->getFuture();
  f.core_->setExecutorNoLock(getExecutor());

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
  p->core_->setInterruptHandlerNoLock(core_->getInterruptHandler());
  folly::MoveWrapper<F> funcm(std::forward<F>(func));

  // grab the Future now before we lose our handle on the Promise
  auto f = p->getFuture();
  f.core_->setExecutorNoLock(getExecutor());

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

template <class T>
template <class Executor, class Arg, class... Args>
auto Future<T>::then(Executor* x, Arg&& arg, Args&&... args)
  -> decltype(this->then(std::forward<Arg>(arg),
                         std::forward<Args>(args)...))
{
  auto oldX = getExecutor();
  setExecutor(x);
  return this->then(std::forward<Arg>(arg), std::forward<Args>(args)...).
               via(oldX);
}

template <class T>
Future<Unit> Future<T>::then() {
  return then([] () {});
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
  p.core_->setInterruptHandlerNoLock(core_->getInterruptHandler());
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
  return this->then([funcw](Try<T>&& t) mutable {
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
inline Future<T> Future<T>::via(Executor* executor, int8_t priority) && {
  throwIfInvalid();

  setExecutor(executor, priority);

  return std::move(*this);
}

template <class T>
inline Future<T> Future<T>::via(Executor* executor, int8_t priority) & {
  throwIfInvalid();

  MoveWrapper<Promise<T>> p;
  auto f = p->getFuture();
  then([p](Try<T>&& t) mutable { p->setTry(std::move(t)); });
  return std::move(f).via(executor, priority);
}


template <class Func>
auto via(Executor* x, Func func)
  -> Future<typename isFuture<decltype(func())>::Inner>
{
  // TODO make this actually more performant. :-P #7260175
  return via(x).then(func);
}

template <class T>
bool Future<T>::isReady() const {
  throwIfInvalid();
  return core_->ready();
}

template <class T>
bool Future<T>::hasValue() {
  return getTry().hasValue();
}

template <class T>
bool Future<T>::hasException() {
  return getTry().hasException();
}

template <class T>
void Future<T>::raise(exception_wrapper exception) {
  core_->raise(std::move(exception));
}

// makeFuture

template <class T>
Future<typename std::decay<T>::type> makeFuture(T&& t) {
  return makeFuture(Try<typename std::decay<T>::type>(std::forward<T>(t)));
}

inline // for multiple translation units
Future<Unit> makeFuture() {
  return makeFuture(Unit{});
}

// makeFutureWith(Future<T>()) -> Future<T>
template <class F>
typename std::enable_if<isFuture<typename std::result_of<F()>::type>::value,
                        typename std::result_of<F()>::type>::type
makeFutureWith(F&& func) {
  using InnerType =
      typename isFuture<typename std::result_of<F()>::type>::Inner;
  try {
    return func();
  } catch (std::exception& e) {
    return makeFuture<InnerType>(
        exception_wrapper(std::current_exception(), e));
  } catch (...) {
    return makeFuture<InnerType>(exception_wrapper(std::current_exception()));
  }
}

// makeFutureWith(T()) -> Future<T>
// makeFutureWith(void()) -> Future<Unit>
template <class F>
typename std::enable_if<
    !(isFuture<typename std::result_of<F()>::type>::value),
    Future<typename Unit::Lift<typename std::result_of<F()>::type>::type>>::type
makeFutureWith(F&& func) {
  using LiftedResult =
      typename Unit::Lift<typename std::result_of<F()>::type>::type;
  return makeFuture<LiftedResult>(makeTryWith([&func]() mutable {
    return func();
  }));
}

template <class T>
Future<T> makeFuture(std::exception_ptr const& e) {
  return makeFuture(Try<T>(e));
}

template <class T>
Future<T> makeFuture(exception_wrapper ew) {
  return makeFuture(Try<T>(std::move(ew)));
}

template <class T, class E>
typename std::enable_if<std::is_base_of<std::exception, E>::value,
                        Future<T>>::type
makeFuture(E const& e) {
  return makeFuture(Try<T>(make_exception_wrapper<E>(e)));
}

template <class T>
Future<T> makeFuture(Try<T>&& t) {
  return Future<T>(new detail::Core<T>(std::move(t)));
}

// via
Future<Unit> via(Executor* executor, int8_t priority) {
  return makeFuture().via(executor, priority);
}

// mapSetCallback calls func(i, Try<T>) when every future completes

template <class T, class InputIterator, class F>
void mapSetCallback(InputIterator first, InputIterator last, F func) {
  for (size_t i = 0; first != last; ++first, ++i) {
    first->setCallback_([func, i](Try<T>&& t) {
      func(i, std::move(t));
    });
  }
}

// collectAll (variadic)

template <typename... Fs>
typename detail::CollectAllVariadicContext<
  typename std::decay<Fs>::type::value_type...>::type
collectAll(Fs&&... fs) {
  auto ctx = std::make_shared<detail::CollectAllVariadicContext<
    typename std::decay<Fs>::type::value_type...>>();
  detail::collectVariadicHelper<detail::CollectAllVariadicContext>(
    ctx, std::forward<typename std::decay<Fs>::type>(fs)...);
  return ctx->p.getFuture();
}

// collectAll (iterator)

template <class InputIterator>
Future<
  std::vector<
  Try<typename std::iterator_traits<InputIterator>::value_type::value_type>>>
collectAll(InputIterator first, InputIterator last) {
  typedef
    typename std::iterator_traits<InputIterator>::value_type::value_type T;

  struct CollectAllContext {
    CollectAllContext(int n) : results(n) {}
    ~CollectAllContext() {
      p.setValue(std::move(results));
    }
    Promise<std::vector<Try<T>>> p;
    std::vector<Try<T>> results;
  };

  auto ctx = std::make_shared<CollectAllContext>(std::distance(first, last));
  mapSetCallback<T>(first, last, [ctx](size_t i, Try<T>&& t) {
    ctx->results[i] = std::move(t);
  });
  return ctx->p.getFuture();
}

// collect (iterator)

namespace detail {

template <typename T>
struct CollectContext {
  struct Nothing { explicit Nothing(int n) {} };

  using Result = typename std::conditional<
    std::is_void<T>::value,
    void,
    std::vector<T>>::type;

  using InternalResult = typename std::conditional<
    std::is_void<T>::value,
    Nothing,
    std::vector<Optional<T>>>::type;

  explicit CollectContext(int n) : result(n) {}
  ~CollectContext() {
    if (!threw.exchange(true)) {
      // map Optional<T> -> T
      std::vector<T> finalResult;
      finalResult.reserve(result.size());
      std::transform(result.begin(), result.end(),
                     std::back_inserter(finalResult),
                     [](Optional<T>& o) { return std::move(o.value()); });
      p.setValue(std::move(finalResult));
    }
  }
  inline void setPartialResult(size_t i, Try<T>& t) {
    result[i] = std::move(t.value());
  }
  Promise<Result> p;
  InternalResult result;
  std::atomic<bool> threw {false};
};

}

template <class InputIterator>
Future<typename detail::CollectContext<
  typename std::iterator_traits<InputIterator>::value_type::value_type>::Result>
collect(InputIterator first, InputIterator last) {
  typedef
    typename std::iterator_traits<InputIterator>::value_type::value_type T;

  auto ctx = std::make_shared<detail::CollectContext<T>>(
    std::distance(first, last));
  mapSetCallback<T>(first, last, [ctx](size_t i, Try<T>&& t) {
    if (t.hasException()) {
       if (!ctx->threw.exchange(true)) {
         ctx->p.setException(std::move(t.exception()));
       }
     } else if (!ctx->threw) {
       ctx->setPartialResult(i, t);
     }
  });
  return ctx->p.getFuture();
}

// collect (variadic)

template <typename... Fs>
typename detail::CollectVariadicContext<
  typename std::decay<Fs>::type::value_type...>::type
collect(Fs&&... fs) {
  auto ctx = std::make_shared<detail::CollectVariadicContext<
    typename std::decay<Fs>::type::value_type...>>();
  detail::collectVariadicHelper<detail::CollectVariadicContext>(
    ctx, std::forward<typename std::decay<Fs>::type>(fs)...);
  return ctx->p.getFuture();
}

// collectAny (iterator)

template <class InputIterator>
Future<
  std::pair<size_t,
            Try<
              typename
              std::iterator_traits<InputIterator>::value_type::value_type>>>
collectAny(InputIterator first, InputIterator last) {
  typedef
    typename std::iterator_traits<InputIterator>::value_type::value_type T;

  struct CollectAnyContext {
    CollectAnyContext() {};
    Promise<std::pair<size_t, Try<T>>> p;
    std::atomic<bool> done {false};
  };

  auto ctx = std::make_shared<CollectAnyContext>();
  mapSetCallback<T>(first, last, [ctx](size_t i, Try<T>&& t) {
    if (!ctx->done.exchange(true)) {
      ctx->p.setValue(std::make_pair(i, std::move(t)));
    }
  });
  return ctx->p.getFuture();
}

// collectN (iterator)

template <class InputIterator>
Future<std::vector<std::pair<size_t, Try<typename
  std::iterator_traits<InputIterator>::value_type::value_type>>>>
collectN(InputIterator first, InputIterator last, size_t n) {
  typedef typename
    std::iterator_traits<InputIterator>::value_type::value_type T;
  typedef std::vector<std::pair<size_t, Try<T>>> V;

  struct CollectNContext {
    V v;
    std::atomic<size_t> completed = {0};
    Promise<V> p;
  };
  auto ctx = std::make_shared<CollectNContext>();

  if (size_t(std::distance(first, last)) < n) {
    ctx->p.setException(std::runtime_error("Not enough futures"));
  } else {
    // for each completed Future, increase count and add to vector, until we
    // have n completed futures at which point we fulfil our Promise with the
    // vector
    mapSetCallback<T>(first, last, [ctx, n](size_t i, Try<T>&& t) {
      auto c = ++ctx->completed;
      if (c <= n) {
        assert(ctx->v.size() < n);
        ctx->v.emplace_back(i, std::move(t));
        if (c == n) {
          ctx->p.setTry(Try<V>(std::move(ctx->v)));
        }
      }
    });
  }

  return ctx->p.getFuture();
}

// reduce (iterator)

template <class It, class T, class F>
Future<T> reduce(It first, It last, T&& initial, F&& func) {
  if (first == last) {
    return makeFuture(std::move(initial));
  }

  typedef typename std::iterator_traits<It>::value_type::value_type ItT;
  typedef typename std::conditional<
    detail::callableWith<F, T&&, Try<ItT>&&>::value, Try<ItT>, ItT>::type Arg;
  typedef isTry<Arg> IsTry;

  folly::MoveWrapper<T> minitial(std::move(initial));
  auto sfunc = std::make_shared<F>(std::move(func));

  auto f = first->then([minitial, sfunc](Try<ItT>& head) mutable {
    return (*sfunc)(std::move(*minitial),
                head.template get<IsTry::value, Arg&&>());
  });

  for (++first; first != last; ++first) {
    f = collectAll(f, *first).then([sfunc](std::tuple<Try<T>, Try<ItT>>& t) {
      return (*sfunc)(std::move(std::get<0>(t).value()),
                  // Either return a ItT&& or a Try<ItT>&& depending
                  // on the type of the argument of func.
                  std::get<1>(t).template get<IsTry::value, Arg&&>());
    });
  }

  return f;
}

// window (collection)

template <class Collection, class F, class ItT, class Result>
std::vector<Future<Result>>
window(Collection input, F func, size_t n) {
  struct WindowContext {
    WindowContext(Collection&& i, F&& fn)
        : input_(std::move(i)), promises_(input_.size()),
          func_(std::move(fn))
      {}
    std::atomic<size_t> i_ {0};
    Collection input_;
    std::vector<Promise<Result>> promises_;
    F func_;

    static inline void spawn(const std::shared_ptr<WindowContext>& ctx) {
      size_t i = ctx->i_++;
      if (i < ctx->input_.size()) {
        // Using setCallback_ directly since we don't need the Future
        ctx->func_(std::move(ctx->input_[i])).setCallback_(
          // ctx is captured by value
          [ctx, i](Try<Result>&& t) {
            ctx->promises_[i].setTry(std::move(t));
            // Chain another future onto this one
            spawn(std::move(ctx));
          });
      }
    }
  };

  auto max = std::min(n, input.size());

  auto ctx = std::make_shared<WindowContext>(
    std::move(input), std::move(func));

  for (size_t i = 0; i < max; ++i) {
    // Start the first n Futures
    WindowContext::spawn(ctx);
  }

  std::vector<Future<Result>> futures;
  futures.reserve(ctx->promises_.size());
  for (auto& promise : ctx->promises_) {
    futures.emplace_back(promise.getFuture());
  }

  return futures;
}

// reduce

template <class T>
template <class I, class F>
Future<I> Future<T>::reduce(I&& initial, F&& func) {
  folly::MoveWrapper<I> minitial(std::move(initial));
  folly::MoveWrapper<F> mfunc(std::move(func));
  return then([minitial, mfunc](T& vals) mutable {
    auto ret = std::move(*minitial);
    for (auto& val : vals) {
      ret = (*mfunc)(std::move(ret), std::move(val));
    }
    return ret;
  });
}

// unorderedReduce (iterator)

template <class It, class T, class F, class ItT, class Arg>
Future<T> unorderedReduce(It first, It last, T initial, F func) {
  if (first == last) {
    return makeFuture(std::move(initial));
  }

  typedef isTry<Arg> IsTry;

  struct UnorderedReduceContext {
    UnorderedReduceContext(T&& memo, F&& fn, size_t n)
        : lock_(), memo_(makeFuture<T>(std::move(memo))),
          func_(std::move(fn)), numThens_(0), numFutures_(n), promise_()
      {};
    folly::MicroSpinLock lock_; // protects memo_ and numThens_
    Future<T> memo_;
    F func_;
    size_t numThens_; // how many Futures completed and called .then()
    size_t numFutures_; // how many Futures in total
    Promise<T> promise_;
  };

  auto ctx = std::make_shared<UnorderedReduceContext>(
    std::move(initial), std::move(func), std::distance(first, last));

  mapSetCallback<ItT>(first, last, [ctx](size_t i, Try<ItT>&& t) {
    folly::MoveWrapper<Try<ItT>> mt(std::move(t));
    // Futures can be completed in any order, simultaneously.
    // To make this non-blocking, we create a new Future chain in
    // the order of completion to reduce the values.
    // The spinlock just protects chaining a new Future, not actually
    // executing the reduce, which should be really fast.
    folly::MSLGuard lock(ctx->lock_);
    ctx->memo_ = ctx->memo_.then([ctx, mt](T&& v) mutable {
      // Either return a ItT&& or a Try<ItT>&& depending
      // on the type of the argument of func.
      return ctx->func_(std::move(v), mt->template get<IsTry::value, Arg&&>());
    });
    if (++ctx->numThens_ == ctx->numFutures_) {
      // After reducing the value of the last Future, fulfill the Promise
      ctx->memo_.setCallback_([ctx](Try<T>&& t2) {
        ctx->promise_.setValue(std::move(t2));
      });
    }
  });

  return ctx->promise_.getFuture();
}

// within

template <class T>
Future<T> Future<T>::within(Duration dur, Timekeeper* tk) {
  return within(dur, TimedOut(), tk);
}

template <class T>
template <class E>
Future<T> Future<T>::within(Duration dur, E e, Timekeeper* tk) {

  struct Context {
    Context(E ex) : exception(std::move(ex)), promise() {}
    E exception;
    Future<Unit> thisFuture;
    Promise<T> promise;
    std::atomic<bool> token {false};
  };

  if (!tk) {
    tk = folly::detail::getTimekeeperSingleton();
  }

  auto ctx = std::make_shared<Context>(std::move(e));

  ctx->thisFuture = this->then([ctx](Try<T>&& t) mutable {
    // TODO: "this" completed first, cancel "after"
    if (ctx->token.exchange(true) == false) {
      ctx->promise.setTry(std::move(t));
    }
  });

  tk->after(dur).then([ctx](Try<Unit> const& t) mutable {
    // "after" completed first, cancel "this"
    ctx->thisFuture.raise(TimedOut());
    if (ctx->token.exchange(true) == false) {
      if (t.hasException()) {
        ctx->promise.setException(std::move(t.exception()));
      } else {
        ctx->promise.setException(std::move(ctx->exception));
      }
    }
  });

  return ctx->promise.getFuture().via(getExecutor());
}

// delayed

template <class T>
Future<T> Future<T>::delayed(Duration dur, Timekeeper* tk) {
  return collectAll(*this, futures::sleep(dur, tk))
    .then([](std::tuple<Try<T>, Try<Unit>> tup) {
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

template <class T>
T Future<T>::get(Duration dur) {
  wait(dur);
  if (isReady()) {
    return std::move(value());
  } else {
    throw TimedOut();
  }
}

template <class T>
T Future<T>::getVia(DrivableExecutor* e) {
  return std::move(waitVia(e).value());
}

namespace detail {
  template <class T>
  struct TryEquals {
    static bool equals(const Try<T>& t1, const Try<T>& t2) {
      return t1.value() == t2.value();
    }
  };
}

template <class T>
Future<bool> Future<T>::willEqual(Future<T>& f) {
  return collectAll(*this, f).then([](const std::tuple<Try<T>, Try<T>>& t) {
    if (std::get<0>(t).hasValue() && std::get<1>(t).hasValue()) {
      return detail::TryEquals<T>::equals(std::get<0>(t), std::get<1>(t));
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

template <class T>
template <class Callback>
auto Future<T>::thenMulti(Callback&& fn)
    -> decltype(this->then(std::forward<Callback>(fn))) {
  // thenMulti with one callback is just a then
  return then(std::forward<Callback>(fn));
}

template <class T>
template <class Callback, class... Callbacks>
auto Future<T>::thenMulti(Callback&& fn, Callbacks&&... fns)
    -> decltype(this->then(std::forward<Callback>(fn)).
                      thenMulti(std::forward<Callbacks>(fns)...)) {
  // thenMulti with two callbacks is just then(a).thenMulti(b, ...)
  return then(std::forward<Callback>(fn)).
         thenMulti(std::forward<Callbacks>(fns)...);
}

template <class T>
template <class Callback, class... Callbacks>
auto Future<T>::thenMultiWithExecutor(Executor* x, Callback&& fn,
                                      Callbacks&&... fns)
    -> decltype(this->then(std::forward<Callback>(fn)).
                      thenMulti(std::forward<Callbacks>(fns)...)) {
  // thenMultiExecutor with two callbacks is
  // via(x).then(a).thenMulti(b, ...).via(oldX)
  auto oldX = getExecutor();
  setExecutor(x);
  return then(std::forward<Callback>(fn)).
         thenMulti(std::forward<Callbacks>(fns)...).via(oldX);
}

template <class T>
template <class Callback>
auto Future<T>::thenMultiWithExecutor(Executor* x, Callback&& fn)
    -> decltype(this->then(std::forward<Callback>(fn))) {
  // thenMulti with one callback is just a then with an executor
  return then(x, std::forward<Callback>(fn));
}

template <class F>
inline Future<Unit> when(bool p, F thunk) {
  return p ? thunk().unit() : makeFuture();
}

template <class P, class F>
Future<Unit> whileDo(P predicate, F thunk) {
  if (predicate()) {
    return thunk().then([=] {
      return whileDo(predicate, thunk);
    });
  }
  return makeFuture();
}

template <class F>
Future<Unit> times(const int n, F thunk) {
  auto count = folly::makeMoveWrapper(
    std::unique_ptr<std::atomic<int>>(new std::atomic<int>(0))
  );
  return folly::whileDo([=]() mutable {
      return (*count)->fetch_add(1) < n;
    }, thunk);
}

namespace futures {
  template <class It, class F, class ItT, class Result>
  std::vector<Future<Result>> map(It first, It last, F func) {
    std::vector<Future<Result>> results;
    for (auto it = first; it != last; it++) {
      results.push_back(it->then(func));
    }
    return results;
  }
}

namespace futures {

namespace detail {

struct retrying_policy_raw_tag {};
struct retrying_policy_fut_tag {};

template <class Policy>
struct retrying_policy_traits {
  using ew = exception_wrapper;
  FOLLY_CREATE_HAS_MEMBER_FN_TRAITS(has_op_call, operator());
  template <class Ret>
  using has_op = typename std::integral_constant<bool,
        has_op_call<Policy, Ret(size_t, const ew&)>::value ||
        has_op_call<Policy, Ret(size_t, const ew&) const>::value>;
  using is_raw = has_op<bool>;
  using is_fut = has_op<Future<bool>>;
  using tag = typename std::conditional<
        is_raw::value, retrying_policy_raw_tag, typename std::conditional<
        is_fut::value, retrying_policy_fut_tag, void>::type>::type;
};

template <class Policy, class FF>
typename std::result_of<FF(size_t)>::type
retrying(size_t k, Policy&& p, FF&& ff) {
  using F = typename std::result_of<FF(size_t)>::type;
  using T = typename F::value_type;
  auto f = ff(k++);
  auto pm = makeMoveWrapper(p);
  auto ffm = makeMoveWrapper(ff);
  return f.onError([=](exception_wrapper x) mutable {
      auto q = (*pm)(k, x);
      auto xm = makeMoveWrapper(std::move(x));
      return q.then([=](bool r) mutable {
          return r
            ? retrying(k, pm.move(), ffm.move())
            : makeFuture<T>(xm.move());
      });
  });
}

template <class Policy, class FF>
typename std::result_of<FF(size_t)>::type
retrying(Policy&& p, FF&& ff, retrying_policy_raw_tag) {
  auto pm = makeMoveWrapper(std::move(p));
  auto q = [=](size_t k, exception_wrapper x) {
    return makeFuture<bool>((*pm)(k, x));
  };
  return retrying(0, std::move(q), std::forward<FF>(ff));
}

template <class Policy, class FF>
typename std::result_of<FF(size_t)>::type
retrying(Policy&& p, FF&& ff, retrying_policy_fut_tag) {
  return retrying(0, std::forward<Policy>(p), std::forward<FF>(ff));
}

//  jittered exponential backoff, clamped to [backoff_min, backoff_max]
template <class URNG>
Duration retryingJitteredExponentialBackoffDur(
    size_t n,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param,
    URNG& rng) {
  using d = Duration;
  auto dist = std::normal_distribution<double>(0.0, jitter_param);
  auto jitter = std::exp(dist(rng));
  auto backoff = d(d::rep(jitter * backoff_min.count() * std::pow(2, n - 1)));
  return std::max(backoff_min, std::min(backoff_max, backoff));
}

template <class Policy, class URNG>
std::function<Future<bool>(size_t, const exception_wrapper&)>
retryingPolicyCappedJitteredExponentialBackoff(
    size_t max_tries,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param,
    URNG rng,
    Policy&& p) {
  auto pm = makeMoveWrapper(std::move(p));
  auto rngp = std::make_shared<URNG>(std::move(rng));
  return [=](size_t n, const exception_wrapper& ex) mutable {
    if (n == max_tries) { return makeFuture(false); }
    return (*pm)(n, ex).then([=](bool v) {
        if (!v) { return makeFuture(false); }
        auto backoff = detail::retryingJitteredExponentialBackoffDur(
            n, backoff_min, backoff_max, jitter_param, *rngp);
        return futures::sleep(backoff).then([] { return true; });
    });
  };
}

template <class Policy, class URNG>
std::function<Future<bool>(size_t, const exception_wrapper&)>
retryingPolicyCappedJitteredExponentialBackoff(
    size_t max_tries,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param,
    URNG rng,
    Policy&& p,
    retrying_policy_raw_tag) {
  auto pm = makeMoveWrapper(std::move(p));
  auto q = [=](size_t n, const exception_wrapper& e) {
    return makeFuture((*pm)(n, e));
  };
  return retryingPolicyCappedJitteredExponentialBackoff(
      max_tries,
      backoff_min,
      backoff_max,
      jitter_param,
      std::move(rng),
      std::move(q));
}

template <class Policy, class URNG>
std::function<Future<bool>(size_t, const exception_wrapper&)>
retryingPolicyCappedJitteredExponentialBackoff(
    size_t max_tries,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param,
    URNG rng,
    Policy&& p,
    retrying_policy_fut_tag) {
  return retryingPolicyCappedJitteredExponentialBackoff(
      max_tries,
      backoff_min,
      backoff_max,
      jitter_param,
      std::move(rng),
      std::move(p));
}

}

template <class Policy, class FF>
typename std::result_of<FF(size_t)>::type
retrying(Policy&& p, FF&& ff) {
  using tag = typename detail::retrying_policy_traits<Policy>::tag;
  return detail::retrying(std::forward<Policy>(p), std::forward<FF>(ff), tag());
}

inline
std::function<bool(size_t, const exception_wrapper&)>
retryingPolicyBasic(
    size_t max_tries) {
  return [=](size_t n, const exception_wrapper&) { return n < max_tries; };
}

template <class Policy, class URNG>
std::function<Future<bool>(size_t, const exception_wrapper&)>
retryingPolicyCappedJitteredExponentialBackoff(
    size_t max_tries,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param,
    URNG rng,
    Policy&& p) {
  using tag = typename detail::retrying_policy_traits<Policy>::tag;
  return detail::retryingPolicyCappedJitteredExponentialBackoff(
      max_tries,
      backoff_min,
      backoff_max,
      jitter_param,
      std::move(rng),
      std::move(p),
      tag());
}

inline
std::function<Future<bool>(size_t, const exception_wrapper&)>
retryingPolicyCappedJitteredExponentialBackoff(
    size_t max_tries,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param) {
  auto p = [](size_t, const exception_wrapper&) { return true; };
  return retryingPolicyCappedJitteredExponentialBackoff(
      max_tries,
      backoff_min,
      backoff_max,
      jitter_param,
      ThreadLocalPRNG(),
      std::move(p));
}

}

// Instantiate the most common Future types to save compile time
extern template class Future<Unit>;
extern template class Future<bool>;
extern template class Future<int>;
extern template class Future<int64_t>;
extern template class Future<std::string>;
extern template class Future<double>;

} // namespace folly
