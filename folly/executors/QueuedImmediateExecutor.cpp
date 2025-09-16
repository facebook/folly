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

#include <folly/executors/QueuedImmediateExecutor.h>

#include <functional>

#include <folly/Indestructible.h>
#include <folly/ScopeGuard.h>

namespace folly {

QueuedImmediateExecutor& QueuedImmediateExecutor::instance() {
  static Indestructible<QueuedImmediateExecutor> instance;
  return *instance;
}

void QueuedImmediateExecutor::add(Func func) {
  auto& [running, queue] = *q_;
  if (running) {
    queue.push(Task{std::move(func), RequestContext::saveContext()});
    return;
  }

  running = true;
  auto cleanup = makeGuard([&r = running] { r = false; });

  // No need to save/restore request context if this is the first call.
  invokeCatchingExns("QueuedImmediateExecutor", std::exchange(func, {}));

  if (queue.empty()) {
    return;
  }

  RequestContextSaverScopeGuard guard;
  while (!queue.empty()) {
    auto& task = queue.front();
    RequestContext::setContext(std::move(task.ctx));
    invokeCatchingExns("QueuedImmediateExecutor", std::ref(task.func));
    queue.pop();
  }
}

} // namespace folly
