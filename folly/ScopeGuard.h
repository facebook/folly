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

#pragma once

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

#include <folly/Portability.h>
#include <folly/Preprocessor.h>
#include <folly/Utility.h>
#include <folly/lang/Exception.h>
#include <folly/lang/UncaughtExceptions.h>

namespace folly {

namespace detail {

struct ScopeGuardDismissed {};

class ScopeGuardImplBase {
 public:
  void dismiss() noexcept { dismissed_ = true; }
  void rehire() noexcept { dismissed_ = false; }

 protected:
  ScopeGuardImplBase(bool dismissed = false) noexcept : dismissed_(dismissed) {}

  [[noreturn]] static void terminate() noexcept;
  static ScopeGuardImplBase makeEmptyScopeGuard() noexcept {
    return ScopeGuardImplBase{};
  }

  template <typename T>
  static const T& asConst(const T& t) noexcept {
    return t;
  }

  bool dismissed_;
};

template <typename FunctionType, bool InvokeNoexcept>
class ScopeGuardImpl : public ScopeGuardImplBase {
 public:
  explicit ScopeGuardImpl(FunctionType& fn) noexcept(
      std::is_nothrow_copy_constructible<FunctionType>::value)
      : ScopeGuardImpl(
            asConst(fn),
            makeFailsafe(
                std::is_nothrow_copy_constructible<FunctionType>{}, &fn)) {}

  explicit ScopeGuardImpl(const FunctionType& fn) noexcept(
      std::is_nothrow_copy_constructible<FunctionType>::value)
      : ScopeGuardImpl(
            fn,
            makeFailsafe(
                std::is_nothrow_copy_constructible<FunctionType>{}, &fn)) {}

  explicit ScopeGuardImpl(FunctionType&& fn) noexcept(
      std::is_nothrow_move_constructible<FunctionType>::value)
      : ScopeGuardImpl(
            std::move_if_noexcept(fn),
            makeFailsafe(
                std::is_nothrow_move_constructible<FunctionType>{}, &fn)) {}

  explicit ScopeGuardImpl(FunctionType&& fn, ScopeGuardDismissed) noexcept(
      std::is_nothrow_move_constructible<FunctionType>::value)
      // No need for failsafe in this case, as the guard is dismissed.
      : ScopeGuardImplBase{true}, function_(std::forward<FunctionType>(fn)) {}

  ScopeGuardImpl(ScopeGuardImpl&& other) noexcept(
      std::is_nothrow_move_constructible<FunctionType>::value)
      : function_(std::move_if_noexcept(other.function_)) {
    // If the above line attempts a copy and the copy throws, other is
    // left owning the cleanup action and will execute it (or not) depending
    // on the value of other.dismissed_. The following lines only execute
    // if the move/copy succeeded, in which case *this assumes ownership of
    // the cleanup action and dismisses other.
    dismissed_ = std::exchange(other.dismissed_, true);
  }

  ~ScopeGuardImpl() noexcept(InvokeNoexcept) {
    if (!dismissed_) {
      execute();
    }
  }

 private:
  static ScopeGuardImplBase makeFailsafe(std::true_type, const void*) noexcept {
    return makeEmptyScopeGuard();
  }

  template <typename Fn>
  static auto makeFailsafe(std::false_type, Fn* fn) noexcept
      -> ScopeGuardImpl<decltype(std::ref(*fn)), InvokeNoexcept> {
    return ScopeGuardImpl<decltype(std::ref(*fn)), InvokeNoexcept>{
        std::ref(*fn)};
  }

  template <typename Fn>
  explicit ScopeGuardImpl(Fn&& fn, ScopeGuardImplBase&& failsafe)
      : ScopeGuardImplBase{}, function_(std::forward<Fn>(fn)) {
    failsafe.dismiss();
  }

  void* operator new(std::size_t) = delete;

  void execute() noexcept(InvokeNoexcept) {
    if (InvokeNoexcept) {
      using R = decltype(function_());
      auto catcher_word = reinterpret_cast<uintptr_t>(&terminate);
      auto catcher = reinterpret_cast<R (*)()>(catcher_word);
      catch_exception(function_, catcher);
    } else {
      function_();
    }
  }

  FunctionType function_;
};

template <typename F, bool INE>
using ScopeGuardImplDecay = ScopeGuardImpl<typename std::decay<F>::type, INE>;

} // namespace detail

/**
 * ScopeGuard is a general implementation of the "Initialization is
 * Resource Acquisition" idiom.  Basically, it guarantees that a function
 * is executed upon leaving the current scope unless otherwise told.
 *
 * The makeGuard() function is used to create a new ScopeGuard object.
 * It can be instantiated with a lambda function, a std::function<void()>,
 * a functor, or a void(*)() function pointer.
 *
 *
 * Usage example: Add a friend to memory if and only if it is also added
 * to the db.
 *
 * void User::addFriend(User& newFriend) {
 *   // add the friend to memory
 *   friends_.push_back(&newFriend);
 *
 *   // If the db insertion that follows fails, we should
 *   // remove it from memory.
 *   auto guard = makeGuard([&] { friends_.pop_back(); });
 *
 *   // this will throw an exception upon error, which
 *   // makes the ScopeGuard execute UserCont::pop_back()
 *   // once the Guard's destructor is called.
 *   db_->addFriend(GetName(), newFriend.GetName());
 *
 *   // an exception was not thrown, so don't execute
 *   // the Guard.
 *   guard.dismiss();
 * }
 *
 * It is also possible to create a guard in dismissed state with
 * makeDismissedGuard(), and later rehire it with the rehire()
 * method.
 *
 * makeDismissedGuard() is not just syntactic sugar for creating a guard and
 * immediately dismissing it, but it has a subtle behavior difference if
 * move-construction of the passed function can throw: if it does, the function
 * will be called by makeGuard(), but not by makeDismissedGuard().
 *
 * Examine ScopeGuardTest.cpp for some more sample usage.
 *
 * Stolen from:
 *   Andrei's and Petru Marginean's CUJ article:
 *     http://drdobbs.com/184403758
 *   and the loki library:
 *     http://loki-lib.sourceforge.net/index.php?n=Idioms.ScopeGuardPointer
 *   and triendl.kj article:
 *     http://www.codeproject.com/KB/cpp/scope_guard.aspx
 */
template <typename F>
FOLLY_NODISCARD detail::ScopeGuardImplDecay<F, true> makeGuard(F&& f) noexcept(
    noexcept(detail::ScopeGuardImplDecay<F, true>(static_cast<F&&>(f)))) {
  return detail::ScopeGuardImplDecay<F, true>(static_cast<F&&>(f));
}

template <typename F>
FOLLY_NODISCARD detail::ScopeGuardImplDecay<F, true>
makeDismissedGuard(F&& f) noexcept(
    noexcept(detail::ScopeGuardImplDecay<F, true>(
        static_cast<F&&>(f), detail::ScopeGuardDismissed{}))) {
  return detail::ScopeGuardImplDecay<F, true>(
      static_cast<F&&>(f), detail::ScopeGuardDismissed{});
}

namespace detail {

#if defined(FOLLY_EXCEPTION_COUNT_USE_CXA_GET_GLOBALS) || \
    defined(FOLLY_EXCEPTION_COUNT_USE_GETPTD) ||          \
    defined(FOLLY_EXCEPTION_COUNT_USE_STD)

/**
 * ScopeGuard used for executing a function when leaving the current scope
 * depending on the presence of a new uncaught exception.
 *
 * If the executeOnException template parameter is true, the function is
 * executed if a new uncaught exception is present at the end of the scope.
 * If the parameter is false, then the function is executed if no new uncaught
 * exceptions are present at the end of the scope.
 *
 * Used to implement SCOPE_FAIL and SCOPE_SUCCESS below.
 */
template <typename FunctionType, bool ExecuteOnException>
class ScopeGuardForNewException {
 public:
  explicit ScopeGuardForNewException(const FunctionType& fn) : guard_(fn) {}

  explicit ScopeGuardForNewException(FunctionType&& fn)
      : guard_(std::move(fn)) {}

  ScopeGuardForNewException(ScopeGuardForNewException&& other) = default;

  ~ScopeGuardForNewException() noexcept(ExecuteOnException) {
    if (ExecuteOnException != (exceptionCounter_ < uncaught_exceptions())) {
      guard_.dismiss();
    }
  }

 private:
  void* operator new(std::size_t) = delete;
  void operator delete(void*) = delete;

  ScopeGuardImpl<FunctionType, ExecuteOnException> guard_;
  int exceptionCounter_{uncaught_exceptions()};
};

/**
 * Internal use for the macro SCOPE_FAIL below
 */
enum class ScopeGuardOnFail {};

template <typename FunctionType>
ScopeGuardForNewException<typename std::decay<FunctionType>::type, true>
operator+(detail::ScopeGuardOnFail, FunctionType&& fn) {
  return ScopeGuardForNewException<
      typename std::decay<FunctionType>::type,
      true>(std::forward<FunctionType>(fn));
}

/**
 * Internal use for the macro SCOPE_SUCCESS below
 */
enum class ScopeGuardOnSuccess {};

template <typename FunctionType>
ScopeGuardForNewException<typename std::decay<FunctionType>::type, false>
operator+(ScopeGuardOnSuccess, FunctionType&& fn) {
  return ScopeGuardForNewException<
      typename std::decay<FunctionType>::type,
      false>(std::forward<FunctionType>(fn));
}

#endif // native uncaught_exception() supported

/**
 * Internal use for the macro SCOPE_EXIT below
 */
enum class ScopeGuardOnExit {};

template <typename FunctionType>
ScopeGuardImpl<typename std::decay<FunctionType>::type, true> operator+(
    detail::ScopeGuardOnExit, FunctionType&& fn) {
  return ScopeGuardImpl<typename std::decay<FunctionType>::type, true>(
      std::forward<FunctionType>(fn));
}
} // namespace detail

} // namespace folly

//  SCOPE_EXIT
//
//  Example:
//
//      /* open scope */ {
//
//        some_resource_t resource;
//        some_resource_init(resource);
//        SCOPE_EXIT { some_resource_fini(resource); };
//
//        if (!cond)
//          throw 0; // the cleanup happens at end of the scope
//        else
//          return; // the cleanup happens at end of the scope
//
//        use_some_resource(resource); // may throw; cleanup will happen
//
//      } /* close scope */
//
//  The code in the braces passed to SCOPE_EXIT executes at the end of the
//  containing scope as if the code is the content of the destructor of an
//  object instantiated at the point of the SCOPE_EXIT, where the destructor
//  reference-captures all local variables it uses.
//
//  The cleanup code - the code in the braces passed to SCOPE_EXIT - always
//  executes at the end of the scope, regardless of whether the scope exits
//  normally or erroneously as if via the throw statement.
//
//  Caution: Suitable for coroutine functions only when the cleanup code does
//  not use captured references to thread-local objects. Recall that there is
//  no assumption that coroutines resume from co-await, co-yield, or co-return
//  in the same thread as the one in which they suspend.
//
//  Caution: May not execute if the scope exits erroneously but stack unwinding
//  is skipped, or if the scope does not exit at all such as with std::abort or
//  setcontext, which fibers use.
#define SCOPE_EXIT                               \
  auto FB_ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE) = \
      ::folly::detail::ScopeGuardOnExit() + [&]() noexcept

#if defined(FOLLY_EXCEPTION_COUNT_USE_CXA_GET_GLOBALS) || \
    defined(FOLLY_EXCEPTION_COUNT_USE_GETPTD) ||          \
    defined(FOLLY_EXCEPTION_COUNT_USE_STD)

//  SCOPE_FAIL
//
//  Like SCOPE_EXIT, but the code in the braces only executes when the
//  containing scope exits erroneously, as-if via the throw statement.
//
//  May be useful in situations where the caller requests a resource where
//  initializations of the resource is multi-step and may fail.
//
//  Example:
//
//      some_resource_t resource;
//      some_resource_init(resource);
//      SCOPE_FAIL { some_resource_fini(resource); };
//
//      if (do_throw)
//        throw 0; // the cleanup happens at the end of the scope
//      else
//        return resource; // the cleanup does not happen
//
//  Warning: Not suitable for coroutine functions.
#define SCOPE_FAIL                               \
  auto FB_ANONYMOUS_VARIABLE(SCOPE_FAIL_STATE) = \
      ::folly::detail::ScopeGuardOnFail() + [&]() noexcept

//  SCOPE_SUCCESS
//
//  Like SCOPE_EXIT, but the code in the braces only executes when the
//  containing scope exits successfully, not as-if via the throw statement.
//
//  In a sense, the opposite of SCOPE_FAIL.
//
//  Example:
//
//      folly::stop_watch<> watch;
//      SCOPE_FAIL { log_failure(watch.elapsed(); };
//      SCOPE_SUCCESS { log_success(watch.elapsed(); };
//
//      if (do_throw)
//        throw 0; // the cleanup does not happen; log failure
//      else
//        return; // the cleanup happens at the end of the scope; log success
//
//  Warning: Not suitable for coroutine functions.
#define SCOPE_SUCCESS                               \
  auto FB_ANONYMOUS_VARIABLE(SCOPE_SUCCESS_STATE) = \
      ::folly::detail::ScopeGuardOnSuccess() + [&]()

#endif // native uncaught_exception() supported
