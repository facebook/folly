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

#include <cstdint>
#include <optional>
#include <utility>

#include <folly/coro/Task.h>
#include <folly/coro/detail/CurrentAsyncFrame.h>
#include <folly/tracing/AsyncStack.h>

#if FOLLY_HAS_COROUTINES && FOLLY_HAS_ASYNC_STACK_METADATA

namespace folly::coro {

// Wraps a `Task` in a visible async-stack scope and attaches `metadata` to it.
template <typename T>
Task<T> co_withMetadata(std::uintptr_t metadata, Task<T> task) {
  auto& wrapperFrame = co_await detail::co_current_async_stack_frame;

  std::optional<AsyncStackMetadataFrame> metadataFrame{
      std::in_place, wrapperFrame, metadata};

  // `co_awaitTry` resumes this wrapper after child failure, so it can remove
  // the marker before final suspend; `co_nothrow` would skip this cleanup.
  auto result = co_await co_awaitTry(std::move(task));
  metadataFrame.reset();

  co_yield co_result(std::move(result));
}

} // namespace folly::coro

#endif // FOLLY_HAS_COROUTINES && FOLLY_HAS_ASYNC_STACK_METADATA
