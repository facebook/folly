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
///
/// WARNING: `auto&& ref = co_await or_unwind(rvalueFn())` dangles; search
/// `result.md` for "LLVM issue #177023".  Safe: `auto val = ...`

#if FOLLY_HAS_RESULT

namespace folly {

// Making these `final` makes `unsafe_mover` simpler due to no slicing risk.

// `co_await or_unwind(valueOnlyResFn())` returns rvalue reference
template <typename T>
class [[nodiscard]] or_unwind<value_only_result<T>&&> final
    : public detail::result_or_unwind<value_only_result<T>&&> {
  using detail::result_or_unwind<value_only_result<T>&&>::result_or_unwind;
};
template <typename T>
or_unwind(value_only_result<T>&&) -> or_unwind<value_only_result<T>&&>;

// `co_await or_unwind(res)` returns lvalue reference
template <typename T>
class [[nodiscard]] or_unwind<value_only_result<T>&> final
    : public detail::result_or_unwind<value_only_result<T>&> {
  using detail::result_or_unwind<value_only_result<T>&>::result_or_unwind;
};
template <typename T>
or_unwind(value_only_result<T>&) -> or_unwind<value_only_result<T>&>;

// `co_await or_unwind(std::as_const(res))` returns lvalue reference to const
template <typename T>
class [[nodiscard]] or_unwind<const value_only_result<T>&> final
    : public detail::result_or_unwind<const value_only_result<T>&> {
  using detail::result_or_unwind<const value_only_result<T>&>::result_or_unwind;
};
template <typename T>
or_unwind(const value_only_result<T>&)
    -> or_unwind<const value_only_result<T>&>;

/// `or_unwind_owning(r)` takes ownership of `r`, while `co_await` returns it.
///
/// Moves the result into the awaitable, unlike `or_unwind` which holds a ref:
///   auto my_unwind() { return or_unwind_owning{value_only_result<T>{...}}; }
/// With `or_unwind`, this would have been a use-after-stack error.
template <typename T>
class [[nodiscard]] or_unwind_owning<value_only_result<T>> final
    : public detail::result_or_unwind_owning<value_only_result<T>> {
  using detail::result_or_unwind_owning<
      value_only_result<T>>::result_or_unwind_owning;
};
template <typename T>
or_unwind_owning(value_only_result<T>)
    -> or_unwind_owning<value_only_result<T>>;

} // namespace folly

#endif // FOLLY_HAS_RESULT
