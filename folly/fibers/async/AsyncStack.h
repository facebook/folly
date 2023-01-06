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

#include <glog/logging.h>
#include <folly/CPortability.h>
#include <folly/ScopeGuard.h>
#include <folly/fibers/async/Async.h>
#include <folly/tracing/AsyncStack.h>

namespace folly::fibers::async {
namespace detail {

template <typename F>
FOLLY_NOINLINE invoke_result_t<F> executeWithNewRoot(
    F&& func, AsyncStackFrame* FOLLY_NULLABLE callerFrame) {
  AsyncStackRoot newRoot;
  newRoot.setStackFrameContext();

  AsyncStackFrame frame;
  frame.setReturnAddress();
  if (callerFrame) {
    frame.setParentFrame(*callerFrame);
  }

  auto* oldRoot = exchangeCurrentAsyncStackRoot(&newRoot);
  activateAsyncStackFrame(newRoot, frame);

  SCOPE_EXIT {
    deactivateAsyncStackFrame(frame);
    CHECK_EQ(&newRoot, exchangeCurrentAsyncStackRoot(oldRoot));
  };

  return func();
}
} // namespace detail

/**
 * Executes `F` with a new async-stack-root, making it possible
 * to stitch stacks across async hops.
 *
 * callerFrame, if not null, must outlive the execution of `F`.
 */
template <typename F>
Async<async_invocable_inner_type_t<F>> executeWithNewRoot(
    F&& func, AsyncStackFrame* FOLLY_NULLABLE callerFrame) {
  return detail::executeWithNewRoot(std::forward<F>(func), callerFrame);
}

/**
 * Connects the execution of synchronous function `F` (on the main thread
 * context) to the calling fiber
 *
 * Note: The calling fiber must have a valid async-stack-root for meaningful
 * stacks.
 */
template <typename F>
invoke_result_t<F> runInMainContextWithTracing(F&& func) {
  DCHECK(detail::onFiber());
  auto* callerFrame = []() {
    auto* root = tryGetCurrentAsyncStackRoot();
    return root ? root->getTopFrame() : nullptr;
  }();
  return runInMainContext([&]() {
    return detail::executeWithNewRoot(std::forward<F>(func), callerFrame);
  });
}
} // namespace folly::fibers::async
