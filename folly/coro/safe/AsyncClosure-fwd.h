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

namespace folly::coro {

class AsyncObjectTag;

namespace detail {
template <auto>
auto bind_captures_to_closure(auto&&, auto);
} // namespace detail

// Tag type used by `async_closure` to trigger cleanup of `capture`s that
// have an `co_cleanup(async_closure_private_t)` overload.
class async_closure_private_t {
 protected:
  friend class AsyncObjectTag;
  template <auto>
  friend auto detail::bind_captures_to_closure(auto&&, auto);

  async_closure_private_t() = default;
};

} // namespace folly::coro
