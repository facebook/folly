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

class exception_wrapper {
 public:
  exception_wrapper() : throwfn_(nullptr) { }

  void throwException() {
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
