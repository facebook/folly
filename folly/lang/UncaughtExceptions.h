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

#include <exception>

#include <folly/CppAttributes.h>

namespace folly {

namespace detail {
[[FOLLY_ATTR_PURE]] int uncaught_exceptions_() noexcept;
}

#if __cpp_lib_uncaught_exceptions >= 201411L || defined(_CPPLIB_VER)

/* using override */ using std::uncaught_exceptions;

#else

//  mimic: std::uncaught_exceptions, c++17
[[FOLLY_ATTR_PURE]] inline int uncaught_exceptions() noexcept {
  return detail::uncaught_exceptions_();
}

#endif

} // namespace folly
