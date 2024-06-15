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

#include <folly/synchronization/HazptrDomain.h>

#include <cstddef>
#include <functional>
#include <queue>
#include <type_traits>

#include <folly/Executor.h>
#include <folly/Indestructible.h>
#include <folly/ScopeGuard.h>
#include <folly/executors/InlineExecutor.h>

namespace folly::detail {

namespace {

/// HazptrDefaultExecutor
///
/// Like QueuedImmediateExecutor, but:
/// * Only a singleton and not otherwise instantiated.
/// * Using keyword thread_local v.s. class ThreadLocal.
struct HazptrDefaultExecutor final : InlineLikeExecutor {
  using Func = Function<void()>;
  using Queue = std::queue<Func>;

  void add(Func func) override {
    thread_local Queue* current;

    std::optional<Queue> oqueue; // do not use this directly!
    if (!current) {
      current = &oqueue.emplace();
    }
    auto cleanup = makeGuard([&] { current = nullptr; });
    auto& queue = *current; // use this directly!

    queue.push(std::move(func));

    if (queue.size() == 1) {
      while (!queue.empty()) {
        auto qfunc = std::ref(queue.front());
        invokeCatchingExns("HazptrDefaultExecutor::run: ", qfunc);
        queue.pop();
      }
    }
  }
};

} // namespace

folly::Executor::KeepAlive<> hazptr_get_default_executor() {
  static Indestructible<HazptrDefaultExecutor> instance;
  return &*instance;
}

} // namespace folly::detail
