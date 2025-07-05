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

#include <folly/Portability.h>

#include <folly/coro/Noexcept.h>
#include <folly/coro/detail/PickTaskWrapper.h>
#include <folly/coro/safe/NowTask.h>
#include <folly/coro/safe/SafeTask.h>
#include <folly/portability/GTest.h>

#if FOLLY_HAS_COROUTINES

namespace folly::coro {

TEST(PickTaskWrapperTest, AllTestsAreStatic) {}

// (unsafe, await now) -> NowTask
static_assert(
    std::is_same_v<
        NowTask<int>,
        detail::PickTaskWrapper<
            int,
            safe_alias::unsafe,
            /*await now*/ true>>);
static_assert(
    std::is_same_v<
        NowTaskWithExecutor<int>,
        detail::PickTaskWithExecutorWrapper<
            int,
            safe_alias::unsafe,
            /*await now*/ true>>);

// (maybe_value, movable) -> ValueTask
static_assert(
    std::is_same_v<
        ValueTask<int>,
        detail::PickTaskWrapper<
            int,
            safe_alias::maybe_value,
            /*await now*/ false>>);
static_assert(
    std::is_same_v<
        SafeTaskWithExecutor<safe_alias::maybe_value, int>,
        detail::PickTaskWithExecutorWrapper<
            int,
            safe_alias::maybe_value,
            /*await now*/ false>>);

// (co_cleanup_safe_ref, movable, wrapper) -> AsNoexcept<CoCleanupSafeTask<>>
static_assert(
    std::is_same_v<
        AsNoexcept<CoCleanupSafeTask<int>, terminateOnCancel>,
        detail::PickTaskWrapper<
            int,
            safe_alias::co_cleanup_safe_ref,
            /*await now*/ false,
            detail::AsNoexceptWithCancelCfg<terminateOnCancel>>>);
static_assert(
    std::is_same_v<
        AsNoexcept<
            SafeTaskWithExecutor<safe_alias::co_cleanup_safe_ref, int>,
            terminateOnCancel>,
        detail::PickTaskWithExecutorWrapper<
            int,
            safe_alias::co_cleanup_safe_ref,
            /*await now*/ false,
            detail::AsNoexceptWithCancelCfg<terminateOnCancel>>>);

} // namespace folly::coro

#endif
