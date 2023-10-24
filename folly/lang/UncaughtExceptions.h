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

#include <cassert>
#include <exception>

#include <folly/CppAttributes.h>
#include <folly/Likely.h>
#include <folly/Portability.h>

namespace folly {

namespace detail {

unsigned int* uncaught_exceptions_ptr() noexcept;

} // namespace detail

//  uncaught_exceptions
//
//  An accelerated version of std::uncaught_exceptions.
//
//  mimic: std::uncaught_exceptions, c++17
[[FOLLY_ATTR_PURE]] FOLLY_EXPORT FOLLY_ALWAYS_INLINE int
uncaught_exceptions() noexcept {
#if defined(_CPPLIB_VER)
  return std::uncaught_exceptions();
#elif defined(__has_feature) && !FOLLY_HAS_FEATURE(cxx_thread_local)
  return std::uncaught_exceptions();
#else
  thread_local unsigned int* ct;
  return FOLLY_LIKELY(!!ct) ? *ct : *(ct = detail::uncaught_exceptions_ptr());
#endif
}

} // namespace folly
