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

#include <folly/detail/AsyncTrace.h>
#include <folly/portability/GTest.h>

TEST(FollyCountersTest, Trivial) {
  folly::Executor* exec = nullptr;
  folly::IOExecutor* io_exec = nullptr;
  folly::async_tracing::logSetGlobalCPUExecutor(exec);
  folly::async_tracing::logGetGlobalCPUExecutor(exec);
  folly::async_tracing::logSetGlobalIOExecutor(io_exec);
  folly::async_tracing::logGetGlobalIOExecutor(io_exec);
  folly::async_tracing::logSetGlobalCPUExecutorToImmutable();
  folly::async_tracing::logGetImmutableCPUExecutor(exec);
  folly::async_tracing::logGetImmutableIOExecutor(io_exec);

  folly::Executor* lastExec = nullptr;
  folly::async_tracing::logSemiFutureVia(lastExec, exec);
  folly::async_tracing::logFutureVia(lastExec, exec);

  folly::async_tracing::logSemiFutureVia(lastExec, exec);
  folly::async_tracing::logFutureVia(lastExec, exec);

  folly::async_tracing::logBlockingOperation(std::chrono::milliseconds{100});

  folly::async_tracing::logSemiFutureDiscard(
      folly::async_tracing::DiscardHasDeferred::NO_EXECUTOR);
}
