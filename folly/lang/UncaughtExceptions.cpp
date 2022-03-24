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

#include <folly/lang/UncaughtExceptions.h>

#include <folly/functional/Invoke.h>

#if defined(__GLIBCXX__) || defined(_LIBCPP_VERSION)

namespace __cxxabiv1 {
struct __cxa_eh_globals {
  void* caught_exceptions_;
  unsigned int uncaught_exceptions_;
};
extern "C" __cxa_eh_globals* __cxa_get_globals();
} // namespace __cxxabiv1

#endif

namespace {
FOLLY_CREATE_FREE_INVOKER( // must be outside namespace folly
    folly_detail_invoke_uncaught_exceptions_,
    uncaught_exceptions,
    std);
}

namespace folly {

namespace detail {

struct fallback_uncaught_exceptions_fn_ {
  int operator()() const noexcept {
#if defined(__GLIBCXX__) || defined(_LIBCPP_VERSION)
    return __cxxabiv1::__cxa_get_globals()->uncaught_exceptions_;
#endif
#if defined(_CPPLIB_VER)
    return std::uncaught_exceptions();
#endif
  }
};

using uncaught_exceptions_fn_ = conditional_t<
    is_invocable_v<folly_detail_invoke_uncaught_exceptions_>,
    folly_detail_invoke_uncaught_exceptions_,
    fallback_uncaught_exceptions_fn_>;

int uncaught_exceptions_() noexcept {
  return detail::uncaught_exceptions_fn_{}();
}

} // namespace detail

} // namespace folly
