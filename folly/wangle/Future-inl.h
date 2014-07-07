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

#include "detail/State.h"
#include <folly/LifoSem.h>

namespace folly { namespace wangle {

template <typename T>
struct isFuture {
  static const bool value = false;
};

template <typename T>
struct isFuture<Future<T> > {
  static const bool value = true;
};

template <class T>
Future<T>::Future(Future<T>&& other) noexcept : state_(nullptr) {
  *this = std::move(other);
}

template <class T>
Future<T>& Future<T>::operator=(Future<T>&& other) {
  std::swap(state_, other.state_);
  return *this;
}

template <class T>
Future<T>::~Future() {
  detach();
}

template <class T>
void Future<T>::detach() {
  if (state_) {
    state_->detachFuture();
    state_ = nullptr;
  }
}

template <class T>
void Future<T>::throwIfInvalid() const {
  if (!state_)
    throw NoState();
}

template <class T>
template <class F>
void Future<T>::setCallback_(F&& func) {
  throwIfInvalid();
  state_->setCallback(std::move(func));
}

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

     state_ can't be moved, it is explicitly disallowed (as is copying). But
     if there's ever a reason to allow it, this is one place that makes that
     assumption and would need to be fixed. We use a standard shared pointer
     for state_ (by copying it in), which means in essence obj holds a shared
     pointer to itself.  But this shouldn't leak because Promise will not
     outlive the continuation, because Promise will setException() with a
     broken Promise if it is destructed before completed. We could use a
     weak pointer but it would have to be converted to a shared pointer when
     func is executed (because the Future returned by func may possibly
     persist beyond the callback, if it gets moved), and so it is an
     optimization to just make it shared from the get-go.

     We have to move in the Promise and func using the MoveWrapper
     hack. (func could be copied but it's a big drag on perf).

     Two subtle but important points about this design. detail::State has no
     back pointers to Future or Promise, so if Future or Promise get moved
     (and they will be moved in performant code) we don't have to do
     anything fancy. And because we store the continuation in the
     detail::State, not in the Future, we can execute the continuation even
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

template <class T>
Future<void> Future<T>::then() {
  return then([] (Try<T>&& t) {});
}

template <class T>
typename std::add_lvalue_reference<T>::type Future<T>::value() {
  throwIfInvalid();

  return state_->value();
}

template <class T>
typename std::add_lvalue_reference<const T>::type Future<T>::value() const {
  throwIfInvalid();

  return state_->value();
}

template <class T>
Try<T>& Future<T>::getTry() {
  throwIfInvalid();

  return state_->getTry();
}

template <class T>
template <typename Executor>
inline Future<T> Future<T>::via(Executor* executor) {
  throwIfInvalid();
  auto f = then([=](Try<T>&& t) {
    MoveWrapper<Promise<T>> promise;
    MoveWrapper<Try<T>> tw(std::move(t));
    auto f2 = promise->getFuture();
    executor->add([=]() mutable { promise->fulfilTry(std::move(*tw)); });
    return f2;
  });
  f.deactivate();
  return f;
}

template <class T>
bool Future<T>::isReady() const {
  throwIfInvalid();
  return state_->ready();
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
typename std::enable_if<std::is_base_of<std::exception, E>::value, Future<T>>::type
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

  ctx->total = n;
  ctx->results.resize(ctx->total);

  auto f_saved = ctx->p.getFuture();

  for (size_t i = 0; first != last; ++first, ++i) {
     auto& f = *first;
     f.setCallback_([ctx, i](Try<T>&& t) {
         ctx->results[i] = std::move(t);
         if (++ctx->count == ctx->total) {
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
  LifoSem sem;
  auto done = f.then([&](Try<T> &&t) {
    sem.post();
    return std::move(t.value());
  });
  sem.wait();
  return done;
}

template<>
inline Future<void> waitWithSemaphore<void>(Future<void>&& f) {
  LifoSem sem;
  auto done = f.then([&](Try<void> &&t) {
    sem.post();
    t.value();
  });
  sem.wait();
  return done;
}

template <typename T, class Duration>
Future<T>
waitWithSemaphore(Future<T>&& f, Duration timeout) {
  auto sem = std::make_shared<LifoSem>();
  auto done = f.then([sem](Try<T> &&t) {
    sem->post();
    return std::move(t.value());
  });
  std::thread t([sem, timeout](){
    std::this_thread::sleep_for(timeout);
    sem->shutdown();
    });
  t.detach();
  try {
    sem->wait();
  } catch (ShutdownSemError & ign) { }
  return done;
}

template <class Duration>
Future<void>
waitWithSemaphore(Future<void>&& f, Duration timeout) {
  auto sem = std::make_shared<LifoSem>();
  auto done = f.then([sem](Try<void> &&t) {
    sem->post();
    t.value();
  });
  std::thread t([sem, timeout](){
    std::this_thread::sleep_for(timeout);
    sem->shutdown();
    });
  t.detach();
  try {
    sem->wait();
  } catch (ShutdownSemError & ign) { }
  return done;
}

}}

// I haven't included a Future<T&> specialization because I don't forsee us
// using it, however it is not difficult to add when needed. Refer to
// Future<void> for guidance. std::future and boost::future code would also be
// instructive.
