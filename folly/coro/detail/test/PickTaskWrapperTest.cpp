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

// (unsafe, await now) -> now_task
static_assert(
    std::is_same_v<
        now_task<int>,
        detail::pick_task_wrapper<
            int,
            safe_alias::unsafe,
            /*await now*/ true>>);
static_assert(
    std::is_same_v<
        now_task_with_executor<int>,
        detail::pick_task_with_executor_wrapper<
            int,
            safe_alias::unsafe,
            /*await now*/ true>>);

// (maybe_value, movable) -> value_task
static_assert(
    std::is_same_v<
        value_task<int>,
        detail::pick_task_wrapper<
            int,
            safe_alias::maybe_value,
            /*await now*/ false>>);
static_assert(
    std::is_same_v<
        safe_task_with_executor<safe_alias::maybe_value, int>,
        detail::pick_task_with_executor_wrapper<
            int,
            safe_alias::maybe_value,
            /*await now*/ false>>);

// (co_cleanup_safe_ref, movable, wrapper) ->
// as_noexcept<co_cleanup_safe_task<>>
static_assert(
    std::is_same_v<
        as_noexcept<co_cleanup_safe_task<int>, terminateOnCancel>,
        detail::pick_task_wrapper<
            int,
            safe_alias::co_cleanup_safe_ref,
            /*await now*/ false,
            detail::as_noexcept_with_cancel_cfg<terminateOnCancel>>>);
static_assert(
    std::is_same_v<
        as_noexcept<
            safe_task_with_executor<safe_alias::co_cleanup_safe_ref, int>,
            terminateOnCancel>,
        detail::pick_task_with_executor_wrapper<
            int,
            safe_alias::co_cleanup_safe_ref,
            /*await now*/ false,
            detail::as_noexcept_with_cancel_cfg<terminateOnCancel>>>);

} // namespace folly::coro

#endif
