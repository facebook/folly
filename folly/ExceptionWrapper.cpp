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

#include <folly/ExceptionWrapper.h>

#include <iostream>

#include <folly/GLog.h>

namespace folly {

[[noreturn]] void exception_wrapper::onNoExceptionError(
    char const* const name) {
  std::ios_base::Init ioinit_; // ensure std::cerr is alive
  std::cerr << "Cannot use `" << name
            << "` with an empty folly::exception_wrapper" << std::endl;
  std::terminate();
}

fbstring exceptionStr(exception_wrapper const& ew) {
  return ew.what();
}

} // namespace folly
