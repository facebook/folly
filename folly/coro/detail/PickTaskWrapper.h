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

#include <folly/Portability.h>
#include <folly/lang/SafeAlias-fwd.h>

/// For functions-of-coros, like `timeout()` or `collectAll()`, we want the
/// outer coro to be able to pass through these attributes of the inner coro:
///  - `must_await_immediately_v`
///  - `noexcept_awaitable_v`
///  - `safe_alias_of_v`
///
/// Variation along these dimensions is currently implemented as a zoo of coro
/// templates and wrappers -- `Task` aka `UnsafeMovableTask`, `NowTask`,
/// `SafeTask`, `AsNoexcept<InnerTask>`.  The type function `PickTaskWrapper`
/// provides common logic for picking a task type with the given attributes.

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

template <typename T>
class Task;
template <typename T>
class TaskWithExecutor;

template <safe_alias, typename>
class SafeTask;
template <safe_alias, typename>
class SafeTaskWithExecutor;

template <typename T>
class NowTask;
template <typename T>
class NowTaskWithExecutor;

template <typename, auto>
class AsNoexcept;

namespace detail {

struct identity_metafunction {
  template <typename T>
  using apply = T;
};

template <safe_alias, bool /*must await immediately (now)*/>
struct PickTaskWrapperImpl;

#if FOLLY_HAS_IMMOVABLE_COROUTINES

template <>
struct PickTaskWrapperImpl<safe_alias::unsafe, /*await now*/ false> {
  template <typename T>
  using Task = Task<T>;
  template <typename T>
  using TaskWithExecutor = TaskWithExecutor<T>;
};

template <>
struct PickTaskWrapperImpl<safe_alias::unsafe, /*await now*/ true> {
  template <typename T>
  using Task = NowTask<T>;
  template <typename T>
  using TaskWithExecutor = NowTaskWithExecutor<T>;
};

// These `SafeTask` types are immovable, so "await now" doesn't matter.
template <safe_alias Safety, bool AwaitNow>
  requires(Safety < safe_alias::closure_min_arg_safety)
struct PickTaskWrapperImpl<Safety, AwaitNow> {
  template <typename T>
  using Task = SafeTask<Safety, T>;
  template <typename T>
  using TaskWithExecutor = SafeTaskWithExecutor<Safety, T>;
};

template <safe_alias Safety>
  requires(Safety >= safe_alias::closure_min_arg_safety)
// Future: There is no principled reason we can't have must-await-immediately
// `SafeTask`s with these higher safety levels, but supporting that cleanly
// would require reorganizing the `folly/coro` task-wrapper implementations. Two
// possible approaches are:
//  - `NowTask<T> = AwaitNow<Task<T>>`
//  - Roll up `NowTask` and `SafeTask` into something like `BasicTask<T, Cfg>`,
//    where `Cfg` captures both safety & immediate-awaitability.
struct PickTaskWrapperImpl<Safety, /*await now*/ false> {
  template <typename T>
  using Task = SafeTask<Safety, T>;
  template <typename T>
  using TaskWithExecutor = SafeTaskWithExecutor<Safety, T>;
};

#else // no FOLLY_HAS_IMMOVABLE_COROUTINES

// This fallback is required because `coro::Future<SafeType>` is safe and is
// available on earlier build systems.  We have no choice but to emit `Task`.
template <safe_alias Safety>
struct PickTaskWrapperImpl<Safety, /*await now*/ false> {
  template <typename T>
  using Task = Task<T>;
  template <typename T>
  using TaskWithExecutor = TaskWithExecutor<T>;
};

#endif // FOLLY_HAS_IMMOVABLE_COROUTINES

// Pass this as `AddWrapperMetaFn` to `PickTaskWrapper` to add `AsNoexcept`.
template <auto CancelCfg>
struct AsNoexceptWithCancelCfg {
  template <typename T>
  using apply = AsNoexcept<T, CancelCfg>;
};

template <
    typename T,
    safe_alias Safety,
    bool MustAwaitImmediately,
    typename AddWrapperMetaFn = identity_metafunction>
using PickTaskWrapper = typename AddWrapperMetaFn::template apply<
    typename PickTaskWrapperImpl<Safety, MustAwaitImmediately>::template Task<
        T>>;

template <
    typename T,
    safe_alias Safety,
    bool MustAwaitImmediately,
    typename AddWrapperMetaFn = identity_metafunction>
using PickTaskWithExecutorWrapper = typename AddWrapperMetaFn::template apply<
    typename PickTaskWrapperImpl<Safety, MustAwaitImmediately>::
        template TaskWithExecutor<T>>;

} // namespace detail
} // namespace folly::coro

#endif
