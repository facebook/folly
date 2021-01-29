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

#include <type_traits>
#include <utility>

#include <folly/functional/Invoke.h>
#include <folly/synchronization/DelayedInit.h>

namespace folly {

/*
 * ConcurrentLazy is for thread-safe, delayed initialization of a value. This
 * combines the benefits of both `folly::Lazy` and `folly::DelayedInit` to
 * compute the value, once, at access time.
 *
 * There are a few differences between the non-concurrent Lazy, most notably:
 *
 *   - this only safely initializes the value; thread-safety of the underlying
 *     value is left up to the caller.
 *   - the underlying types are not copyable or moveable, which means that this
 *     type is also not copyable or moveable.
 *
 * Otherwise, all design considerations from `folly::Lazy` are reflected here.
 */

template <class Func>
struct ConcurrentLazy {
  using result_type = invoke_result_t<Func>;

  static_assert(
      !std::is_const<Func>::value,
      "Func should not be a const-qualified type");
  static_assert(
      !std::is_reference<Func>::value,
      "Func should not be a reference type");

  explicit ConcurrentLazy(Func&& f) : func_(std::move(f)) {}
  explicit ConcurrentLazy(const Func& f) : func_(f) {}

  const result_type& operator()() const {
    return value_.try_emplace_with(func_);
  }

  result_type& operator()() { return value_.try_emplace_with(func_); }

 private:
  folly::DelayedInit<result_type> value_;
  mutable Func func_;
};

template <class Func>
auto concurrent_lazy(Func&& fun) {
  return ConcurrentLazy<remove_cvref_t<Func>>(std::forward<Func>(fun));
}

} // namespace folly
