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

#ifndef FOLLY_EXCEPTIONWRAPPER_H
#define FOLLY_EXCEPTIONWRAPPER_H

#include <cassert>
#include <exception>
#include <memory>
#include "folly/detail/ExceptionWrapper.h"

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
 * users that prefer convenience. exception_wrapper is flexible enough to accept
 * any std::exception. For performance sensitive applications, exception_wrapper
 * exposes a get() function. These users can use dynamic_cast to retrieve
 * desired derived types (hence the decision to limit usage to just
 * std::exception instead of void*).
 *
 * Example usage:
 *
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
 *   auto ep = globalExceptionWrapper.get();
 *   if (ep) {
 *     auto faceplant = dynamic_cast<FacePlantException*>(ep);
 *     if (faceplant) {
 *       LOG(ERROR) << "FACEPLANT";
 *     } else {
 *       auto failwhale = dynamic_cast<FailWhaleException*>(ep);
 *       if (failwhale) {
 *         LOG(ERROR) << "FAILWHALE!";
 *       }
 *     }
 *   }
 * }
 *
 */
class exception_wrapper {
 public:
  exception_wrapper() : throwfn_(nullptr) { }

  void throwException() const {
    if (throwfn_) {
      throwfn_(item_.get());
    }
  }

  std::exception* get() { return item_.get(); }
  const std::exception* get() const { return item_.get(); }

  std::exception* operator->() { return get(); }
  const std::exception* operator->() const { return get(); }

  std::exception& operator*() { assert(get()); return *get(); }
  const std::exception& operator*() const { assert(get()); return *get(); }

  explicit operator bool() const { return get(); }

 private:
  std::shared_ptr<std::exception> item_;
  void (*throwfn_)(std::exception*);

  template <class T, class... Args>
  friend exception_wrapper make_exception_wrapper(Args&&... args);
};

template <class T, class... Args>
exception_wrapper make_exception_wrapper(Args&&... args) {
  exception_wrapper ew;
  ew.item_ = std::make_shared<T>(std::forward<Args>(args)...);
  ew.throwfn_ = folly::detail::Thrower<T>::doThrow;
  return ew;
}

}
#endif
