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

#include <folly/functional/Invoke.h>
#include <folly/lang/CustomizationPoint.h>

namespace folly {
namespace coro {

//  co_invoke
//
//  This utility callable is a safe way to instantiate a coroutine using a
//  coroutine callable. It guarantees that the callable and the arguments
//  outlive the coroutine which invocation returns. Otherwise, the callable
//  and the arguments are not safe to be used within the coroutine body.
//
//  For example, if the callable is a lambda with captures, the captures would
//  not otherwise be safe to use in the coroutine body without using co_invoke.
//
//  Models invoke for any callable which returns a coroutine type which declares
//  support for co_invoke, including:
//    * AsyncGenerator<...>
//    * Task<...>
//
//  Like invoke in that the callable is invoked with the cvref-qual with which
//  it is passed to co_invoke and the arguments are passed with the cvref-quals
//  with which they were passed to co_invoke.
//
//  Different from invoke in that the callable and all arguments are decay-
//  copied and it is the copies that are held for the lifetime of the coroutine
//  and used in the invocation, whereas invoke merely forwards them directly in
//  the invocation without first constructing any values copied from them.
//
//  Example:
//
//      auto gen = co_invoke([range]() -> AsyncGenerator<T> {
//        for (auto value : range) {
//          co_yield co_await make<T>(value);
//        }
//      });
//
//  Example:
//
//      auto task = co_invoke([name, dob]() -> Task<T> {
//        co_return co_await make<T>(name, dob);
//      });
//
//  A word of caution. The callable and each argument is decay-copied by the
//  customizations. No effort is made to coalesce copies when copies would have
//  been made with direct invocation.
//
//      string name = "foobar"; // will be copied twice
//      auto task = co_invoke([](string n) -> Task<T> {
//        co_return co_await make<T>(n);
//      }, name); // passed as &
//
//      string name = "foobar"; // will be moved twice and copied zero times
//      auto task = co_invoke([](string n) -> Task<T> {
//        co_return co_await make<T>(n);
//      }, std::move(name)); // passed as &&
struct co_invoke_fn {
  template <typename F, typename... A>
  FOLLY_ERASE constexpr auto operator()(F&& f, A&&... a) const
      noexcept(noexcept(tag_invoke(
          tag<co_invoke_fn>,
          tag<invoke_result_t<F, A...>, F, A...>,
          static_cast<F&&>(f),
          static_cast<A&&>(a)...)))
          -> decltype(tag_invoke(
              tag<co_invoke_fn>,
              tag<invoke_result_t<F, A...>, F, A...>,
              static_cast<F&&>(f),
              static_cast<A&&>(a)...)) {
    return tag_invoke(
        tag<co_invoke_fn>,
        tag<invoke_result_t<F, A...>, F, A...>,
        static_cast<F&&>(f),
        static_cast<A&&>(a)...);
  }
};
FOLLY_DEFINE_CPO(co_invoke_fn, co_invoke)

} // namespace coro
} // namespace folly
