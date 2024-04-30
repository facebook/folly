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

#include <folly/experimental/coro/Retry.h>

#include <stdexcept>

#include "fmt/chrono.h"
#include "fmt/format.h"

#include <folly/Portability.h>
#include <folly/futures/detail/Types.h>
#include <folly/lang/Exception.h>

#if FOLLY_HAS_COROUTINES

using namespace folly::coro;

[[noreturn]]
FOLLY_NOINLINE void detail::throwExceptionForInvalidBackoffArgs(
    const folly::Duration& minBackoff, const folly::Duration& maxBackoff) {
  folly::throw_exception<std::invalid_argument>(fmt::format(
      "minBackoff ({}) must be <= maxBackoff ({})", minBackoff, maxBackoff));
}

#endif // FOLLY_HAS_COROUTINES
