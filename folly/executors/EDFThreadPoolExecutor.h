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

#include <cstddef>
#include <memory>
#include <utility>

#include <folly/executors/StripedEDFThreadPoolExecutor.h>

namespace folly {

/**
 * DEPRECATED: use `folly::StripedEDFThreadPoolExecutor` directly. This wrapper
 * exists only for legacy callers.
 *
 * `EDFThreadPoolExecutor` is a `StripedEDFThreadPoolExecutor` restricted to a
 * single stripe (`strictOrdering = true`): a `SoftRealTimeExecutor` that
 * implements a strict global earliest-deadline-first scheduling policy, with
 * deadline ties resolved by submission order.
 */
class EDFThreadPoolExecutor : public StripedEDFThreadPoolExecutor {
 public:
  struct Options {
    ThrottledLifoSem::Options tlsOptions;
  };

  explicit EDFThreadPoolExecutor(
      std::size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("EDFThreadPool"),
      const Options& options = {})
      : StripedEDFThreadPoolExecutor(
            numThreads,
            std::move(threadFactory),
            {.tlsOptions = options.tlsOptions, .strictOrdering = true}) {}
};

} // namespace folly
