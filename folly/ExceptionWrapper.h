/*
 * Copyright 2016 Facebook, Inc.
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

#include <exception>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <folly/ExceptionString.h>
#include <folly/FBString.h>

namespace folly {

/*
 * Throwing exceptions can be a convenient way to handle errors. Storing
 * exceptions in an exception_ptr makes it easy to handle exceptions in a
 * different thread or at a later time. exception_ptr can also be used in a very
 * generic result/exception wrapper.
 *
 * However, there are some issues with throwing exceptions and
 * std::exception_ptr. These issues revolve around throw being expensive,
 * particularly in a multithreaded environment (see
 * ExceptionWrapperBenchmark.cpp).
 *
 * Imagine we have a library that has an API which returns a result/exception
 * wrapper. Let's consider some approaches for implementing this wrapper.
 * First, we could store a std::exception. This approach loses the derived
 * exception type, which can make exception handling more difficult for users
 * that prefer rethrowing the exception. We could use a folly::dynamic for every
 * possible type of exception. This is not very flexible - adding new types of
 * exceptions requires a change to the result/exception wrapper. We could use an
 * exception_ptr. However, constructing an exception_ptr as well as accessing
 * the error requires a call to throw. That means that there will be two calls
 * to throw in order to process the exception. For performance sensitive
 * applications, this may be unacceptable.
 *
 * exception_wrapper is designed to handle exception management for both
 * convenience and high performance use cases. make_exception_wrapper is
 * templated on derived type, allowing us to rethrow the exception properly for
 * users that prefer convenience. These explicitly named exception types can
 * therefore be handled without any peformance penalty.  exception_wrapper is
 * also flexible enough to accept any type. If a caught exception is not of an
 * explicitly named type, then std::exception_ptr is used to preserve the
 * exception state. For performance sensitive applications, the accessor methods
 * can test or extract a pointer to a specific exception type with very little
 * overhead.
 *
 * \par Example usage:
 * \par
 * \code
 * exception_wrapper globalExceptionWrapper;
 *
 * // Thread1
 * void doSomethingCrazy() {
 *   int rc = doSomethingCrazyWithLameReturnCodes();
 *   if (rc == NAILED_IT) {
 *     globalExceptionWrapper = exception_wrapper();
 *   } else if (rc == FACE_PLANT) {
 *     globalExceptionWrapper = make_exception_wrapper<FacePlantException>();
 *   } else if (rc == FAIL_WHALE) {
 *     globalExceptionWrapper = make_exception_wrapper<FailWhaleException>();
 *   }
 * }
 *
 * // Thread2: Exceptions are ok!
 * void processResult() {
 *   try {
 *     globalExceptionWrapper.throwException();
 *   } catch (const FacePlantException& e) {
 *     LOG(ERROR) << "FACEPLANT!";
 *   } catch (const FailWhaleException& e) {
 *     LOG(ERROR) << "FAILWHALE!";
 *   }
 * }
 *
 * // Thread2: Exceptions are bad!
 * void processResult() {
 *   globalExceptionWrapper.with_exception(
 *       [&](FacePlantException& faceplant) {
 *         LOG(ERROR) << "FACEPLANT";
 *       }) ||
 *   globalExceptionWrapper.with_exception(
 *       [&](FailWhaleException& failwhale) {
 *         LOG(ERROR) << "FAILWHALE!";
 *       }) ||
 *   LOG(FATAL) << "Unrecognized exception";
 * }
 * \endcode
 *
 */
class exception_wrapper {
 private:
  template <typename Ex>
  struct optimize;

 public:
  exception_wrapper() = default;

  // Implicitly construct an exception_wrapper from a qualifying exception.
  // See the optimize struct for details.
  template <typename Ex, typename =
    typename std::enable_if<optimize<typename std::decay<Ex>::type>::value>
    ::type>
  /* implicit */ exception_wrapper(Ex&& exn) {
    typedef typename std::decay<Ex>::type DEx;
    assign_sptr(std::make_shared<DEx>(std::forward<Ex>(exn)));
  }

  // The following two constructors are meant to emulate the behavior of
  // try_and_catch in performance sensitive code as well as to be flexible
  // enough to wrap exceptions of unknown type. There is an overload that
  // takes an exception reference so that the wrapper can extract and store
  // the exception's type and what() when possible.
  //
  // The canonical use case is to construct an all-catching exception wrapper
  // with minimal overhead like so:
  //
  //   try {
  //     // some throwing code
  //   } catch (const std::exception& e) {
  //     // won't lose e's type and what()
  //     exception_wrapper ew{std::current_exception(), e};
  //   } catch (...) {
  //     // everything else
  //     exception_wrapper ew{std::current_exception()};
  //   }
  //
  // try_and_catch is cleaner and preferable. Use it unless you're sure you need
  // something like this instead.
  template <typename Ex>
  explicit exception_wrapper(std::exception_ptr eptr, Ex& exn) {
    assign_eptr(eptr, exn);
  }

  explicit exception_wrapper(std::exception_ptr eptr) {
    assign_eptr(eptr);
  }

  // If the exception_wrapper does not contain an exception, std::terminate()
  // is invoked to assure the [[noreturn]] behaviour.
  [[noreturn]] void throwException() const;

  explicit operator bool() const {
    return item_ || eptr_;
  }

  // This implementation is similar to std::exception_ptr's implementation
  // where two exception_wrappers are equal when the address in the underlying
  // reference field both point to the same exception object.  The reference
  // field remains the same when the exception_wrapper is copied or when
  // the exception_wrapper is "rethrown".
  bool operator==(const exception_wrapper& a) const {
    if (item_) {
      return a.item_ && item_.get() == a.item_.get();
    } else {
      return eptr_ == a.eptr_;
    }
  }

  bool operator!=(const exception_wrapper& a) const {
    return !(*this == a);
  }

  // This will return a non-nullptr only if the exception is held as a
  // copy.  It is the only interface which will distinguish between an
  // exception held this way, and by exception_ptr.  You probably
  // shouldn't use it at all.
  std::exception* getCopied() { return item_.get(); }
  const std::exception* getCopied() const { return item_.get(); }

  fbstring what() const;
  fbstring class_name() const;

  template <class Ex>
  bool is_compatible_with() const {
    return with_exception<Ex>([](const Ex&) {});
  }

  template <class F>
  bool with_exception(F&& f) {
    using arg_type = typename functor_traits<F>::arg_type_decayed;
    return with_exception<arg_type>(std::forward<F>(f));
  }

  template <class F>
  bool with_exception(F&& f) const {
    using arg_type = typename functor_traits<F>::arg_type_decayed;
    return with_exception<arg_type>(std::forward<F>(f));
  }

  // If this exception wrapper wraps an exception of type Ex, with_exception
  // will call f with the wrapped exception as an argument and return true, and
  // will otherwise return false.
  template <class Ex, class F>
  bool with_exception(F f) {
    return with_exception1<typename std::decay<Ex>::type>(f, this);
  }

  // Const overload
  template <class Ex, class F>
  bool with_exception(F f) const {
    return with_exception1<typename std::decay<Ex>::type>(f, this);
  }

  std::exception_ptr getExceptionPtr() const {
    if (eptr_) {
      return eptr_;
    }

    try {
      if (*this) {
        throwException();
      }
    } catch (...) {
      return std::current_exception();
    }
    return std::exception_ptr();
  }

 private:
  template <typename Ex>
  struct optimize {
    static const bool value =
      std::is_base_of<std::exception, Ex>::value &&
      std::is_copy_assignable<Ex>::value &&
      !std::is_abstract<Ex>::value;
  };

  template <typename Ex>
  void assign_sptr(std::shared_ptr<Ex> sptr) {
    this->item_ = std::move(sptr);
    this->throwfn_ = Thrower<Ex>::doThrow;
  }

  template <typename Ex>
  void assign_eptr(std::exception_ptr eptr, Ex& e) {
    this->eptr_ = eptr;
    this->estr_ = exceptionStr(e).toStdString();
    this->ename_ = demangle(typeid(e)).toStdString();
  }

  void assign_eptr(std::exception_ptr eptr) {
    this->eptr_ = eptr;
  }

  // Optimized case: if we know what type the exception is, we can
  // store a copy of the concrete type, and a helper function so we
  // can rethrow it.
  std::shared_ptr<std::exception> item_;
  void (*throwfn_)(std::exception&){nullptr};
  // Fallback case: store the library wrapper, which is less efficient
  // but gets the job done.  Also store exceptionPtr() the name of the
  // exception type, so we can at least get those back out without
  // having to rethrow.
  std::exception_ptr eptr_;
  std::string estr_;
  std::string ename_;

  template <class T, class... Args>
  friend exception_wrapper make_exception_wrapper(Args&&... args);

 private:
  template <typename F>
  struct functor_traits {
    template <typename T>
    struct impl;
    template <typename C, typename R, typename A>
    struct impl<R(C::*)(A)> { using arg_type = A; };
    template <typename C, typename R, typename A>
    struct impl<R(C::*)(A) const> { using arg_type = A; };
    using functor_decayed = typename std::decay<F>::type;
    using functor_op = decltype(&functor_decayed::operator());
    using arg_type = typename impl<functor_op>::arg_type;
    using arg_type_decayed = typename std::decay<arg_type>::type;
  };

  template <class T>
  class Thrower {
   public:
    static void doThrow(std::exception& obj) {
      throw static_cast<T&>(obj);
    }
  };

  template <typename T>
  using is_exception_ = std::is_base_of<std::exception, T>;

  template <bool V, typename T, typename F>
  using conditional_t_ = typename std::conditional<V, T, F>::type;

  template <typename T, typename F>
  static typename std::enable_if<is_exception_<T>::value, T*>::type
  try_dynamic_cast_exception(F* from) {
    return dynamic_cast<T*>(from);
  }
  template <typename T, typename F>
  static typename std::enable_if<!is_exception_<T>::value, T*>::type
  try_dynamic_cast_exception(F*) {
    return nullptr;
  }

  // What makes this useful is that T can be exception_wrapper* or
  // const exception_wrapper*, and the compiler will use the
  // instantiation which works with F.
  template <class Ex, class F, class T>
  static bool with_exception1(F f, T* that) {
    using CEx = conditional_t_<std::is_const<T>::value, const Ex, Ex>;
    if (is_exception_<Ex>::value && that->item_) {
      if (auto ex = try_dynamic_cast_exception<CEx>(that->item_.get())) {
        f(*ex);
        return true;
      }
    } else if (that->eptr_) {
      try {
        std::rethrow_exception(that->eptr_);
      } catch (CEx& e) {
        f(e);
        return true;
      } catch (...) {
        // fall through
      }
    }
    return false;
  }
};

template <class T, class... Args>
exception_wrapper make_exception_wrapper(Args&&... args) {
  exception_wrapper ew;
  ew.assign_sptr(std::make_shared<T>(std::forward<Args>(args)...));
  return ew;
}

// For consistency with exceptionStr() functions in ExceptionString.h
fbstring exceptionStr(const exception_wrapper& ew);

/*
 * try_and_catch is a simple replacement for try {} catch(){} that allows you to
 * specify which derived exceptions you would like to catch and store in an
 * exception_wrapper.
 *
 * Because we cannot build an equivalent of std::current_exception(), we need
 * to catch every derived exception that we are interested in catching.
 *
 * Exceptions should be listed in the reverse order that you would write your
 * catch statements (that is, std::exception& should be first).
 *
 * NOTE: Although implemented as a derived class (for syntactic delight), don't
 * be confused - you should not pass around try_and_catch objects!
 *
 * Example Usage:
 *
 * // This catches my runtime_error and if I call throwException() on ew, it
 * // will throw a runtime_error
 * auto ew = folly::try_and_catch<std::exception, std::runtime_error>([=]() {
 *   if (badThingHappens()) {
 *     throw std::runtime_error("ZOMG!");
 *   }
 * });
 *
 * // This will catch the exception and if I call throwException() on ew, it
 * // will throw a std::exception
 * auto ew = folly::try_and_catch<std::exception, std::runtime_error>([=]() {
 *   if (badThingHappens()) {
 *     throw std::exception();
 *   }
 * });
 *
 * // This will not catch the exception and it will be thrown.
 * auto ew = folly::try_and_catch<std::runtime_error>([=]() {
 *   if (badThingHappens()) {
 *     throw std::exception();
 *   }
 * });
 */

namespace try_and_catch_detail {

template <bool V, typename T = void>
using enable_if_t_ = typename std::enable_if<V, T>::type;

template <typename... Args>
using is_wrap_ctor = std::is_constructible<exception_wrapper, Args...>;

template <typename Ex>
inline enable_if_t_<!is_wrap_ctor<Ex&>::value, exception_wrapper> make(Ex& ex) {
  return exception_wrapper(std::current_exception(), ex);
}

template <typename Ex>
inline enable_if_t_<is_wrap_ctor<Ex&>::value, exception_wrapper> make(Ex& ex) {
  return typeid(Ex&) == typeid(ex)
      ? exception_wrapper(ex)
      : exception_wrapper(std::current_exception(), ex);
}

template <typename F>
inline exception_wrapper impl(F&& f) {
  return (f(), exception_wrapper());
}

template <typename F, typename Ex, typename... Exs>
inline exception_wrapper impl(F&& f) {
  try {
    return impl<F, Exs...>(std::forward<F>(f));
  } catch (Ex& ex) {
    return make(ex);
  }
}
} // try_and_catch_detail

template <typename... Exceptions, typename F>
exception_wrapper try_and_catch(F&& fn) {
  return try_and_catch_detail::impl<F, Exceptions...>(std::forward<F>(fn));
}
} // folly
