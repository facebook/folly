/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/CancellationToken.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/lang/CustomizationPoint.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

namespace detail {
namespace adl {

// Default implementation that does not hook the cancellation token.
// Types must opt-in to hooking cancellation by customising this function.
template <typename Awaitable>
Awaitable&& co_withCancellation(
    const folly::CancellationToken&, Awaitable&& awaitable) noexcept {
  return (Awaitable &&) awaitable;
}

struct WithCancellationFunction {
  template <typename Awaitable>
  auto operator()(
      const folly::CancellationToken& cancelToken, Awaitable&& awaitable) const
      noexcept(
          noexcept(co_withCancellation(cancelToken, (Awaitable &&) awaitable)))
          -> decltype(
              co_withCancellation(cancelToken, (Awaitable &&) awaitable)) {
    return co_withCancellation(cancelToken, (Awaitable &&) awaitable);
  }

  template <typename Awaitable>
  auto operator()(folly::CancellationToken&& cancelToken, Awaitable&& awaitable)
      const noexcept(noexcept(co_withCancellation(
          std::move(cancelToken), (Awaitable &&) awaitable)))
          -> decltype(co_withCancellation(
              std::move(cancelToken), (Awaitable &&) awaitable)) {
    return co_withCancellation(
        std::move(cancelToken), (Awaitable &&) awaitable);
  }
};
} // namespace adl
} // namespace detail

FOLLY_DEFINE_CPO(detail::adl::WithCancellationFunction, co_withCancellation)

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES
