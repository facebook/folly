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

#include <folly/result/coro.h>
#include <folly/result/value_only_result.h>

/// See `value_only_result.h` for why this type exists.
/// See `coro.h` for the `or_unwind` docs`.

#if FOLLY_HAS_RESULT

namespace folly {

// Making these `final` makes `unsafe_mover` simpler due to no slicing risk.

template <typename T>
class or_unwind<value_only_result<T>&&> final
    : public detail::or_unwind_base<value_only_result<T>&&> {
  using detail::or_unwind_base<value_only_result<T>&&>::or_unwind_base;
};
template <typename T>
or_unwind(value_only_result<T>&&) -> or_unwind<value_only_result<T>&&>;

template <typename T>
class or_unwind<value_only_result<T>&> final
    : public detail::or_unwind_base<value_only_result<T>&> {
  using detail::or_unwind_base<value_only_result<T>&>::or_unwind_base;
};
template <typename T>
or_unwind(value_only_result<T>&) -> or_unwind<value_only_result<T>&>;

template <typename T>
class or_unwind<const value_only_result<T>&> final
    : public detail::or_unwind_base<const value_only_result<T>&> {
  using detail::or_unwind_base<const value_only_result<T>&>::or_unwind_base;
};
template <typename T>
or_unwind(const value_only_result<T>&)
    -> or_unwind<const value_only_result<T>&>;

} // namespace folly

#endif // FOLLY_HAS_RESULT
