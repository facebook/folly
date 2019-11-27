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

#include <folly/experimental/coro/Concat.h>

namespace folly {
namespace coro {

template <typename Head>
detail::enable_if_async_generator_t<Head> concat(Head head) {
  return std::move(head);
}

template <typename Head, typename... Tail>
detail::enable_if_async_generator_t<Head> concat(Head head, Tail... tail) {
  using Reference = typename Head::reference;
  while (auto val = co_await head.next()) {
    co_yield std::move(val).value();
  }

  auto tailGen = concat(std::move(tail)...);
  while (auto val = co_await tailGen.next()) {
    co_yield std::move(val).value();
  }
}

} // namespace coro
} // namespace folly
